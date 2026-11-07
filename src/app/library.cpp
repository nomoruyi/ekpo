#include "library.h"

#include <KConfig>
#include <KConfigGroup>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace {

std::optional<QJsonObject> readJson(const QString &path, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!doc.isObject()) {
        error = QStringLiteral("%1: not a JSON object (%2)").arg(path, parseError.errorString());
        return std::nullopt;
    }
    return doc.object();
}

// Atomic: a crash leaves either the old or the new file, never half of one.
bool writeJson(const QString &path, const QJsonObject &json, QString &error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(json).toJson()) < 0 || !file.commit()) {
        error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

// Copies a theme folder. Links count only if they stay inside the folder; their
// target's content is copied. Anything skipped is described in notes.
bool copyTree(const QString &src, const QString &dst, QStringList &notes, QString &error)
{
    const QDir srcDir(src);
    const QString rootCanonical = QFileInfo(src).canonicalFilePath() + QLatin1Char('/');
    QDirIterator it(src, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        const QString rel = srcDir.relativeFilePath(info.filePath());
        const QString target = dst + QLatin1Char('/') + rel;
        QString from = info.filePath();
        if (info.isSymLink()) {
            from = info.canonicalFilePath();
            if (!from.startsWith(rootCanonical) || !QFileInfo(from).isFile()) {
                notes << QStringLiteral("Not copied: the link %1 points outside the theme folder or to a folder.").arg(rel);
                continue;
            }
        } else if (info.isDir()) {
            if (!QDir().mkpath(target)) {
                error = QStringLiteral("Can't create %1").arg(target);
                return false;
            }
            continue;
        } else if (!info.isFile()) {
            notes << QStringLiteral("Not copied: %1 is not a regular file.").arg(rel);
            continue;
        }
        if (!QDir().mkpath(QFileInfo(target).path()) || !QFile::copy(from, target)) {
            error = QStringLiteral("Can't copy %1").arg(rel);
            return false;
        }
    }
    return true;
}

QString manifestPath(const QString &library)
{
    return library + QStringLiteral("/library.json");
}

// The licence that comes with a font file: a licence file in its folder, else the
// copyright file of the Debian package that ships it. Empty if there is none.
QString fontLicence(const QString &file)
{
    const QFileInfo info(file);
    const QStringList next = info.dir().entryList({QStringLiteral("LICEN[CS]E*"), QStringLiteral("OFL*"), QStringLiteral("COPYING*")},
                                                  QDir::Files, QDir::Name); // name filters ignore case
    if (!next.isEmpty())
        return info.dir().filePath(next.first());

    for (const QString &path : {info.absoluteFilePath(), info.canonicalFilePath()}) {
        // dpkg-query takes a glob pattern: Ubuntu[wdth,wght].ttf must match literally.
        QString pattern;
        for (const QChar c : path)
            pattern += QStringView(u"*?[]\\").contains(c) ? QStringLiteral("\\") + c : QString(c);
        QProcess dpkg;
        dpkg.start(QStringLiteral("dpkg-query"), {QStringLiteral("-S"), pattern});
        if (!dpkg.waitForFinished(10000) || dpkg.exitStatus() != QProcess::NormalExit || dpkg.exitCode() != 0)
            continue;
        // "fonts-ubuntu: /usr/share/fonts/...", or "pkg:amd64, other: ..."; skip "diversion by" lines.
        for (const QString &line : QString::fromUtf8(dpkg.readAllStandardOutput()).split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1String("diversion ")) || !line.contains(QLatin1String(": /")))
                continue;
            const QString package = line.section(QLatin1String(": "), 0, 0).section(QLatin1String(", "), 0, 0).section(QLatin1Char(':'), 0, 0);
            const QString copyright = QStringLiteral("/usr/share/doc/%1/copyright").arg(package);
            if (QFileInfo(copyright).isFile())
                return copyright;
        }
    }
    return {};
}

} // namespace

QString Library::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/ekpo");
}

namespace {
QString configFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/ekporc");
}
} // namespace

QString Library::configuredPath()
{
    const QString path = KConfig(configFile(), KConfig::SimpleConfig).group(QStringLiteral("Library")).readEntry("Path");
    return path.isEmpty() ? defaultPath() : path;
}

