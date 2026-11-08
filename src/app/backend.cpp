#include "backend.h"
#include "bugreport.h"
#include "gencommon.h"

#include "importer.h"
#include "playback.h"
#include "sysinfo.h"

#include <QBuffer>
#include <QClipboard>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSysInfo>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>

#include <utility>

namespace {

QVariantMap line(const QString &level, const QString &text) // level: ok, info, warning
{
    return {{QStringLiteral("level"), level}, {QStringLiteral("text"), text}};
}

QVariantMap card(Stage stage, const QString &current, const QVariantList &lines)
{
    return {{QStringLiteral("stage"), stageKey(stage)}, {QStringLiteral("current"), current}, {QStringLiteral("lines"), lines}};
}

// What GRUB actually waits for. On EFI with /boot on btrfs or ZFS, GRUB can't record
// failed boots, so 00_header uses GRUB_RECORDFAIL_TIMEOUT instead (CLAUDE.md).
int effectiveTimeout(const SystemState &s, const QString &root)
{
    const QString fs = QString::fromLatin1(QStorageInfo(root + QStringLiteral("/boot")).fileSystemType());
    if (fs == QLatin1String("btrfs") || fs == QLatin1String("zfs"))
        return s.grub.value(QStringLiteral("GRUB_RECORDFAIL_TIMEOUT"), QStringLiteral("30")).toInt();
    return s.grub.value(QStringLiteral("GRUB_TIMEOUT"), QStringLiteral("5")).toInt();
}

QVariantMap bootMenuCard(const SystemState &s, const QString &root)
{
    QVariantList lines;
    const QString themePath = s.grub.value(QStringLiteral("GRUB_THEME"));
    QString current = Backend::tr("No theme");
    if (themePath.isEmpty()) {
        lines << line(QStringLiteral("info"), Backend::tr("GRUB shows its plain text menu."));
    } else {
        static const QRegularExpression inThemesDir(QStringLiteral("^/boot/grub/themes/([^/]+)/theme\\.txt$"));
        const auto match = inThemesDir.match(themePath);
        current = match.hasMatch() ? match.captured(1) : themePath;
        if (!QFileInfo::exists(root + themePath))
            lines << line(QStringLiteral("warning"), Backend::tr("The theme file is missing; GRUB falls back to its text menu."));
    }
    lines << line(QStringLiteral("info"), s.ekpoGrubDropIn ? Backend::tr("Set by Ekpo.") : Backend::tr("Set in /etc/default/grub; Ekpo hasn't changed GRUB yet."));
    if (const QString mode = s.grub.value(QStringLiteral("GRUB_GFXMODE")); !mode.isEmpty())
        lines << line(QStringLiteral("info"), Backend::tr("Video mode: %1").arg(mode));
    // The resolution boot menus get built for, and the mode GRUB really used (SPEC.md §4.5, §4.12).
    if (!s.primaryMode.isEmpty() && !s.bootMode.isEmpty())
        lines << line(QStringLiteral("info"), Backend::tr("Primary screen %1 at %2×%3; this boot's menu showed at %4×%5.")
                                                  .arg(s.primaryOutput).arg(s.primaryMode.width()).arg(s.primaryMode.height()).arg(s.bootMode.width()).arg(s.bootMode.height()));
    const int timeout = effectiveTimeout(s, root);
    if (timeout == 0)
        lines << line(QStringLiteral("warning"), Backend::tr("The menu is skipped (0 s); it can't be reached without a live USB."));
    else
        lines << line(QStringLiteral("info"), Backend::tr("The menu shows for %1 s.").arg(timeout));
    QVariantMap c = card(Stage::BootMenu, current, lines);
    c.insert(QStringLiteral("timeout"), timeout);
    return c;
}

QVariantMap bootSplashCard(const SystemState &s, bool lastApplyNewestOnly)
{
    QVariantList lines;
    const QString id = QFileInfo(s.plymouthAlternative).dir().dirName();
    QString current = id.isEmpty() ? Backend::tr("None selected") : id;
    for (const SystemState::PlymouthTheme &t : s.plymouthThemes) {
        if (t.id == id)
            current = t.name;
    }

    if (!s.cmdlineSplash)
        lines << line(QStringLiteral("warning"), Backend::tr("The kernel command line has no \"splash\", so no boot splash shows."));
    if (!s.plymouthDaemonTheme.isEmpty() && s.plymouthDaemonTheme != id)
        lines << line(QStringLiteral("warning"), Backend::tr("plymouthd.conf names \"%1\" but \"%2\" is selected; boot and shutdown may look different.").arg(s.plymouthDaemonTheme, id));

    qsizetype withInitrd = 0;
    qsizetype outdated = 0;
    for (const SystemState::Kernel &k : s.kernels) {
        if (!k.hasInitrd)
            continue;
        ++withInitrd;
        if (s.plymouthSelectedAt.isValid() && k.initrdModified < s.plymouthSelectedAt)
            ++outdated;
    }
    if (outdated > 0)
        lines << (lastApplyNewestOnly
                      ? line(QStringLiteral("info"), Backend::tr("The other %1 kernels keep the previous splash as a fallback (the last apply rebuilt only the newest kernel).").arg(outdated))
                      : line(QStringLiteral("warning"), Backend::tr("%1 of %2 kernels still boot with an older splash (initrd built before the last change).").arg(outdated).arg(withInitrd)));
    else if (withInitrd > 0)
        lines << line(QStringLiteral("ok"), Backend::tr("All %1 initrds carry this splash.").arg(withInitrd));

    lines << line(QStringLiteral("info"), s.plymouthAlternativeMode == QLatin1String("manual") ? Backend::tr("Selected manually.") : Backend::tr("Chosen automatically (highest priority)."));
    QVariantMap c = card(Stage::BootSplash, current, lines);
    if (outdated > 0 && !lastApplyNewestOnly)
        c.insert(QStringLiteral("rebuildSplash"), true);
    return c;
}

QVariantMap plasmaSplashCard(const SystemState &s)
{
    QVariantList lines;
    if (s.splashEngine.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0)
        return card(Stage::PlasmaSplash, Backend::tr("Off"), {line(QStringLiteral("info"), Backend::tr("The Plasma splash is turned off."))});

    const QString id = s.splashTheme.isEmpty() ? QStringLiteral("org.kde.breeze.desktop") : s.splashTheme;
    QString current = id;
    bool found = false;
    for (const SystemState::SplashPackage &p : s.splashPackages) {
        if (p.id == id) {
            current = p.name;
            found = true;
            lines << line(QStringLiteral("info"), p.user ? Backend::tr("Installed in your home folder.") : Backend::tr("Installed with Plasma."));
        }
    }
    if (!found)
        lines << line(QStringLiteral("warning"), Backend::tr("The package \"%1\" isn't installed; Plasma shows no splash.").arg(id));
    return card(Stage::PlasmaSplash, current, lines);
}

QVariantMap importToMap(const ImportedTheme &t)
{
    return {
        {QStringLiteral("id"), t.id},
        {QStringLiteral("stage"), stageKey(t.stage)},
        {QStringLiteral("name"), t.name},
        {QStringLiteral("verdict"), verdictKey(t.verdict)},
        {QStringLiteral("installedId"), t.installedId},
        {QStringLiteral("sourcePath"), t.sourcePath},
        {QStringLiteral("report"), t.report},
    };
}

QVariantMap resultToMap(const ImportResult &result)
{
    QStringList names;
    for (const ImportedTheme &t : result.themes)
        names << t.name;
    return {{QStringLiteral("names"), names}, {QStringLiteral("errors"), result.errors}};
}

} // namespace

