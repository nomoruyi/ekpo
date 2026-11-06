#include "golden.h"
#include "probe.h"
#include "testmain.h"

#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

class TestProbe : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void referenceSystem();
    void missingFilesAreReported();
};

// The reference fixture mirrors this machine on 2026-11-05 (read-only copies, theme
// contents replaced by placeholders).
void TestProbe::referenceSystem()
{
    // Work on a copy: git doesn't keep file times, and the initrd times matter.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.filePath(QStringLiteral("root"));
    QCOMPARE(QProcess::execute(QStringLiteral("cp"), {QStringLiteral("-a"), QStringLiteral(FIXTURES_DIR "/roots/reference"), root}), 0);
    const QList<std::pair<const char *, const char *>> initrdTimes = {
        {"7.0.0-27-generic", "2026-11-05T20:52:34Z"},
        {"7.0.0-29-generic", "2026-11-05T20:52:28Z"},
        {"7.0.0-30-generic", "2026-11-05T20:52:22Z"},
        {"7.0.0-38-generic", "2026-11-05T20:52:17Z"},
        {"7.0.0-42-generic", "2026-11-05T20:52:12Z"},
    };
    for (const auto &[version, time] : initrdTimes) {
        QFile initrd(root + QStringLiteral("/boot/initrd.img-") + QLatin1String(version));
        QVERIFY(initrd.open(QIODevice::ReadWrite));
        QVERIFY(initrd.setFileTime(QDateTime::fromString(QLatin1String(time), Qt::ISODate), QFileDevice::FileModificationTime));
    }

    // The Plymouth selection changed on 2026-09-25 (the switch to fallout).
    QCOMPARE(QProcess::execute(QStringLiteral("touch"), {QStringLiteral("-h"), QStringLiteral("-d"), QStringLiteral("2026-09-25T04:09:00Z"),
                                                         root + QStringLiteral("/etc/alternatives/default.plymouth"),
                                                         root + QStringLiteral("/etc/plymouth/plymouthd.conf")}), 0);

    const SystemState state = probeSystem(root, root + QStringLiteral("/home"));
    QVERIFY(matchesGolden(QStringLiteral("probe-reference.json"), QJsonDocument(state.toJson()).toJson()));
}

void TestProbe::missingFilesAreReported()
{
    QTemporaryDir empty;
    QVERIFY(empty.isValid());
    const SystemState state = probeSystem(empty.path(), empty.path());
    QVERIFY(state.initramfsGenerator.isEmpty());
    QVERIFY(state.grub.isEmpty());
    QVERIFY(state.kernels.isEmpty());
    QCOMPARE(state.errors.size(), 2); // GRUB settings and the Plymouth alternative
}

PERSONAE_TEST_MAIN(TestProbe)
#include "tst_probe.moc"