bool Library::setConfiguredPath(const QString &path, QString &error)
{
    KConfig config(configFile(), KConfig::SimpleConfig);
    KConfigGroup group = config.group(QStringLiteral("Library"));
    if (QDir::cleanPath(path) == QDir::cleanPath(defaultPath()))
        group.deleteEntry("Path");
    else
        group.writeEntry("Path", QDir::cleanPath(path));
    if (!config.sync()) {
        error = QStringLiteral("Can't write %1").arg(configFile());
        return false;
    }
    return true;
}

std::optional<QString> Library::relocate(const QString &from, const QString &to, QString &error)
{
    const QString source = QDir::cleanPath(from);
    const QString target = QDir::cleanPath(to);
    if (source == target)
        return target;
    if (QFile::exists(manifestPath(target)))
        return target; // use the library found there
    const QDir targetDir(target);
    if (targetDir.exists() && !targetDir.isEmpty()) {
        error = QStringLiteral("%1 is not empty and not an Ekpo library.").arg(target);
        return std::nullopt;
    }
    if (target.startsWith(source + QLatin1Char('/'))) {
        error = QStringLiteral("The library can't move into itself.");
        return std::nullopt;
    }
    targetDir.rmdir(QStringLiteral("."));
    QDir().mkpath(QFileInfo(target).path());
    if (QDir().rename(source, target))
        return target;

    // Another file system: copy, check, then move the old copy to the trash.
    QStringList notes;
    if (!QDir().mkpath(target) || !copyTree(source, target, notes, error) || !notes.isEmpty()) {
        QDir(target).removeRecursively();
        if (error.isEmpty())
            error = QStringLiteral("Can't copy the library to %1.").arg(target);
        return std::nullopt;
    }
    QFile::moveToTrash(source);
    return target;
}

std::optional<Library> Library::open(const QString &path, QString &error)
{
    Library library(QDir::cleanPath(path));
    const QDir dir(library.m_path);

    if (!QFile::exists(manifestPath(library.m_path))) {
        if (dir.exists() && !dir.isEmpty()) {
            error = QStringLiteral("%1 is not empty and not a Ekpo library").arg(library.m_path);
            return std::nullopt;
        }
        if (!dir.mkpath(QStringLiteral(".")) || !library.saveManifest({}, error))
            return std::nullopt;
    }

    const auto manifest = readJson(manifestPath(library.m_path), error);
    if (!manifest)
        return std::nullopt;
    const int format = manifest->value(QStringLiteral("format")).toInt();
    if (format > currentFormat) {
        error = QStringLiteral("The library was written by a newer Ekpo (format %1).").arg(format);
        return std::nullopt;
    }
    if (format < 1) {
        error = QStringLiteral("Unknown library format %1.").arg(format);
        return std::nullopt;
    }
    library.m_settings = Settings::fromJson(manifest->value(QStringLiteral("settings")).toObject());

    for (const QString &sub : {QStringLiteral("imports"), QStringLiteral("designs"), QStringLiteral("assets"), QStringLiteral("masks"),
                               QStringLiteral("history")}) {
        if (!dir.mkpath(sub)) {
            error = QStringLiteral("Can't create %1").arg(dir.filePath(sub));
            return std::nullopt;
        }
    }
    // ponytail: format 2 only added designs/ and assets/ (made above), so migrating
    // is writing the new number; add real steps here when a format changes files.
    if (format < currentFormat && !library.saveManifest(library.m_settings, error))
        return std::nullopt;
    return library;
}

bool Library::saveManifest(const Settings &settings, QString &error)
{
    const QJsonObject manifest{{QStringLiteral("format"), currentFormat}, {QStringLiteral("settings"), settings.toJson()}};
    return writeJson(manifestPath(m_path), manifest, error);
}

bool Library::saveSettings(const Settings &settings, QString &error)
{
    if (!saveManifest(settings, error))
        return false;
    m_settings = settings;
    return true;
}

QString Library::importFilesPath(const QString &id) const
{
    return m_path + QStringLiteral("/imports/") + id + QStringLiteral("/files");
}

