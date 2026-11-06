#include "backend.h"

#include "importer.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

QVariantMap line(const QString &level, const QString &text) // level: ok, info, warning
{
    return {{QStringLiteral("level"), level}, {QStringLiteral("text"), text}};
}

QVariantMap card(Stage stage, const QString &current, const QVariantList &lines)
{
    return {{QStringLiteral("stage"), stageKey(stage)}, {QStringLiteral("current"), current}, {QStringLiteral("lines"), lines}};
}

QVariantMap bootMenuCard(const SystemState &s, const QString &root)
{
    QVariantList lines;
    const QString themePath = s.grub.value(QStringLiteral("GRUB_THEME"));
    QString current = Backend::tr("No theme");
    if (themePath.isEmpty()) {
        lines << line(QStringLiteral("info"), Backend::tr("GRUB shows its plain text menu."));
    } else {
        static const QRegularExpression inThemesDir(QStringLiteral("^/boot/grub/themes/([^/]+)/theme\\.txt$"));
        const auto match = inThemesDir.match(themePath);
        current = match.hasMatch() ? match.captured(1) : themePath;
        if (!QFileInfo::exists(root + themePath))
            lines << line(QStringLiteral("warning"), Backend::tr("The theme file is missing; GRUB falls back to its text menu."));
    }
    lines << line(QStringLiteral("info"), s.ekpoGrubDropIn ? Backend::tr("Set by Ekpo.") : Backend::tr("Set in /etc/default/grub; Ekpo hasn't changed GRUB yet."));
    if (const QString mode = s.grub.value(QStringLiteral("GRUB_GFXMODE")); !mode.isEmpty())
        lines << line(QStringLiteral("info"), Backend::tr("Video mode: %1").arg(mode));
    return card(Stage::BootMenu, current, lines);
}

QVariantMap bootSplashCard(const SystemState &s)
{
    QVariantList lines;
    const QString id = QFileInfo(s.plymouthAlternative).dir().dirName();
    QString current = id.isEmpty() ? Backend::tr("None selected") : id;
    for (const SystemState::PlymouthTheme &t : s.plymouthThemes) {
        if (t.id == id)
            current = t.name;
    }

    if (!s.cmdlineSplash)
        lines << line(QStringLiteral("warning"), Backend::tr("The kernel command line has no \"splash\", so no boot splash shows."));
    if (!s.plymouthDaemonTheme.isEmpty() && s.plymouthDaemonTheme != id)
        lines << line(QStringLiteral("warning"), Backend::tr("plymouthd.conf names \"%1\" but \"%2\" is selected; boot and shutdown may look different.").arg(s.plymouthDaemonTheme, id));

    qsizetype withInitrd = 0;
    qsizetype outdated = 0;
    for (const SystemState::Kernel &k : s.kernels) {
        if (!k.hasInitrd)
            continue;
        ++withInitrd;
        if (s.plymouthSelectedAt.isValid() && k.initrdModified < s.plymouthSelectedAt)
            ++outdated;
    }
    if (outdated > 0)
        lines << line(QStringLiteral("warning"), Backend::tr("%1 of %2 kernels still boot with an older splash (initrd built before the last change).").arg(outdated).arg(withInitrd));
    else if (withInitrd > 0)
        lines << line(QStringLiteral("ok"), Backend::tr("All %1 initrds carry this splash.").arg(withInitrd));

    lines << line(QStringLiteral("info"), s.plymouthAlternativeMode == QLatin1String("manual") ? Backend::tr("Selected manually.") : Backend::tr("Chosen automatically (highest priority)."));
    return card(Stage::BootSplash, current, lines);
}

QVariantMap plasmaSplashCard(const SystemState &s)
{
    QVariantList lines;
    if (s.splashEngine.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0)
        return card(Stage::PlasmaSplash, Backend::tr("Off"), {line(QStringLiteral("info"), Backend::tr("The Plasma splash is turned off."))});

    const QString id = s.splashTheme.isEmpty() ? QStringLiteral("org.kde.breeze.desktop") : s.splashTheme;
    QString current = id;
    bool found = false;
    for (const SystemState::SplashPackage &p : s.splashPackages) {
        if (p.id == id) {
            current = p.name;
            found = true;
            lines << line(QStringLiteral("info"), p.user ? Backend::tr("Installed in your home folder.") : Backend::tr("Installed with Plasma."));
        }
    }
    if (!found)
        lines << line(QStringLiteral("warning"), Backend::tr("The package \"%1\" isn't installed; Plasma shows no splash.").arg(id));
    return card(Stage::PlasmaSplash, current, lines);
}

