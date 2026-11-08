#include "planner.h"

#include "configtext.h"
#include "gengrub.h"
#include "genksplash.h"
#include "genplymouth.h"
#include "validate.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStorageInfo>

namespace {

const QString grubThemesDir = QStringLiteral("/boot/grub/themes/");
const QString plymouthThemesDir = QStringLiteral("/usr/share/plymouth/themes/");
const QString userLookAndFeel = QStringLiteral("/.local/share/plasma/look-and-feel/");
constexpr int secondsPerInitrd = 8; // measured here (INTERVIEW.md round 5)

QString rootOf(const PlanContext &c)
{
    return c.rootDir == QLatin1String("/") ? QString() : c.rootDir;
}

QString readText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QList<PayloadFile> filesIn(const QString &dir)
{
    QList<PayloadFile> files;
    for (QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories); it.hasNext();) {
        QFile f(it.next());
        if (f.open(QIODevice::ReadOnly))
            files.append({QDir(dir).relativeFilePath(f.fileName()), f.readAll()});
    }
    std::sort(files.begin(), files.end(), [](const PayloadFile &a, const PayloadFile &b) { return a.path < b.path; });
    return files;
}

// The first version whose folder is free or already holds exactly these files
// (the helper accepts re-installing identical files).
template<typename MakeFiles>
std::pair<QString, QList<PayloadFile>> pickVersion(const QString &dir, const QString &name, MakeFiles makeFiles)
{
    for (int n = 1;; ++n) {
        const QString id = QStringLiteral("ekpo-%1-v%2").arg(slug(name)).arg(n);
        const QList<PayloadFile> files = makeFiles(id);
        if (!QFileInfo::exists(dir + id))
            return {id, files};
        const bool same = std::all_of(files.cbegin(), files.cend(), [&](const PayloadFile &f) {
            QFile existing(dir + id + QLatin1Char('/') + f.path);
            return existing.open(QIODevice::ReadOnly) && existing.readAll() == f.data;
        });
        if (same)
            return {id, files};
    }
}

// Drops files the helper wouldn't accept, with a note for each.
QList<PayloadFile> allowedFiles(ThemeTarget target, const QList<PayloadFile> &files, QStringList &notes)
{
    QList<PayloadFile> kept;
    for (const PayloadFile &f : files) {
        if (validRelativePath(f.path) && checkFile(target, f).isEmpty())
            kept << f;
        else
            notes << QStringLiteral("Not installed: %1 (not a file type a theme needs).").arg(f.path);
    }
    return kept;
}

// A Plymouth copy needs <id>.plymouth and ImageDir/ScriptFile pointing at its new folder.
QList<PayloadFile> normalizedPlymouth(const QList<PayloadFile> &files, const QString &originalFolder, const QString &id)
{
    QList<PayloadFile> out;
    const QString oldPrefix = plymouthThemesDir + originalFolder + QLatin1Char('/');
    for (const PayloadFile &f : files) {
        if (!f.path.endsWith(QLatin1String(".plymouth"))) {
            out << f;
            continue;
        }
        QStringList lines = QString::fromUtf8(f.data).split(QLatin1Char('\n'));
        for (QString &line : lines) {
            const qsizetype eq = line.indexOf(QLatin1Char('='));
            const QString key = line.left(eq).trimmed();
            if (eq < 0)
                continue;
            if (key == QLatin1String("ImageDir")) {
                line = QStringLiteral("ImageDir=") + plymouthThemesDir + id;
            } else if (key == QLatin1String("ScriptFile")) {
                const QString value = QDir::cleanPath(line.mid(eq + 1).trimmed());
                const QString rel = value.startsWith(oldPrefix) ? value.mid(oldPrefix.size()) : QFileInfo(value).fileName();
                line = QStringLiteral("ScriptFile=") + plymouthThemesDir + id + QLatin1Char('/') + rel;
            }
        }
        out << PayloadFile{id + QStringLiteral(".plymouth"), lines.join(QLatin1Char('\n')).toUtf8()};
    }
    return out;
}

QString pluginId(const QList<PayloadFile> &files)
{
    for (const PayloadFile &f : files) {
        if (f.path == QLatin1String("metadata.json"))
            return QJsonDocument::fromJson(f.data).object().value(QStringLiteral("KPlugin")).toObject().value(QStringLiteral("Id")).toString();
    }
    return {};
}