QList<ImportedTheme> Library::imports(QStringList &errors) const
{
    QList<ImportedTheme> result;
    const QDir dir(m_path + QStringLiteral("/imports"));
    for (const QString &id : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString error;
        const auto json = readJson(dir.filePath(id + QStringLiteral("/import.json")), error);
        const auto theme = json ? ImportedTheme::fromJson(id, *json) : std::nullopt;
        if (theme)
            result.append(*theme);
        else
            errors << (error.isEmpty() ? QStringLiteral("%1: not a valid import").arg(dir.filePath(id)) : error);
    }
    std::sort(result.begin(), result.end(), [](const ImportedTheme &a, const ImportedTheme &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return result;
}

bool Library::saveImport(ImportedTheme &theme, const QString &themeRoot, QString &error)
{
    const QString id = theme.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : theme.id;
    const QString files = importFilesPath(id);
    const QString fresh = files + QStringLiteral(".new");
    QDir(fresh).removeRecursively();

    QStringList notes;
    if (!QDir().mkpath(fresh) || !copyTree(themeRoot, fresh, notes, error)) {
        QDir(fresh).removeRecursively();
        if (error.isEmpty())
            error = QStringLiteral("Can't create %1").arg(fresh);
        return false;
    }
    // Swap in the new copy only once it is complete.
    QDir(files).removeRecursively();
    if (!QDir().rename(fresh, files)) {
        error = QStringLiteral("Can't replace %1").arg(files);
        return false;
    }

    ImportedTheme saved = theme;
    saved.id = id;
    saved.report << notes;
    if (!writeJson(m_path + QStringLiteral("/imports/") + id + QStringLiteral("/import.json"), saved.toJson(), error))
        return false;
    theme = saved;
    return true;
}

bool Library::removeImport(const QString &id, QString &error, QString *trashedPath)
{
    QString inTrash;
    if (id.isEmpty() || !QFile::moveToTrash(m_path + QStringLiteral("/imports/") + id, &inTrash)) {
        error = QStringLiteral("Can't move import %1 to the trash.").arg(id);
        return false;
    }
    if (trashedPath)
        *trashedPath = inTrash;
    return true;
}

QString Library::buildHash(const QList<PayloadFile> &files)
{
    QList<PayloadFile> sorted = files;
    std::sort(sorted.begin(), sorted.end(), [](const PayloadFile &a, const PayloadFile &b) { return a.path < b.path; });
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const PayloadFile &f : sorted) {
        hash.addData(f.path.toUtf8() + '\0' + QByteArray::number(f.data.size()) + '\0');
        hash.addData(f.data);
    }
    return QString::fromLatin1(hash.result().toHex().left(24));
}

bool Library::saveBuild(const QList<PayloadFile> &files, QString &error)
{
    const QString dir = m_path + QStringLiteral("/builds/") + buildHash(files);
    if (QFileInfo::exists(dir))
        return true;
    const QString fresh = dir + QStringLiteral(".new");
    QDir(fresh).removeRecursively();
    for (const PayloadFile &f : files) {
        const QString target = fresh + QLatin1Char('/') + f.path;
        QSaveFile out(target);
        if (!validRelativePath(f.path) || !QDir().mkpath(QFileInfo(target).path()) || !out.open(QIODevice::WriteOnly) || out.write(f.data) < 0 || !out.commit()) {
            error = QStringLiteral("Can't store build file %1").arg(f.path);
            QDir(fresh).removeRecursively();
            return false;
        }
    }
    if (files.isEmpty())
        QDir().mkpath(fresh);
    if (!QDir().rename(fresh, dir)) {
        error = QStringLiteral("Can't store build %1").arg(dir);
        return false;
    }
    return true;
}

std::optional<QList<PayloadFile>> Library::buildFiles(const QString &hash) const
{
    const QString dir = m_path + QStringLiteral("/builds/") + hash;
    if (hash.isEmpty() || !QFileInfo(dir).isDir())
        return std::nullopt;
    QList<PayloadFile> files;
    for (QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories); it.hasNext();) {
        QFile f(it.next());
        if (!f.open(QIODevice::ReadOnly))
            return std::nullopt;
        files.append({QDir(dir).relativeFilePath(f.fileName()), f.readAll()});
    }
    return files;
}

