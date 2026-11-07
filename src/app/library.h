#pragma once

#include "model.h"
#include "payload.h"

#include <QStringList>

#include <optional>

// The plain-file library (ARCHITECTURE.md §5): library.json, imports/, designs/,
// assets/, masks/, builds/, history/.
class Library
{
public:
    static constexpr int currentFormat = 2; // 2: designs/ and assets/

    static QString defaultPath(); // ~/.local/share/ekpo
    // The library location from ~/.config/ekporc ([Library] Path), else defaultPath().
    static QString configuredPath();
    static bool setConfiguredPath(const QString &path, QString &error);
    // Moves the library at from to the folder to (missing or empty), or, if to
    // already holds an Ekpo library, leaves both alone and uses that one. Returns
    // the path to use; refuses a folder with other files.
    static std::optional<QString> relocate(const QString &from, const QString &to, QString &error);

    // Opens the library at path, creating it if the folder is missing or empty.
    // Refuses a folder that holds other files, and a newer or unknown format.
    static std::optional<Library> open(const QString &path, QString &error);

    const QString &path() const { return m_path; }
    const Settings &settings() const { return m_settings; }
    bool saveSettings(const Settings &settings, QString &error);

    QList<ImportedTheme> imports(QStringList &errors) const; // sorted by name
    // Copies the theme folder verbatim into imports/<id>/files (replacing an earlier
    // copy) and writes import.json. Symlinks are followed only within the folder.
    bool saveImport(ImportedTheme &theme, const QString &themeRoot, QString &error);
    bool removeImport(const QString &id, QString &error, QString *trashedPath = nullptr);
    QString importFilesPath(const QString &id) const;

    // Theme files Ekpo installs as copies, stored by content hash (builds/<hash>/) so
    // history entries can be restored after the import is gone.
    static QString buildHash(const QList<PayloadFile> &files);
    bool saveBuild(const QList<PayloadFile> &files, QString &error); // no-op if stored already
    std::optional<QList<PayloadFile>> buildFiles(const QString &hash) const;
    // Deletes builds no history entry refers to (history/<n>/entry.json target.*.build).
    void pruneBuilds();

    QList<Design> designs(QStringList &errors) const; // current versions, sorted by name
    // The design as saved in version (0 = the current one).
    std::optional<Design> design(const QString &id, int version, QString &error) const;
    QList<int> designVersions(const QString &id) const; // ascending
    // Saves the next version (all are kept), assigns id, version and savedAt, and
    // clears the draft.
    bool saveDesign(Design &design, QString &error);
    // Crash recovery: the editor writes the open design here while it has unsaved
    // changes. Assigns the id of a design that was never saved.
    bool saveDraft(Design &design, QString &error);
    QList<Design> drafts(QStringList &errors) const; // version = the one it started from
    void clearDraft(const QString &id);
    bool removeDesign(const QString &id, QString &error, QString *trashedPath = nullptr); // all versions, to the trash

    // Shared logos and palettes (shared.json), sorted by name.
    QList<SharedElement> sharedElements(QStringList &errors) const;
    bool saveSharedElement(SharedElement &element, QString &error); // assigns the id
    // The design with its shared elements put in, which is what the generators build:
    // an element with "shared" gets that logo's asset, a colour "@accent" (and so on)
    // the linked palette's ("palette" in the content). Missing ones go to problems.
    Design resolved(const Design &design, QStringList &problems) const;
    // Changes whenever what a resolved design builds changes (name and content).
    static QString stamp(const Design &resolved);

    QList<Asset> assets(QStringList &errors) const;
    QString assetPath(const Asset &asset) const;
    // Copies the file into assets/ and registers it; the same content is stored
    // once, and adding it again returns the first registration. source is where it
    // came from, if not file itself (e.g. the video a converted MP4 was made from).
    std::optional<Asset> addAsset(const QString &file, Asset::Kind kind, const QString &name, QString &error,
                                  const QString &licence = {}, const QString &source = {});
    // A TrueType/OpenType font, with its licence: a LICENSE, OFL or COPYING file
    // next to it, else the copyright file of the Debian package that ships it.
    std::optional<Asset> addFont(const QString &file, const QString &name, QString &error);

    QList<Mask> masks(QStringList &errors) const; // sorted by name; unreadable files go to errors
    bool saveMask(Mask &mask, QString &error); // assigns the id on first save
    bool removeMask(const QString &id, QString &error, QString *trashedPath = nullptr);

    QList<HistoryEntry> history(QStringList &errors) const; // newest first
    // Numbers the entry, writes extra text files next to it (e.g. RESTORE.txt) and
    // keeps the newest historySize entries.
    bool addHistoryEntry(HistoryEntry &entry, QString &error, const QMap<QString, QByteArray> &files = {});
    std::optional<QJsonObject> original(Stage stage) const;
    // Original is captured once per stage, right before Ekpo first changes it.
    bool saveOriginalIfMissing(Stage stage, const QJsonObject &state, QString &error);
    // Replaces Original for every stage, e.g. after a reinstall ("Start fresh").
    bool resetOriginal(const QJsonObject &stages, QString &error);

    // The after-reboot question for the last boot-stage apply: {number, title, bootId}.
    std::optional<QJsonObject> pendingCheck() const;
    bool setPendingCheck(const QJsonObject &check, QString &error);
    void clearPendingCheck();

private:
    explicit Library(const QString &path) : m_path(path) {}
    bool saveManifest(const Settings &settings, QString &error);
    QList<int> historyNumbers() const; // ascending

    QString m_path;
    Settings m_settings;
};
