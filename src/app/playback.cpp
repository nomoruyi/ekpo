#include "playback.h"

#include "gengrub.h"
#include "genksplash.h"
#include "planner.h"
#include "target.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>
#include <QUrl>

namespace {

// How long each stage shows: about as long as a boot shows it.
constexpr int menuSeconds = 5;
constexpr int splashSeconds = 6;
constexpr int plasmaSeconds = 5;

QList<PayloadFile> readFolder(const QString &dir)
{
    QList<PayloadFile> files;
    for (QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
        QFile f(it.next());
        if (f.size() < (32 << 20) && f.open(QIODevice::ReadOnly))
            files << PayloadFile{QDir(dir).relativeFilePath(f.fileName()), f.readAll()};
    }
    return files;
}

bool writeFolder(const QString &dir, const QList<PayloadFile> &files, const QMap<QString, QString> &links = {})
{
    QDir(dir).removeRecursively();
    for (const PayloadFile &f : files) {
        QSaveFile out(dir + QLatin1Char('/') + f.path);
        if (!QDir().mkpath(QFileInfo(out.fileName()).path()) || !out.open(QIODevice::WriteOnly) || out.write(f.data) != f.data.size() || !out.commit())
            return false;
    }
    for (auto it = links.cbegin(); it != links.cend(); ++it) {
        const QString link = dir + QLatin1Char('/') + it.key();
        if (!QDir().mkpath(QFileInfo(link).path()) || !QFile::link(*it, link))
            return false;
    }
    return true;
}

} // namespace

QVariantMap plymouthPreviewMap(const PlymouthLayout &layout, const QString &dir)
{
    QVariantList frames, images;
    for (const QString &f : layout.frameFiles)
        frames << QUrl::fromLocalFile(dir + QLatin1Char('/') + f);
    for (const PlymouthImage &image : layout.images)
        images << QVariantMap{{QStringLiteral("source"), QUrl::fromLocalFile(dir + QLatin1Char('/') + image.file)},
                              {QStringLiteral("centreX"), image.centre.x()}, {QStringLiteral("centreY"), image.centre.y()},
                              {QStringLiteral("width"), image.size.width()}, {QStringLiteral("height"), image.size.height()},
                              {QStringLiteral("animation"), image.animation}};
    QVariantMap map{{QStringLiteral("background"), QUrl::fromLocalFile(dir + QStringLiteral("/background.png"))},
                    {QStringLiteral("frames"), frames},
                    {QStringLiteral("images"), images},
                    {QStringLiteral("seconds"), layout.seconds},
                    {QStringLiteral("centreX"), layout.centre.x()},
                    {QStringLiteral("centreY"), layout.centre.y()},
                    {QStringLiteral("module"), layout.module},
                    {QStringLiteral("hideMessages"), layout.hideMessages},
                    {QStringLiteral("builtWidth"), builtScreen.width()},
                    {QStringLiteral("builtHeight"), builtScreen.height()}};
    if (layout.progress) {
        const QRect r = *layout.progress;
        const ProgressShape &shape = layout.progressShape;
        const auto file = [&](const char *name) { return QUrl::fromLocalFile(dir + QStringLiteral("/progress-%1.png").arg(QLatin1String(name))); };
        map.insert(QStringLiteral("progress"),
                   QVariantMap{{QStringLiteral("x"), r.x()}, {QStringLiteral("y"), r.y()}, {QStringLiteral("width"), r.width()},
                               {QStringLiteral("height"), r.height()}, {QStringLiteral("color"), layout.progressColour},
                               {QStringLiteral("background"), layout.progressBackground}, {QStringLiteral("style"), layout.progressStyle},
                               {QStringLiteral("caps"), shape.caps}, {QStringLiteral("margin"), shape.margin},
                               {QStringLiteral("segments"), shape.segments}, {QStringLiteral("segmentWidth"), shape.segmentWidth},
                               {QStringLiteral("gap"), shape.gap}, {QStringLiteral("box"), file("box")}, {QStringLiteral("left"), file("left")},
                               {QStringLiteral("middle"), file("middle")}, {QStringLiteral("right"), file("right")},
                               {QStringLiteral("segment"), file("segment")}});
    }
    return map;
}