namespace {
QString designDir(const QString &library, const QString &id)
{
    return library + QStringLiteral("/designs/") + id;
}

// Every designs/<id>/<file> that exists, read as a design, sorted by name.
QList<Design> readDesigns(const QString &library, const QString &file, QStringList &errors)
{
    QList<Design> result;
    const QDir dir(library + QStringLiteral("/designs"));
    for (const QString &id : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString path = dir.filePath(id + QLatin1Char('/') + file);
        if (!QFileInfo::exists(path))
            continue;
        QString error;
        const auto json = readJson(path, error);
        const auto design = json ? Design::fromJson(id, *json) : std::nullopt;
        if (design)
            result.append(*design);
        else
            errors << (error.isEmpty() ? QStringLiteral("%1: not a valid design").arg(path) : error);
    }
    std::sort(result.begin(), result.end(), [](const Design &a, const Design &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return result;
}
} // namespace

QList<Design> Library::designs(QStringList &errors) const
{
    return readDesigns(m_path, QStringLiteral("design.json"), errors);
}

QList<Design> Library::drafts(QStringList &errors) const
{
    return readDesigns(m_path, QStringLiteral("draft.json"), errors);
}

std::optional<Design> Library::design(const QString &id, int version, QString &error) const
{
    const QString file = version == 0 ? QStringLiteral("/design.json") : QStringLiteral("/versions/%1.json").arg(version);
    const auto json = readJson(designDir(m_path, id) + file, error);
    const auto design = json ? Design::fromJson(id, *json) : std::nullopt;
    if (json && !design)
        error = QStringLiteral("%1: not a valid design").arg(designDir(m_path, id) + file);
    return design;
}

QList<int> Library::designVersions(const QString &id) const
{
    QList<int> numbers;
    for (const QString &name : QDir(designDir(m_path, id) + QStringLiteral("/versions")).entryList({QStringLiteral("*.json")}, QDir::Files)) {
        bool ok = false;
        const int n = name.chopped(5).toInt(&ok);
        if (ok && n > 0)
            numbers.append(n);
    }
    std::sort(numbers.begin(), numbers.end());
    return numbers;
}

bool Library::saveDesign(Design &design, QString &error)
{
    if (design.name.trimmed().isEmpty()) {
        error = QStringLiteral("A design needs a name.");
        return false;
    }
    Design saved = design;
    if (saved.id.isEmpty())
        saved.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString dir = designDir(m_path, saved.id);
    const QList<int> versions = designVersions(saved.id);
    saved.version = versions.isEmpty() ? 1 : versions.last() + 1;
    saved.savedAt = QDateTime::currentDateTimeUtc();
    if (!QDir().mkpath(dir + QStringLiteral("/versions"))) {
        error = QStringLiteral("Can't create %1").arg(dir);
        return false;
    }
    // The version first: a crash in between leaves design.json one version behind, never ahead.
    if (!writeJson(dir + QStringLiteral("/versions/%1.json").arg(saved.version), saved.toJson(), error)
        || !writeJson(dir + QStringLiteral("/design.json"), saved.toJson(), error))
        return false;
    QFile::remove(dir + QStringLiteral("/draft.json"));
    design = saved;
    return true;
}

bool Library::saveDraft(Design &design, QString &error)
{
    const QString id = design.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : design.id;
    const QString dir = designDir(m_path, id);
    if (!QDir().mkpath(dir)) {
        error = QStringLiteral("Can't create %1").arg(dir);
        return false;
    }
    Design draft = design;
    draft.id = id;
    if (!writeJson(dir + QStringLiteral("/draft.json"), draft.toJson(), error))
        return false;
    design.id = id;
    return true;
}

void Library::clearDraft(const QString &id)
{
    QFile::remove(designDir(m_path, id) + QStringLiteral("/draft.json"));
    QDir().rmdir(designDir(m_path, id)); // only if empty: a design that was never saved
}

bool Library::removeDesign(const QString &id, QString &error, QString *trashedPath)
{
    QString inTrash;
    if (id.isEmpty() || !QFile::moveToTrash(designDir(m_path, id), &inTrash)) {
        error = QStringLiteral("Can't move design %1 to the trash.").arg(id);
        return false;
    }
    if (trashedPath)
        *trashedPath = inTrash;
    return true;
}

namespace {
QString sharedFile(const QString &library)
{
    return library + QStringLiteral("/shared.json");
}
} // namespace

QList<SharedElement> Library::sharedElements(QStringList &errors) const
{
    QList<SharedElement> result;
    if (!QFile::exists(sharedFile(m_path)))
        return result;
    QString error;
    const auto json = readJson(sharedFile(m_path), error);
    if (!json) {
        errors << error;
        return result;
    }
    for (auto it = json->constBegin(); it != json->constEnd(); ++it) {
        if (const auto e = SharedElement::fromJson(it.key(), it->toObject()))
            result.append(*e);
        else
            errors << QStringLiteral("%1: entry %2 is not a shared element").arg(sharedFile(m_path), it.key());
    }
    std::sort(result.begin(), result.end(), [](const SharedElement &a, const SharedElement &b) { return QString::localeAwareCompare(a.name, b.name) < 0; });
    return result;
}

bool Library::saveSharedElement(SharedElement &element, QString &error)
{
    QJsonObject all;
    if (QFile::exists(sharedFile(m_path))) {
        const auto json = readJson(sharedFile(m_path), error);
        if (!json)
            return false;
        all = *json;
    }
    SharedElement saved = element;
    if (saved.id.isEmpty())
        saved.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    all.insert(saved.id, saved.toJson());
    if (!writeJson(sharedFile(m_path), all, error))
        return false;
    element = saved;
    return true;
}

Design Library::resolved(const Design &design, QStringList &problems) const
{
    QStringList errors;
    QMap<QString, SharedElement> shared;
    for (const SharedElement &e : sharedElements(errors))
        shared.insert(e.id, e);
    problems << errors;

    std::optional<SharedElement> palette;
    if (const QString id = design.content.value(QStringLiteral("palette")).toString(); !id.isEmpty()) {
        const auto it = shared.constFind(id);
        if (it == shared.cend() || it->kind != SharedElement::Kind::Palette)
            problems << QStringLiteral("The design's palette is no longer in the library.");
        else
            palette = *it;
    }
    // Only colours: a text may well start with "@".
    const auto colours = [&](QJsonObject o) {
        for (const QString &key : {QStringLiteral("color"), QStringLiteral("color2"), QStringLiteral("background")}) {
            const QString value = o.value(key).toString();
            if (!value.startsWith(QLatin1Char('@')))
                continue;
            if (palette && palette->colours.contains(value.mid(1)))
                o.insert(key, palette->colours.value(value.mid(1)));
            else if (palette)
                problems << QStringLiteral("The palette “%1” has no colour “%2”.").arg(palette->name, value.mid(1));
            else if (design.content.value(QStringLiteral("palette")).toString().isEmpty())
                problems << QStringLiteral("The colour %1 needs a palette, and the design has none.").arg(value);
        }
        return o;
    };

    Design out = design;
    out.content.insert(QStringLiteral("background"), colours(design.content.value(QStringLiteral("background")).toObject()));
    QJsonArray elements;
    for (const QJsonValue &v : design.content.value(QStringLiteral("elements")).toArray()) {
        QJsonObject e = colours(v.toObject());
        if (const QString id = e.value(QStringLiteral("shared")).toString(); !id.isEmpty()) {
            const auto it = shared.constFind(id);
            if (it == shared.cend() || it->kind != SharedElement::Kind::Logo)
                problems << QStringLiteral("The design uses a shared logo that is no longer in the library.");
            else
                e.insert(QStringLiteral("asset"), it->asset);
        }
        elements.append(e);
    }
    out.content.insert(QStringLiteral("elements"), elements);
    return out;
}

QString Library::stamp(const Design &resolved)
{
    const QJsonObject input{{QStringLiteral("stage"), stageKey(resolved.stage)}, {QStringLiteral("name"), resolved.name}, {QStringLiteral("content"), resolved.content}};
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(input).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex().left(24));
}

namespace {
QString assetsManifest(const QString &library)
{
    return library + QStringLiteral("/assets/assets.json");
}
} // namespace

QList<Asset> Library::assets(QStringList &errors) const
{
    QList<Asset> result;
    if (!QFile::exists(assetsManifest(m_path)))
        return result;
    QString error;
    const auto json = readJson(assetsManifest(m_path), error);
    if (!json) {
        errors << error;
        return result;
    }
    for (auto it = json->constBegin(); it != json->constEnd(); ++it) {
        if (const auto asset = Asset::fromJson(it.key(), it.value().toObject()))
            result.append(*asset);
        else
            errors << QStringLiteral("%1: entry %2 is not a valid asset").arg(assetsManifest(m_path), it.key());
    }
    return result;
}

QString Library::assetPath(const Asset &asset) const
{
    return m_path + QStringLiteral("/assets/") + asset.hash + (asset.suffix.isEmpty() ? QString() : QLatin1Char('.') + asset.suffix);
}

std::optional<Asset> Library::addAsset(const QString &file, Asset::Kind kind, const QString &name, QString &error, const QString &licence,
                                       const QString &source)
{
    QFile in(file);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!in.open(QIODevice::ReadOnly) || !hash.addData(&in)) {
        error = QStringLiteral("%1: %2").arg(file, in.errorString());
        return std::nullopt;
    }
    Asset asset{QString::fromLatin1(hash.result().toHex().left(24)),
                kind == Asset::Kind::Licence ? QStringLiteral("txt") : QFileInfo(file).suffix().toLower(),
                kind,
                name,
                source.isEmpty() ? QFileInfo(file).absoluteFilePath() : QFileInfo(source).absoluteFilePath(),
                licence,
                QDateTime::currentDateTimeUtc()};

