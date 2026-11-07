#pragma once

#include <QCoreApplication>
#include <QTest>

#include <sys/prctl.h>

// Like QTEST_GUILESS_MAIN, but a crashing test never leaves a core dump
// outside the repo (systemd-coredump, DrKonqi), and any warning fails it.
#define EKPO_TEST_MAIN(TestClass)                                                              \
    int main(int argc, char *argv[])                                                               \
    {                                                                                              \
        prctl(PR_SET_DUMPABLE, 0);                                                                 \
        QCoreApplication app(argc, argv);                                                          \
        QTest::failOnWarning();                                                                    \
        TestClass test;                                                                            \
        return QTest::qExec(&test, argc, argv);                                                    \
    }

#ifdef QT_GUI_LIB
#include <QGuiApplication>

// The same for tests that need a GUI application (QML, images); run them with
// QT_QPA_PLATFORM=offscreen.
#define EKPO_GUI_TEST_MAIN(TestClass)                                                          \
    int main(int argc, char *argv[])                                                               \
    {                                                                                              \
        prctl(PR_SET_DUMPABLE, 0);                                                                 \
        QGuiApplication app(argc, argv);                                                           \
        QTest::failOnWarning();                                                                    \
        TestClass test;                                                                            \
        return QTest::qExec(&test, argc, argv);                                                    \
    }
#endif
