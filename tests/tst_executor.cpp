#include "executor.h"
#include "golden.h"
#include "importer.h"
#include "sysinfo.h"
#include "testmain.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>

class TestExecutor : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void planForAMask();
    void appliesAndRestoresOriginal();
    void failedHelperUndoesThePlasmaSplash();
    void brokenImportBlocks();
    void activeThemeIsNoChange();

private:
    QString at(const QString &abs) const { return m_root + abs; }
    void runStub(const QString &name, const QStringList &args = {});
    QString importOne(const QString &folder);
    SystemState probe() const { return probeSystem(m_root, m_home); }
    Plan plan(const Mask &mask)
    {
        const SystemState state = probe();
        return planMask(PlanContext{*m_library, state, m_root, m_home}, mask);
    }
    bool execute(const Plan &plan, QString *error = nullptr, int *number = nullptr);
    QString ksplash(const char *key) const
    {
        return KConfig(m_home + QStringLiteral("/.config/ksplashrc"), KConfig::SimpleConfig).group(QStringLiteral("KSplash")).readEntry(key);
    }
    QJsonObject summary(const Plan &plan) const;

    std::unique_ptr<QTemporaryDir> m_tmp;
    QString m_root;
    QString m_home;
    std::optional<Library> m_library;
    QString m_hollow, m_windoze, m_plasma, m_fallout;
};

void TestExecutor::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    m_root = m_tmp->filePath(QStringLiteral("root"));
    QCOMPARE(QProcess::execute(QStringLiteral("cp"), {QStringLiteral("-a"), QStringLiteral(FIXTURES_DIR "/roots/reference"), m_root}), 0);
    m_home = m_root + QStringLiteral("/home");
    QDir().mkpath(at(QStringLiteral("/boot/grub")));
    QDir().mkpath(at(QStringLiteral("/var/log")));
    runStub(QStringLiteral("update-grub"));
    runStub(QStringLiteral("update-initramfs"), {QStringLiteral("-u"), QStringLiteral("-k"), QStringLiteral("all")});

    QString error;
    m_library = Library::open(m_tmp->filePath(QStringLiteral("lib")), error);
    QVERIFY2(m_library, qPrintable(error));
    m_hollow = importOne(at(QStringLiteral("/boot/grub/themes/hollow-grub")));
    m_windoze = importOne(at(QStringLiteral("/usr/share/plymouth/themes/WindozeXP.Plymouth")));
    m_plasma = importOne(QStringLiteral(FIXTURES_DIR "/themes/plasma-ok"));
    m_fallout = importOne(at(QStringLiteral("/usr/share/plymouth/themes/fallout")));
}

void TestExecutor::runStub(const QString &name, const QStringList &args)
{
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("EKPO_HELPER_ROOT"), m_root);
    proc.setProcessEnvironment(env);
    proc.start(QStringLiteral(STUBS_DIR "/") + name, args);
    QVERIFY(proc.waitForFinished());
    QCOMPARE(proc.exitCode(), 0);
}

QString TestExecutor::importOne(const QString &folder)
{
    const ImportResult result = importThemes(*m_library, folder, m_root, m_home);
    if (result.themes.size() != 1)
        qFatal("import of %s failed", qPrintable(folder));
    return result.themes.first().id;
}

bool TestExecutor::execute(const Plan &plan, QString *error, int *number)
{
    HelperLauncher launcher{QStringLiteral(HELPER_TEST_BIN), {}};
    launcher.environment.insert(QStringLiteral("EKPO_HELPER_ROOT"), m_root);
    launcher.environment.insert(QStringLiteral("EKPO_HELPER_BIN"), QStringLiteral(STUBS_DIR));
    Executor executor(*m_library, m_home, launcher);
    QSignalSpy spy(&executor, &Executor::finished);
    executor.apply(plan);
    if (spy.isEmpty() && !spy.wait(60000))
        qFatal("executor timed out");
    const QList<QVariant> args = spy.takeFirst();
    if (error)
        *error = args.at(1).toString();
    if (number)
        *number = args.at(2).toInt();
    return args.at(0).toBool();
}