Backend::Backend(QObject *parent)
    : QObject(parent)
{
    const AppEnvironment env = detectEnvironment();
    m_rootDir = env.rootDir;
    m_homeDir = env.homeDir;
    m_launcher = env.launcher;
    m_library = Library::open(Library::configuredPath(), m_libraryError);
    createExecutor();
    connect(&m_editor, &DesignEditor::libraryChanged, this, &Backend::refresh);
    refresh();

    m_updater = std::make_unique<Updater>(updateSource(), QCoreApplication::applicationVersion(),
                                          QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/updates"));
    m_update.insert(QStringLiteral("canCheck"), m_updater->canCheck());
    connect(m_updater.get(), &Updater::checked, this, [this](bool found, const Release &release, const QString &error) {
        m_update.insert(QStringLiteral("checking"), false);
        m_update.insert(QStringLiteral("checked"), error.isEmpty());
        m_update.insert(QStringLiteral("checkError"), error);
        if (found) {
            m_release = release;
            m_update.insert(QStringLiteral("available"), true);
            m_update.insert(QStringLiteral("version"), release.version);
            m_update.insert(QStringLiteral("notes"), release.notes);
        }
        Q_EMIT updateChanged();
    });
    connect(m_updater.get(), &Updater::progress, this, [this](const QString &text, double fraction) {
        m_update.insert(QStringLiteral("busy"), true);
        m_update.insert(QStringLiteral("text"), text);
        m_update.insert(QStringLiteral("fraction"), fraction);
        Q_EMIT updateChanged();
    });
    connect(m_updater.get(), &Updater::finished, this, [this](bool ok, const QString &error) {
        m_update.insert(QStringLiteral("busy"), false);
        m_update.insert(QStringLiteral("installed"), ok);
        m_update.insert(QStringLiteral("error"), error); // empty when cancelled
        Q_EMIT updateChanged();
    });
    if (checkUpdates())
        checkForUpdates();
}

void Backend::reloadAssets()
{
    m_assetList.clear();
    if (!m_library) {
        Q_EMIT assetsChanged();
        return;
    }
    QStringList errors;
    const QMap<QString, QList<Library::Use>> uses = m_library->uses(errors);
    QHash<QString, QString> designNames; // current names, also of designs never saved
    for (const Design &d : m_library->designs(errors))
        designNames.insert(d.id, d.name);
    for (const Design &d : m_library->drafts(errors))
        designNames.insert(d.id, d.name);
    const auto withUses = [&](QVariantMap item, const QString &key) {
        QStringList all, short_;
        for (const Library::Use &u : uses.value(key)) {
            all << u.label;
            const QString entry = u.design.isEmpty() ? u.label : QStringLiteral("“%1”").arg(designNames.value(u.design, u.label));
            if (!short_.contains(entry))
                short_ << entry;
        }
        item.insert(QStringLiteral("uses"), all);
        item.insert(QStringLiteral("usedBy"), short_);
        return item;
    };
    const QLocale locale;
    QHash<QString, QString> licences; // licence hash -> file
    const QList<Asset> assets = m_library->assets(errors);
    for (const Asset &a : assets) {
        if (a.kind == Asset::Kind::Licence)
            licences.insert(a.hash, m_library->assetPath(a));
    }
    for (const Asset &a : assets) {
        if (a.kind == Asset::Kind::Licence)
            continue; // shown with its font
        const QString path = m_library->assetPath(a);
        const QString kind = a.kind == Asset::Kind::Image ? QStringLiteral("image") : a.kind == Asset::Kind::Video ? QStringLiteral("video") : QStringLiteral("font");
        m_assetList << withUses({{QStringLiteral("key"), a.hash},
                                 {QStringLiteral("kind"), kind},
                                 {QStringLiteral("name"), a.name},
                                 {QStringLiteral("url"), QUrl::fromLocalFile(path)},
                                 {QStringLiteral("size"), locale.formattedDataSize(QFileInfo(path).size())},
                                 {QStringLiteral("added"), locale.toString(a.addedAt.toLocalTime(), QLocale::ShortFormat)},
                                 {QStringLiteral("source"), a.source},
                                 {QStringLiteral("licenceUrl"), licences.contains(a.licence) ? QUrl::fromLocalFile(licences.value(a.licence)) : QUrl()}},
                                a.hash);
    }
    QHash<QString, QString> assetFiles;
    for (const Asset &a : assets)
        assetFiles.insert(a.hash, m_library->assetPath(a));
    for (const SharedElement &e : m_library->sharedElements(errors)) {
        QVariantList colours;
        for (const QString &key : paletteKeys) {
            if (e.colours.contains(key))
                colours << e.colours.value(key);
        }
        const bool palette = e.kind == SharedElement::Kind::Palette;
        m_assetList << withUses({{QStringLiteral("key"), e.id},
                                 {QStringLiteral("kind"), palette ? QStringLiteral("palette") : QStringLiteral("logo")},
                                 {QStringLiteral("name"), e.name},
                                 {QStringLiteral("url"), palette ? QUrl() : QUrl::fromLocalFile(assetFiles.value(e.asset))},
                                 {QStringLiteral("colours"), colours}},
                                e.id);
    }
    static const QStringList order = {QStringLiteral("image"), QStringLiteral("video"), QStringLiteral("font"), QStringLiteral("palette"), QStringLiteral("logo")};
    std::stable_sort(m_assetList.begin(), m_assetList.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap x = a.toMap(), y = b.toMap();
        const qsizetype kx = order.indexOf(x.value(QStringLiteral("kind")).toString()), ky = order.indexOf(y.value(QStringLiteral("kind")).toString());
        return kx != ky ? kx < ky : QString::localeAwareCompare(x.value(QStringLiteral("name")).toString(), y.value(QStringLiteral("name")).toString()) < 0;
    });
    Q_EMIT assetsChanged();
}

QVariantMap Backend::addAssetFiles(const QList<QUrl> &files)
{
    if (!m_library)
        return {{QStringLiteral("added"), 0}, {QStringLiteral("errors"), QStringList{m_libraryError}}};
    static const QStringList images = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("webp"), QStringLiteral("svg")};
    static const QStringList fonts = {QStringLiteral("ttf"), QStringLiteral("otf"), QStringLiteral("ttc")};
    int added = 0;
    QStringList errors;
    for (const QUrl &url : files) {
        const QString file = url.toLocalFile();
        const QFileInfo info(file);
        const QString suffix = info.suffix().toLower();
        QString error;
        std::optional<Asset> asset;
        if (images.contains(suffix))
            asset = m_library->addAsset(file, Asset::Kind::Image, info.completeBaseName(), error);
        else if (fonts.contains(suffix))
            asset = m_library->addFont(file, info.completeBaseName(), error);
        else
            error = tr("%1: only images and fonts can be added here. Videos are added in the editor, which converts them for their splash.").arg(info.fileName());
        if (asset)
            ++added;
        else
            errors << error;
    }
    reloadAssets();
    return {{QStringLiteral("added"), added}, {QStringLiteral("errors"), errors}};
}

QString Backend::paletteFromImage(const QUrl &file)
{
    if (!m_library)
        return m_libraryError;
    const QString path = file.toLocalFile();
    const QImage image(path);
    if (image.isNull())
        return tr("%1 isn't an image Ekpo can read.").arg(QFileInfo(path).fileName());
    SharedElement palette{{}, SharedElement::Kind::Palette, tr("Palette of %1").arg(QFileInfo(path).completeBaseName()), {}, ::paletteFromImage(image)};
    QString error;
    if (!m_library->savePalette(palette, error))
        return error;
    reloadAssets();
    return {};
}

