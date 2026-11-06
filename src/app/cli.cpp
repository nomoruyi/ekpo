#include "cli.h"

#include "executor.h"
#include "planner.h"

#include <QEventLoop>
#include <QFile>
#include <QTextStream>

#include <cstdio>

namespace {

const QStringList commands = {QStringLiteral("status"), QStringLiteral("apply"), QStringLiteral("restore"), QStringLiteral("recover"),
                              QStringLiteral("help"), QStringLiteral("--help"), QStringLiteral("-h")};

const QMap<Stage, QString> stageNames = {
    {Stage::BootMenu, QStringLiteral("Boot menu")},
    {Stage::BootSplash, QStringLiteral("Boot splash")},
    {Stage::PlasmaSplash, QStringLiteral("Plasma splash")},
};

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err()
{
    static QTextStream stream(stderr);
    return stream;
}

int usage(int code)
{
    (code == 0 ? out() : err()) << "Usage: ekpo [command]\n"
                                   "  (no command)                start the app\n"
                                   "  status                      show the current look and anything that needs attention\n"
                                   "  apply <mask>                apply a Mask (by name or id)\n"
                                   "  restore <number|original>   restore a history entry, or the look from before Ekpo\n"
                                   "  recover                     roll back an apply that was cut off\n"
                                   "Applies show what will change and ask before doing anything.\n"
                                << Qt::flush;
    return code;
}

void printPlan(const Plan &plan)
{
    out() << "\n" << plan.title << "\n";
    int n = 0;
    for (const PlannedStage &stage : plan.stages) {
        out() << "\n" << ++n << ". " << stageNames.value(stage.stage) << ": " << stage.from << " -> " << stage.to << "\n";
        for (const QString &action : stage.actions)
            out() << "   - " << action << "\n";
        for (const FileDiff &d : stage.diffs) {
            out() << "   " << d.path << ":\n";
            if (d.before.isEmpty())
                out() << "     (new file)\n";
            for (const QString &line : d.before.trimmed().split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                out() << "     - " << line << "\n";
            if (d.after.isEmpty())
                out() << "     (deleted)\n";
            for (const QString &line : d.after.trimmed().split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                out() << "     + " << line << "\n";
        }
    }
    if (!plan.commands.isEmpty()) {
        out() << "\nCommands:\n";
        for (const QString &c : plan.commands)
            out() << "   # " << c << "\n";
    }
    for (const QString &note : plan.notes)
        out() << "\nNote: " << note << "\n";
    if (plan.estimatedSeconds > 0)
        out() << "\nTakes about " << plan.estimatedSeconds << " s.\n";
    for (const QString &problem : plan.problems)
        out() << "\nProblem: " << problem << "\n";
    out() << Qt::flush;
}

bool confirm(const QString &question)
{
    out() << "\n" << question << " [y/N] " << Qt::flush;
    QTextStream in(stdin);
    const QString answer = in.readLine().trimmed().toLower();
    return answer == QLatin1String("y") || answer == QLatin1String("yes");
}

// Runs an apply or a recover to the end, printing the helper's progress.
int run(Executor &executor, const std::function<void()> &start)
{
    QEventLoop loop;
    bool ok = false;
    QString error;
    int number = 0;
    QObject::connect(&executor, &Executor::progress, &loop, [](const QString &text) { out() << "  " << text << "\n" << Qt::flush; });
    QObject::connect(&executor, &Executor::finished, &loop, [&](bool success, const QString &message, int entry) {
        ok = success;
        error = message;
        number = entry;
        loop.quit();
    });
    start();
    if (executor.isRunning())
        loop.exec();
    if (!ok) {
        err() << "Failed: " << error << "\n" << Qt::flush;
        return 1;
    }
    if (!error.isEmpty())
        err() << error << "\n";
    out() << (number > 0 ? QStringLiteral("Done. Saved as history entry #%1.\n").arg(number) : QStringLiteral("Done.\n")) << Qt::flush;
    return 0;
}

int applyPlan(Library &library, const AppEnvironment &env, const Plan &plan)
{
    printPlan(plan);
    if (plan.stages.isEmpty() && plan.problems.isEmpty()) {
        out() << "\nThis is already the current look.\n";
        return 0;
    }
    if (!plan.canApply()) {
        err() << "\nCan't apply this; nothing was changed.\n";
        return 1;
    }
    if (!confirm(plan.request ? QStringLiteral("Apply? This asks for your password.") : QStringLiteral("Apply?"))) {
        out() << "Nothing was changed.\n";
        return 1;
    }
    Executor executor(library, env.homeDir, env.launcher);
    return run(executor, [&] { executor.apply(plan); });
}

int status(const Library &library, const AppEnvironment &env)
{
    const SystemState state = probeSystem(env.rootDir, env.homeDir);
    const Target current = currentTarget(state, env.rootDir);
    const QList<Stage> drifted = driftedStages(library, current);

    out() << "Boot menu:     " << current.bootMenu->label << (current.bootMenu->original ? " (set in /etc/default/grub)" : " (set by Ekpo)") << "\n";
    out() << "Boot splash:   " << current.bootSplash->label << " (" << current.bootSplash->mode << ")\n";
    out() << "Plasma splash: " << (current.plasmaSplash->label.isEmpty() ? QStringLiteral("Plasma default") : current.plasmaSplash->label) << "\n";
    out() << "Initrds:       built by " << (state.initramfsGenerator.isEmpty() ? QStringLiteral("an unknown tool") : state.initramfsGenerator) << "\n";

    QStringList attention = state.errors;
    if (state.leftoverJournal)
        attention << QStringLiteral("An apply was cut off: run \"ekpo recover\".");
    if (!state.cmdlineSplash)
        attention << QStringLiteral("The kernel command line has no \"splash\", so no boot splash shows.");
    for (Stage stage : drifted)
        attention << QStringLiteral("%1 was changed outside Ekpo since its last apply.").arg(stageNames.value(stage));
    int outdated = 0;
    for (const SystemState::Kernel &k : state.kernels) {
        if (k.hasInitrd && state.plymouthSelectedAt.isValid() && k.initrdModified < state.plymouthSelectedAt)
            ++outdated;
    }
    if (outdated > 0)
        attention << QStringLiteral("%1 kernels still boot with an older splash.").arg(outdated);
    QStringList errors;
    const QList<HistoryEntry> history = library.history(errors);
    if (!history.isEmpty())
        out() << "Last apply:    #" << history.first().number << " " << history.first().title << " ("
              << history.first().createdAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")) << ")\n";
    out() << (attention.isEmpty() ? QStringLiteral("\nNothing needs attention.\n") : QStringLiteral("\nNeeds attention:\n  - ") + attention.join(QStringLiteral("\n  - ")) + QLatin1Char('\n'))
          << Qt::flush;
    return attention.isEmpty() ? 0 : 3;
}

bool isCliCommand(const QString &arg)
{
    return commands.contains(arg);
}

} // namespace

int runCli(const QStringList &args)
{
    const QString command = args.value(0);
    if (command == QLatin1String("help") || command == QLatin1String("--help") || command == QLatin1String("-h"))
        return usage(0);
    const bool takesArgument = command == QLatin1String("apply") || command == QLatin1String("restore");
    if (!isCliCommand(command) || args.size() != (takesArgument ? 2 : 1))
        return usage(2);

    QString error;
    auto library = Library::open(Library::defaultPath(), error);
    if (!library) {
        err() << error << "\n";
        return 1;
    }
    const AppEnvironment env = detectEnvironment();

    if (command == QLatin1String("status"))
        return status(*library, env);
    if (command == QLatin1String("recover")) {
        if (!probeSystem(env.rootDir, env.homeDir).leftoverJournal) {
            out() << "Nothing to recover.\n";
            return 0;
        }
        Executor executor(*library, env.homeDir, env.launcher);
        return run(executor, [&] { executor.recover(); });
    }

    const SystemState state = probeSystem(env.rootDir, env.homeDir);
    const PlanContext context{*library, state, env.rootDir, env.homeDir};
    const QString arg = args.at(1);
    QStringList errors;
    if (command == QLatin1String("apply")) {
        for (const Mask &mask : library->masks(errors)) {
            if (mask.id == arg || mask.name.compare(arg, Qt::CaseInsensitive) == 0)
                return applyPlan(*library, env, planMask(context, mask));
        }
        err() << "No Mask called \"" << arg << "\". Masks: ";
        QStringList names;
        for (const Mask &mask : library->masks(errors))
            names << mask.name;
        err() << (names.isEmpty() ? QStringLiteral("none yet") : names.join(QStringLiteral(", "))) << "\n";
        return 1;
    }

    // restore
    if (arg.compare(QLatin1String("original"), Qt::CaseInsensitive) == 0) {
        const Target original = originalTarget(*library);
        if (original.isEmpty()) {
            out() << "Ekpo hasn't changed anything yet, so there is no Original to restore.\n";
            return 0;
        }
        return applyPlan(*library, env, planTarget(context, original, QStringLiteral("Restore Original")));
    }
    bool isNumber = false;
    const int number = QString(arg).remove(QLatin1Char('#')).toInt(&isNumber);
    for (const HistoryEntry &e : library->history(errors)) {
        if (isNumber && e.number == number)
            return applyPlan(*library, env, planTarget(context, Target::fromJson(e.state.value(QStringLiteral("target")).toObject()),
                                                       QStringLiteral("Restore #%1: %2").arg(number).arg(e.title)));
    }
    err() << "No history entry " << arg << ". Use \"ekpo status\" or the app's History page.\n";
    return 1;
}
