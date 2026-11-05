#include "library.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
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

QString manifestPath(const QString &library)
{
    return library + QStringLiteral("/library.json");
}

} // namespace

QString Library::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/personae");
}

std::optional<Library> Library::open(const QString &path, QString &error)
{
    Library library(QDir::cleanPath(path));
    const QDir dir(library.m_path);

    if (!QFile::exists(manifestPath(library.m_path))) {
        if (dir.exists() && !dir.isEmpty()) {
            error = QStringLiteral("%1 is not empty and not a Personae library").arg(library.m_path);
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
        error = QStringLiteral("The library was written by a newer Personae (format %1).").arg(format);
        return std::nullopt;
    }
    // ponytail: no migrations yet; add a step per format here once format 2 exists.
    if (format != currentFormat) {
        error = QStringLiteral("Unknown library format %1.").arg(format);
        return std::nullopt;
    }
    library.m_settings = Settings::fromJson(manifest->value(QStringLiteral("settings")).toObject());

    for (const QString &sub : {QStringLiteral("personas"), QStringLiteral("history")}) {
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

QList<Persona> Library::personas(QStringList &errors) const
{
    QList<Persona> result;
    const QDir dir(m_path + QStringLiteral("/personas"));
    for (const QString &file : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        QString error;
        const auto json = readJson(dir.filePath(file), error);
        const auto persona = json ? Persona::fromJson(file.chopped(5), *json) : std::nullopt;
        if (persona)
            result.append(*persona);
        else
            errors << (error.isEmpty() ? QStringLiteral("%1: not a valid Persona").arg(dir.filePath(file)) : error);
    }
    std::sort(result.begin(), result.end(), [](const Persona &a, const Persona &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return result;
}

bool Library::savePersona(Persona &persona, QString &error)
{
    if (persona.name.trimmed().isEmpty()) {
        error = QStringLiteral("A Persona needs a name.");
        return false;
    }
    const QString id = persona.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : persona.id;
    if (!writeJson(m_path + QStringLiteral("/personas/") + id + QStringLiteral(".json"), persona.toJson(), error))
        return false;
    persona.id = id;
    return true;
}

bool Library::removePersona(const QString &id, QString &error, QString *trashedPath)
{
    QString inTrash;
    if (!QFile::moveToTrash(m_path + QStringLiteral("/personas/") + id + QStringLiteral(".json"), &inTrash)) {
        error = QStringLiteral("Can't move Persona %1 to the trash.").arg(id);
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

bool Library::addHistoryEntry(HistoryEntry &entry, QString &error)
{
    QList<int> numbers = historyNumbers();
    const int number = numbers.isEmpty() ? 1 : numbers.last() + 1;
    const QString dir = QStringLiteral("%1/history/%2").arg(m_path).arg(number);
    if (!QDir().mkpath(dir) || !writeJson(dir + QStringLiteral("/entry.json"), entry.toJson(), error)) {
        if (error.isEmpty())
            error = QStringLiteral("Can't create %1").arg(dir);
        return false;
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
