#include "bugreport.h"
#include "cli.h"
#include "executor.h"
#include "updater.h"
#include "library.h"

#include <QGuiApplication>
#include <QCryptographicHash>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include <cstdio>
#include <sys/prctl.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    // Dev builds: a fake root set without dev/env.sh would reach the real system.
    if (const QString error = sandboxSetupError(); !error.isEmpty()) {
        std::fprintf(stderr, "ekpo: %s\n", qPrintable(error));
        return 2;
    }

    // Dev and test runs (dev/env.sh): no core dumps, so a crash can't make
    // systemd-coredump and DrKonqi write outside the repo.
    if (qEnvironmentVariableIsSet("EKPO_DEV"))
        prctl(PR_SET_DUMPABLE, 0);

    // A command runs the command line without any window (SPEC.md §4.15). Building a
    // boot splash design draws text and images, which needs a GUI application; the
    // offscreen platform gives it one without a display.
    if (argc > 1) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        QGuiApplication app(argc, argv);
        app.setApplicationName(QStringLiteral("ekpo"));
        return runCli(app.arguments().mid(1));
    }

    installMessageLog(); // the last messages go into bug reports
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ekpo"));
    app.setApplicationDisplayName(QStringLiteral("Ekpo"));
    app.setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    app.setDesktopFileName(QStringLiteral("io.github.nomoruyi.ekpo"));
    app.setWindowIcon(QIcon(QStringLiteral(":/io.github.nomoruyi.ekpo.svg")));

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    // One window per user and library: a second start brings the first one forward.
    // (Not in screenshot mode, so tests never hand over to a running app.)
    const QString instanceName = QStringLiteral("ekpo-%1-%2").arg(::getuid()).arg(
        QString::fromLatin1(QCryptographicHash::hash(Library::configuredPath().toUtf8(), QCryptographicHash::Sha1).toHex().left(12)));
    QLocalServer instanceServer;
    const bool screenshot = !qEnvironmentVariableIsEmpty("EKPO_SCREENSHOT");
    if (!screenshot) {
        QLocalSocket other;
        other.connectToServer(instanceName);
        if (other.waitForConnected(500)) {
            // On Wayland only a token from the launcher lets the first window come forward.
            other.write("activate " + qgetenv("XDG_ACTIVATION_TOKEN") + "\n");
            other.waitForBytesWritten(500);
            return 0;
        }
        QLocalServer::removeServer(instanceName); // a stale socket from a crash
        instanceServer.setSocketOptions(QLocalServer::UserAccessOption);
        instanceServer.listen(instanceName);
    }

    QQmlApplicationEngine engine;
    // Dev-only start options for screenshots.
    QVariantMap start;
    if (const QString section = qEnvironmentVariable("EKPO_SECTION"); !section.isEmpty())
        start.insert(QStringLiteral("startSection"), section);
    if (qEnvironmentVariableIsSet("EKPO_DEV")) {
        start.insert(QStringLiteral("startReview"), qEnvironmentVariable("EKPO_REVIEW_IMPORT"));
        start.insert(QStringLiteral("startEdit"), qEnvironmentVariable("EKPO_EDIT_DESIGN"));
        start.insert(QStringLiteral("startBugReport"), qEnvironmentVariableIsSet("EKPO_BUG_REPORT"));
        start.insert(QStringLiteral("startTour"), qEnvironmentVariableIsSet("EKPO_TOUR"));
        start.insert(QStringLiteral("startUpdateDialog"), qEnvironmentVariableIsSet("EKPO_UPDATE_DIALOG"));
    }
    engine.setInitialProperties(start);
    engine.loadFromModule("Ekpo", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    QObject::connect(&instanceServer, &QLocalServer::newConnection, &app, [&instanceServer, &engine] {
        QLocalSocket *other = instanceServer.nextPendingConnection();
        other->waitForReadyRead(500);
        const QByteArray token = other->readLine().trimmed().mid(9); // after "activate "
        delete other;
        if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0))) {
            if (!token.isEmpty())
                qputenv("XDG_ACTIVATION_TOKEN", token); // Qt's Wayland backend uses it once
            window->show();
            window->requestActivate();
        }
    });

    // Screenshot mode (ARCHITECTURE.md §8): render once, save, quit.
    const QString shot = qEnvironmentVariable("EKPO_SCREENSHOT");
    if (!shot.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        // (The page tour takes about 6.5 s.)
        QTimer::singleShot(qEnvironmentVariableIsSet("EKPO_TOUR") ? 7500 : 1000, &app, [window, shot] {
            const bool saved = window->grabWindow().save(shot);
            QCoreApplication::exit(saved ? 0 : 2);
        });
    }
    const int code = app.exec();
    // After an update (SPEC.md §4.14): the new Ekpo starts once this one has let go of
    // the single-instance socket, which it would otherwise hand over to.
    if (restartRequested()) {
        instanceServer.close();
        QProcess::startDetached(restartPath(), {});
    }
    return code;
}
