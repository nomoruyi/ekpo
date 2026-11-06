#pragma once

#include "library.h"
#include "probe.h"

#include <QObject>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

// What the QML pages see: system state per stage, the library's imports, and
// installed themes not imported yet. Plain maps and lists keep the QML simple.
class Backend : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList stageCards READ stageCards NOTIFY changed)
    Q_PROPERTY(QVariantMap system READ system NOTIFY changed)
    Q_PROPERTY(QVariantList imports READ imports NOTIFY changed)
    Q_PROPERTY(QVariantList installed READ installed NOTIFY changed)

public:
    explicit Backend(QObject *parent = nullptr);

    QVariantList stageCards() const { return m_stageCards; }
    QVariantMap system() const { return m_system; }
    QVariantList imports() const { return m_imports; }
    QVariantList installed() const { return m_installed; }

    Q_INVOKABLE void refresh();
    // Imports a folder or archive; returns {names: [...], errors: [...]}.
    Q_INVOKABLE QVariantMap importPath(const QUrl &path);
    Q_INVOKABLE QVariantMap reimport(const QString &id);
    Q_INVOKABLE QString removeImport(const QString &id); // empty on success

Q_SIGNALS:
    void changed();

private:
    QString m_rootDir = QStringLiteral("/");
    QString m_homeDir;
    std::optional<Library> m_library;
    QString m_libraryError;
    SystemState m_state;

    QVariantList m_stageCards;
    QVariantMap m_system;
    QVariantList m_imports;
    QVariantList m_installed;
};