QJsonObject TestExecutor::summary(const Plan &plan) const
{
    const auto clean = [this](QString s) { return s.replace(m_home, QStringLiteral("<home>")).replace(m_root, QStringLiteral("<root>")); };
    QJsonArray stages;
    for (const PlannedStage &s : plan.stages) {
        QJsonArray diffs;
        for (const FileDiff &d : s.diffs)
            diffs.append(QJsonObject{{QStringLiteral("path"), clean(d.path)}, {QStringLiteral("before"), d.before}, {QStringLiteral("after"), d.after}});
        QStringList actions = s.actions;
        for (QString &a : actions)
            a = clean(a);
        stages.append(QJsonObject{{QStringLiteral("stage"), stageKey(s.stage)}, {QStringLiteral("from"), s.from}, {QStringLiteral("to"), s.to},
                                  {QStringLiteral("actions"), QJsonArray::fromStringList(actions)}, {QStringLiteral("diffs"), diffs}});
    }
    QStringList payload;
    for (const PayloadFile &f : plan.payload)
        payload << f.path;
    return {
        {QStringLiteral("title"), plan.title},
        {QStringLiteral("stages"), stages},
        {QStringLiteral("request"), plan.request ? QJsonDocument::fromJson(serializeRequest(*plan.request)).object() : QJsonObject()},
        {QStringLiteral("payload"), QJsonArray::fromStringList(payload)},
        {QStringLiteral("commands"), QJsonArray::fromStringList(plan.commands)},
        {QStringLiteral("notes"), QJsonArray::fromStringList(plan.notes)},
        {QStringLiteral("problems"), QJsonArray::fromStringList(plan.problems)},
        {QStringLiteral("estimatedSeconds"), plan.estimatedSeconds},
        {QStringLiteral("target"), plan.target.toJson()},
    };
}

void TestExecutor::planForAMask()
{
    const Mask mask{{}, QStringLiteral("Night"),
                    {{Stage::BootMenu, Slot{Slot::Kind::Import, m_hollow}},
                     {Stage::BootSplash, Slot{Slot::Kind::Import, m_windoze}},
                     {Stage::PlasmaSplash, Slot{Slot::Kind::Import, m_plasma}}}};
    const Plan p = plan(mask);
    QVERIFY2(p.canApply(), qPrintable(p.problems.join(QLatin1Char('\n'))));
    QVERIFY(matchesGolden(QStringLiteral("plan-mask.json"), QJsonDocument(summary(p)).toJson()));
}

