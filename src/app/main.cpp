#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include <sys/prctl.h>

int main(int argc, char *argv[])
{
    // Dev and test runs (dev/env.sh): no core dumps, so a crash can't make
    // systemd-coredump and DrKonqi write outside the repo.
    if (qEnvironmentVariableIsSet("EKPO_DEV"))
        prctl(PR_SET_DUMPABLE, 0);

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ekpo"));
    app.setApplicationDisplayName(QStringLiteral("Ekpo"));
    app.setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    app.setDesktopFileName(QStringLiteral("io.github.nomoruyi.ekpo"));
    app.setWindowIcon(QIcon(QStringLiteral(":/io.github.nomoruyi.ekpo.svg")));

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QQmlApplicationEngine engine;
    // Dev-only start options for screenshots.
    QVariantMap start;
    if (const QString section = qEnvironmentVariable("EKPO_SECTION"); !section.isEmpty())
        start.insert(QStringLiteral("startSection"), section);
    if (qEnvironmentVariableIsSet("EKPO_DEV"))
        start.insert(QStringLiteral("startReview"), qEnvironmentVariable("EKPO_REVIEW_IMPORT"));
    engine.setInitialProperties(start);
    engine.loadFromModule("Ekpo", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    // Screenshot mode (ARCHITECTURE.md §8): render once, save, quit.
    const QString shot = qEnvironmentVariable("EKPO_SCREENSHOT");
    if (!shot.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QTimer::singleShot(1000, &app, [window, shot] {
            const bool saved = window->grabWindow().save(shot);
            QCoreApplication::exit(saved ? 0 : 2);
        });
    }
    return app.exec();
}