QVariantMap importToMap(const ImportedTheme &t)
{
    return {
        {QStringLiteral("id"), t.id},
        {QStringLiteral("stage"), stageKey(t.stage)},
        {QStringLiteral("name"), t.name},
        {QStringLiteral("verdict"), verdictKey(t.verdict)},
        {QStringLiteral("installedId"), t.installedId},
        {QStringLiteral("sourcePath"), t.sourcePath},
        {QStringLiteral("report"), t.report},
    };
}

QVariantMap resultToMap(const ImportResult &result)
{
    QStringList names;
    for (const ImportedTheme &t : result.themes)
        names << t.name;
    return {{QStringLiteral("names"), names}, {QStringLiteral("errors"), result.errors}};
}

} // namespace

Backend::Backend(QObject *parent)
    : QObject(parent)
    , m_homeDir(QDir::homePath())
{
    // Sandbox mode for dev runs and screenshots (ARCHITECTURE.md §8): read a fake system root.
    if (qEnvironmentVariableIsSet("EKPO_DEV") && qEnvironmentVariableIsSet("EKPO_SYSTEM_ROOT")) {
        m_rootDir = qEnvironmentVariable("EKPO_SYSTEM_ROOT");
        m_homeDir = qEnvironmentVariable("EKPO_SYSTEM_HOME", m_homeDir);
    }
    m_library = Library::open(Library::defaultPath(), m_libraryError);

    // In the sandbox the test helper changes the fake root; otherwise pkexec runs the real one.
    m_launcher = systemHelperLauncher();
    if (m_rootDir != QLatin1String("/")) {
        m_launcher = HelperLauncher{QStringLiteral(EKPO_HELPER_TEST_PATH), {}};
        m_launcher.environment.insert(QStringLiteral("EKPO_HELPER_ROOT"), m_rootDir);
        m_launcher.environment.insert(QStringLiteral("EKPO_HELPER_BIN"), QStringLiteral(EKPO_STUBS_DIR));
    }
    if (m_library) {
        m_executor = std::make_unique<Executor>(*m_library, m_homeDir, m_launcher);
        connect(m_executor.get(), &Executor::progress, this, &Backend::applyProgress);
        connect(m_executor.get(), &Executor::finished, this, [this](bool ok, const QString &error, int number) {
            Q_EMIT applyingChanged();
            refresh();
            Q_EMIT applyFinished(ok, error, number);
        });
    }
    refresh();
}

void Backend::refresh()
{
    const QString root = m_rootDir == QLatin1String("/") ? QString() : m_rootDir;
    m_state = probeSystem(m_rootDir, m_homeDir);
    m_stageCards = {bootMenuCard(m_state, root), bootSplashCard(m_state), plasmaSplashCard(m_state)};

    QStringList errors = m_state.errors;
    if (!m_library)
        errors << m_libraryError;
    m_system = {
        {QStringLiteral("generator"), m_state.initramfsGenerator},
        {QStringLiteral("leftoverJournal"), m_state.leftoverJournal},
        {QStringLiteral("errors"), errors},
    };

    m_imports.clear();
    QList<ImportedTheme> imported;
    if (m_library) {
        QStringList loadErrors;
        imported = m_library->imports(loadErrors);
        for (const ImportedTheme &t : imported)
            m_imports << importToMap(t);
        m_system[QStringLiteral("errors")] = errors + loadErrors;
    }

    m_history.clear();
    if (m_library) {
        QStringList historyErrors;
        for (const HistoryEntry &e : m_library->history(historyErrors)) {
            const Target target = Target::fromJson(e.state.value(QStringLiteral("target")).toObject());
            QVariantList stages;
            if (target.plasmaSplash)
                stages << QVariantMap{{QStringLiteral("stage"), stageKey(Stage::PlasmaSplash)}, {QStringLiteral("label"), target.plasmaSplash->label}};
            if (target.bootMenu)
                stages << QVariantMap{{QStringLiteral("stage"), stageKey(Stage::BootMenu)}, {QStringLiteral("label"), target.bootMenu->original ? tr("Original") : target.bootMenu->label}};
            if (target.bootSplash)
                stages << QVariantMap{{QStringLiteral("stage"), stageKey(Stage::BootSplash)}, {QStringLiteral("label"), target.bootSplash->label}};
            m_history << QVariantMap{{QStringLiteral("number"), e.number},
                                     {QStringLiteral("title"), e.title},
                                     {QStringLiteral("createdAt"), e.createdAt.toLocalTime()},
                                     {QStringLiteral("stages"), stages},
                                     {QStringLiteral("restoreFile"), QStringLiteral("%1/history/%2/RESTORE.txt").arg(m_library->path()).arg(e.number)}};
        }
    }

    // Installed folders whose themes aren't in the library yet.
    m_installed.clear();
    for (const auto &[stage, folder] : installedThemeFolders(m_state, m_rootDir, m_homeDir)) {
        const bool done = std::any_of(imported.cbegin(), imported.cend(), [&](const ImportedTheme &t) {
            return t.sourcePath == folder || t.sourcePath.startsWith(folder + QLatin1Char('/'));
        });
        if (!done) {
            m_installed << QVariantMap{{QStringLiteral("stage"), stageKey(stage)},
                                       {QStringLiteral("name"), QDir(folder).dirName()},
                                       {QStringLiteral("path"), folder}};
        }
    }
    Q_EMIT changed();
}

