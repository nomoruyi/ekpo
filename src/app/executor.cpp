#include "executor.h"

#include <KConfig>
#include <KConfigGroup>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace {

const QString userLookAndFeel = QStringLiteral("/.local/share/plasma/look-and-feel/");

// Console steps that bring back the state from before an apply (SPEC.md §4.11),
// for when Ekpo itself isn't available.
QByteArray restoreText(const Plan &plan)
{
    QStringList t{QStringLiteral("Ekpo history entry: %1").arg(plan.title),
                  QStringLiteral("Applied %1.").arg(QDateTime::currentDateTime().toString(Qt::ISODate)),
                  QString(),
                  QStringLiteral("To return to the state from before this apply without Ekpo, run these in a terminal"),
                  QStringLiteral("(or a text console: Ctrl+Alt+F3, or a live USB after chroot):"),
                  QString()};
    const Target &before = plan.before;
    if (plan.target.bootMenu) {
        t << QStringLiteral("# Boot menu");
        if (before.bootMenu->original) {
            t << QStringLiteral("sudo rm -f /etc/default/grub.d/zz-ekpo.cfg");
        } else {
            const auto diff = std::find_if(plan.stages.cbegin(), plan.stages.cend(), [](const PlannedStage &s) { return s.stage == Stage::BootMenu; });
            t << QStringLiteral("sudo tee /etc/default/grub.d/zz-ekpo.cfg <<'EOF'") << diff->diffs.value(0).before.trimmed() << QStringLiteral("EOF");
        }
        t << QStringLiteral("sudo update-grub") << QString();
    }
    if (plan.target.bootSplash) {
        const BootSplashTarget &s = *before.bootSplash;
        t << QStringLiteral("# Boot splash");
        if (s.mode == QLatin1String("manual"))
            t << QStringLiteral("sudo update-alternatives --set default.plymouth /usr/share/plymouth/themes/%1/%1.plymouth").arg(s.theme);
        else
            t << QStringLiteral("sudo update-alternatives --auto default.plymouth");
        t << QStringLiteral("sudo sed -i 's/^Theme=.*/Theme=%1/' /etc/plymouth/plymouthd.conf").arg(s.daemonTheme)
          << QStringLiteral("sudo update-initramfs -u -k all   # or, with dracut: sudo dracut --regenerate-all --force") << QString();
    }
    if (plan.target.plasmaSplash) {
        const PlasmaSplashTarget &p = *before.plasmaSplash;
        t << QStringLiteral("# Plasma splash (as your user, no sudo)");
        t << (p.theme.isEmpty() ? QStringLiteral("kwriteconfig6 --file ksplashrc --group KSplash --key Theme --delete")
                                : QStringLiteral("kwriteconfig6 --file ksplashrc --group KSplash --key Theme %1").arg(p.theme));
        t << (p.engine.isEmpty() ? QStringLiteral("kwriteconfig6 --file ksplashrc --group KSplash --key Engine --delete")
                                 : QStringLiteral("kwriteconfig6 --file ksplashrc --group KSplash --key Engine %1").arg(p.engine));
    }
    return (t.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
}

} // namespace

QString currentBootId()
{
    if (qEnvironmentVariableIsSet("EKPO_DEV") && qEnvironmentVariableIsSet("EKPO_BOOT_ID"))
        return qEnvironmentVariable("EKPO_BOOT_ID"); // tests and the sandbox "reboot" this way
    QFile f(QStringLiteral("/proc/sys/kernel/random/boot_id"));
    return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readAll()).trimmed() : QString();
}

HelperLauncher systemHelperLauncher()
{
    return {QStringLiteral("/usr/bin/systemd-inhibit"),
            {QStringLiteral("--what=shutdown:sleep"), QStringLiteral("--who=Ekpo"), QStringLiteral("--why=Applying a new boot look"),
             QStringLiteral("/usr/bin/pkexec"), QStringLiteral(EKPO_HELPER_PATH)}};
}

