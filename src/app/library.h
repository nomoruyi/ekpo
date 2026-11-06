#pragma once

#include "model.h"

#include <QStringList>

#include <optional>

// The plain-file library (ARCHITECTURE.md §5): library.json, imports/, personas/,
// history/. Assets, designs and builds join it in later steps.
class Library
{
public:
    static constexpr int currentFormat = 1;

    static QString defaultPath(); // ~/.local/share/personae

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

    QList<Persona> personas(QStringList &errors) const; // sorted by name; unreadable files go to errors
    bool savePersona(Persona &persona, QString &error); // assigns the id on first save
    bool removePersona(const QString &id, QString &error, QString *trashedPath = nullptr);

    QList<HistoryEntry> history(QStringList &errors) const; // newest first
    bool addHistoryEntry(HistoryEntry &entry, QString &error); // numbers it, keeps the newest historySize
    std::optional<QJsonObject> original(Stage stage) const;
    // Original is captured once per stage, right before Personae first changes it.
    bool saveOriginalIfMissing(Stage stage, const QJsonObject &state, QString &error);

private:
    explicit Library(const QString &path) : m_path(path) {}
    bool saveManifest(const Settings &settings, QString &error);
    QList<int> historyNumbers() const; // ascending

    QString m_path;
    Settings m_settings;
};