QString Backend::renameItem(const QString &key, const QString &name)
{
    if (!m_library)
        return m_libraryError;
    if (name.trimmed().isEmpty())
        return tr("A name can't be empty.");
    QString error;
    QStringList errors;
    for (SharedElement e : m_library->sharedElements(errors)) {
        if (e.id == key) {
            e.name = name.trimmed();
            if (!m_library->saveSharedElement(e, error))
                return error;
            reloadAssets();
            return {};
        }
    }
    if (!m_library->renameAsset(key, name, error))
        return error;
    reloadAssets();
    return {};
}

void Backend::offerUndo(const QString &text, std::function<QString()> undo)
{
    m_undo = std::move(undo);
    Q_EMIT trashed(text);
}

QString Backend::undoRemoval()
{
    const std::function<QString()> undo = std::exchange(m_undo, {});
    if (!undo)
        return tr("There is nothing to undo.");
    const QString error = undo();
    refresh();
    reloadAssets();
    return error;
}

namespace {
std::function<QString()> fromTrash(const QString &trashed)
{
    return [trashed] {
        QString error;
        return trashed.isEmpty() || restoreFromTrash(trashed, error) ? QString() : error;
    };
}
} // namespace

QString Backend::removeItem(const QString &key)
{
    QString name;
    for (const QVariant &v : std::as_const(m_assetList))
        if (v.toMap().value(QStringLiteral("key")) == key)
            name = v.toMap().value(QStringLiteral("name")).toString();
    std::function<QString()> undo;
    if (const QString error = removeOne(key, undo); !error.isEmpty())
        return error;
    reloadAssets();
    offerUndo(tr("\u201c%1\u201d moved to the trash.").arg(name), undo);
    return {};
}

QString Backend::removeOne(const QString &key, std::function<QString()> &undo)
{
    if (!m_library)
        return m_libraryError;
    QStringList errors;
    const QList<Library::Use> users = m_library->uses(errors).value(key);
    if (!users.isEmpty()) {
        QStringList names;
        for (const Library::Use &u : users)
            names << u.label;
        return tr("Still used by %1.").arg(names.mid(0, 3).join(QStringLiteral(", ")) + (names.size() > 3 ? tr(" and %n more", "", int(names.size() - 3)) : QString()));
    }
    QString error;
    for (const SharedElement &e : m_library->sharedElements(errors)) {
        if (e.id != key)
            continue;
        if (!m_library->removeSharedElement(key, error))
            return error;
        undo = [this, e] {
            SharedElement again = e;
            QString error;
            return m_library && m_library->saveSharedElement(again, error) ? QString() : error;
        };
        return {};
    }
    std::optional<Asset> asset, licence;
    const QList<Asset> assets = m_library->assets(errors);
    for (const Asset &a : assets)
        if (a.hash == key)
            asset = a;
    if (!asset)
        return tr("The library has no such asset.");
    QString trashed, licenceTrashed;
    if (!m_library->removeAsset(key, error, &trashed))
        return error;
    // A font's licence text goes with it, unless another font has the same one.
    if (!asset->licence.isEmpty() && m_library->uses(errors).value(asset->licence).isEmpty()) {
        for (const Asset &a : assets)
            if (a.hash == asset->licence)
                licence = a;
        if (licence && !m_library->removeAsset(licence->hash, error, &licenceTrashed))
            licence.reset();
    }
    undo = [this, asset, trashed, licence, licenceTrashed] {
        QString error;
        if (!m_library || !m_library->restoreAsset(*asset, trashed, error))
            return error.isEmpty() ? m_libraryError : error;
        if (licence && !m_library->restoreAsset(*licence, licenceTrashed, error))
            return error;
        return QString();
    };
    return {};
}

QVariantMap Backend::removeUnused()
{
    int removed = 0;
    QStringList errors;
    QList<std::function<QString()>> undos;
    const QVariantList items = m_assetList;
    for (const QVariant &v : items) {
        const QVariantMap item = v.toMap();
        if (!item.value(QStringLiteral("uses")).toStringList().isEmpty())
            continue;
        std::function<QString()> undo;
        if (const QString error = removeOne(item.value(QStringLiteral("key")).toString(), undo); error.isEmpty()) {
            ++removed;
            undos << undo;
        } else {
            errors << error;
        }
    }
    reloadAssets();
    if (removed > 0)
        offerUndo(tr("%n unused item(s) moved to the trash.", "", removed), [undos] {
            QStringList errors;
            for (const auto &undo : undos)
                if (const QString error = undo(); !error.isEmpty())
                    errors << error;
            return errors.join(QLatin1Char('\n'));
        });
    return {{QStringLiteral("removed"), removed}, {QStringLiteral("errors"), errors}};
}

namespace {
QString archivePath(const QUrl &file)
{
    QString path = file.toLocalFile();
    if (!path.endsWith(QLatin1String(".tar.gz")) && !path.endsWith(QLatin1String(".tgz")))
        path += QStringLiteral(".tar.gz");
    return path;
}
} // namespace

QVariantMap Backend::planExport(const QString &kind, const QString &id, const QString &screen)
{
    if (!m_library)
        return {{QStringLiteral("problems"), QStringList{m_libraryError}}};
    QSize size = primaryScreenOr(m_state);
    static const QRegularExpression wxh(QStringLiteral("^(\\d+)x(\\d+)$"));
    if (const QRegularExpressionMatch m = wxh.match(screen); m.hasMatch())
        size = QSize(m.captured(1).toInt(), m.captured(2).toInt());
    const bool mask = kind == QLatin1String("mask");
    m_export = mask ? exportMask(*m_library, id, size, m_state.menuEntries) : exportDesign(*m_library, id, size, m_state.menuEntries);

    // Boot menus and boot splashes Ekpo builds are made for one screen size.
    QStringList errors;
    QStringList designs;
    if (mask) {
        for (const Mask &m : m_library->masks(errors)) {
            if (m.id == id)
                for (const Slot &slot : m.stageSlots)
                    if (slot.kind == Slot::Kind::Design)
                        designs << slot.ref;
        }
    } else {
        designs << id;
    }
    bool needsScreen = false;
    for (const QString &d : std::as_const(designs)) {
        QString error;
        if (const auto design = m_library->design(d, 0, error))
            needsScreen = needsScreen || design->stage != Stage::PlasmaSplash;
    }
    QStringList screens{QStringLiteral("%1x%2").arg(primaryScreenOr(m_state).width()).arg(primaryScreenOr(m_state).height())};
    for (const char *common : {"2560x1440", "1920x1080", "1280x720"}) {
        if (!screens.contains(QLatin1String(common)))
            screens << QLatin1String(common);
    }
    QVariantList items;
    for (const ExportItem &item : std::as_const(m_export.thirdParty))
        items << QVariantMap{{QStringLiteral("kind"), item.kind},
                             {QStringLiteral("name"), item.name},
                             {QStringLiteral("source"), item.source},
                             {QStringLiteral("licence"), item.licence},
                             {QStringLiteral("free"), item.free}};
    return {{QStringLiteral("folder"), m_export.folder},
            {QStringLiteral("thirdParty"), items},
            {QStringLiteral("problems"), m_export.problems},
            {QStringLiteral("needsScreen"), needsScreen},
            {QStringLiteral("screen"), QStringLiteral("%1x%2").arg(size.width()).arg(size.height())},
            {QStringLiteral("screens"), screens}};
}

QString Backend::writeExport(const QUrl &file)
{
    if (m_export.files.isEmpty())
        return tr("There is nothing to export.");
    return writeArchive(m_export.files, archivePath(file));
}