AppEnvironment detectEnvironment()
{
    AppEnvironment env{QStringLiteral("/"), QDir::homePath(), systemHelperLauncher()};
#ifdef EKPO_SANDBOX
    if (qEnvironmentVariableIsSet("EKPO_DEV") && qEnvironmentVariableIsSet("EKPO_SYSTEM_ROOT")) {
        env.rootDir = qEnvironmentVariable("EKPO_SYSTEM_ROOT");
        env.homeDir = qEnvironmentVariable("EKPO_SYSTEM_HOME", env.homeDir);
        env.launcher = HelperLauncher{QStringLiteral(EKPO_HELPER_TEST_PATH), {}};
        env.launcher.environment.insert(QStringLiteral("EKPO_HELPER_ROOT"), env.rootDir);
        env.launcher.environment.insert(QStringLiteral("EKPO_HELPER_BIN"), QStringLiteral(EKPO_STUBS_DIR));
    }
#endif
    return env;
}

Executor::Executor(Library &library, const QString &homeDir, const HelperLauncher &launcher, QObject *parent)
    : QObject(parent)
    , m_library(library)
    , m_homeDir(homeDir)
    , m_launcher(launcher)
{
}

Executor::~Executor() = default;

void Executor::apply(const Plan &plan)
{
    if (m_running)
        return;
    m_plan = plan;
    m_plasmaUndo = {};
    m_buffer.clear();
    m_result = {};
    m_running = true;

    if (!plan.canApply())
        return fail(plan.problems.isEmpty() ? tr("Nothing to change.") : plan.problems.join(QLatin1Char('\n')));

    QString error;
    for (auto it = plan.builds.cbegin(); it != plan.builds.cend(); ++it) {
        if (!m_library.saveBuild(it.value(), error))
            return fail(error);
    }
    for (Stage stage : allStages) {
        const QJsonObject wanted = plan.target.stageJson(stage);
        if (!wanted.isEmpty() && !m_library.saveOriginalIfMissing(stage, plan.before.stageJson(stage), error))
            return fail(error);
    }

    if (plan.target.plasmaSplash) {
        Q_EMIT progress(tr("Setting the Plasma splash"));
        if (!applyPlasma(*plan.target.plasmaSplash, error)) {
            undoPlasma();
            return fail(error);
        }
    }
    if (!plan.request)
        return succeed({});

    Q_EMIT progress(tr("Asking for permission to change the boot stages"));
    QBuffer input;
    input.open(QIODevice::WriteOnly);
    Request request = *plan.request;
    request.keep = keptThemes(m_library, plan.target);
    input.write(serializeRequest(request) + '\n');
    writePayload(input, plan.payload);
    startHelper(input.data());
}

void Executor::recover()
{
    if (m_running)
        return;
    m_plan = {};
    m_plasmaUndo = {};
    m_buffer.clear();
    m_result = {};
    m_running = true;
    m_recovering = true;
    Q_EMIT progress(tr("Asking for permission to roll back the interrupted apply"));
    Request request;
    request.operation = Request::Operation::Recover;
    startHelper(serializeRequest(request) + '\n');
}

void Executor::startHelper(const QByteArray &input)
{
    m_process = std::make_unique<QProcess>();
    m_process->setProcessEnvironment(m_launcher.environment);
    connect(m_process.get(), &QProcess::readyReadStandardOutput, this, &Executor::readHelperOutput);
    connect(m_process.get(), &QProcess::finished, this, &Executor::helperFinished);
    connect(m_process.get(), &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            helperFinished();
    });
    m_process->start(m_launcher.program, m_launcher.arguments);
    m_process->write(input);
    m_process->closeWriteChannel();
}

bool Executor::applyPlasma(const PlasmaSplashTarget &target, QString &error)
{
    if (!target.build.isEmpty()) {
        const auto files = m_library.buildFiles(target.build);
        const QString dir = m_homeDir + userLookAndFeel + target.theme;
        if (!files) {
            error = tr("The files of %1 are missing from the library.").arg(target.label);
            return false;
        }
        if (QFileInfo::exists(dir)) {
            for (const PayloadFile &f : *files) {
                QFile existing(dir + QLatin1Char('/') + f.path);
                if (!existing.open(QIODevice::ReadOnly) || existing.readAll() != f.data) {
                    error = tr("A different Plasma splash is already installed as %1.").arg(dir);
                    return false;
                }
            }
        } else {
            m_plasmaUndo.createdPackage = dir;
            for (const PayloadFile &f : *files) {
                QSaveFile out(dir + QLatin1Char('/') + f.path);
                if (!QDir().mkpath(QFileInfo(out.fileName()).path()) || !out.open(QIODevice::WriteOnly) || out.write(f.data) < 0 || !out.commit()) {
                    error = tr("Can't copy the Plasma splash to %1.").arg(dir);
                    return false;
                }
            }
        }
    }

    KConfig config(m_homeDir + QStringLiteral("/.config/ksplashrc"), KConfig::SimpleConfig);
    KConfigGroup group = config.group(QStringLiteral("KSplash"));
    m_plasmaUndo.theme = group.readEntry("Theme");
    m_plasmaUndo.engine = group.readEntry("Engine");
    m_plasmaUndo.changed = true;
    const auto set = [&group](const char *key, const QString &value) {
        if (value.isEmpty())
            group.deleteEntry(key);
        else
            group.writeEntry(key, value);
    };
    set("Theme", target.theme);
    set("Engine", target.engine);
    if (!config.sync()) {
        error = tr("Can't write ~/.config/ksplashrc.");
        return false;
    }
    return true;
}

