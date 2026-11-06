#pragma once

#include "model.h"
#include "payload.h"

#include <QStringList>

#include <optional>

// The plain-file library (ARCHITECTURE.md §5): library.json, imports/, masks/,
// history/. Assets, designs and builds join it in later steps.
class Library
{
public:
    static constexpr int currentFormat = 1;

    static QString defaultPath(); // ~/.local/share/ekpo

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