QString Backend::exportLibraryTo(const QUrl &file)
{
    if (!m_library)
        return m_libraryError;
    return exportLibrary(*m_library, archivePath(file));
}

void Backend::setCheckUpdates(bool check)
{
    if (!m_library)
        return;
    Settings settings = m_library->settings();
    settings.checkUpdates = check;
    QString error;
    m_library->saveSettings(settings, error);
    Q_EMIT changed();
}

void Backend::setPreviewResolution(int resolution)
{
    if (!m_library || (resolution != 1440 && resolution != 1080 && resolution != 720))
        return;
    Settings settings = m_library->settings();
    settings.previewResolution = resolution;
    QString error;
    m_library->saveSettings(settings, error);
    Q_EMIT changed();
}

void Backend::checkForUpdates()
{
    if (!m_updater->canCheck() || m_updater->busy())
        return;
    m_update.insert(QStringLiteral("checking"), true);
    m_update.insert(QStringLiteral("checkError"), QString());
    Q_EMIT updateChanged();
    m_updater->check();
}

void Backend::startUpdate()
{
    if (applying() || m_updater->busy() || m_release.version.isEmpty())
        return;
    m_update.insert(QStringLiteral("error"), QString());
    m_updater->update(m_release);
}

void Backend::cancelUpdate()
{
    m_updater->cancel();
}

void Backend::restartApp()
{
    requestRestart();
}

// Also hands the (new) library to the editor.
void Backend::createExecutor()
{
    m_editor.setLibrary(m_library ? &*m_library : nullptr);
    m_executor.reset();
    if (m_library) {
        m_executor = std::make_unique<Executor>(*m_library, m_homeDir, m_launcher);
        connect(m_executor.get(), &Executor::progress, this, &Backend::applyProgress);
        connect(m_executor.get(), &Executor::finished, this, [this](bool ok, const QString &error, int number) {
            if (std::exchange(m_removingFiles, false) && ok)
                removePlasmaCopies(*m_library, m_homeDir, currentTarget(probeSystem(m_rootDir, m_homeDir), m_rootDir).plasmaSplash->theme);
            Q_EMIT applyingChanged();
            refresh();
            Q_EMIT applyFinished(ok, error, number);
        });
    }
}