void Executor::undoPlasma()
{
    if (m_plasmaUndo.changed) {
        KConfig config(m_homeDir + QStringLiteral("/.config/ksplashrc"), KConfig::SimpleConfig);
        KConfigGroup group = config.group(QStringLiteral("KSplash"));
        const auto set = [&group](const char *key, const QString &value) {
            if (value.isEmpty())
                group.deleteEntry(key);
            else
                group.writeEntry(key, value);
        };
        set("Theme", m_plasmaUndo.theme);
        set("Engine", m_plasmaUndo.engine);
        config.sync();
    }
    if (!m_plasmaUndo.createdPackage.isEmpty())
        QDir(m_plasmaUndo.createdPackage).removeRecursively();
    m_plasmaUndo = {};
}

void Executor::readHelperOutput()
{
    m_buffer += m_process->readAllStandardOutput();
    qsizetype newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QJsonObject o = QJsonDocument::fromJson(m_buffer.left(newline)).object();
        m_buffer.remove(0, newline + 1);
        if (o.value(QStringLiteral("event")) == QLatin1String("progress"))
            Q_EMIT progress(o.value(QStringLiteral("text")).toString());
        else if (o.value(QStringLiteral("event")) == QLatin1String("result"))
            m_result = o;
    }
}

void Executor::helperFinished()
{
    if (!m_running || !m_process)
        return;
    readHelperOutput();
    const int outcome = m_result.value(QStringLiteral("outcome")).toInt(-1);
    QString error = m_result.value(QStringLiteral("error")).toString();
    m_process.release()->deleteLater();
    if (m_recovering) {
        m_recovering = false;
        m_running = false;
        Q_EMIT finished(outcome == 0, outcome == -1 ? tr("The helper didn't run (the password prompt may have been cancelled).") : error, 0);
        return;
    }
    if (outcome == 0)
        return succeed(m_result.value(QStringLiteral("details")).toObject().value(QStringLiteral("menuEntries")).toArray());

    undoPlasma();
    if (outcome == -1)
        error = tr("The helper didn't run (the password prompt may have been cancelled). Nothing was changed.");
    else if (outcome == 3)
        error = tr("%1\nEkpo couldn't undo everything. Open Ekpo again to finish the rollback.").arg(error);
    fail(error);
}

void Executor::succeed(const QJsonArray &menuEntries)
{
    HistoryEntry entry{0, QDateTime::currentDateTimeUtc(), m_plan.title,
                       {{QStringLiteral("target"), m_plan.target.toJson()},
                        {QStringLiteral("before"), m_plan.before.toJson()},
                        {QStringLiteral("menuEntries"), menuEntries}}};
    QString error;
    const bool saved = m_library.addHistoryEntry(entry, error, {{QStringLiteral("RESTORE.txt"), restoreText(m_plan)}});
    if (saved)
        m_library.pruneBuilds();
    // Boot stages only show after a reboot: ask then whether they worked (SPEC.md §4.9).
    if (saved && (m_plan.target.bootMenu || m_plan.target.bootSplash))
        m_library.setPendingCheck({{QStringLiteral("number"), entry.number}, {QStringLiteral("title"), m_plan.title}, {QStringLiteral("bootId"), currentBootId()}}, error);
    m_running = false;
    Q_EMIT finished(true, saved ? QString() : tr("Applied, but the history entry couldn't be saved: %1").arg(error), entry.number);
}

void Executor::fail(const QString &error)
{
    m_running = false;
    Q_EMIT finished(false, error, 0);
}
