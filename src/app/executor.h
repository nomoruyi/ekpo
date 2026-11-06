#pragma once

#include "library.h"
#include "planner.h"

#include <QObject>
#include <QProcess>

#include <memory>

// How the helper is started: through pkexec on the real system, or the test
// helper on a sandbox root.
struct HelperLauncher {
    QString program;
    QStringList arguments;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
};

// systemd-inhibit (no shutdown or sleep meanwhile) + pkexec + the installed helper.
HelperLauncher systemHelperLauncher();

// Identifies the current boot; it changes with every reboot.
QString currentBootId();

// Where the app looks and how it reaches the helper. Normally "/", the user's home
// and pkexec; in sandbox mode (EKPO_DEV + EKPO_SYSTEM_ROOT, ARCHITECTURE.md §8) a
// fake root, its home and the test helper with stub commands.
struct AppEnvironment {
    QString rootDir = QStringLiteral("/");
    QString homeDir;
    HelperLauncher launcher;
};
AppEnvironment detectEnvironment();

// Moves the Plasma splash packages Ekpo copied (history entries with a build) to
// the trash, except activeTheme. Returns their ids.
QStringList removePlasmaCopies(const Library &library, const QString &homeDir, const QString &activeTheme);

// Carries out a Plan (SPEC.md §4.9): stores new builds, captures Original, applies
// the Plasma splash, runs the helper for the boot stages, and records history. If
// the helper fails, the Plasma splash change is undone too.
class Executor : public QObject
{
    Q_OBJECT

public:
    Executor(Library &library, const QString &homeDir, const HelperLauncher &launcher, QObject *parent = nullptr);
    ~Executor() override;

    bool isRunning() const { return m_running; }
    void apply(const Plan &plan);
    // Rolls back an apply the helper didn't finish (its journal is still there).
    void recover();
    // Removes Ekpo's theme copies and alternatives (helper "uninstall"); the
    // helper refuses while anything of Ekpo's is still selected.
    void removeEkpoFiles();

Q_SIGNALS:
    void progress(const QString &text);
    void finished(bool ok, const QString &error, int historyNumber);

private:
    struct PlasmaUndo {
        QString theme;
        QString engine;
        QString createdPackage; // removed again on failure
        bool changed = false;
    };

    void startHelper(const QByteArray &input);
    void runPlainOperation(Request::Operation operation, const QString &message);
    bool applyPlasma(const PlasmaSplashTarget &target, QString &error);
    void undoPlasma();
    void readHelperOutput();
    void helperFinished();
    void succeed(const QJsonArray &menuEntries);
    void fail(const QString &error);

    Library &m_library;
    QString m_homeDir;
    HelperLauncher m_launcher;
    Plan m_plan;
    PlasmaUndo m_plasmaUndo;
    std::unique_ptr<QProcess> m_process;
    QByteArray m_buffer;
    QJsonObject m_result;
    bool m_running = false;
    bool m_plainOperation = false;
};