void Backend::refresh()
{
    const QString root = m_rootDir == QLatin1String("/") ? QString() : m_rootDir;
    m_state = probeSystem(m_rootDir, m_homeDir);
    const auto lastSplash = m_library ? lastApplied(*m_library).bootSplash : std::nullopt;
    m_stageCards = {bootMenuCard(m_state, root), bootSplashCard(m_state, lastSplash && !lastSplash->allKernels), plasmaSplashCard(m_state)};
    if (m_library) {
        const Target current = currentTarget(m_state, m_rootDir);
        const Target last = lastApplied(*m_library);
        const QList<Stage> drifted = driftedStages(*m_library, current);
        for (QVariant &v : m_stageCards) {
            QVariantMap c = v.toMap();
            const auto stage = stageFromKey(c.value(QStringLiteral("stage")).toString());
            const bool drift = stage && drifted.contains(*stage);
            c.insert(QStringLiteral("drift"), drift);
            if (drift) {
                QVariantList lines = c.value(QStringLiteral("lines")).toList();
                const QString applied = last.stageJson(*stage).value(QStringLiteral("label")).toString();
                lines.prepend(line(QStringLiteral("warning"), tr("Changed outside Ekpo since it last applied \u201c%1\u201d.").arg(applied)));
                c.insert(QStringLiteral("lines"), lines);
            }
            v = c;
        }

        m_atOriginal = isAtOriginal(PlanContext{*m_library, m_state, m_rootDir, m_homeDir});
        m_welcomeBack = looksLikeFreshSystem(PlanContext{*m_library, m_state, m_rootDir, m_homeDir});
        if (m_welcomeBack)
            m_library->clearPendingCheck(); // a question about the old system makes no sense now
        m_ekpoCopies = int(installedEkpoCopies(m_rootDir).size());
        m_oldSystemCopies = oldSystemCopies(*m_library, m_rootDir, current);
        m_oldPlasmaCopies = oldPlasmaCopies(*m_library, m_homeDir);

        // New kernels: apt builds their initrds, which normally carry the selected splash.
        m_kernelNotice.clear();
        QStringList installed, fresh;
        bool outdated = false;
        for (const SystemState::Kernel &k : m_state.kernels)
            installed << k.version;
        if (m_library->settings().seenKernels.isEmpty() && !installed.isEmpty()) {
            Settings settings = m_library->settings(); // the first start: nothing new to tell
            settings.seenKernels = installed;
            QString error;
            m_library->saveSettings(settings, error);
        }
        for (const SystemState::Kernel &k : m_state.kernels) {
            if (m_library->settings().seenKernels.contains(k.version))
                continue;
            fresh << k.version;
            outdated = outdated || !k.hasInitrd || (m_state.plymouthSelectedAt.isValid() && k.initrdModified < m_state.plymouthSelectedAt);
        }
        if (!fresh.isEmpty())
            m_kernelNotice = {{QStringLiteral("versions"), fresh}, {QStringLiteral("outdated"), outdated}};

        // Ask after a reboot whether the last boot-stage apply worked.
        m_pendingCheck.clear();
        if (const auto check = m_library->pendingCheck(); check && check->value(QStringLiteral("bootId")).toString() != currentBootId())
            m_pendingCheck = check->toVariantMap();
    }

    m_editor.setMenuEntries(m_state.menuEntries); // what the boot menu preview lists

    QStringList errors = m_state.errors;
    if (!m_library)
        errors << m_libraryError;
    m_system = {
        {QStringLiteral("generator"), m_state.initramfsGenerator},
        {QStringLiteral("leftoverJournal"), m_state.leftoverJournal},
        {QStringLiteral("errors"), errors},
    };

    m_imports.clear();
    QList<ImportedTheme> imported;
    if (m_library) {
        QStringList loadErrors;
        imported = m_library->imports(loadErrors);
        for (const ImportedTheme &t : imported)
            m_imports << importToMap(t);
        m_system[QStringLiteral("errors")] = errors + loadErrors;
    }

    m_designs.clear();
    QMap<QString, QString> designNames;
    if (m_library) {
        QStringList loadErrors;
        const QList<Design> drafts = m_library->drafts(loadErrors);
        const auto map = [](const Design &d, int version, bool hasDraft) {
            return QVariantMap{{QStringLiteral("id"), d.id}, {QStringLiteral("name"), d.name}, {QStringLiteral("stage"), stageKey(d.stage)},
                               {QStringLiteral("version"), version}, {QStringLiteral("hasDraft"), hasDraft}};
        };
        for (const Design &d : m_library->designs(loadErrors)) {
            const bool hasDraft = std::any_of(drafts.cbegin(), drafts.cend(), [&](const Design &draft) { return draft.id == d.id; });
            m_designs << map(d, d.version, hasDraft);
            designNames.insert(d.id, d.name);
        }
        for (const Design &d : drafts) {
            if (!designNames.contains(d.id)) // never saved
                m_designs << map(d, 0, true);
        }
        m_system[QStringLiteral("errors")] = m_system.value(QStringLiteral("errors")).toStringList() + loadErrors;
    }

    // Applied looks built from a design that has changed since (SPEC.md §3).
    const QMap<Stage, QString> updates = m_library ? designUpdates(*m_library) : QMap<Stage, QString>();
    for (QVariant &v : m_stageCards) {
        QVariantMap c = v.toMap();
        const auto stage = stageFromKey(c.value(QStringLiteral("stage")).toString());
        if (stage && updates.contains(*stage)) {
            QVariantList lines = c.value(QStringLiteral("lines")).toList();
            lines.prepend(line(QStringLiteral("info"), tr("Update available: \u201c%1\u201d has changed since it was applied; apply its Mask again.")
                                                            .arg(designNames.value(updates.value(*stage)))));
            c.insert(QStringLiteral("lines"), lines);
            v = c;
        }
    }

    // The applied boot menu design against this screen and this boot (SPEC.md §4.12).
    const BootMenuCheck menu = m_library ? checkBootMenu(*m_library, m_state) : BootMenuCheck{};
    m_rebuildBootMenu.clear();
    for (QVariant &v : m_stageCards) {
        QVariantMap c = v.toMap();
        if (menu.design.isEmpty() || c.value(QStringLiteral("stage")) != stageKey(Stage::BootMenu))
            continue;
        QVariantList lines = c.value(QStringLiteral("lines")).toList();
        const auto size = [](QSize s) { return QStringLiteral("%1×%2").arg(s.width()).arg(s.height()); };
        if (menu.screenChanged) {
            lines.prepend(line(QStringLiteral("warning"), tr("Built for %1, but the primary screen now runs %2: its text has the wrong size until it is rebuilt.")
                                                              .arg(size(menu.builtFor), size(m_state.primaryMode))));
            c.insert(QStringLiteral("rebuild"), size(m_state.primaryMode));
            m_rebuildBootMenu = menu.design;
        }
        if (menu.fellBack)
            lines.prepend(line(QStringLiteral("warning"), tr("On this boot GRUB couldn't use %1 and showed %2: the firmware doesn't offer that mode, so GRUB fell back. "
                                                             "Build it for a mode the firmware offers (the review lets you pick one).")
                                                              .arg(size(menu.builtFor), m_state.bootMode.isEmpty() ? tr("its text menu") : size(m_state.bootMode))));
        if (!menu.clipped.isEmpty())
            lines.prepend(line(QStringLiteral("warning"), tr("GRUB cuts off %n entries, such as \u201c%1\u201d. A smaller menu font fixes it.", "", int(menu.clipped.size()))
                                                              .arg(menu.clipped.first())));
        c.insert(QStringLiteral("lines"), lines);
        v = c;
    }

    m_masks.clear();
    if (m_library) {
        QStringList maskErrors;
        QMap<QString, QString> names = designNames;
        for (const QVariant &v : std::as_const(m_imports))
            names.insert(v.toMap().value(QStringLiteral("id")).toString(), v.toMap().value(QStringLiteral("name")).toString());
        for (const Mask &mask : m_library->masks(maskErrors)) {
            QVariantMap maskSlots;
            bool update = false;
            for (Stage stage : allStages) {
                update = update || (updates.contains(stage) && mask.stageSlots.value(stage).ref == updates.value(stage));
                const Slot slot = mask.stageSlots.value(stage);
                maskSlots.insert(stageKey(stage), slot.kind != Slot::Kind::LeaveAsIs
                                                  ? QVariantMap{{QStringLiteral("ref"), slot.ref}, {QStringLiteral("name"), names.value(slot.ref, tr("(missing theme)"))}}
                                                  : QVariantMap());
            }
            m_masks << QVariantMap{{QStringLiteral("id"), mask.id}, {QStringLiteral("name"), mask.name}, {QStringLiteral("slots"), maskSlots},
                                   {QStringLiteral("update"), update}};
        }
    }

    m_history.clear();
    if (m_library) {
        QStringList historyErrors;
        for (const HistoryEntry &e : m_library->history(historyErrors)) {
            const Target target = Target::fromJson(e.state.value(QStringLiteral("target")).toObject());
            QVariantList stages;
            if (target.plasmaSplash)
                stages << QVariantMap{{QStringLiteral("stage"), stageKey(Stage::PlasmaSplash)}, {QStringLiteral("label"), target.plasmaSplash->label}};
            if (target.bootMenu)
                stages << QVariantMap{{QStringLiteral("stage"), stageKey(Stage::BootMenu)}, {QStringLiteral("label"), target.bootMenu->original ? tr("Original") : target.bootMenu->label}};
            if (target.bootSplash)
                stages << QVariantMap{{QStringLiteral("stage"), stageKey(Stage::BootSplash)}, {QStringLiteral("label"), target.bootSplash->label}};
            m_history << QVariantMap{{QStringLiteral("number"), e.number},
                                     {QStringLiteral("title"), e.title},
                                     {QStringLiteral("createdAt"), e.createdAt.toLocalTime()},
                                     {QStringLiteral("stages"), stages},
                                     {QStringLiteral("restoreFile"), QStringLiteral("%1/history/%2/RESTORE.txt").arg(m_library->path()).arg(e.number)}};
        }
    }

    // Installed folders whose themes aren't in the library yet.
    m_installed.clear();
    for (const auto &[stage, folder] : installedThemeFolders(m_state, m_rootDir, m_homeDir)) {
        const bool done = std::any_of(imported.cbegin(), imported.cend(), [&](const ImportedTheme &t) {
            return t.sourcePath == folder || t.sourcePath.startsWith(folder + QLatin1Char('/'));
        });
        if (!done) {
            m_installed << QVariantMap{{QStringLiteral("stage"), stageKey(stage)},
                                       {QStringLiteral("name"), QDir(folder).dirName()},
                                       {QStringLiteral("path"), folder}};
        }
    }
    Q_EMIT changed();
}

QVariantMap Backend::importPath(const QUrl &path)
{
    if (!m_library)
        return {{QStringLiteral("names"), QStringList()}, {QStringLiteral("errors"), QStringList{m_libraryError}}};
    const QString local = path.isLocalFile() ? path.toLocalFile() : path.toString();
    const ImportResult result = importThemes(*m_library, local, m_rootDir, m_homeDir);
    refresh();
    return resultToMap(result);
}

QVariantMap Backend::reimport(const QString &id)
{
    for (const QVariant &v : std::as_const(m_imports)) {
        const QVariantMap t = v.toMap();
        if (t.value(QStringLiteral("id")) == id)
            return importPath(QUrl::fromLocalFile(t.value(QStringLiteral("sourcePath")).toString()));
    }
    return {{QStringLiteral("names"), QStringList()}, {QStringLiteral("errors"), QStringList{tr("Unknown import %1").arg(id)}}};
}

QString Backend::removeImport(const QString &id)
{
    QString error;
    QStringList errors;
    if (m_library) {
        for (const Mask &mask : m_library->masks(errors)) {
            for (const Slot &slot : mask.stageSlots) {
                if (slot.kind == Slot::Kind::Import && slot.ref == id)
                    return tr("Used by the Mask \u201c%1\u201d; remove it from there first.").arg(mask.name);
            }
        }
    }
    QString name, trashed;
    if (m_library)
        for (const ImportedTheme &t : m_library->imports(errors))
            if (t.id == id)
                name = t.name;
    if (!m_library || !m_library->removeImport(id, error, &trashed))
        return error.isEmpty() ? m_libraryError : error;
    refresh();
    offerUndo(tr("\u201c%1\u201d moved to the trash.").arg(name), fromTrash(trashed));
    return {};
}

bool Backend::originalAvailable() const
{
    return m_library && !originalTarget(*m_library).isEmpty();
}

