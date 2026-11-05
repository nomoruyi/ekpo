#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include <sys/prctl.h>

int main(int argc, char *argv[])
{
    // Dev and test runs (dev/env.sh): no core dumps, so a crash can't make
    // systemd-coredump and DrKonqi write outside the repo.
    if (qEnvironmentVariableIsSet("PERSONAE_DEV"))
        prctl(PR_SET_DUMPABLE, 0);

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("personae"));
    app.setApplicationDisplayName(QStringLiteral("Personae"));
    app.setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    app.setDesktopFileName(QStringLiteral("io.github.nomoruyi.personae"));

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QQmlApplicationEngine engine;
    engine.loadFromModule("Personae", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    // Screenshot mode (ARCHITECTURE.md §8): render once, save, quit.
    const QString shot = qEnvironmentVariable("PERSONAE_SCREENSHOT");
    if (!shot.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QTimer::singleShot(1000, &app, [window, shot] {
            const bool saved = window->grabWindow().save(shot);
            QCoreApplication::exit(saved ? 0 : 2);
        });
    }
    return app.exec();
}