    QJsonObject entries;
    if (QFile::exists(assetsManifest(m_path))) {
        const auto json = readJson(assetsManifest(m_path), error);
        if (!json)
            return std::nullopt;
        entries = *json;
    }
    if (const auto known = Asset::fromJson(asset.hash, entries.value(asset.hash).toObject()))
        return known;

    const QString target = assetPath(asset);
    const QString fresh = target + QStringLiteral(".new");
    QFile::remove(fresh);
    if (!QFile::exists(target) && (!QFile::copy(file, fresh) || !QFile::rename(fresh, target))) {
        QFile::remove(fresh);
        error = QStringLiteral("Can't copy %1 into the library.").arg(file);
        return std::nullopt;
    }
    entries.insert(asset.hash, asset.toJson());
    if (!writeJson(assetsManifest(m_path), entries, error))
        return std::nullopt;
    return asset;
}

std::optional<Asset> Library::addFont(const QString &file, const QString &name, QString &error)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("%1: %2").arg(file, in.errorString());
        return std::nullopt;
    }
    const QByteArray magic = in.read(4);
    if (magic != QByteArray("\x00\x01\x00\x00", 4) && magic != "OTTO" && magic != "true" && magic != "ttcf") {
        error = QStringLiteral("%1 is not a TrueType or OpenType font.").arg(file);
        return std::nullopt;
    }
    QString licenceHash;
    if (const QString licence = fontLicence(file); !licence.isEmpty()) {
        const auto text = addAsset(licence, Asset::Kind::Licence, QStringLiteral("Licence of %1").arg(name), error);
        if (!text)
            return std::nullopt;
        licenceHash = text->hash;
    }
    return addAsset(file, Asset::Kind::Font, name, error, licenceHash);
}