void Backend::setPlan(const Plan &plan)
{
    m_plan = plan;
    QVariantList stages;
    for (const PlannedStage &stage : plan.stages) {
        QVariantList diffs;
        for (const FileDiff &d : stage.diffs)
            diffs << QVariantMap{{QStringLiteral("path"), d.path}, {QStringLiteral("before"), d.before}, {QStringLiteral("after"), d.after}};
        stages << QVariantMap{{QStringLiteral("stage"), stageKey(stage.stage)},
                              {QStringLiteral("from"), stage.from},
                              {QStringLiteral("to"), stage.to},
                              {QStringLiteral("actions"), stage.actions},
                              {QStringLiteral("diffs"), diffs}};
    }
    m_planMap = {
        {QStringLiteral("title"), plan.title},
        {QStringLiteral("canApply"), plan.canApply()},
        {QStringLiteral("needsPassword"), bool(plan.request)},
        {QStringLiteral("stages"), stages},
        {QStringLiteral("commands"), plan.commands},
        {QStringLiteral("notes"), plan.notes},
        {QStringLiteral("problems"), plan.stages.isEmpty() && plan.problems.isEmpty() ? QStringList{tr("This is already the current look.")} : plan.problems},
        {QStringLiteral("estimatedSeconds"), plan.estimatedSeconds},
        {QStringLiteral("touchesSplash"), plan.touchesSplash},
        {QStringLiteral("allKernels"), plan.allKernels},
        {QStringLiteral("newestKernel"), plan.newestKernel},
        {QStringLiteral("kernelCount"), plan.kernelCount},
    };
    // The screen a design is built for: the primary one, or a common size (SPEC.md §4.5).
    if (!plan.screen.isEmpty()) {
        QStringList screens{QStringLiteral("%1x%2").arg(primaryScreenOr(m_state).width()).arg(primaryScreenOr(m_state).height())};
        for (const char *common : {"2560x1440", "1920x1080", "1280x720"}) {
            if (!screens.contains(QLatin1String(common)))
                screens << QLatin1String(common);
        }
        m_planMap.insert(QStringLiteral("screen"), QStringLiteral("%1x%2").arg(plan.screen.width()).arg(plan.screen.height()));
        m_planMap.insert(QStringLiteral("screens"), screens);
    }
    Q_EMIT planChanged();
}

void Backend::planApplyImport(const QString &id)
{
    if (!m_library)
        return;
    refresh();
    QStringList errors;
    for (const ImportedTheme &t : m_library->imports(errors)) {
        if (t.id == id)
            return preparePlan([mask = Mask{{}, t.name, {{t.stage, Slot{Slot::Kind::Import, t.id}}}}](const PlanContext &c) { return planMask(c, mask); },
                               m_library->settings().rebuildAllKernels);
    }
}

void Backend::planRebuildBootMenu()
{
    if (!m_library || m_rebuildBootMenu.isEmpty())
        return;
    QString error;
    if (const auto d = m_library->design(m_rebuildBootMenu, 0, error)) {
        const QString title = tr("Rebuild \u201c%1\u201d for %2×%3").arg(d->name).arg(primaryScreenOr(m_state).width()).arg(primaryScreenOr(m_state).height());
        preparePlan([mask = Mask{{}, title, {{Stage::BootMenu, Slot{Slot::Kind::Design, d->id}}}}](const PlanContext &c) { return planMask(c, mask); }, true);
    }
}

void Backend::planApplyDesign(const QString &id)
{
    if (!m_library)
        return;
    refresh();
    QString error;
    if (const auto d = m_library->design(id, 0, error))
        preparePlan([mask = Mask{{}, d->name, {{d->stage, Slot{Slot::Kind::Design, d->id}}}}](const PlanContext &c) { return planMask(c, mask); },
                    m_library->settings().rebuildAllKernels);
}

QString Backend::removeDesign(const QString &id)
{
    if (!m_library)
        return m_libraryError;
    QStringList errors;
    for (const Mask &mask : m_library->masks(errors)) {
        for (const Slot &slot : mask.stageSlots) {
            if (slot.kind == Slot::Kind::Design && slot.ref == id)
                return tr("Used by the Mask \u201c%1\u201d; remove it from there first.").arg(mask.name);
        }
    }
    QString name, error, trashed;
    if (const auto design = m_library->design(id, 0, error))
        name = design->name;
    for (const Design &d : m_library->drafts(errors))
        if (d.id == id && name.isEmpty())
            name = d.name;
    if (!m_library->removeDesign(id, error, &trashed))
        return error;
    refresh();
    offerUndo(tr("\u201c%1\u201d moved to the trash.").arg(name), fromTrash(trashed));
    return {};
}

void Backend::planRestoreEntry(int number)
{
    if (!m_library)
        return;
    refresh();
    QStringList errors;
    for (const HistoryEntry &e : m_library->history(errors)) {
        if (e.number == number) {
            const Target target = Target::fromJson(e.state.value(QStringLiteral("target")).toObject());
            const QString title = tr("Restore #%1: %2").arg(number).arg(e.title);
            return preparePlan([target, title](const PlanContext &c) { return planTarget(c, target, title); }, true);
        }
    }
}

void Backend::planRestoreOriginal()
{
    if (!m_library)
        return;
    refresh();
    preparePlan([target = originalTarget(*m_library), title = tr("Restore Original")](const PlanContext &c) { return planTarget(c, target, title); }, true);
}

void Backend::applyPlan()
{
    if (!m_executor || m_executor->isRunning())
        return;
    m_executor->apply(m_plan);
    Q_EMIT applyingChanged();
}

void Backend::adoptStage(const QString &stage)
{
    const auto s = stageFromKey(stage);
    if (!m_library || !s)
        return;
    // Recorded as a history entry so Ekpo treats the current state as its own from now on.
    const QJsonObject current = currentTarget(m_state, m_rootDir).stageJson(*s);
    HistoryEntry entry{0, QDateTime::currentDateTimeUtc(), tr("Adopted a change made outside Ekpo"),
                       {{QStringLiteral("target"), QJsonObject{{stage, current}}}, {QStringLiteral("before"), QJsonObject{{stage, current}}}}};
    QString error;
    m_library->addHistoryEntry(entry, error);
    refresh();
}

void Backend::planReapplyStage(const QString &stage)
{
    const auto s = stageFromKey(stage);
    if (!m_library || !s)
        return;
    refresh();
    const QJsonObject last = lastApplied(*m_library).stageJson(*s);
    preparePlan([target = Target::fromJson({{stage, last}}), title = tr("Re-apply \u201c%1\u201d").arg(last.value(QStringLiteral("label")).toString())](
                    const PlanContext &c) { return planTarget(c, target, title); },
                m_library->settings().rebuildAllKernels);
}

void Backend::planTimeout(int seconds)
{
    if (!m_library)
        return;
    refresh();
    // Keeps whatever Ekpo already set (theme, video modes) and only changes the timeout.
    BootMenuTarget menu = *currentTarget(m_state, m_rootDir).bootMenu;
    if (menu.original) {
        menu = BootMenuTarget{};
        menu.label = tr("default theme");
    }
    menu.timeout = seconds;
    menu.timeoutStyle = seconds == 0 ? QStringLiteral("hidden") : QStringLiteral("menu");
    menu.label = tr("%1, %2 s menu").arg(menu.label).arg(seconds);
    Target target;
    target.bootMenu = menu;
    preparePlan([target, title = tr("Menu timeout: %1 s").arg(seconds)](const PlanContext &c) { return planTarget(c, target, title); }, true);
}

void Backend::recover()
{
    if (!m_executor || m_executor->isRunning())
        return;
    m_executor->recover();
    Q_EMIT applyingChanged();
}

