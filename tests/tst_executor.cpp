#include "executor.h"
#include "golden.h"
#include "importer.h"
#include "playback.h"
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
#include <QStandardPaths>
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
    void asksAfterRebootAndSeesOutsideChanges();
    void recoversAnInterruptedApply();
    void timeoutOnlyChange();
    void newestKernelOnlyThenRestoreAll();
    void historyLimitPrunesCopiesAndBuilds();
    void welcomeBackAfterReinstall();
    void designAppliesAsPlasmaSplash();
    void designAppliesAsBootSplash();
    void sharedChangeMeansUpdate();
    void designAppliesAsBootMenu();
    void playsTheBoot();

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
    if (plan.title == QLatin1String("<recover>"))
        executor.recover();
    else
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
    // "New Mask from What's Applied" gives the same slots back.
    QCOMPARE(slotsFromTarget(*m_library, Target::fromJson(history.first().state.value(QStringLiteral("target")).toObject())), mask.stageSlots);

    // Restoring Original puts every stage back.
    const SystemState state = probe();
    const Plan restore = planTarget(PlanContext{*m_library, state, m_root, m_home}, originalTarget(*m_library), QStringLiteral("Restore Original"));
    QCOMPARE(restore.stages.size(), 3);
    QVERIFY2(execute(restore, &error, &number), qPrintable(error));
    QCOMPARE(number, 2);
    QVERIFY(!QFile::exists(at(QStringLiteral("/etc/default/grub.d/zz-ekpo.cfg"))));
    // Original's boot splash is Fallout, which is imported; GRUB's own settings aren't a theme Ekpo set.
    QCOMPARE(slotsFromTarget(*m_library, Target::fromJson(m_library->history(errors).first().state.value(QStringLiteral("target")).toObject())),
             (QMap<Stage, Slot>{{Stage::BootSplash, Slot{Slot::Kind::Import, m_fallout}}}));
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

    // Unless the initrds should get it again (a new kernel's, say): the same selection, rebuilt.
    const SystemState state = probe();
    PlanContext context{*m_library, state, m_root, m_home};
    context.rebuildSplash = true;
    Target target;
    target.bootSplash = currentTarget(state, m_root).bootSplash;
    const Plan rebuild = planTarget(context, target, QStringLiteral("Rebuild"));
    QVERIFY2(rebuild.canApply(), qPrintable(rebuild.problems.join(QLatin1Char('\n'))));
    QCOMPARE(rebuild.stages.size(), 1);
    QVERIFY(rebuild.commands.contains(QStringLiteral("update-initramfs -u -k all")));
    QVERIFY(rebuild.payload.isEmpty()); // nothing new to install
    QString error;
    QVERIFY2(execute(rebuild, &error), qPrintable(error));
    QCOMPARE(currentTarget(probe(), m_root).bootSplash->theme, QStringLiteral("fallout"));
}

void TestExecutor::asksAfterRebootAndSeesOutsideChanges()
{
    qputenv("EKPO_BOOT_ID", "boot-1");
    QString error;
    QVERIFY2(execute(plan(Mask{{}, QStringLiteral("XP"), {{Stage::BootSplash, Slot{Slot::Kind::Import, m_windoze}}}}), &error), qPrintable(error));
    const auto check = m_library->pendingCheck();
    QVERIFY(check);
    QCOMPARE(check->value(QStringLiteral("number")).toInt(), 1);
    QCOMPARE(check->value(QStringLiteral("bootId")).toString(), QStringLiteral("boot-1"));
    QCOMPARE(currentBootId(), QStringLiteral("boot-1"));
    qunsetenv("EKPO_BOOT_ID");

    QVERIFY(driftedStages(*m_library, currentTarget(probe(), m_root)).isEmpty());
    // Something else (e.g. System Settings) switches the splash back.
    runStub(QStringLiteral("update-alternatives"), {QStringLiteral("--set"), QStringLiteral("default.plymouth"), QStringLiteral("/usr/share/plymouth/themes/fallout/fallout.plymouth")});
    QCOMPARE(driftedStages(*m_library, currentTarget(probe(), m_root)), QList<Stage>{Stage::BootSplash});
    QCOMPARE(lastApplied(*m_library).bootSplash->theme, QStringLiteral("ekpo-windozexp-v1"));
}

