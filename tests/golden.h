#pragma once

#include <QFile>
#include <QTest>

// Compares output with tests/golden/<name>. Set EKPO_UPDATE_GOLDEN=1 to rewrite
// the file instead, then review the diff.
inline bool matchesGolden(const QString &name, const QByteArray &actual)
{
    QFile file(QStringLiteral(GOLDEN_DIR "/") + name);
    if (qEnvironmentVariableIsSet("EKPO_UPDATE_GOLDEN")) {
        if (!file.open(QIODevice::WriteOnly))
            return false;
        file.write(actual);
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qInfo("missing golden file %s; run with EKPO_UPDATE_GOLDEN=1", qPrintable(file.fileName()));
        return false;
    }
    const QByteArray expected = file.readAll();
    if (expected != actual)
        qInfo("%s differs from the output:\n%s", qPrintable(file.fileName()), actual.constData());
    return expected == actual;
}
