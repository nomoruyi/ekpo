#include "golden.h"
#include "probe.h"
#include "sysinfo.h"
#include "testmain.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

class TestProbe : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void referenceSystem();
    void missingFilesAreReported();
    void packageVersions();
    void primaryScreenFromKScreen();
    void bootModeFromBootParams();
    void menuEntries();
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

void TestProbe::packageVersions()
{
    const QString root = QStringLiteral(FIXTURES_DIR "/roots/reference");
    QCOMPARE(packageVersion(root, QStringLiteral("initramfs-tools")), QStringLiteral("0.151ubuntu1"));
    QCOMPARE(packageVersion(root, QStringLiteral("dracut-core")), QStringLiteral("110-11ubuntu0.1"));
    QCOMPARE(packageVersion(root, QStringLiteral("dracut")), QString()); // a prefix of dracut-core, not installed
    QCOMPARE(installedKernels(root).last(), QStringLiteral("7.0.0-42-generic"));
}

void TestProbe::primaryScreenFromKScreen()
{
    const auto output = [](const char *name, bool enabled, int priority, int w, int h) {
        return QJsonObject{{QStringLiteral("name"), QLatin1String(name)}, {QStringLiteral("enabled"), enabled}, {QStringLiteral("priority"), priority},
                           {QStringLiteral("currentModeId"), QStringLiteral("7")},
                           {QStringLiteral("modes"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("9")}, {QStringLiteral("size"), QJsonObject{{QStringLiteral("width"), 640}, {QStringLiteral("height"), 480}}}},
                                                                QJsonObject{{QStringLiteral("id"), QStringLiteral("7")}, {QStringLiteral("size"), QJsonObject{{QStringLiteral("width"), w}, {QStringLiteral("height"), h}}}}}}};
    };
    const auto json = [](const QJsonArray &outputs) { return QJsonDocument(QJsonObject{{QStringLiteral("outputs"), outputs}}).toJson(); };
    // Priority 1 wins wherever it is listed; a disabled output never does.
    auto screen = primaryScreen(json({output("HDMI-A-1", true, 2, 1920, 1080), output("DP-1", true, 1, 2560, 1440), output("DP-2", false, 0, 800, 600)}));
    QVERIFY(screen);
    QCOMPARE(screen->first, QStringLiteral("DP-1"));
    QCOMPARE(screen->second, QSize(2560, 1440)); // the current mode, not the first listed
    screen = primaryScreen(json({output("DP-1", false, 1, 2560, 1440), output("HDMI-A-1", true, 2, 1920, 1080)}));
    QCOMPARE(screen->first, QStringLiteral("HDMI-A-1"));
    QVERIFY(!primaryScreen("not json"));
}

void TestProbe::bootModeFromBootParams()
{
    QByteArray data(4096, 0);
    bool efi = true;
    QCOMPARE(bootFramebuffer(data, efi), QSize()); // text mode
    QVERIFY(!efi);
    data[0x0f] = char(0x70);
    data[0x12] = char(2560 & 0xff);
    data[0x13] = char(2560 >> 8);
    data[0x14] = char(1440 & 0xff);
    data[0x15] = char(1440 >> 8);
    QCOMPARE(bootFramebuffer(data, efi), QSize(2560, 1440));
    QVERIFY(efi);
    data[0x0f] = char(0x23); // VESA, from a BIOS boot
    QCOMPARE(bootFramebuffer(data, efi), QSize(2560, 1440));
    QVERIFY(!efi);
    QCOMPARE(bootFramebuffer(data.left(8), efi), QSize());
}

void TestProbe::menuEntries()
{
    const QString root = QStringLiteral(FIXTURES_DIR "/roots/reference");
    const SystemState state = probeSystem(root, root + QStringLiteral("/home"));
    QCOMPARE(state.primaryOutput, QStringLiteral("DP-1"));
    QCOMPARE(state.primaryMode, QSize(2560, 1440));
    QCOMPARE(state.bootMode, QSize(2560, 1440));
    QVERIFY(state.efi);
    // Newest kernel first, each with its recovery entry; Windows through os-prober; the
    // firmware settings on EFI.
    QCOMPARE(state.menuEntries.size(), 2 + 5 * 2 + 2);
    QCOMPARE(state.menuEntries.first(), QStringLiteral("Kubuntu"));
    QCOMPARE(state.menuEntries[1], QStringLiteral("Advanced options for Kubuntu"));
    QCOMPARE(state.menuEntries[2], QStringLiteral("Kubuntu, with Linux 7.0.0-42-generic"));
    QCOMPARE(state.menuEntries[3], QStringLiteral("Kubuntu, with Linux 7.0.0-42-generic (recovery mode)"));
    QCOMPARE(state.menuEntries[12], QStringLiteral("Windows Boot Manager (on /dev/nvme1n1p1)"));
    QCOMPARE(state.menuEntries.last(), QStringLiteral("UEFI Firmware Settings"));
}

EKPO_TEST_MAIN(TestProbe)
#include "tst_probe.moc"
