#include "importer.h"
#include "sysinfo.h"
#include "testmain.h"

#include <QDir>
#include <QProcess>
#include <QTemporaryDir>

struct CliRun {
    int exitCode = -1;
    QString out;
    QString err;
};

class TestCli : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void usageErrors();
    void status();
    void declinedApplyChangesNothing();
    void appliesAndRestoresOriginal();
    void unknownTargets();

private:
    QString at(const QString &abs) const { return m_root + abs; }
    CliRun ekpo(const QStringList &args, const QByteArray &input = {});
    QString plymouth() const { return readAlternative(m_root, QStringLiteral("default.plymouth"))->value; }

    std::unique_ptr<QTemporaryDir> m_tmp;
    QString m_root;
};

void TestCli::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    m_root = m_tmp->filePath(QStringLiteral("root"));
    QCOMPARE(QProcess::execute(QStringLiteral("cp"), {QStringLiteral("-a"), QStringLiteral(FIXTURES_DIR "/roots/reference"), m_root}), 0);
    QDir().mkpath(at(QStringLiteral("/boot/grub")));
    QDir().mkpath(at(QStringLiteral("/var/log")));
    for (const auto &[stub, args] : QList<std::pair<QString, QStringList>>{{QStringLiteral("update-grub"), {}},
                                                                             {QStringLiteral("update-initramfs"), {QStringLiteral("-u"), QStringLiteral("-k"), QStringLiteral("all")}}}) {
        QProcess proc;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("EKPO_HELPER_ROOT"), m_root);
        proc.setProcessEnvironment(env);
        proc.start(QStringLiteral(STUBS_DIR "/") + stub, args);
        QVERIFY(proc.waitForFinished());
    }

    // The library the CLI finds through XDG_DATA_HOME: two imports and the Mask "Night".
    QString error;
    auto library = Library::open(m_tmp->filePath(QStringLiteral("data/ekpo")), error);
    QVERIFY2(library, qPrintable(error));
    const QString home = m_root + QStringLiteral("/home");
    const auto hollow = importThemes(*library, at(QStringLiteral("/boot/grub/themes/hollow-grub")), m_root, home);
    const auto windoze = importThemes(*library, at(QStringLiteral("/usr/share/plymouth/themes/WindozeXP.Plymouth")), m_root, home);
    Mask mask{{}, QStringLiteral("Night"),
              {{Stage::BootMenu, Slot{Slot::Kind::Import, hollow.themes.first().id}}, {Stage::BootSplash, Slot{Slot::Kind::Import, windoze.themes.first().id}}}};
    QVERIFY(library->saveMask(mask, error));
}

CliRun TestCli::ekpo(const QStringList &args, const QByteArray &input)
{
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("EKPO_SYSTEM_ROOT"), m_root);
    env.insert(QStringLiteral("EKPO_SYSTEM_HOME"), m_root + QStringLiteral("/home"));
    env.insert(QStringLiteral("XDG_DATA_HOME"), m_tmp->filePath(QStringLiteral("data")));
    proc.setProcessEnvironment(env);
    proc.start(QStringLiteral(EKPO_BIN), args);
    proc.write(input);
    proc.closeWriteChannel();
    if (!proc.waitForFinished(60000))
        qFatal("ekpo timed out");
    return {proc.exitCode(), QString::fromUtf8(proc.readAllStandardOutput()), QString::fromUtf8(proc.readAllStandardError())};
}

void TestCli::usageErrors()
{
    QCOMPARE(ekpo({QStringLiteral("help")}).exitCode, 0);
    QCOMPARE(ekpo({QStringLiteral("bogus")}).exitCode, 2);
    QCOMPARE(ekpo({QStringLiteral("apply")}).exitCode, 2);
    QCOMPARE(ekpo({QStringLiteral("status"), QStringLiteral("extra")}).exitCode, 2);
    QVERIFY(ekpo({QStringLiteral("apply"), QStringLiteral("Night"), QStringLiteral("--all-kernels")}, "n\n").out.contains(QLatin1String("# update-initramfs -u -k all")));
}

void TestCli::status()
{
    const CliRun r = ekpo({QStringLiteral("status")});
    QCOMPARE(r.exitCode, 0);
    QVERIFY2(r.out.contains(QLatin1String("Boot splash:   Fallout (manual)")), qPrintable(r.out));
    QVERIFY(r.out.contains(QLatin1String("Nothing needs attention.")));
    QCOMPARE(ekpo({QStringLiteral("recover")}).out, QStringLiteral("Nothing to recover.\n"));
}