void TestExecutor::recoversAnInterruptedApply()
{
    QDir().mkpath(at(QStringLiteral("/stub-fail")));
    QFile marker(at(QStringLiteral("/stub-fail/update-grub")));
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.write("always");
    marker.close();
    QString error;
    QVERIFY(!execute(plan(Mask{{}, QStringLiteral("XP"), {{Stage::BootSplash, Slot{Slot::Kind::Import, m_windoze}}}}), &error));
    QVERIFY(error.contains(QLatin1String("Open Ekpo again")));
    QVERIFY(probe().leftoverJournal);

    QVERIFY(QFile::remove(at(QStringLiteral("/stub-fail/update-grub"))));
    Plan recover;
    recover.title = QStringLiteral("<recover>");
    QVERIFY2(execute(recover, &error), qPrintable(error));
    QVERIFY(!probe().leftoverJournal);
    QCOMPARE(readAlternative(m_root, QStringLiteral("default.plymouth"))->value, QStringLiteral("/usr/share/plymouth/themes/fallout/fallout.plymouth"));
}

void TestExecutor::timeoutOnlyChange()
{
    Target target;
    target.bootMenu = BootMenuTarget{false, {}, {}, 0, QStringLiteral("hidden"), {}, QStringLiteral("0 s"), {}, {}};
    const SystemState state = probe();
    const Plan p = planTarget(PlanContext{*m_library, state, m_root, m_home}, target, QStringLiteral("Timeout"));
    QVERIFY2(p.canApply(), qPrintable(QStringLiteral("%1 stages; %2").arg(p.stages.size()).arg(p.problems.join(QLatin1Char(';')))));
    QCOMPARE(p.stages.first().diffs.first().after,
             QStringLiteral("# Written by Ekpo (io.github.nomoruyi.ekpo). Delete this file to return to /etc/default/grub.\n"
                            "GRUB_TIMEOUT=0\nGRUB_RECORDFAIL_TIMEOUT=0\nGRUB_TIMEOUT_STYLE=hidden\n"));
    QString error;
    QVERIFY2(execute(p, &error), qPrintable(error));
    // GRUB keeps the theme from /etc/default/grub.
    QFile cfg(at(QStringLiteral("/boot/grub/grub.cfg")));
    QVERIFY(cfg.open(QIODevice::ReadOnly));
    QVERIFY(cfg.readAll().contains("themes/hollow-grub/theme.txt"));
}

void TestExecutor::newestKernelOnlyThenRestoreAll()
{
    const SystemState state = probe();
    PlanContext context{*m_library, state, m_root, m_home};
    context.allKernels = false;
    const Plan p = planMask(context, Mask{{}, QStringLiteral("XP"), {{Stage::BootSplash, Slot{Slot::Kind::Import, m_windoze}}}});
    QVERIFY2(p.canApply(), qPrintable(p.problems.join(QLatin1Char('\n'))));
    QVERIFY(p.touchesSplash);
    QVERIFY(!p.allKernels);
    QCOMPARE(p.newestKernel, QStringLiteral("7.0.0-42-generic"));
    QCOMPARE(p.kernelCount, 5);
    QCOMPARE(p.estimatedSeconds, 8 + 5);
    QVERIFY(!p.request->plymouth->allKernels);
    QVERIFY(p.commands.contains(QStringLiteral("update-initramfs -u -k 7.0.0-42-generic")));
    QVERIFY(p.stages.first().actions.last().contains(QLatin1String("The other 4 keep the current splash")));
    QString error;
    QVERIFY2(execute(p, &error), qPrintable(error));
    QFile older(at(QStringLiteral("/boot/initrd.img-7.0.0-27-generic")));
    QVERIFY(older.open(QIODevice::ReadOnly));
    QVERIFY(older.readAll().contains("fallout"));
    QVERIFY(!lastApplied(*m_library).bootSplash->allKernels);

    // Restores rebuild every kernel.
    const SystemState now = probe();
    const Plan restore = planTarget(PlanContext{*m_library, now, m_root, m_home}, originalTarget(*m_library), QStringLiteral("Restore Original"));
    QVERIFY(restore.allKernels);
    QVERIFY(restore.commands.contains(QStringLiteral("update-initramfs -u -k all")));
}

