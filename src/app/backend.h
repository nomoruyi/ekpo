#pragma once

#include "executor.h"
#include "library.h"
#include "planner.h"
#include "probe.h"

#include <QObject>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <optional>

// What the QML pages see: system state per stage, the library's imports, installed
// themes not imported yet, history, and the plan under review. Plain maps and lists
// keep the QML simple.
class Backend : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList stageCards READ stageCards NOTIFY changed)
    Q_PROPERTY(QVariantMap system READ system NOTIFY changed)
    Q_PROPERTY(QVariantList imports READ imports NOTIFY changed)
    Q_PROPERTY(QVariantList installed READ installed NOTIFY changed)
    Q_PROPERTY(QVariantList history READ history NOTIFY changed)
    Q_PROPERTY(bool originalAvailable READ originalAvailable NOTIFY changed)
    Q_PROPERTY(bool technicalDetailsExpanded READ technicalDetailsExpanded NOTIFY changed)
    Q_PROPERTY(QVariantMap plan READ plan NOTIFY planChanged)
    Q_PROPERTY(bool applying READ applying NOTIFY applyingChanged)
    Q_PROPERTY(QVariantMap pendingCheck READ pendingCheck NOTIFY changed)
    Q_PROPERTY(QVariantList masks READ masks NOTIFY changed)
    Q_PROPERTY(bool showIntro READ showIntro NOTIFY changed)

public:
    explicit Backend(QObject *parent = nullptr);

    QVariantList stageCards() const { return m_stageCards; }
    QVariantMap system() const { return m_system; }
    QVariantList imports() const { return m_imports; }
    QVariantList installed() const { return m_installed; }
    QVariantList history() const { return m_history; }
    bool originalAvailable() const;
    bool technicalDetailsExpanded() const { return m_library && m_library->settings().technicalDetailsExpanded; }
    QVariantMap plan() const { return m_planMap; }
    bool applying() const { return m_executor && m_executor->isRunning(); }
    QVariantMap pendingCheck() const { return m_pendingCheck; }
    QVariantList masks() const { return m_masks; }
    bool showIntro() const { return m_library && m_library->settings().showIntro; }
    Q_INVOKABLE void setShowIntro(bool show);

    // Masks (SPEC.md §4.8). Empty refs leave a stage as it is.
    Q_INVOKABLE QString saveMask(const QString &id, const QString &name, const QString &bootMenu, const QString &bootSplash, const QString &plasmaSplash);
    Q_INVOKABLE QString removeMask(const QString &id);
    Q_INVOKABLE void planApplyMask(const QString &id);

    Q_INVOKABLE void refresh();
    // Imports a folder or archive; returns {names: [...], errors: [...]}.
    Q_INVOKABLE QVariantMap importPath(const QUrl &path);
    Q_INVOKABLE QVariantMap reimport(const QString &id);
    Q_INVOKABLE QString removeImport(const QString &id); // empty on success

    // Each prepares the plan property for the review page.
    Q_INVOKABLE void planApplyImport(const QString &id);
    Q_INVOKABLE void planRestoreEntry(int number);
    Q_INVOKABLE void planRestoreOriginal();
    Q_INVOKABLE void applyPlan();

    // Changes made outside Ekpo (SPEC.md §4.12).
    Q_INVOKABLE void adoptStage(const QString &stage);
    Q_INVOKABLE void planReapplyStage(const QString &stage);
    Q_INVOKABLE void planTimeout(int seconds);
    Q_INVOKABLE void recover(); // rolls back an interrupted apply
    // The after-reboot question.
    Q_INVOKABLE void keepCheck();
    Q_INVOKABLE void planRevertCheck();

Q_SIGNALS:
    void changed();
    void planChanged();
    void applyingChanged();
    void applyProgress(const QString &text);
    void applyFinished(bool ok, const QString &error, int historyNumber);

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
    QVariantList m_history;
    QVariantMap m_pendingCheck;
    QVariantList m_masks;

    void setPlan(const Plan &plan);
    Plan m_plan;
    QVariantMap m_planMap;
    HelperLauncher m_launcher;
    std::unique_ptr<Executor> m_executor;
};
