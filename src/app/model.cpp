#include "model.h"

#include <QJsonArray>

namespace {

const QMap<Stage, QString> stageKeys = {
    {Stage::BootMenu, QStringLiteral("bootMenu")},
    {Stage::BootSplash, QStringLiteral("bootSplash")},
    {Stage::PlasmaSplash, QStringLiteral("plasmaSplash")},
};

const QMap<ImportedTheme::Verdict, QString> verdictKeys = {
    {ImportedTheme::Verdict::Works, QStringLiteral("works")},
    {ImportedTheme::Verdict::Warnings, QStringLiteral("warnings")},
    {ImportedTheme::Verdict::Repaired, QStringLiteral("repaired")},
    {ImportedTheme::Verdict::Broken, QStringLiteral("broken")},
};

} // namespace

QString stageKey(Stage stage)
{
    return stageKeys.value(stage);
}

std::optional<Stage> stageFromKey(const QString &key)
{
    for (auto it = stageKeys.cbegin(); it != stageKeys.cend(); ++it) {
        if (it.value() == key)
            return it.key();
    }
    return std::nullopt;
}

QString verdictKey(ImportedTheme::Verdict verdict)
{
    return verdictKeys.value(verdict);
}

QJsonObject ImportedTheme::toJson() const
{
    return {
        {QStringLiteral("stage"), stageKey(stage)},
        {QStringLiteral("name"), name},
        {QStringLiteral("sourcePath"), sourcePath},
        {QStringLiteral("installedId"), installedId},
        {QStringLiteral("importedAt"), importedAt.toUTC().toString(Qt::ISODate)},
        {QStringLiteral("verdict"), verdictKey(verdict)},
        {QStringLiteral("report"), QJsonArray::fromStringList(report)},
    };
}

std::optional<ImportedTheme> ImportedTheme::fromJson(const QString &id, const QJsonObject &json)
{
    const auto stage = stageFromKey(json.value(QStringLiteral("stage")).toString());
    const auto verdict = verdictKeys.key(json.value(QStringLiteral("verdict")).toString(), ImportedTheme::Verdict(-1));
    if (!stage || !verdictKeys.contains(verdict))
        return std::nullopt;

    ImportedTheme t;
    t.id = id;
    t.stage = *stage;
    t.name = json.value(QStringLiteral("name")).toString();
    t.sourcePath = json.value(QStringLiteral("sourcePath")).toString();
    t.installedId = json.value(QStringLiteral("installedId")).toString();
    t.importedAt = QDateTime::fromString(json.value(QStringLiteral("importedAt")).toString(), Qt::ISODate);
    t.verdict = verdict;
    for (const QJsonValue &line : json.value(QStringLiteral("report")).toArray())
        t.report << line.toString();
    return t;
}

QJsonObject Slot::toJson() const
{
    if (kind == Kind::Import)
        return {{QStringLiteral("kind"), QStringLiteral("import")}, {QStringLiteral("ref"), ref}};
    return {{QStringLiteral("kind"), QStringLiteral("leaveAsIs")}};
}

std::optional<Slot> Slot::fromJson(const QJsonObject &json)
{
    const QString kind = json.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("leaveAsIs"))
        return Slot{};
    const QString ref = json.value(QStringLiteral("ref")).toString();
    if (kind == QLatin1String("import") && !ref.isEmpty())
        return Slot{Kind::Import, ref};
    return std::nullopt;
}

QJsonObject Persona::toJson() const
{
    QJsonObject slotsJson;
    for (auto it = stageSlots.cbegin(); it != stageSlots.cend(); ++it)
        slotsJson.insert(stageKey(it.key()), it.value().toJson());
    return {{QStringLiteral("name"), name}, {QStringLiteral("slots"), slotsJson}};
}

std::optional<Persona> Persona::fromJson(const QString &id, const QJsonObject &json)
{
    Persona p{id, json.value(QStringLiteral("name")).toString(), {}};
    const QJsonObject slotsJson = json.value(QStringLiteral("slots")).toObject();
    for (auto it = slotsJson.constBegin(); it != slotsJson.constEnd(); ++it) {
        const auto stage = stageFromKey(it.key());
        const auto slot = Slot::fromJson(it.value().toObject());
        if (!stage || !slot)
            return std::nullopt;
        p.stageSlots.insert(*stage, *slot);
    }
    if (p.name.isEmpty())
        return std::nullopt;
    return p;
}

QJsonObject HistoryEntry::toJson() const
{
    return {
        {QStringLiteral("createdAt"), createdAt.toUTC().toString(Qt::ISODate)},
        {QStringLiteral("title"), title},
        {QStringLiteral("state"), state},
    };
}

std::optional<HistoryEntry> HistoryEntry::fromJson(int number, const QJsonObject &json)
{
    HistoryEntry e{number,
                   QDateTime::fromString(json.value(QStringLiteral("createdAt")).toString(), Qt::ISODate),
                   json.value(QStringLiteral("title")).toString(),
                   json.value(QStringLiteral("state")).toObject()};
    if (!e.createdAt.isValid())
        return std::nullopt;
    return e;
}

QJsonObject Settings::toJson() const
{
    return {
        {QStringLiteral("historySize"), historySize},
        {QStringLiteral("technicalDetailsExpanded"), technicalDetailsExpanded},
    };
}

Settings Settings::fromJson(const QJsonObject &json)
{
    Settings s;
    s.historySize = qBound(1, json.value(QStringLiteral("historySize")).toInt(s.historySize), 100);
    s.technicalDetailsExpanded = json.value(QStringLiteral("technicalDetailsExpanded")).toBool(s.technicalDetailsExpanded);
    return s;
}
