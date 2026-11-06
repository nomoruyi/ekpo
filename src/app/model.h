#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

// The three boot stages, in boot order (SPEC.md §1).
enum class Stage { BootMenu, BootSplash, PlasmaSplash };

inline const QList<Stage> allStages = {Stage::BootMenu, Stage::BootSplash, Stage::PlasmaSplash};

QString stageKey(Stage stage); // "bootMenu", "bootSplash", "plasmaSplash"
std::optional<Stage> stageFromKey(const QString &key);

// A third-party theme copied into the library read-only (SPEC.md §4.3).
struct ImportedTheme {
    enum class Verdict {
        Works, // no problems found
        Warnings, // applies, but parts may not show
        Repaired, // structure fixed on import; installs as a cleaned-up copy
        Broken, // can't be applied
    };

    QString id; // folder name in imports/, assigned on first save
    Stage stage = Stage::BootMenu;
    QString name;
    QString sourcePath; // the theme folder it came from, for Re-import
    QString installedId; // set when the source is installed and can be selected in place
    QDateTime importedAt;
    Verdict verdict = Verdict::Works;
    QStringList report; // one finding per line, for the import report

    QJsonObject toJson() const;
    static std::optional<ImportedTheme> fromJson(const QString &id, const QJsonObject &json);
};

QString verdictKey(ImportedTheme::Verdict verdict); // "works", "warnings", "repaired", "broken"

// One slot of a Mask (SPEC.md §4.8). M2 adds designs as a third kind.
struct Slot {
    enum class Kind { LeaveAsIs, Import };
    Kind kind = Kind::LeaveAsIs;
    QString ref; // import id when kind == Import

    QJsonObject toJson() const;
    static std::optional<Slot> fromJson(const QJsonObject &json);
};

struct Mask {
    QString id; // file name in masks/, assigned on first save
    QString name;
    QMap<Stage, Slot> stageSlots; // missing stage = leave as is

    QJsonObject toJson() const;
    static std::optional<Mask> fromJson(const QString &id, const QJsonObject &json);
};

// One apply or restore (SPEC.md §4.11). The per-stage state is defined by the
// planner and executor; the library stores it as given.
struct HistoryEntry {
    int number = 0; // assigned by the library, increasing
    QDateTime createdAt;
    QString title; // e.g. the Mask name, or "Restore Original"
    QJsonObject state;

    QJsonObject toJson() const;
    static std::optional<HistoryEntry> fromJson(int number, const QJsonObject &json);
};

// App preferences (SPEC.md §4.13). Machine boot options live in history, not here.
struct Settings {
    int historySize = 10;
    bool technicalDetailsExpanded = false;
    bool showIntro = true; // the intro dialog at start (SPEC.md §4.2)
    bool rebuildAllKernels = false; // default of the review's switch for boot splash applies

    QJsonObject toJson() const;
    static Settings fromJson(const QJsonObject &json);
};