bool Backend::removeOldCopies()
{
    if (!m_library || !m_executor || m_executor->isRunning())
        return false;
    trashOldPlasmaCopies(*m_library, m_homeDir);
    if (m_oldSystemCopies.isEmpty()) {
        refresh();
        return false;
    }
    // What the helper keeps: what history needs (it checks itself what is selected).
    m_executor->pruneCopies(keptThemes(*m_library, std::nullopt));
    Q_EMIT applyingChanged();
    return true;
}

void Backend::acknowledgeKernels()
{
    if (!m_library)
        return;
    Settings settings = m_library->settings();
    settings.seenKernels.clear();
    for (const SystemState::Kernel &k : m_state.kernels)
        settings.seenKernels << k.version;
    QString error;
    m_library->saveSettings(settings, error);
    refresh();
}

void Backend::planRebuildSplash()
{
    if (!m_library)
        return;
    refresh();
    const Target current = currentTarget(m_state, m_rootDir);
    if (!current.bootSplash)
        return;
    Target target;
    target.bootSplash = current.bootSplash;
    preparePlan(
        [target, title = tr("Rebuild the boot splash into every initrd")](const PlanContext &c) {
            PlanContext forced = c;
            forced.rebuildSplash = true;
            return planTarget(forced, target, title);
        },
        true);
}

QVariantList Backend::bootPlayback(const QString &maskId)
{
    if (!m_library || !m_playbackDir.isValid())
        return {};
    refresh();
    QMap<Stage, Slot> chosen;
    QStringList errors;
    for (const Mask &mask : m_library->masks(errors)) {
        if (mask.id == maskId)
            chosen = mask.stageSlots;
    }
    // A new folder each time: the player's images would come from Qt's cache otherwise.
    static int run = 0;
    return playbackSteps(*m_library, m_state, m_rootDir, m_homeDir, chosen, m_playbackDir.filePath(QString::number(++run)));
}

void Backend::keepCheck()
{
    if (m_library)
        m_library->clearPendingCheck();
    m_pendingCheck.clear();
    Q_EMIT changed();
}

void Backend::planRevertCheck()
{
    if (!m_library || m_pendingCheck.isEmpty())
        return;
    const int number = m_pendingCheck.value(QStringLiteral("number")).toInt();
    QStringList errors;
    for (const HistoryEntry &e : m_library->history(errors)) {
        if (e.number != number)
            continue;
        // Only the stages that entry changed go back.
        const QJsonObject target = e.state.value(QStringLiteral("target")).toObject();
        const QJsonObject before = e.state.value(QStringLiteral("before")).toObject();
        QJsonObject revert;
        for (auto it = target.constBegin(); it != target.constEnd(); ++it)
            revert.insert(it.key(), before.value(it.key()));
        keepCheck();
        refresh();
        preparePlan([target = Target::fromJson(revert), title = tr("Undo \u201c%1\u201d").arg(e.title)](const PlanContext &c) { return planTarget(c, target, title); },
                    true);
        return;
    }
    keepCheck();
}

void Backend::setShowIntro(bool show)
{
    if (!m_library)
        return;
    Settings settings = m_library->settings();
    settings.showIntro = show;
    QString error;
    m_library->saveSettings(settings, error);
    Q_EMIT changed();
}

QString Backend::saveMask(const QString &id, const QString &name, const QString &bootMenu, const QString &bootSplash, const QString &plasmaSplash)
{
    if (!m_library)
        return m_libraryError;
    Mask mask{id, name.trimmed(), {}};
    const QList<std::pair<Stage, QString>> refs = {{Stage::BootMenu, bootMenu}, {Stage::BootSplash, bootSplash}, {Stage::PlasmaSplash, plasmaSplash}};
    for (const auto &[stage, ref] : refs) {
        if (ref.isEmpty())
            continue;
        const bool design = std::any_of(m_designs.cbegin(), m_designs.cend(), [&](const QVariant &d) { return d.toMap().value(QStringLiteral("id")) == ref; });
        mask.stageSlots.insert(stage, Slot{design ? Slot::Kind::Design : Slot::Kind::Import, ref});
    }
    QString error;
    if (!m_library->saveMask(mask, error))
        return error;
    refresh();
    return {};
}

QString Backend::maskFromApplied()
{
    if (!m_library)
        return m_libraryError;
    QStringList errors;
    const QList<HistoryEntry> history = m_library->history(errors);
    if (history.isEmpty())
        return tr("Ekpo hasn't applied anything yet.");
    // What the newest entry applied knows the designs and imports (the system doesn't).
    Mask mask;
    mask.name = tr("Applied on %1").arg(QLocale().toString(QDate::currentDate(), QLocale::ShortFormat));
    mask.stageSlots = slotsFromTarget(*m_library, Target::fromJson(history.first().state.value(QStringLiteral("target")).toObject()));
    if (mask.stageSlots.isEmpty())
        return tr("What's applied isn't in the library: import or design it first.");
    QString error;
    if (!m_library->saveMask(mask, error))
        return error;
    refresh();
    return {};
}

QString Backend::removeMask(const QString &id)
{
    QString error, name, trashed;
    QStringList errors;
    if (m_library)
        for (const Mask &m : m_library->masks(errors))
            if (m.id == id)
                name = m.name;
    if (!m_library || !m_library->removeMask(id, error, &trashed))
        return error.isEmpty() ? m_libraryError : error;
    refresh();
    offerUndo(tr("The Mask \u201c%1\u201d moved to the trash.").arg(name), fromTrash(trashed));
    return {};
}

void Backend::planApplyMask(const QString &id)
{
    if (!m_library)
        return;
    refresh();
    QStringList errors;
    for (const Mask &mask : m_library->masks(errors)) {
        if (mask.id == id)
            return preparePlan([mask](const PlanContext &c) { return planMask(c, mask); }, m_library->settings().rebuildAllKernels);
    }
}

void Backend::preparePlan(const std::function<Plan(const PlanContext &)> &make, bool allKernels, QSize screen)
{
    m_makePlan = make;
    m_planScreen = screen.isEmpty() ? primaryScreenOr(m_state) : screen;
    PlanContext context{*m_library, m_state, m_rootDir, m_homeDir};
    context.allKernels = allKernels;
    context.screen = m_planScreen;
    setPlan(make(context));
}

void Backend::setPlanAllKernels(bool all)
{
    if (m_library && m_makePlan && !applying())
        preparePlan(m_makePlan, all, m_planScreen);
}

void Backend::setPlanScreen(const QString &size)
{
    static const QRegularExpression mode(QStringLiteral("^(\\d{3,5})x(\\d{3,5})$"));
    const auto m = mode.match(size);
    if (m_library && m_makePlan && !applying() && m.hasMatch())
        preparePlan(m_makePlan, m_plan.allKernels, QSize(m.captured(1).toInt(), m.captured(2).toInt()));
}

void Backend::setRebuildAllKernels(bool all)
{
    if (!m_library)
        return;
    Settings settings = m_library->settings();
    settings.rebuildAllKernels = all;
    QString error;
    m_library->saveSettings(settings, error);
    Q_EMIT changed();
}

void Backend::setTechnicalDetailsExpanded(bool expanded)
{
    if (!m_library)
        return;
    Settings settings = m_library->settings();
    settings.technicalDetailsExpanded = expanded;
    QString error;
    m_library->saveSettings(settings, error);
    Q_EMIT changed();
}

void Backend::removeEkpoFiles()
{
    if (!m_executor || m_executor->isRunning() || !m_atOriginal)
        return;
    m_removingFiles = true;
    m_executor->removeEkpoFiles();
    Q_EMIT applyingChanged();
}