// ksplashrc Engine= for a QML splash: keep what is set (empty means Plasma's
// default, KSplashQML) unless splashes are switched off.
QString qmlEngine(const Target &before)
{
    const QString engine = before.plasmaSplash->engine;
    return engine.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0 ? QStringLiteral("KSplashQML") : engine;
}

} // namespace

QString slug(const QString &name)
{
    QString s = name.toLower();
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    s = s.left(32);
    while (s.startsWith(QLatin1Char('-')))
        s.remove(0, 1);
    while (s.endsWith(QLatin1Char('-')))
        s.chop(1);
    return s.isEmpty() ? QStringLiteral("theme") : s;
}

QSize primaryScreenOr(const SystemState &state)
{
    return state.primaryMode.isEmpty() ? builtScreen : state.primaryMode;
}

Plan planMask(const PlanContext &context, const Mask &mask)
{
    bool builtForScreen = false;
    const QString root = rootOf(context);
    const Target before = currentTarget(context.state, context.rootDir);
    QStringList loadErrors;
    const QList<ImportedTheme> imports = context.library.imports(loadErrors);

    Target target;
    QMap<QString, QList<PayloadFile>> builds;
    QStringList problems;
    QStringList notes;
    for (auto it = mask.stageSlots.cbegin(); it != mask.stageSlots.cend(); ++it) {
        if (it.value().kind == Slot::Kind::Design) {
            QString error;
            const auto saved = context.library.design(it.value().ref, 0, error);
            if (!saved || saved->stage != it.key()) {
                problems << QStringLiteral("The Mask refers to a design that is no longer in the library.");
                continue;
            }
            // Built with its shared logos and palette as they are now (SPEC.md §3).
            const std::optional<Design> design = context.library.resolved(*saved, problems);
            const QString stamp = Library::stamp(*design);
            if (design->stage == Stage::BootMenu) {
                // Built for one screen, the mode GRUB_GFXMODE then asks for (SPEC.md §4.5).
                GrubLayout layout;
                QStringList buildProblems;
                const QList<PayloadFile> built = buildBootMenu(*design, context.library, context.screen, context.state.menuEntries, buildProblems, &layout);
                problems << buildProblems;
                if (!layout.fits)
                    notes << QStringLiteral("\u201c%1\u201d won't fit: the menu needs %2 px and the screen is %3 px wide. A smaller menu font fixes it.")
                                 .arg(layout.longestEntry)
                                 .arg(layout.neededWidth)
                                 .arg(context.screen.width());
                const auto [id, build] = pickVersion(root + grubThemesDir, design->name, [&](const QString &) { return built; });
                const BootMenuTarget &now = *before.bootMenu;
                BootMenuTarget menu;
                menu.label = design->name;
                menu.theme = id;
                menu.build = Library::buildHash(build);
                menu.gfxmode = QStringLiteral("%1x%2,auto").arg(context.screen.width()).arg(context.screen.height());
                menu.timeout = now.original ? std::nullopt : now.timeout;
                menu.timeoutStyle = now.original ? QString() : now.timeoutStyle;
                menu.design = design->id;
                menu.stamp = stamp;
                builds.insert(menu.build, build);
                target.bootMenu = menu;
                builtForScreen = true;
                continue;
            }
            if (design->stage == Stage::BootSplash) {
                // Built once; each candidate id only renames the .plymouth file and its paths.
                QStringList buildProblems;
                const QString builtAs = QStringLiteral("ekpo-design-v1");
                const QList<PayloadFile> built = buildPlymouthSplash(*design, builtAs, context.library, context.screen, buildProblems);
                problems << buildProblems;
                const auto [id, build] = pickVersion(root + plymouthThemesDir, design->name, [&](const QString &id) { return normalizedPlymouth(built, builtAs, id); });
                BootSplashTarget splash;
                splash.label = design->name;
                splash.theme = id;
                splash.daemonTheme = id;
                splash.build = Library::buildHash(build);
                splash.design = design->id;
                splash.stamp = stamp;
                builds.insert(splash.build, build);
                target.bootSplash = splash;
                builtForScreen = true;
                continue;
            }
            // A new versioned package whenever the files differ, so every history entry stays restorable.
            QStringList buildProblems; // the same for every candidate id: keep the last call's
            const auto [id, build] = pickVersion(context.homeDir + userLookAndFeel, design->name, [&](const QString &id) {
                buildProblems.clear();
                return buildPlasmaSplash(*design, id, context.library, buildProblems);
            });
            problems << buildProblems;
            PlasmaSplashTarget plasma;
            plasma.label = design->name;
            plasma.engine = qmlEngine(before);
            plasma.theme = id;
            plasma.build = Library::buildHash(build);
            plasma.design = design->id;
            plasma.stamp = stamp;
            builds.insert(plasma.build, build);
            target.plasmaSplash = plasma;
            continue;
        }
        if (it.value().kind != Slot::Kind::Import)
            continue;
        const auto found = std::find_if(imports.cbegin(), imports.cend(), [&](const ImportedTheme &t) { return t.id == it.value().ref; });
        if (found == imports.cend() || found->stage != it.key()) {
            problems << QStringLiteral("The Mask refers to a theme that is no longer in the library.");
            continue;
        }
        const ImportedTheme &theme = *found;
        if (theme.verdict == ImportedTheme::Verdict::Broken) {
            problems << QStringLiteral("%1 is broken and can't be applied (see its import report).").arg(theme.name);
            continue;
        }
        const QList<PayloadFile> files = filesIn(context.library.importFilesPath(theme.id));

        switch (theme.stage) {
        case Stage::BootMenu: {
            BootMenuTarget menu;
            menu.label = theme.name;
            const BootMenuTarget &now = *before.bootMenu;
            menu.gfxmode = defaultGfxmode; // a theme made for several sizes, or none in particular (SPEC.md §4.9)
            menu.timeout = now.original ? std::nullopt : now.timeout;
            menu.timeoutStyle = now.original ? QString() : now.timeoutStyle;
            if (!theme.installedId.isEmpty()) {
                menu.theme = theme.installedId;
            } else {
                const QList<PayloadFile> kept = allowedFiles(ThemeTarget::Grub, files, notes);
                const auto [id, build] = pickVersion(root + grubThemesDir, theme.name, [&](const QString &) { return kept; });
                menu.theme = id;
                menu.build = Library::buildHash(build);
                builds.insert(menu.build, build);
            }
            target.bootMenu = menu;
            break;
        }
        case Stage::BootSplash: {
            BootSplashTarget splash;
            splash.label = theme.name;
            if (!theme.installedId.isEmpty()) {
                splash.theme = theme.installedId;
            } else {
                const QList<PayloadFile> kept = allowedFiles(ThemeTarget::Plymouth, files, notes);
                const QString originalFolder = QFileInfo(theme.sourcePath).fileName();
                const auto [id, build] = pickVersion(root + plymouthThemesDir, theme.name, [&](const QString &id) { return normalizedPlymouth(kept, originalFolder, id); });
                splash.theme = id;
                splash.build = Library::buildHash(build);
                builds.insert(splash.build, build);
            }
            splash.daemonTheme = splash.theme;
            target.bootSplash = splash;
            break;
        }
        case Stage::PlasmaSplash: {
            PlasmaSplashTarget plasma;
            plasma.label = theme.name;
            plasma.engine = qmlEngine(before);
            if (!theme.installedId.isEmpty()) {
                plasma.theme = theme.installedId;
            } else {
                plasma.theme = pluginId(files);
                plasma.build = Library::buildHash(files);
                builds.insert(plasma.build, files);
                if (plasma.theme.isEmpty() || !validInstalledThemeId(plasma.theme))
                    problems << QStringLiteral("%1 has no usable package ID.").arg(theme.name);
            }
            target.plasmaSplash = plasma;
            break;
        }
        }
    }

    Plan plan = planTarget(context, target, mask.name, builds);
    if (builtForScreen)
        plan.screen = context.screen;
    plan.problems = problems + plan.problems;
    plan.notes = notes + plan.notes;
    if (target.isEmpty() && problems.isEmpty())
        plan.problems << QStringLiteral("The Mask leaves every stage as it is.");
    return plan;
}

