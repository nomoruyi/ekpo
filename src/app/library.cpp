#include "library.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
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

} // namespace

QString Library::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/ekpo");
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
    // ponytail: no migrations yet; add a step per format here once format 2 exists.
    if (format != currentFormat) {
        error = QStringLiteral("Unknown library format %1.").arg(format);
        return std::nullopt;
    }
    library.m_settings = Settings::fromJson(manifest->value(QStringLiteral("settings")).toObject());

    for (const QString &sub : {QStringLiteral("imports"), QStringLiteral("masks"), QStringLiteral("history")}) {
        if (!dir.mkpath(sub)) {
            error = QStringLiteral("Can't create %1").arg(dir.filePath(sub));
            return std::nullopt;
        }
    }
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