QString Backend::setLibraryLocation(const QUrl &folder)
{
    if (applying())
        return tr("Wait until the apply has finished.");
    QString error;
    const auto path = Library::relocate(m_library ? m_library->path() : Library::configuredPath(), folder.toLocalFile(), error);
    if (!path)
        return error;
    auto library = Library::open(*path, error);
    if (!library || !Library::setConfiguredPath(*path, error))
        return error;
    m_executor.reset();
    m_library = std::move(library);
    m_undo = {}; // it belongs to the other library
    createExecutor();
    refresh();
    return {};
}

void Backend::setHistorySize(int size)
{
    if (!m_library)
        return;
    Settings settings = m_library->settings();
    settings.historySize = qBound(1, size, 100);
    QString error;
    m_library->saveSettings(settings, error);
    Q_EMIT changed();
}

void Backend::planReapplyLastLook()
{
    if (!m_library)
        return;
    refresh();
    preparePlan([target = lastApplied(*m_library), title = tr("Re-apply my last look")](const PlanContext &c) { return planTarget(c, target, title); },
                m_library->settings().rebuildAllKernels);
}

void Backend::startFresh()
{
    if (!m_library)
        return;
    // The current state becomes both Original and Ekpo's reference.
    const QJsonObject current = currentTarget(m_state, m_rootDir).toJson();
    QString error;
    m_library->resetOriginal(current, error);
    HistoryEntry entry{0, QDateTime::currentDateTimeUtc(), tr("Started fresh after a reinstall"),
                       {{QStringLiteral("target"), current}, {QStringLiteral("before"), current}}};
    m_library->addHistoryEntry(entry, error);
    refresh();
}

QString Backend::systemReport(bool extended)
{
    const QString root = m_rootDir == QLatin1String("/") ? QString() : m_rootDir;
    QStringList lines = systemBasics(root);
    if (!extended)
        return lines.join(QLatin1Char('\n'));
    refresh();
    const Target current = currentTarget(m_state, m_rootDir);
    QStringList packages;
    for (const QString &p : {QStringLiteral("grub2-common"), QStringLiteral("plymouth"), QStringLiteral("initramfs-tools"), QStringLiteral("dracut"),
                             QStringLiteral("plasma-workspace")}) {
        if (const QString v = packageVersion(root, p); !v.isEmpty())
            packages << QStringLiteral("%1 %2").arg(p, v);
    }
    lines << QStringLiteral("Packages: %1").arg(packages.join(QStringLiteral(", ")))
          << QStringLiteral("Initrds built by: %1").arg(m_state.initramfsGenerator.isEmpty() ? QStringLiteral("unknown") : m_state.initramfsGenerator)
          << QString()
          << QStringLiteral("Boot menu: %1 (%2)").arg(current.bootMenu->label, current.bootMenu->original ? QStringLiteral("/etc/default/grub") : QStringLiteral("Ekpo drop-in"));
    for (auto it = m_state.grub.cbegin(); it != m_state.grub.cend(); ++it)
        lines << QStringLiteral("  %1=%2").arg(it.key(), it.value());
    lines << QStringLiteral("Boot splash: %1 (%2, plymouthd.conf Theme=%3)").arg(current.bootSplash->label, current.bootSplash->mode, m_state.plymouthDaemonTheme)
          << QStringLiteral("Plasma splash: %1").arg(current.plasmaSplash->label.isEmpty() ? QStringLiteral("Plasma default") : current.plasmaSplash->label)
          << QStringLiteral("Kernel command line has splash: %1").arg(m_state.cmdlineSplash ? QStringLiteral("yes") : QStringLiteral("no"))
          << QStringLiteral("Primary screen: %1").arg(m_state.primaryMode.isEmpty() ? QStringLiteral("unknown")
                                                      : QStringLiteral("%1 at %2×%3").arg(m_state.primaryOutput).arg(m_state.primaryMode.width()).arg(m_state.primaryMode.height()))
          << QStringLiteral("This boot's video mode: %1 (%2)").arg(m_state.bootMode.isEmpty() ? QStringLiteral("text")
                                                                   : QStringLiteral("%1×%2").arg(m_state.bootMode.width()).arg(m_state.bootMode.height()),
                                                                   m_state.efi ? QStringLiteral("EFI") : QStringLiteral("BIOS"))
          << QStringLiteral("Expected boot menu entries: %1").arg(m_state.menuEntries.join(QStringLiteral(" | ")))
          << QStringLiteral("Kernels:");
    for (const SystemState::Kernel &k : m_state.kernels)
        lines << QStringLiteral("  %1, initrd %2").arg(k.version, k.hasInitrd ? k.initrdModified.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QStringLiteral("missing"));
    lines << QString() << QStringLiteral("Library: %1").arg(libraryPath());
    if (!m_history.isEmpty()) {
        const QVariantMap last = m_history.first().toMap();
        lines << QStringLiteral("Last apply: #%1 %2").arg(last.value(QStringLiteral("number")).toInt()).arg(last.value(QStringLiteral("title")).toString());
    }
    lines << QStringLiteral("Ekpo theme copies installed: %1").arg(m_ekpoCopies);
    const QStringList problems = m_system.value(QStringLiteral("errors")).toStringList();
    if (!problems.isEmpty())
        lines << QStringLiteral("Problems: %1").arg(problems.join(QStringLiteral("; ")));
    return lines.join(QLatin1Char('\n'));
}

void Backend::copyToClipboard(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

QString Backend::captureScreen(QObject *window)
{
    auto *quickWindow = qobject_cast<QQuickWindow *>(window);
    m_screenshot = quickWindow ? quickWindow->grabWindow() : QImage();
    if (m_screenshot.isNull())
        return {};
    QBuffer png;
    png.open(QIODevice::WriteOnly);
    m_screenshot.scaledToWidth(qMin(m_screenshot.width(), 800), Qt::SmoothTransformation).save(&png, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.data().toBase64());
}

QString Backend::recentLog()
{
    const QStringList messages = recentMessages();
    const QStringList helper = helperLog(m_rootDir, 20);
    return (QStringList{QStringLiteral("Ekpo:")} + (messages.isEmpty() ? QStringList{QStringLiteral("(no messages)")} : messages)
            + QStringList{QString(), QStringLiteral("ekpo-helper:")} + (helper.isEmpty() ? QStringList{QStringLiteral("(nothing logged)")} : helper))
        .join(QLatin1Char('\n'));
}

QString Backend::fitBugReport(const QString &title, const QString &body)
{
    return fitIssueBody(title, body);
}

QString Backend::openBugReport(const QString &title, const QString &body, bool withScreenshot)
{
    if (withScreenshot && !m_screenshot.isNull())
        QGuiApplication::clipboard()->setImage(m_screenshot);
    return openIssue(title, body);
}

QVariantMap Backend::saveBugReport(const QString &title, const QString &body, bool withScreenshot)
{
    QString error;
    const QString path = saveReport(bugReportsDir(), title, body, withScreenshot ? m_screenshot : QImage(), error);
    return path.isEmpty() ? QVariantMap{{QStringLiteral("error"), error}} : QVariantMap{{QStringLiteral("path"), path}};
}

QString Backend::bugReportsDir() const
{
    return localReportsDir();
}

void Backend::notify(const QString &summary, const QString &body)
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                                                          QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    message << QStringLiteral("Ekpo") << uint(0) << QStringLiteral("io.github.nomoruyi.ekpo") << summary << body << QStringList()
            << QVariantMap{{QStringLiteral("desktop-entry"), QStringLiteral("io.github.nomoruyi.ekpo")}} << int(-1);
    QDBusConnection::sessionBus().call(message, QDBus::NoBlock);
}