QMap<Stage, QString> designUpdates(const Library &library)
{
    QMap<Stage, QString> updates;
    const Target last = lastApplied(library);
    const auto check = [&](Stage stage, const QString &id, const QString &stamp) {
        QString error;
        QStringList problems;
        if (id.isEmpty())
            return;
        // ponytail: a changed stamp whose build comes out the same (a key the generator
        // ignores) still says "update available"; build and compare if that shows up.
        if (const auto design = library.design(id, 0, error); design && Library::stamp(library.resolved(*design, problems)) != stamp)
            updates.insert(stage, design->id);
    };
    if (last.bootMenu)
        check(Stage::BootMenu, last.bootMenu->design, last.bootMenu->stamp);
    if (last.bootSplash)
        check(Stage::BootSplash, last.bootSplash->design, last.bootSplash->stamp);
    if (last.plasmaSplash)
        check(Stage::PlasmaSplash, last.plasmaSplash->design, last.plasmaSplash->stamp);
    return updates;
}

Plan planTarget(const PlanContext &context, const Target &wanted, const QString &title, const QMap<QString, QList<PayloadFile>> &newBuilds)
{
    const QString root = rootOf(context);
    Plan plan;
    plan.title = title;
    plan.before = currentTarget(context.state, context.rootDir);
    Request request;

    // Build files come from this plan or, for restores, from the library.
    const auto buildFiles = [&](const QString &hash) -> std::optional<QList<PayloadFile>> {
        if (newBuilds.contains(hash)) {
            plan.builds.insert(hash, newBuilds.value(hash));
            return newBuilds.value(hash);
        }
        return context.library.buildFiles(hash);
    };
    const auto addPayload = [&](const QString &id, const QString &hash, ThemeTarget themeTarget) {
        const auto files = buildFiles(hash);
        if (!files) {
            plan.problems << QStringLiteral("The files of %1 are no longer in the library.").arg(id);
            return;
        }
        for (const QString &e : validateTheme(themeTarget, id, *files))
            plan.problems << QStringLiteral("%1: %2").arg(id, e);
        request.themes << Request::Theme{id, themeTarget};
        for (const PayloadFile &f : *files)
            plan.payload << PayloadFile{id + QLatin1Char('/') + f.path, f.data};
    };

    if (wanted.bootMenu && !wanted.bootMenu->sameState(*plan.before.bootMenu)) {
        const BootMenuTarget &menu = *wanted.bootMenu;
        PlannedStage stage{Stage::BootMenu, plan.before.bootMenu->label, menu.original ? QStringLiteral("Original") : menu.label, {}, {}};
        const QString dropIn = QStringLiteral("/etc/default/grub.d/zz-ekpo.cfg");
        if (menu.original) {
            request.grub = Request::Grub{true, {}, {}, {}, {}};
            stage.actions << QStringLiteral("Delete Ekpo's GRUB settings, so /etc/default/grub decides again.");
            stage.diffs << FileDiff{dropIn, readText(root + dropIn), {}};
        } else {
            request.grub = Request::Grub{false, menu.theme, menu.gfxmode, menu.timeout, menu.timeoutStyle};
            if (!menu.build.isEmpty()) {
                addPayload(menu.theme, menu.build, ThemeTarget::Grub);
                stage.actions << QStringLiteral("Install a copy as %1%2.").arg(grubThemesDir, menu.theme);
            } else if (!menu.theme.isEmpty() && !QFileInfo::exists(root + grubThemesDir + menu.theme + QStringLiteral("/theme.txt"))) {
                plan.problems << QStringLiteral("The GRUB theme %1 is no longer installed.").arg(menu.theme);
            }
            QStringList parts;
            if (!menu.theme.isEmpty())
                parts << QStringLiteral("theme");
            if (!menu.gfxmode.isEmpty())
                parts << QStringLiteral("video modes");
            if (menu.timeout)
                parts << QStringLiteral("timeout %1 s").arg(*menu.timeout);
            stage.actions << QStringLiteral("Write Ekpo's GRUB settings (%1).").arg(parts.join(QStringLiteral(", ")));
            stage.diffs << FileDiff{dropIn, readText(root + dropIn), QString::fromUtf8(grubDropInText(*request.grub))};
        }
        plan.target.bootMenu = menu;
        plan.stages << stage;
    }

    if (wanted.bootSplash && (context.rebuildSplash || !wanted.bootSplash->sameState(*plan.before.bootSplash))) {
        const BootSplashTarget &splash = *wanted.bootSplash;
        PlannedStage stage{Stage::BootSplash, plan.before.bootSplash->label, splash.label, {}, {}};
        request.plymouth = Request::Plymouth{splash.mode, splash.theme, splash.daemonTheme, context.allKernels};
        if (!splash.build.isEmpty()) {
            addPayload(splash.theme, splash.build, ThemeTarget::Plymouth);
            stage.actions << QStringLiteral("Install a copy as %1%2.").arg(plymouthThemesDir, splash.theme);
        } else if (splash.mode == QLatin1String("manual") && !QFileInfo::exists(root + plymouthThemesDir + splash.theme + QLatin1Char('/') + splash.theme + QStringLiteral(".plymouth"))) {
            plan.problems << QStringLiteral("The Plymouth theme %1 is no longer installed.").arg(splash.theme);
        }
        const QString path = plymouthThemesDir + splash.theme + QLatin1Char('/') + splash.theme + QStringLiteral(".plymouth");
        const bool registered = std::any_of(context.state.plymouthChoices.cbegin(), context.state.plymouthChoices.cend(), [&](const auto &c) { return c.path == path; });
        if (splash.mode == QLatin1String("manual")) {
            if (!registered)
                plan.commands << QStringLiteral("update-alternatives --install /usr/share/plymouth/themes/default.plymouth default.plymouth %1 50").arg(path);
            plan.commands << QStringLiteral("update-alternatives --set default.plymouth %1").arg(path);
            stage.actions << QStringLiteral("Select it as the boot splash.");
        } else {
            plan.commands << QStringLiteral("update-alternatives --auto default.plymouth");
            stage.actions << QStringLiteral("Let Plymouth's priorities pick the boot splash again.");
        }
        const QString conf = QStringLiteral("/etc/plymouth/plymouthd.conf");
        const QString confBefore = readText(root + conf);
        stage.diffs << FileDiff{conf, confBefore, QString::fromUtf8(withDaemonTheme(confBefore.toUtf8(), splash.daemonTheme))};

        qsizetype initrds = 0;
        qint64 initrdBytes = 0;
        for (const SystemState::Kernel &k : context.state.kernels) {
            if (k.hasInitrd) {
                ++initrds;
                initrdBytes += QFileInfo(root + QStringLiteral("/boot/initrd.img-") + k.version).size();
            }
        }
        const QString newest = context.state.kernels.isEmpty() ? QString() : context.state.kernels.last().version;
        const bool all = context.allKernels || initrds <= 1;
        const bool dracut = context.state.initramfsGenerator == QLatin1String("dracut");
        if (context.state.initramfsGenerator.isEmpty())
            plan.problems << QStringLiteral("No initramfs generator is installed, so the boot splash can't change.");
        if (all) {
            plan.commands << (dracut ? QStringLiteral("dracut --regenerate-all --force") : QStringLiteral("update-initramfs -u -k all"));
            stage.actions << QStringLiteral("Rebuild the initrds of all %1 kernels (about %2 s).").arg(initrds).arg(initrds * secondsPerInitrd);
            plan.estimatedSeconds += int(initrds) * secondsPerInitrd;
        } else {
            plan.commands << (dracut ? QStringLiteral("dracut --force /boot/initrd.img-%1 %1").arg(newest) : QStringLiteral("update-initramfs -u -k %1").arg(newest));
            stage.actions << QStringLiteral("Rebuild the initrd of the newest kernel, %1 (about %2 s). The other %3 keep the current splash as a fallback.")
                                 .arg(newest).arg(secondsPerInitrd).arg(initrds - 1);
            plan.estimatedSeconds += secondsPerInitrd;
            initrdBytes = QFileInfo(root + QStringLiteral("/boot/initrd.img-") + newest).size();
        }
        if (QFileInfo::exists(root + QStringLiteral("/etc/timeshift/timeshift.json")))
            plan.notes << QStringLiteral("Timeshift snapshots keep the replaced initrd%1 (about %2 MB) until they rotate.").arg(all ? QStringLiteral("s") : QString()).arg(initrdBytes >> 20);
        plan.touchesSplash = true;
        plan.allKernels = all;
        plan.newestKernel = newest;
        plan.kernelCount = int(initrds);
        request.plymouth->allKernels = all;
        plan.target.bootSplash = splash;
        plan.target.bootSplash->allKernels = all;
        plan.stages << stage;
    }

    if (wanted.plasmaSplash && !wanted.plasmaSplash->sameState(*plan.before.plasmaSplash)) {
        const PlasmaSplashTarget &plasma = *wanted.plasmaSplash;
        PlannedStage stage{Stage::PlasmaSplash, plan.before.plasmaSplash->label, plasma.label, {}, {}};
        const QString packageDir = context.homeDir + userLookAndFeel + plasma.theme;
        if (!plasma.build.isEmpty()) {
            if (!buildFiles(plasma.build))
                plan.problems << QStringLiteral("The files of %1 are no longer in the library.").arg(plasma.label);
            stage.actions << QStringLiteral("Copy the package to %1.").arg(packageDir);
        } else if (!QFileInfo::exists(packageDir) && !QFileInfo::exists(root + QStringLiteral("/usr/share/plasma/look-and-feel/") + plasma.theme)) {
            plan.problems << QStringLiteral("The Plasma splash %1 is no longer installed.").arg(plasma.theme);
        }
        stage.actions << QStringLiteral("Set it as the Plasma splash (no password needed).");
        const auto ksplashrc = [](const PlasmaSplashTarget &t) {
            QString text = QStringLiteral("[KSplash]\n");
            if (!t.engine.isEmpty())
                text += QStringLiteral("Engine=%1\n").arg(t.engine);
            if (!t.theme.isEmpty())
                text += QStringLiteral("Theme=%1\n").arg(t.theme);
            return text;
        };
        stage.diffs << FileDiff{QStringLiteral("~/.config/ksplashrc"), ksplashrc(*plan.before.plasmaSplash), ksplashrc(plasma)};
        plan.target.plasmaSplash = plasma;
        plan.stages.prepend(stage); // applied first: no root, and easy to undo
    }

    if (request.grub || request.plymouth) {
        plan.commands << QStringLiteral("update-grub");
        plan.request = request;
        qint64 bytes = 0;
        for (const PayloadFile &f : std::as_const(plan.payload))
            bytes += f.data.size();
        const qint64 needed = bytes + (request.plymouth ? 512LL << 20 : 16LL << 20);
        if (QStorageInfo(root + QStringLiteral("/boot")).bytesAvailable() < needed)
            plan.problems << QStringLiteral("/boot needs %1 MB free.").arg(needed >> 20);
        plan.estimatedSeconds += 5;
    }
    return plan;
}