void TestExecutor::historyLimitPrunesCopiesAndBuilds()
{
    QString error;
    Settings settings = m_library->settings();
    settings.historySize = 2;
    QVERIFY(m_library->saveSettings(settings, error));
    const QString vortex = importOne(at(QStringLiteral("/usr/share/plymouth/themes/vortex-ubuntu")));
    const QString dots = importOne(QStringLiteral(FIXTURES_DIR "/themes/plymouth-two-step"));

    for (const QString &id : {m_windoze, vortex, dots})
        QVERIFY2(execute(plan(Mask{{}, id, {{Stage::BootSplash, Slot{Slot::Kind::Import, id}}}}), &error), qPrintable(error));

    // History keeps the last two entries: the first copy and its build are gone.
    QVERIFY(!QFileInfo::exists(at(QStringLiteral("/usr/share/plymouth/themes/ekpo-windozexp-v1"))));
    QVERIFY(QFileInfo::exists(at(QStringLiteral("/usr/share/plymouth/themes/ekpo-vortex-ubuntu-v1"))));
    QVERIFY(QFileInfo::exists(at(QStringLiteral("/usr/share/plymouth/themes/ekpo-dots-fixture-v1"))));
    QCOMPARE(QDir(m_library->path() + QStringLiteral("/builds")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 2);

    // A copy left over (say, a prune that failed) is an old copy; those in use aren't.
    const Target current = currentTarget(probe(), m_root);
    QVERIFY(oldSystemCopies(*m_library, m_root, current).isEmpty());
    QVERIFY(QDir().mkpath(at(QStringLiteral("/usr/share/plymouth/themes/ekpo-stale-v1"))));
    QCOMPARE(oldSystemCopies(*m_library, m_root, current), QStringList{QStringLiteral("ekpo-stale-v1")});
}

void TestExecutor::welcomeBackAfterReinstall()
{
    const Mask mask{{}, QStringLiteral("Night"),
                    {{Stage::BootMenu, Slot{Slot::Kind::Import, m_hollow}}, {Stage::BootSplash, Slot{Slot::Kind::Import, m_windoze}}}};
    QString error;
    QVERIFY2(execute(plan(mask), &error), qPrintable(error));
    const auto fresh = [this] {
        const SystemState state = probe();
        return looksLikeFreshSystem(PlanContext{*m_library, state, m_root, m_home});
    };
    QVERIFY(!fresh());

    // A reinstall replaces / but keeps the library in /home.
    const auto reinstall = [this] {
        QVERIFY(QDir(m_root).removeRecursively());
        QCOMPARE(QProcess::execute(QStringLiteral("cp"), {QStringLiteral("-a"), QStringLiteral(FIXTURES_DIR "/roots/reference"), m_root}), 0);
        QDir().mkpath(at(QStringLiteral("/boot/grub")));
        QDir().mkpath(at(QStringLiteral("/var/log")));
        runStub(QStringLiteral("update-grub"));
        runStub(QStringLiteral("update-initramfs"), {QStringLiteral("-u"), QStringLiteral("-k"), QStringLiteral("all")});
    };
    reinstall();
    QVERIFY(fresh());

    // "Re-apply my last look": the copy comes back from its stored build.
    const SystemState state = probe();
    const Plan redo = planTarget(PlanContext{*m_library, state, m_root, m_home}, lastApplied(*m_library), QStringLiteral("Re-apply"));
    QVERIFY2(redo.canApply(), qPrintable(redo.problems.join(QLatin1Char('\n'))));
    QVERIFY2(execute(redo, &error), qPrintable(error));
    QVERIFY(!fresh());

    // "Start fresh": the current state becomes Original and Ekpo's reference.
    reinstall();
    QVERIFY(fresh());
    const QJsonObject current = currentTarget(probe(), m_root).toJson();
    QVERIFY(m_library->resetOriginal(current, error));
    HistoryEntry entry{0, QDateTime::currentDateTimeUtc(), QStringLiteral("Fresh"), {{QStringLiteral("target"), current}, {QStringLiteral("before"), current}}};
    QVERIFY(m_library->addHistoryEntry(entry, error));
    QVERIFY(!fresh());
    QCOMPARE(m_library->original(Stage::BootSplash)->value(QStringLiteral("theme")).toString(), QStringLiteral("fallout"));
}


void TestExecutor::designAppliesAsPlasmaSplash()
{
    QString error;
    const auto logo = m_library->addAsset(QStringLiteral(FIXTURES_DIR "/assets/logo.png"), Asset::Kind::Image, QStringLiteral("Logo"), error);
    QVERIFY2(logo, qPrintable(error));
    Design design;
    design.stage = Stage::PlasmaSplash;
    design.name = QStringLiteral("Dawn");
    design.content = {{QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#102030")}}},
                      {QStringLiteral("elements"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("image")}, {QStringLiteral("asset"), logo->hash},
                                                                          {QStringLiteral("x"), 40}, {QStringLiteral("y"), 40}, {QStringLiteral("width"), 20}, {QStringLiteral("height"), 20}}}}};
    QVERIFY(m_library->saveDesign(design, error));
    const Mask mask{{}, QStringLiteral("Dawn"), {{Stage::PlasmaSplash, Slot{Slot::Kind::Design, design.id}}}};

    const Plan first = plan(mask);
    QVERIFY2(first.canApply(), qPrintable(first.problems.join(QLatin1Char('\n'))));
    QCOMPARE(first.target.plasmaSplash->theme, QStringLiteral("ekpo-dawn-v1"));
    QCOMPARE(first.target.plasmaSplash->label, QStringLiteral("Dawn"));
    QVERIFY2(execute(first, &error), qPrintable(error));
    const QString v1 = m_home + QStringLiteral("/.local/share/plasma/look-and-feel/ekpo-dawn-v1");
    QVERIFY(QFile::exists(v1 + QStringLiteral("/contents/splash/Splash.qml")));
    QVERIFY(QFile::exists(v1 + QStringLiteral("/contents/splash/images/") + logo->hash + QStringLiteral(".png")));
    QCOMPARE(ksplash("Theme"), QStringLiteral("ekpo-dawn-v1"));

    // Applying it unchanged again is no change; an edited version gets its own package.
    QVERIFY(plan(mask).stages.isEmpty());
    design.content.insert(QStringLiteral("fadeIn"), 300);
    QVERIFY(m_library->saveDesign(design, error));
    const Plan second = plan(mask);
    QVERIFY2(second.canApply(), qPrintable(second.problems.join(QLatin1Char('\n'))));
    QCOMPARE(second.target.plasmaSplash->theme, QStringLiteral("ekpo-dawn-v2"));
    QVERIFY2(execute(second, &error), qPrintable(error));
    QCOMPARE(ksplash("Theme"), QStringLiteral("ekpo-dawn-v2"));
    QVERIFY(QFile::exists(v1)); // history entry 1 still needs it
    QVERIFY(oldPlasmaCopies(*m_library, m_home).isEmpty());

    // Once no history entry needs them, the next apply moves the old copies to the trash.
    Settings settings = m_library->settings();
    settings.historySize = 1;
    QVERIFY(m_library->saveSettings(settings, error));
    design.content.insert(QStringLiteral("fadeIn"), 600);
    QVERIFY(m_library->saveDesign(design, error));
    QVERIFY2(execute(plan(mask), &error), qPrintable(error));
    QCOMPARE(ksplash("Theme"), QStringLiteral("ekpo-dawn-v3"));
    QVERIFY(!QFile::exists(v1));
    QVERIFY(!QFile::exists(m_home + QStringLiteral("/.local/share/plasma/look-and-feel/ekpo-dawn-v2")));
    QVERIFY(QFile::exists(m_home + QStringLiteral("/.local/share/plasma/look-and-feel/ekpo-dawn-v3")));

    // A Mask whose design is gone can't be applied.
    const Plan missing = plan(Mask{{}, QStringLiteral("Gone"), {{Stage::PlasmaSplash, Slot{Slot::Kind::Design, QStringLiteral("nope")}}}});
    QVERIFY(!missing.canApply());
    QVERIFY(missing.problems.first().contains(QLatin1String("design")));
}

