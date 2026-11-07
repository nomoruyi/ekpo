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

// One slot of a Mask (SPEC.md §4.8).
struct Slot {
    enum class Kind { LeaveAsIs, Import, Design };
    Kind kind = Kind::LeaveAsIs;
    QString ref; // import or design id

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

// A design made in one of Ekpo's editors (SPEC.md §4.4). Each save is a version;
// the content is defined by that stage's editor and generator, the library stores
// it as given.
struct Design {
    QString id; // folder name in designs/, assigned on first save or draft
    Stage stage = Stage::PlasmaSplash;
    QString name;
    int version = 0; // the saved version; 0 = never saved
    QDateTime savedAt;
    QJsonObject content;

    QJsonObject toJson() const;
    static std::optional<Design> fromJson(const QString &id, const QJsonObject &json);
};

// A file stored once in the library and referenced by designs (SPEC.md §3), with
// where it came from and its licence.
struct Asset {
    enum class Kind { Image, Video, Font, Licence };

    QString hash; // content hash; the file is assets/<hash>.<suffix>
    QString suffix; // lower case, may be empty
    Kind kind = Kind::Image;
    QString name;
    QString source; // the file it was copied from
    QString licence; // hash of the licence text asset; empty if none was found
    QDateTime addedAt;

    QJsonObject toJson() const;
    static std::optional<Asset> fromJson(const QString &hash, const QJsonObject &json);
};

// A logo or palette designs link to (SPEC.md §3): changing it changes every design
// that uses it; looks already applied change when they are applied again.
struct SharedElement {
    enum class Kind { Logo, Palette };

    QString id; // assigned on first save
    Kind kind = Kind::Logo;
    QString name;
    QString asset; // a logo's image asset
    QMap<QString, QString> colours; // a palette's: paletteKeys → "#rrggbb"

    QJsonObject toJson() const;
    static std::optional<SharedElement> fromJson(const QString &id, const QJsonObject &json);
};

// A design's colour "@accent" is its palette's accent colour, and so on (SPEC.md §4.4):
// gradient is a background gradient's second colour, accent2 a second accent.
inline const QStringList paletteKeys = {QStringLiteral("background"), QStringLiteral("gradient"), QStringLiteral("accent"), QStringLiteral("accent2"),
                                        QStringLiteral("text")};

// App preferences (SPEC.md §4.13). Machine boot options live in history, not here.
struct Settings {
    int historySize = 10;
    bool technicalDetailsExpanded = false;
    bool showIntro = true; // the intro dialog at start (SPEC.md §4.2)
    bool rebuildAllKernels = false; // default of the review's switch for boot splash applies
    bool checkUpdates = true; // ask GitHub for a newer release at start (SPEC.md §4.14)

    QJsonObject toJson() const;
    static Settings fromJson(const QJsonObject &json);
};