Target originalTarget(const Library &library)
{
    QJsonObject json;
    for (Stage stage : allStages) {
        if (const auto original = library.original(stage))
            json.insert(stageKey(stage), *original);
    }
    return Target::fromJson(json);
}

Target lastApplied(const Library &library)
{
    QStringList errors;
    QJsonObject merged;
    for (const HistoryEntry &e : library.history(errors)) { // newest first
        const QJsonObject target = e.state.value(QStringLiteral("target")).toObject();
        for (auto it = target.constBegin(); it != target.constEnd(); ++it) {
            if (!merged.contains(it.key()))
                merged.insert(it.key(), it.value());
        }
    }
    return Target::fromJson(merged);
}

BootMenuCheck checkBootMenu(const Library &library, const SystemState &state)
{
    QStringList errors;
    for (const HistoryEntry &e : library.history(errors)) { // newest first: the last apply of the boot menu
        const Target target = Target::fromJson(e.state.value(QStringLiteral("target")).toObject());
        if (!target.bootMenu)
            continue;
        BootMenuCheck check;
        if (target.bootMenu->original || target.bootMenu->design.isEmpty())
            return check;
        static const QRegularExpression mode(QStringLiteral("^(\\d+)x(\\d+)"));
        const auto m = mode.match(target.bootMenu->gfxmode);
        check.design = target.bootMenu->design;
        check.builtFor = m.hasMatch() ? QSize(m.captured(1).toInt(), m.captured(2).toInt()) : QSize();
        check.screenChanged = !state.primaryMode.isEmpty() && state.primaryMode != check.builtFor;
        check.fellBack = state.bootedAt.isValid() && e.createdAt < state.bootedAt && state.bootMode != check.builtFor;
        for (const QJsonValue &v : e.state.value(QStringLiteral("clippedEntries")).toArray())
            check.clipped << v.toString();
        return check;
    }
    return {};
}