QVariantMap Backend::importPath(const QUrl &path)
{
    if (!m_library)
        return {{QStringLiteral("names"), QStringList()}, {QStringLiteral("errors"), QStringList{m_libraryError}}};
    const QString local = path.isLocalFile() ? path.toLocalFile() : path.toString();
    const ImportResult result = importThemes(*m_library, local, m_rootDir, m_homeDir);
    refresh();
    return resultToMap(result);
}

QVariantMap Backend::reimport(const QString &id)
{
    for (const QVariant &v : std::as_const(m_imports)) {
        const QVariantMap t = v.toMap();
        if (t.value(QStringLiteral("id")) == id)
            return importPath(QUrl::fromLocalFile(t.value(QStringLiteral("sourcePath")).toString()));
    }
    return {{QStringLiteral("names"), QStringList()}, {QStringLiteral("errors"), QStringList{tr("Unknown import %1").arg(id)}}};
}

QString Backend::removeImport(const QString &id)
{
    QString error;
    if (!m_library || !m_library->removeImport(id, error))
        return error.isEmpty() ? m_libraryError : error;
    refresh();
    return {};
}

bool Backend::originalAvailable() const
{
    return m_library && !originalTarget(*m_library).isEmpty();
}

void Backend::setPlan(const Plan &plan)
{
    m_plan = plan;
    QVariantList stages;
    for (const PlannedStage &stage : plan.stages) {
        QVariantList diffs;
        for (const FileDiff &d : stage.diffs)
            diffs << QVariantMap{{QStringLiteral("path"), d.path}, {QStringLiteral("before"), d.before}, {QStringLiteral("after"), d.after}};
        stages << QVariantMap{{QStringLiteral("stage"), stageKey(stage.stage)},
                              {QStringLiteral("from"), stage.from},
                              {QStringLiteral("to"), stage.to},
                              {QStringLiteral("actions"), stage.actions},
                              {QStringLiteral("diffs"), diffs}};
    }
    m_planMap = {
        {QStringLiteral("title"), plan.title},
        {QStringLiteral("canApply"), plan.canApply()},
        {QStringLiteral("needsPassword"), bool(plan.request)},
        {QStringLiteral("stages"), stages},
        {QStringLiteral("commands"), plan.commands},
        {QStringLiteral("notes"), plan.notes},
        {QStringLiteral("problems"), plan.stages.isEmpty() && plan.problems.isEmpty() ? QStringList{tr("This is already the current look.")} : plan.problems},
        {QStringLiteral("estimatedSeconds"), plan.estimatedSeconds},
    };
    Q_EMIT planChanged();
}

void Backend::planApplyImport(const QString &id)
{
    if (!m_library)
        return;
    refresh();
    QStringList errors;
    for (const ImportedTheme &t : m_library->imports(errors)) {
        if (t.id == id)
            return setPlan(planMask(PlanContext{*m_library, m_state, m_rootDir, m_homeDir}, Mask{{}, t.name, {{t.stage, Slot{Slot::Kind::Import, t.id}}}}));
    }
}

void Backend::planRestoreEntry(int number)
{
    if (!m_library)
        return;
    refresh();
    QStringList errors;
    for (const HistoryEntry &e : m_library->history(errors)) {
        if (e.number == number) {
            const Target target = Target::fromJson(e.state.value(QStringLiteral("target")).toObject());
            return setPlan(planTarget(PlanContext{*m_library, m_state, m_rootDir, m_homeDir}, target, tr("Restore #%1: %2").arg(number).arg(e.title)));
        }
    }
}

void Backend::planRestoreOriginal()
{
    if (!m_library)
        return;
    refresh();
    setPlan(planTarget(PlanContext{*m_library, m_state, m_rootDir, m_homeDir}, originalTarget(*m_library), tr("Restore Original")));
}

void Backend::applyPlan()
{
    if (!m_executor || m_executor->isRunning())
        return;
    m_executor->apply(m_plan);
    Q_EMIT applyingChanged();
}
