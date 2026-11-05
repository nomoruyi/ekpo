#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>

#include <optional>

// The three boot stages, in boot order (SPEC.md §1).
enum class Stage { BootMenu, BootSplash, PlasmaSplash };

inline const QList<Stage> allStages = {Stage::BootMenu, Stage::BootSplash, Stage::PlasmaSplash};

QString stageKey(Stage stage); // "bootMenu", "bootSplash", "plasmaSplash"
std::optional<Stage> stageFromKey(const QString &key);

// One slot of a Persona (SPEC.md §4.8). M2 adds designs as a third kind.
struct Slot {
    enum class Kind { LeaveAsIs, Import };
    Kind kind = Kind::LeaveAsIs;
    QString ref; // import id when kind == Import

    QJsonObject toJson() const;
    static std::optional<Slot> fromJson(const QJsonObject &json);
};

struct Persona {
    QString id; // file name in personas/, assigned on first save
    QString name;
    QMap<Stage, Slot> stageSlots; // missing stage = leave as is

    QJsonObject toJson() const;
    static std::optional<Persona> fromJson(const QString &id, const QJsonObject &json);
};

// One apply or restore (SPEC.md §4.11). The per-stage state is defined by the
// planner and executor; the library stores it as given.
struct HistoryEntry {
    int number = 0; // assigned by the library, increasing
    QDateTime createdAt;
    QString title; // e.g. the Persona name, or "Restore Original"
    QJsonObject state;

    QJsonObject toJson() const;
    static std::optional<HistoryEntry> fromJson(int number, const QJsonObject &json);
};

// App preferences (SPEC.md §4.13). Machine boot options live in history, not here.
struct Settings {
    int historySize = 10;
    bool technicalDetailsExpanded = false;

    QJsonObject toJson() const;
    static Settings fromJson(const QJsonObject &json);
};