QList<Stage> driftedStages(const Library &library, const Target &current)
{
    const Target last = lastApplied(library);
    QList<Stage> drifted;
    if (last.bootMenu && current.bootMenu && !last.bootMenu->sameState(*current.bootMenu))
        drifted << Stage::BootMenu;
    if (last.bootSplash && current.bootSplash && !last.bootSplash->sameState(*current.bootSplash))
        drifted << Stage::BootSplash;
    if (last.plasmaSplash && current.plasmaSplash && !last.plasmaSplash->sameState(*current.plasmaSplash))
        drifted << Stage::PlasmaSplash;
    return drifted;
}

QStringList keptThemes(const Library &library, const std::optional<Target> &newTarget)
{
    QStringList errors;
    const QList<HistoryEntry> history = library.history(errors); // newest first
    QList<Target> targets;
    if (newTarget)
        targets << *newTarget;
    // A new entry pushes the oldest out once history is full.
    for (qsizetype i = 0; i < history.size() && i < library.settings().historySize - (newTarget ? 1 : 0); ++i)
        targets << Target::fromJson(history[i].state.value(QStringLiteral("target")).toObject());
    QStringList kept;
    for (const Target &t : targets) {
        const QStringList ids = {t.bootMenu ? t.bootMenu->theme : QString(), t.bootSplash ? t.bootSplash->theme : QString(),
                                 t.bootSplash ? t.bootSplash->daemonTheme : QString()};
        for (const QString &id : ids) {
            if (id.startsWith(QLatin1String("ekpo-")) && !kept.contains(id))
                kept << id;
        }
    }
    return kept;
}