void TestExecutor::designAppliesAsBootSplash()
{
    Design design;
    design.stage = Stage::BootSplash;
    design.name = QStringLiteral("Night");
    design.content = {{QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#102030")}}},
                      {QStringLiteral("hideMessages"), true},
                      {QStringLiteral("elements"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("spinner")}, {QStringLiteral("color"), QStringLiteral("#ffffff")},
                                                                          {QStringLiteral("x"), 48.5}, {QStringLiteral("y"), 60}, {QStringLiteral("width"), 3}, {QStringLiteral("height"), 5}}}}};
    QString error;
    QVERIFY(m_library->saveDesign(design, error));
    const Mask mask{{}, QStringLiteral("Night"), {{Stage::BootSplash, Slot{Slot::Kind::Design, design.id}}}};

    const Plan first = plan(mask);
    QVERIFY2(first.canApply(), qPrintable(first.problems.join(QLatin1Char('\n'))));
    QCOMPARE(first.target.bootSplash->theme, QStringLiteral("ekpo-night-v1"));
    QCOMPARE(first.target.bootSplash->daemonTheme, QStringLiteral("ekpo-night-v1"));
    QVERIFY2(execute(first, &error), qPrintable(error));
    const QString dir = at(QStringLiteral("/usr/share/plymouth/themes/ekpo-night-v1/"));
    for (const char *file : {"ekpo-night-v1.plymouth", "background.png", "throbber-0060.png", "lock.png", "entry.png", "bullet.png"})
        QVERIFY2(QFile::exists(dir + QLatin1String(file)), file);
    const SystemState state = probe();
    QCOMPARE(state.plymouthAlternative, QStringLiteral("/usr/share/plymouth/themes/ekpo-night-v1/ekpo-night-v1.plymouth"));
    QCOMPARE(state.plymouthDaemonTheme, QStringLiteral("ekpo-night-v1"));

    // The same design again is no change (the build is the same, byte for byte).
    QVERIFY(plan(mask).stages.isEmpty());
}