QVariantList playbackSteps(const Library &library, const SystemState &state, const QString &rootDir, const QString &homeDir,
                           const QMap<Stage, Slot> &maskSlots, const QString &outDir)
{
    const QString root = rootDir == QLatin1String("/") ? QString() : rootDir;
    const Target current = currentTarget(state, rootDir);
    QMap<Stage, Slot> chosen = slotsFromTarget(library, current);
    for (auto it = maskSlots.cbegin(); it != maskSlots.cend(); ++it) {
        if (it->kind != Slot::Kind::LeaveAsIs)
            chosen.insert(it.key(), *it);
    }
    QStringList problems, errors; // a preview shows what builds
    const QList<ImportedTheme> imports = library.imports(errors);
    const auto design = [&](Stage stage) -> std::optional<Design> {
        const Slot slot = chosen.value(stage);
        QString error;
        const auto d = slot.kind == Slot::Kind::Design ? library.design(slot.ref, 0, error) : std::nullopt;
        return d ? std::optional(library.resolved(*d, problems)) : std::nullopt;
    };
    const auto imported = [&](Stage stage) -> std::optional<ImportedTheme> {
        const Slot slot = chosen.value(stage);
        for (const ImportedTheme &t : imports) {
            if (slot.kind == Slot::Kind::Import && t.id == slot.ref)
                return t;
        }
        return std::nullopt;
    };
    QVariantList steps;

    // The boot menu, drawn as GRUB draws it (a few seconds of its countdown).
    {
        const QSize screen = primaryScreenOr(state);
        QVariantMap step{{QStringLiteral("stage"), QStringLiteral("bootMenu")}, {QStringLiteral("seconds"), menuSeconds},
                         {QStringLiteral("width"), screen.width()}, {QStringLiteral("height"), screen.height()}};
        const QStringList entries = state.menuEntries.isEmpty()
            ? QStringList{QStringLiteral("Kubuntu"), QStringLiteral("Advanced options for Kubuntu"), QStringLiteral("UEFI Firmware Settings")}
            : state.menuEntries;
        QList<PayloadFile> files;
        if (const auto d = design(Stage::BootMenu)) {
            files = buildBootMenu(*d, library, screen, entries, problems);
            step.insert(QStringLiteral("label"), d->name);
        } else if (const auto t = imported(Stage::BootMenu)) {
            files = readFolder(library.importFilesPath(t->id));
            step.insert(QStringLiteral("label"), t->name);
        } else if (const QString theme = state.grub.value(QStringLiteral("GRUB_THEME")); !theme.isEmpty()) {
            files = readFolder(root + QFileInfo(theme).path());
            step.insert(QStringLiteral("label"), QFileInfo(theme).dir().dirName());
        }
        const bool themed = std::any_of(files.cbegin(), files.cend(), [](const PayloadFile &f) { return f.path == QLatin1String("theme.txt"); });
        const QString image = outDir + QStringLiteral("/menu.png");
        if (themed && QDir().mkpath(outDir) && renderBootMenu(files, screen, entries, menuSeconds).save(image)) {
            step.insert(QStringLiteral("kind"), QStringLiteral("image"));
            step.insert(QStringLiteral("source"), QUrl::fromLocalFile(image));
        } else if (!step.contains(QStringLiteral("label"))) {
            step.insert(QStringLiteral("kind"), QStringLiteral("none"));
            step.insert(QStringLiteral("label"), QStringLiteral("GRUB's text menu"));
            step.insert(QStringLiteral("note"), QStringLiteral("No theme: GRUB shows its own text menu."));
        } else {
            step.insert(QStringLiteral("kind"), QStringLiteral("none"));
            step.insert(QStringLiteral("note"), QStringLiteral("Ekpo couldn't draw this boot menu."));
        }
        steps << step;
    }

    // The boot splash: Ekpo can only draw the ones it makes.
    {
        QVariantMap step{{QStringLiteral("stage"), QStringLiteral("bootSplash")}, {QStringLiteral("seconds"), splashSeconds}, {QStringLiteral("kind"), QStringLiteral("none")},
                         {QStringLiteral("note"), QStringLiteral("Ekpo can only play the boot splashes it makes.")}};
        if (const auto d = design(Stage::BootSplash)) {
            PlymouthLayout layout;
            const QString dir = outDir + QStringLiteral("/splash");
            step.insert(QStringLiteral("label"), d->name);
            if (writeFolder(dir, buildPlymouthSplash(*d, QStringLiteral("ekpo-preview"), library, builtScreen, problems, &layout))) {
                step.insert(QStringLiteral("kind"), QStringLiteral("plymouth"));
                step.insert(QStringLiteral("layout"), plymouthPreviewMap(layout, dir));
            }
        } else if (const auto t = imported(Stage::BootSplash)) {
            step.insert(QStringLiteral("label"), t->name);
        } else {
            QString name = QFileInfo(state.plymouthAlternative).dir().dirName();
            for (const SystemState::PlymouthTheme &t : state.plymouthThemes) {
                if (t.id == name)
                    name = t.name;
            }
            step.insert(QStringLiteral("label"), name);
        }
        steps << step;
    }

    // The Plasma splash: its own QML, Ekpo's or another's.
    {
        QVariantMap step{{QStringLiteral("stage"), QStringLiteral("plasmaSplash")}, {QStringLiteral("seconds"), plasmaSeconds}, {QStringLiteral("kind"), QStringLiteral("none")},
                         {QStringLiteral("note"), QStringLiteral("Its files aren't on this system.")}};
        QString splash;
        if (const auto d = design(Stage::PlasmaSplash)) {
            QMap<QString, QString> links;
            const QString dir = outDir + QStringLiteral("/plasma");
            step.insert(QStringLiteral("label"), d->name);
            if (writeFolder(dir, buildPlasmaSplash(*d, QStringLiteral("ekpo-preview"), library, problems, &links), links))
                splash = dir + QStringLiteral("/contents/splash/Splash.qml");
        } else if (const auto t = imported(Stage::PlasmaSplash)) {
            step.insert(QStringLiteral("label"), t->name);
            splash = library.importFilesPath(t->id) + QStringLiteral("/contents/splash/Splash.qml");
        } else if (state.splashEngine.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0) {
            step.insert(QStringLiteral("label"), QStringLiteral("Off"));
            step.insert(QStringLiteral("note"), QStringLiteral("The Plasma splash is turned off."));
        } else if (current.plasmaSplash && !current.plasmaSplash->theme.isEmpty()) {
            const QString id = current.plasmaSplash->theme;
            step.insert(QStringLiteral("label"), current.plasmaSplash->label.isEmpty() ? id : current.plasmaSplash->label);
            for (const QString &dir : {homeDir + QStringLiteral("/.local/share/plasma/look-and-feel/"), root + QStringLiteral("/usr/share/plasma/look-and-feel/")}) {
                if (QFileInfo::exists(dir + id + QStringLiteral("/contents/splash/Splash.qml"))) {
                    splash = dir + id + QStringLiteral("/contents/splash/Splash.qml");
                    break;
                }
            }
        }
        if (!splash.isEmpty() && QFileInfo::exists(splash)) {
            step.insert(QStringLiteral("kind"), QStringLiteral("qml"));
            step.insert(QStringLiteral("source"), QUrl::fromLocalFile(splash));
        }
        steps << step;
    }
    return steps;
}