QStringList oldSystemCopies(const Library &library, const QString &rootDir, const Target &current)
{
    QStringList kept = keptThemes(library, std::nullopt);
    kept << (current.bootMenu ? current.bootMenu->theme : QString()) << (current.bootSplash ? current.bootSplash->theme : QString())
         << (current.bootSplash ? current.bootSplash->daemonTheme : QString());
    QStringList old;
    for (const QString &id : installedEkpoCopies(rootDir)) {
        if (!kept.contains(id) && !old.contains(id))
            old << id;
    }
    return old;
}

QStringList installedEkpoCopies(const QString &rootDir)
{
    const QString root = rootDir == QLatin1String("/") ? QString() : rootDir;
    QStringList copies;
    for (const QString &dir : {grubThemesDir, plymouthThemesDir})
        copies += QDir(root + dir).entryList({QStringLiteral("ekpo-*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    return copies;
}

bool isAtOriginal(const PlanContext &context)
{
    return !context.state.ekpoGrubDropIn && planTarget(context, originalTarget(context.library), QString()).stages.isEmpty();
}

bool looksLikeFreshSystem(const PlanContext &context)
{
    const Target last = lastApplied(context.library);
    const bool ekpoSetBootLook = (last.bootMenu && !last.bootMenu->original) || last.bootSplash;
    if (!ekpoSetBootLook || context.state.ekpoGrubDropIn || !installedEkpoCopies(context.rootDir).isEmpty())
        return false;
    const QList<Stage> drifted = driftedStages(context.library, currentTarget(context.state, context.rootDir));
    return drifted.contains(Stage::BootMenu) || drifted.contains(Stage::BootSplash);
}

QMap<Stage, Slot> slotsFromTarget(const Library &library, const Target &target)
{
    QStringList errors;
    const QList<ImportedTheme> imports = library.imports(errors);
    const auto slotFor = [&](Stage stage, const QString &design, const QString &theme, const QString &label) -> std::optional<Slot> {
        QString error;
        if (!design.isEmpty() && library.design(design, 0, error))
            return Slot{Slot::Kind::Design, design};
        for (const ImportedTheme &t : imports) {
            if (t.stage == stage && ((!t.installedId.isEmpty() && t.installedId == theme) || t.name == label))
                return Slot{Slot::Kind::Import, t.id};
        }
        return std::nullopt;
    };
    QMap<Stage, Slot> result;
    if (target.bootMenu && !target.bootMenu->original)
        if (const auto slot = slotFor(Stage::BootMenu, target.bootMenu->design, target.bootMenu->theme, target.bootMenu->label))
            result.insert(Stage::BootMenu, *slot);
    if (target.bootSplash)
        if (const auto slot = slotFor(Stage::BootSplash, target.bootSplash->design, target.bootSplash->theme, target.bootSplash->label))
            result.insert(Stage::BootSplash, *slot);
    if (target.plasmaSplash)
        if (const auto slot = slotFor(Stage::PlasmaSplash, target.plasmaSplash->design, target.plasmaSplash->theme, target.plasmaSplash->label))
            result.insert(Stage::PlasmaSplash, *slot);
    return result;
}