void TestExecutor::sharedChangeMeansUpdate()
{
    QString error;
    SharedElement palette{{}, SharedElement::Kind::Palette, QStringLiteral("Night"), {},
                          {{QStringLiteral("background"), QStringLiteral("#102030")}, {QStringLiteral("accent"), QStringLiteral("#ff8800")}, {QStringLiteral("text"), QStringLiteral("#ffffff")}}};
    QVERIFY(m_library->saveSharedElement(palette, error));
    Design design;
    design.stage = Stage::PlasmaSplash;
    design.name = QStringLiteral("Dusk");
    design.content = {{QStringLiteral("palette"), palette.id},
                      {QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("@background")}}},
                      {QStringLiteral("elements"), QJsonArray()}};
    QVERIFY(m_library->saveDesign(design, error));
    const Mask mask{{}, QStringLiteral("Dusk"), {{Stage::PlasmaSplash, Slot{Slot::Kind::Design, design.id}}}};
    QVERIFY2(execute(plan(mask), &error), qPrintable(error));
    QVERIFY(designUpdates(*m_library).isEmpty());

    // Saving the design again unchanged is no update; a palette change is.
    QVERIFY(m_library->saveDesign(design, error));
    QVERIFY(designUpdates(*m_library).isEmpty());
    palette.colours.insert(QStringLiteral("background"), QStringLiteral("#000000"));
    QVERIFY(m_library->saveSharedElement(palette, error));
    QCOMPARE(designUpdates(*m_library), (QMap<Stage, QString>{{Stage::PlasmaSplash, design.id}}));

    // Applying it again brings the look up to date.
    const Plan update = plan(mask);
    QCOMPARE(update.target.plasmaSplash->theme, QStringLiteral("ekpo-dusk-v2"));
    QVERIFY2(execute(update, &error), qPrintable(error));
    QVERIFY(designUpdates(*m_library).isEmpty());
}