QList<Mask> Library::masks(QStringList &errors) const
{
    QList<Mask> result;
    const QDir dir(m_path + QStringLiteral("/masks"));
    for (const QString &file : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        QString error;
        const auto json = readJson(dir.filePath(file), error);
        const auto mask = json ? Mask::fromJson(file.chopped(5), *json) : std::nullopt;
        if (mask)
            result.append(*mask);
        else
            errors << (error.isEmpty() ? QStringLiteral("%1: not a valid Mask").arg(dir.filePath(file)) : error);
    }
    std::sort(result.begin(), result.end(), [](const Mask &a, const Mask &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return result;
}

bool Library::saveMask(Mask &mask, QString &error)
{
    if (mask.name.trimmed().isEmpty()) {
        error = QStringLiteral("A Mask needs a name.");
        return false;
    }
    const QString id = mask.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : mask.id;
    if (!writeJson(m_path + QStringLiteral("/masks/") + id + QStringLiteral(".json"), mask.toJson(), error))
        return false;
    mask.id = id;
    return true;
}

bool Library::removeMask(const QString &id, QString &error, QString *trashedPath)
{
    QString inTrash;
    if (!QFile::moveToTrash(m_path + QStringLiteral("/masks/") + id + QStringLiteral(".json"), &inTrash)) {
        error = QStringLiteral("Can't move Mask %1 to the trash.").arg(id);
        return false;
    }
    if (trashedPath)
        *trashedPath = inTrash;
    return true;
}

QList<int> Library::historyNumbers() const
{
    QList<int> numbers;
    for (const QString &name : QDir(m_path + QStringLiteral("/history")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok = false;
        const int n = name.toInt(&ok);
        if (ok && n > 0)
            numbers.append(n);
    }
    std::sort(numbers.begin(), numbers.end());
    return numbers;
}

QList<HistoryEntry> Library::history(QStringList &errors) const
{
    QList<HistoryEntry> result;
    const QList<int> numbers = historyNumbers();
    for (auto it = numbers.crbegin(); it != numbers.crend(); ++it) {
        const QString file = QStringLiteral("%1/history/%2/entry.json").arg(m_path).arg(*it);
        QString error;
        const auto json = readJson(file, error);
        const auto entry = json ? HistoryEntry::fromJson(*it, *json) : std::nullopt;
        if (entry)
            result.append(*entry);
        else
            errors << (error.isEmpty() ? QStringLiteral("%1: not a valid history entry").arg(file) : error);
    }
    return result;
}

bool Library::addHistoryEntry(HistoryEntry &entry, QString &error, const QMap<QString, QByteArray> &files)
{
    QList<int> numbers = historyNumbers();
    const int number = numbers.isEmpty() ? 1 : numbers.last() + 1;
    const QString dir = QStringLiteral("%1/history/%2").arg(m_path).arg(number);
    if (!QDir().mkpath(dir) || !writeJson(dir + QStringLiteral("/entry.json"), entry.toJson(), error)) {
        if (error.isEmpty())
            error = QStringLiteral("Can't create %1").arg(dir);
        return false;
    }
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        QSaveFile extra(dir + QLatin1Char('/') + it.key());
        if (!extra.open(QIODevice::WriteOnly) || extra.write(it.value()) < 0 || !extra.commit()) {
            error = QStringLiteral("Can't write %1/%2").arg(dir, it.key());
            return false;
        }
    }
    entry.number = number;

    numbers.append(number);
    while (numbers.size() > m_settings.historySize)
        QDir(QStringLiteral("%1/history/%2").arg(m_path).arg(numbers.takeFirst())).removeRecursively();
    return true;
}

std::optional<QJsonObject> Library::original(Stage stage) const
{
    QString error;
    const auto json = readJson(m_path + QStringLiteral("/history/original.json"), error);
    const QJsonValue value = json ? json->value(stageKey(stage)) : QJsonValue();
    return value.isObject() ? std::optional(value.toObject()) : std::nullopt;
}

bool Library::saveOriginalIfMissing(Stage stage, const QJsonObject &state, QString &error)
{
    const QString file = m_path + QStringLiteral("/history/original.json");
    QJsonObject all;
    if (QFile::exists(file)) {
        const auto json = readJson(file, error);
        if (!json)
            return false;
        all = *json;
    }
    if (all.contains(stageKey(stage)))
        return true;
    all.insert(stageKey(stage), state);
    return writeJson(file, all, error);
}

std::optional<QJsonObject> Library::pendingCheck() const
{
    QString error;
    return readJson(m_path + QStringLiteral("/pending-check.json"), error);
}

bool Library::setPendingCheck(const QJsonObject &check, QString &error)
{
    return writeJson(m_path + QStringLiteral("/pending-check.json"), check, error);
}

void Library::clearPendingCheck()
{
    QFile::remove(m_path + QStringLiteral("/pending-check.json"));
}

void Library::pruneBuilds()
{
    QStringList errors;
    QSet<QString> used;
    for (const HistoryEntry &e : history(errors)) {
        const QJsonObject target = e.state.value(QStringLiteral("target")).toObject();
        for (auto it = target.constBegin(); it != target.constEnd(); ++it)
            used.insert(it.value().toObject().value(QStringLiteral("build")).toString());
    }
    if (!errors.isEmpty())
        return; // an unreadable entry might need a build: keep everything
    const QDir builds(m_path + QStringLiteral("/builds"));
    for (const QString &hash : builds.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!used.contains(hash))
            QDir(builds.filePath(hash)).removeRecursively();
    }
}

bool Library::resetOriginal(const QJsonObject &stages, QString &error)
{
    return writeJson(m_path + QStringLiteral("/history/original.json"), stages, error);
}
