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
};