void TestExecutor::designAppliesAsBootMenu()
{
    if (QStandardPaths::findExecutable(QStringLiteral("grub-mkfont")).isEmpty())
        QSKIP("grub-mkfont isn't installed (grub-common)");
    QString error;
    const auto font = m_library->addAsset(QStringLiteral("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"), Asset::Kind::Font, QStringLiteral("Noto Sans"), error);
    QVERIFY2(font, qPrintable(error));
    Design design;
    design.stage = Stage::BootMenu;
    design.name = QStringLiteral("Night Menu");
    design.content = {{QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#102030")}}},
                      {QStringLiteral("elements"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("menu")}, {QStringLiteral("font"), font->hash},
                                                                          {QStringLiteral("size"), 2.2}, {QStringLiteral("x"), 30}, {QStringLiteral("y"), 30},
                                                                          {QStringLiteral("width"), 40}, {QStringLiteral("height"), 40}}}}};
    QVERIFY(m_library->saveDesign(design, error));
    const Mask mask{{}, QStringLiteral("Night"), {{Stage::BootMenu, Slot{Slot::Kind::Design, design.id}}}};

    // Built for the primary screen, which GRUB_GFXMODE then asks for (and auto after it).
    const Plan first = plan(mask);
    QVERIFY2(first.canApply(), qPrintable(first.problems.join(QLatin1Char('\n'))));
    QCOMPARE(first.screen, QSize(2560, 1440));
    QCOMPARE(first.target.bootMenu->theme, QStringLiteral("ekpo-night-menu-v1"));
    QCOMPARE(first.target.bootMenu->gfxmode, QStringLiteral("2560x1440,auto"));
    QCOMPARE(first.target.bootMenu->design, design.id);
    QVERIFY2(execute(first, &error), qPrintable(error));
    QVERIFY(QFile::exists(at(QStringLiteral("/boot/grub/themes/ekpo-night-menu-v1/theme.txt"))));
    QFile dropInFile(at(QStringLiteral("/etc/default/grub.d/zz-ekpo.cfg")));
    QVERIFY(dropInFile.open(QIODevice::ReadOnly));
    const QString dropIn = QString::fromUtf8(dropInFile.readAll());
    QVERIFY2(dropIn.contains(QLatin1String("GRUB_GFXMODE=\"2560x1440,auto\"")) || dropIn.contains(QLatin1String("GRUB_GFXMODE=2560x1440,auto")), qPrintable(dropIn));

    // The real grub.cfg's entries fit; this boot predates the apply, so it says nothing yet.
    QStringList errors;
    QCOMPARE(m_library->history(errors).first().state.value(QStringLiteral("clippedEntries")).toArray(), QJsonArray());
    SystemState state = probe();
    BootMenuCheck check = checkBootMenu(*m_library, state);
    QCOMPARE(check.design, design.id);
    QCOMPARE(check.builtFor, QSize(2560, 1440));
    QVERIFY(!check.screenChanged);
    QVERIFY(!check.fellBack);

    // Another monitor: rebuild for it. A boot after the apply in another mode: GRUB fell back.
    state.primaryMode = QSize(1920, 1080);
    QVERIFY(checkBootMenu(*m_library, state).screenChanged);
    state.bootedAt = QDateTime::currentDateTimeUtc().addSecs(60);
    state.bootMode = QSize(1024, 768);
    QVERIFY(checkBootMenu(*m_library, state).fellBack);
    PlanContext context{*m_library, state, m_root, m_home};
    context.screen = QSize(1920, 1080);
    const Plan rebuild = planMask(context, mask);
    QCOMPARE(rebuild.target.bootMenu->theme, QStringLiteral("ekpo-night-menu-v2"));
    QCOMPARE(rebuild.target.bootMenu->gfxmode, QStringLiteral("1920x1080,auto"));

    // An imported theme isn't built for one size: GRUB gets the usual list again.
    const Plan imported = plan(Mask{{}, QStringLiteral("Hollow"), {{Stage::BootMenu, Slot{Slot::Kind::Import, m_hollow}}}});
    QCOMPARE(imported.target.bootMenu->gfxmode, defaultGfxmode);
    QVERIFY(imported.screen.isEmpty());
}

