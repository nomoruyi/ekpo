#pragma once

#include "designeditor.h"
#include "executor.h"
#include "library.h"
#include "planner.h"
#include "probe.h"
#include "updater.h"

#include <QImage>
#include <QObject>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
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
    Q_PROPERTY(QVariantList designs READ designs NOTIFY changed)
    Q_PROPERTY(DesignEditor *editor READ editor CONSTANT)
    Q_PROPERTY(QVariantList installed READ installed NOTIFY changed)
    Q_PROPERTY(QVariantList history READ history NOTIFY changed)
    Q_PROPERTY(bool originalAvailable READ originalAvailable NOTIFY changed)
    Q_PROPERTY(bool technicalDetailsExpanded READ technicalDetailsExpanded NOTIFY changed)
    Q_PROPERTY(QVariantMap plan READ plan NOTIFY planChanged)
    Q_PROPERTY(bool applying READ applying NOTIFY applyingChanged)
    Q_PROPERTY(QVariantMap pendingCheck READ pendingCheck NOTIFY changed)
    Q_PROPERTY(QVariantList masks READ masks NOTIFY changed) // {id, name, slots, update: a design in it changed since it was applied}
    Q_PROPERTY(bool showIntro READ showIntro NOTIFY changed)
    Q_PROPERTY(bool rebuildAllKernels READ rebuildAllKernels NOTIFY changed)
    Q_PROPERTY(bool atOriginal READ atOriginal NOTIFY changed)
    Q_PROPERTY(QString libraryPath READ libraryPath NOTIFY changed)
    Q_PROPERTY(int historySize READ historySize NOTIFY changed)
    Q_PROPERTY(int ekpoCopies READ ekpoCopies NOTIFY changed)
    Q_PROPERTY(bool welcomeBack READ welcomeBack NOTIFY changed)
    Q_PROPERTY(QString bugReportsDir READ bugReportsDir CONSTANT) // empty: no local reports (packages)
    // {canCheck, checking, checked, checkError, available, version, notes, busy, text, fraction, error, installed}
    Q_PROPERTY(QVariantMap updateInfo READ updateInfo NOTIFY updateChanged)
    Q_PROPERTY(bool checkUpdates READ checkUpdates NOTIFY changed)

public:
    explicit Backend(QObject *parent = nullptr);

    QVariantList stageCards() const { return m_stageCards; }
    QVariantMap system() const { return m_system; }
    QVariantList imports() const { return m_imports; }
    // Saved designs and unsaved drafts (version 0): {id, name, stage, version, hasDraft}.
    QVariantList designs() const { return m_designs; }
    DesignEditor *editor() { return &m_editor; }
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
    bool rebuildAllKernels() const { return m_library && m_library->settings().rebuildAllKernels; }
    Q_INVOKABLE void setRebuildAllKernels(bool all);
    Q_INVOKABLE void setTechnicalDetailsExpanded(bool expanded);
    // The review's switch: re-plans the current review for all kernels or the newest only.
    Q_INVOKABLE void setPlanAllKernels(bool all);
    // The review's screen choice ("2560x1440"): re-plans the current review for it.
    Q_INVOKABLE void setPlanScreen(const QString &size);

    // Removing Ekpo from the system (SPEC.md §5): Restore Original first, then this.
    bool atOriginal() const { return m_atOriginal; }
    int ekpoCopies() const { return m_ekpoCopies; }
    Q_INVOKABLE void removeEkpoFiles();

    // After a reinstall (SPEC.md §4.2).
    bool welcomeBack() const { return m_welcomeBack; }
    Q_INVOKABLE void planReapplyLastLook();
    Q_INVOKABLE void startFresh();

    // For the About page: versions and the current state as plain text.
    Q_INVOKABLE QString systemReport(bool extended = true); // not extended: only Ekpo's version, the OS and the kernel
    Q_INVOKABLE void copyToClipboard(const QString &text);
    // A desktop notification (org.freedesktop.Notifications), for applies that end
    // while the window is in the background.
    Q_INVOKABLE void notify(const QString &summary, const QString &body);

    // Report a Bug (bugreport.h). captureScreen() grabs the window as it is now, for the
    // report, and returns a small preview as a data: URL (empty if the grab failed).
    Q_INVOKABLE QString captureScreen(QObject *window);
    Q_INVOKABLE QString recentLog(); // Ekpo's last messages and the helper's last log lines
    // Puts the screenshot on the clipboard and opens the pre-filled issue in the browser.
    Q_INVOKABLE QString fitBugReport(const QString &title, const QString &body); // shortened to fit a GitHub link
    Q_INVOKABLE QString openBugReport(const QString &title, const QString &body, bool withScreenshot); // empty on success
    Q_INVOKABLE QVariantMap saveBugReport(const QString &title, const QString &body, bool withScreenshot); // {path} or {error}
    QString bugReportsDir() const;

    // Updates from GitHub releases (SPEC.md §4.14).
    QVariantMap updateInfo() const { return m_update; }
    bool checkUpdates() const { return !m_library || m_library->settings().checkUpdates; }
    Q_INVOKABLE void setCheckUpdates(bool check);
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void startUpdate(); // the newer release checkForUpdates found
    Q_INVOKABLE void cancelUpdate();
    Q_INVOKABLE void restartApp();

    QString libraryPath() const { return m_library ? m_library->path() : Library::configuredPath(); }
    int historySize() const { return m_library ? m_library->settings().historySize : 10; }
    // Moves the library to an empty folder, or switches to a library found there.
    Q_INVOKABLE QString setLibraryLocation(const QUrl &folder); // empty on success
    Q_INVOKABLE void setHistorySize(int size);

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
    Q_INVOKABLE void planApplyDesign(const QString &id);
    // After a monitor change: the applied boot menu design for the primary screen (SPEC.md §4.12).
    Q_INVOKABLE void planRebuildBootMenu();
    Q_INVOKABLE QString removeDesign(const QString &id); // empty on success
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
    void bugReportRequested(); // a page's Report a Bug button; Main.qml opens the dialog
    void updateChanged();

private:
    QString m_rootDir = QStringLiteral("/");
    QString m_homeDir;
    std::optional<Library> m_library;
    QString m_libraryError;
    DesignEditor m_editor; // after m_library: it writes its draft on destruction
    SystemState m_state;
    QImage m_screenshot; // for the bug report being written

    QVariantList m_stageCards;
    QVariantMap m_system;
    QVariantList m_imports;
    QVariantList m_designs;
    QVariantList m_installed;
    QVariantList m_history;
    QVariantMap m_pendingCheck;
    QVariantList m_masks;
    bool m_atOriginal = true;
    int m_ekpoCopies = 0;
    bool m_removingFiles = false;
    bool m_welcomeBack = false;

    void createExecutor();
    void setPlan(const Plan &plan);
    // screen: what designs are built for; empty = the primary screen's mode.
    void preparePlan(const std::function<Plan(const PlanContext &)> &make, bool allKernels, QSize screen = {});
    QSize m_planScreen;
    QString m_rebuildBootMenu; // the boot menu design built for another screen, if any
    std::function<Plan(const PlanContext &)> m_makePlan;
    Plan m_plan;
    QVariantMap m_planMap;
    HelperLauncher m_launcher;
    std::unique_ptr<Executor> m_executor;
    std::unique_ptr<Updater> m_updater;
    Release m_release; // the newer release, once a check found one
    QVariantMap m_update;
};