void TestCli::declinedApplyChangesNothing()
{
    for (const QByteArray &answer : {QByteArray("n\n"), QByteArray()}) { // "no", and no answer at all
        const CliRun r = ekpo({QStringLiteral("apply"), QStringLiteral("Night")}, answer);
        QCOMPARE(r.exitCode, 1);
        QVERIFY(r.out.contains(QLatin1String("2. Boot splash: Fallout -> WindozeXP")));
        QVERIFY(r.out.contains(QLatin1String("Apply? This asks for your password. [y/N]")));
        QVERIFY(r.out.contains(QLatin1String("Nothing was changed.")));
    }
    QCOMPARE(plymouth(), QStringLiteral("/usr/share/plymouth/themes/fallout/fallout.plymouth"));
}

void TestCli::appliesAndRestoresOriginal()
{
    const CliRun applied = ekpo({QStringLiteral("apply"), QStringLiteral("night")}, "y\n");
    QVERIFY2(applied.exitCode == 0, qPrintable(applied.err));
    QVERIFY(applied.out.contains(QLatin1String("# update-initramfs -u -k 7.0.0-42-generic"))); // the default: newest kernel only
    QVERIFY(applied.out.contains(QLatin1String("Done. Saved as history entry #1.")));
    QCOMPARE(plymouth(), QStringLiteral("/usr/share/plymouth/themes/ekpo-windozexp-v1/ekpo-windozexp-v1.plymouth"));
    const CliRun afterApply = ekpo({QStringLiteral("status")});
    QVERIFY(afterApply.out.contains(QLatin1String("Last apply:    #1 Night")));
    QVERIFY2(afterApply.out.contains(QLatin1String("keep the previous splash as a fallback")), qPrintable(afterApply.out));
    QCOMPARE(afterApply.exitCode, 0);
    const CliRun tooEarly = ekpo({QStringLiteral("remove-files")});
    QCOMPARE(tooEarly.exitCode, 1);
    QVERIFY(tooEarly.err.contains(QLatin1String("Restore Original first")));

    const CliRun restored = ekpo({QStringLiteral("restore"), QStringLiteral("original")}, "yes\n");
    QVERIFY2(restored.exitCode == 0, qPrintable(restored.err));
    QVERIFY(restored.out.contains(QLatin1String("Restore Original")));
    QVERIFY(restored.out.contains(QLatin1String("# update-initramfs -u -k all"))); // restores rebuild every kernel
    QCOMPARE(plymouth(), QStringLiteral("/usr/share/plymouth/themes/fallout/fallout.plymouth"));
    QVERIFY(!QFile::exists(at(QStringLiteral("/etc/default/grub.d/zz-ekpo.cfg"))));

    // Removing Ekpo's files needs Original first, then deletes the copies.
    const CliRun removed = ekpo({QStringLiteral("remove-files")}, "y\n");
    QVERIFY2(removed.exitCode == 0, qPrintable(removed.err + removed.out));
    QVERIFY(removed.out.contains(QLatin1String("ekpo-windozexp-v1")));
    QVERIFY(!QFileInfo::exists(at(QStringLiteral("/usr/share/plymouth/themes/ekpo-windozexp-v1"))));
    QCOMPARE(ekpo({QStringLiteral("remove-files")}).out, QStringLiteral("Ekpo has no theme copies installed.\n"));

    const CliRun again = ekpo({QStringLiteral("restore"), QStringLiteral("#1"), QStringLiteral("--newest-kernel")}, "y\n");
    QVERIFY(again.out.contains(QLatin1String("# update-initramfs -u -k 7.0.0-42-generic")));
    QVERIFY2(again.exitCode == 0, qPrintable(again.err));
    QCOMPARE(plymouth(), QStringLiteral("/usr/share/plymouth/themes/ekpo-windozexp-v1/ekpo-windozexp-v1.plymouth"));
}

void TestCli::unknownTargets()
{
    const CliRun mask = ekpo({QStringLiteral("apply"), QStringLiteral("Day")});
    QCOMPARE(mask.exitCode, 1);
    QVERIFY(mask.err.contains(QLatin1String("Masks: Night")));
    QCOMPARE(ekpo({QStringLiteral("restore"), QStringLiteral("99")}).exitCode, 1);
    QVERIFY(ekpo({QStringLiteral("restore"), QStringLiteral("original")}).out.contains(QLatin1String("no Original to restore")));
}

EKPO_TEST_MAIN(TestCli)
#include "tst_cli.moc"