void TestExecutor::playsTheBoot()
{
    // The applied look: the reference system's GRUB theme and Plymouth theme, Breeze.
    QTemporaryDir out;
    const SystemState state = probe();
    QVariantList steps = playbackSteps(*m_library, state, m_root, m_home, {}, out.path());
    QCOMPARE(steps.size(), 3);
    QCOMPARE(steps[0].toMap().value(QStringLiteral("stage")).toString(), QStringLiteral("bootMenu"));
    QCOMPARE(steps[1].toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("none")); // not a splash Ekpo made
    QVERIFY(!steps[1].toMap().value(QStringLiteral("note")).toString().isEmpty());
    QCOMPARE(steps[2].toMap().value(QStringLiteral("stage")).toString(), QStringLiteral("plasmaSplash"));

    // A Mask of designs: each stage drawn as Ekpo previews it; the boot menu stays as applied.
    Design splash;
    splash.stage = Stage::BootSplash;
    splash.name = QStringLiteral("Night");
    splash.content = {{QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#102030")}}},
                      {QStringLiteral("elements"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("spinner")}, {QStringLiteral("x"), 48.5},
                                                                          {QStringLiteral("y"), 60}, {QStringLiteral("width"), 3}, {QStringLiteral("height"), 5}}}}};
    Design plasma;
    plasma.stage = Stage::PlasmaSplash;
    plasma.name = QStringLiteral("Dawn");
    plasma.content = {{QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#102030")}}}, {QStringLiteral("elements"), QJsonArray()}};
    QString error;
    QVERIFY(m_library->saveDesign(splash, error));
    QVERIFY(m_library->saveDesign(plasma, error));
    steps = playbackSteps(*m_library, state, m_root, m_home,
                          {{Stage::BootMenu, Slot{}}, {Stage::BootSplash, Slot{Slot::Kind::Design, splash.id}}, {Stage::PlasmaSplash, Slot{Slot::Kind::Design, plasma.id}}},
                          out.path());
    const QVariantMap boot = steps[1].toMap(), desktop = steps[2].toMap();
    QCOMPARE(boot.value(QStringLiteral("kind")).toString(), QStringLiteral("plymouth"));
    QCOMPARE(boot.value(QStringLiteral("label")).toString(), QStringLiteral("Night"));
    const QVariantMap layout = boot.value(QStringLiteral("layout")).toMap();
    QVERIFY(QFile::exists(layout.value(QStringLiteral("background")).toUrl().toLocalFile()));
    QCOMPARE(layout.value(QStringLiteral("frames")).toList().size(), 60);
    QCOMPARE(desktop.value(QStringLiteral("kind")).toString(), QStringLiteral("qml"));
    QVERIFY(QFile::exists(desktop.value(QStringLiteral("source")).toUrl().toLocalFile()));
}

EKPO_GUI_TEST_MAIN(TestExecutor)
#include "tst_executor.moc"