void TestExecutor::appliesAndRestoresOriginal()
{
    const Mask mask{{}, QStringLiteral("Night"),
                    {{Stage::BootMenu, Slot{Slot::Kind::Import, m_hollow}},
                     {Stage::BootSplash, Slot{Slot::Kind::Import, m_windoze}},
                     {Stage::PlasmaSplash, Slot{Slot::Kind::Import, m_plasma}}}};
    QString error;
    int number = 0;
    QVERIFY2(execute(plan(mask), &error, &number), qPrintable(error));
    QCOMPARE(number, 1);

    QVERIFY(QFile::exists(at(QStringLiteral("/etc/default/grub.d/zz-ekpo.cfg"))));
    QCOMPARE(readAlternative(m_root, QStringLiteral("default.plymouth"))->value, QStringLiteral("/usr/share/plymouth/themes/ekpo-windozexp-v1/ekpo-windozexp-v1.plymouth"));
    QCOMPARE(ksplash("Theme"), QStringLiteral("org.example.splash"));
    QVERIFY(QFile::exists(m_home + QStringLiteral("/.local/share/plasma/look-and-feel/org.example.splash/contents/splash/Splash.qml")));
    for (Stage stage : allStages)
        QVERIFY(m_library->original(stage));
    QVERIFY(QFile::exists(m_library->path() + QStringLiteral("/history/1/RESTORE.txt")));
    QStringList errors;
    const auto history = m_library->history(errors);
    QCOMPARE(history.size(), 1);
    QVERIFY(!history.first().state.value(QStringLiteral("menuEntries")).toArray().isEmpty());

    // Restoring Original puts every stage back.
    const SystemState state = probe();
    const Plan restore = planTarget(PlanContext{*m_library, state, m_root, m_home}, originalTarget(*m_library), QStringLiteral("Restore Original"));
    QCOMPARE(restore.stages.size(), 3);
    QVERIFY2(execute(restore, &error, &number), qPrintable(error));
    QCOMPARE(number, 2);
    QVERIFY(!QFile::exists(at(QStringLiteral("/etc/default/grub.d/zz-ekpo.cfg"))));
    const auto alt = readAlternative(m_root, QStringLiteral("default.plymouth"));
    QCOMPARE(alt->mode, QStringLiteral("manual"));
    QCOMPARE(alt->value, QStringLiteral("/usr/share/plymouth/themes/fallout/fallout.plymouth"));
    QCOMPARE(ksplash("Theme"), QStringLiteral("Overload"));

    // History entry 1 can be restored from its stored build, even after its import is gone.
    QVERIFY(m_library->removeImport(m_windoze, error));
    const Target entryTarget = Target::fromJson(m_library->history(errors).last().state.value(QStringLiteral("target")).toObject());
    const SystemState again = probe();
    const Plan redo = planTarget(PlanContext{*m_library, again, m_root, m_home}, entryTarget, QStringLiteral("Restore entry 1"));
    QVERIFY2(redo.canApply(), qPrintable(redo.problems.join(QLatin1Char('\n'))));
    QVERIFY2(execute(redo, &error), qPrintable(error));
    QCOMPARE(readAlternative(m_root, QStringLiteral("default.plymouth"))->value, QStringLiteral("/usr/share/plymouth/themes/ekpo-windozexp-v1/ekpo-windozexp-v1.plymouth"));
}

void TestExecutor::failedHelperUndoesThePlasmaSplash()
{
    QDir().mkpath(at(QStringLiteral("/stub-fail")));
    QFile marker(at(QStringLiteral("/stub-fail/update-initramfs")));
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.write("once");
    marker.close();

    const Mask mask{{}, QStringLiteral("Night"), {{Stage::BootSplash, Slot{Slot::Kind::Import, m_windoze}}, {Stage::PlasmaSplash, Slot{Slot::Kind::Import, m_plasma}}}};
    QString error;
    QVERIFY(!execute(plan(mask), &error));
    QVERIFY(error.contains(QLatin1String("update-initramfs")));
    QCOMPARE(ksplash("Theme"), QStringLiteral("Overload"));
    QVERIFY(!QFile::exists(m_home + QStringLiteral("/.local/share/plasma/look-and-feel/org.example.splash")));
    QCOMPARE(readAlternative(m_root, QStringLiteral("default.plymouth"))->value, QStringLiteral("/usr/share/plymouth/themes/fallout/fallout.plymouth"));
    QStringList errors;
    QVERIFY(m_library->history(errors).isEmpty());
}

void TestExecutor::brokenImportBlocks()
{
    const QString odd = importOne(QStringLiteral(FIXTURES_DIR "/themes/plymouth-missing-module"));
    const Plan p = plan(Mask{{}, QStringLiteral("Odd"), {{Stage::BootSplash, Slot{Slot::Kind::Import, odd}}}});
    QVERIFY(!p.canApply());
    QVERIFY(p.problems.first().contains(QLatin1String("broken")));
}

void TestExecutor::activeThemeIsNoChange()
{
    const Plan p = plan(Mask{{}, QStringLiteral("Same"), {{Stage::BootSplash, Slot{Slot::Kind::Import, m_fallout}}}});
    QVERIFY(p.stages.isEmpty());
    QVERIFY(!p.canApply());
}

EKPO_TEST_MAIN(TestExecutor)
#include "tst_executor.moc"
