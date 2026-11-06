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
