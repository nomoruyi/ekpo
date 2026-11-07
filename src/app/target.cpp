#include "target.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

// Which design a stage was built from, when it was.
QJsonObject withDesign(QJsonObject o, const QString &design, const QString &stamp)
{
    if (!design.isEmpty()) {
        o.insert(QStringLiteral("design"), design);
        o.insert(QStringLiteral("stamp"), stamp);
    }
    return o;
}

QJsonObject bootMenuJson(const BootMenuTarget &t)
{
    if (t.original)
        return {{QStringLiteral("original"), true}, {QStringLiteral("label"), t.label}};
    QJsonObject o{{QStringLiteral("theme"), t.theme}, {QStringLiteral("gfxmode"), t.gfxmode}, {QStringLiteral("timeoutStyle"), t.timeoutStyle},
                  {QStringLiteral("build"), t.build}, {QStringLiteral("label"), t.label}};
    if (t.timeout)
        o.insert(QStringLiteral("timeout"), *t.timeout);
    return withDesign(o, t.design, t.stamp);
}

QJsonObject bootSplashJson(const BootSplashTarget &t)
{
    return withDesign({{QStringLiteral("mode"), t.mode}, {QStringLiteral("theme"), t.theme}, {QStringLiteral("daemonTheme"), t.daemonTheme},
                       {QStringLiteral("build"), t.build}, {QStringLiteral("label"), t.label},
                       {QStringLiteral("initrds"), t.allKernels ? QStringLiteral("all") : QStringLiteral("newest")}},
                      t.design, t.stamp);
}

QJsonObject plasmaSplashJson(const PlasmaSplashTarget &t)
{
    return withDesign({{QStringLiteral("theme"), t.theme}, {QStringLiteral("engine"), t.engine}, {QStringLiteral("build"), t.build}, {QStringLiteral("label"), t.label}},
                      t.design, t.stamp);
}

QString str(const QJsonObject &o, const char *key)
{
    return o.value(QLatin1String(key)).toString();
}

} // namespace

QJsonObject Target::toJson() const
{
    QJsonObject o;
    if (bootMenu)
        o.insert(stageKey(Stage::BootMenu), bootMenuJson(*bootMenu));
    if (bootSplash)
        o.insert(stageKey(Stage::BootSplash), bootSplashJson(*bootSplash));
    if (plasmaSplash)
        o.insert(stageKey(Stage::PlasmaSplash), plasmaSplashJson(*plasmaSplash));
    return o;
}

QJsonObject Target::stageJson(Stage stage) const
{
    return toJson().value(stageKey(stage)).toObject();
}

Target Target::fromJson(const QJsonObject &json)
{
    Target t;
    if (const QJsonValue v = json.value(stageKey(Stage::BootMenu)); v.isObject()) {
        const QJsonObject o = v.toObject();
        BootMenuTarget m;
        m.original = o.value(QStringLiteral("original")).toBool();
        m.theme = str(o, "theme");
        m.gfxmode = str(o, "gfxmode");
        if (o.contains(QStringLiteral("timeout")))
            m.timeout = o.value(QStringLiteral("timeout")).toInt();
        m.timeoutStyle = str(o, "timeoutStyle");
        m.build = str(o, "build");
        m.label = str(o, "label");
        m.design = str(o, "design");
        m.stamp = str(o, "stamp");
        t.bootMenu = m;
    }
    if (const QJsonValue v = json.value(stageKey(Stage::BootSplash)); v.isObject()) {
        const QJsonObject o = v.toObject();
        t.bootSplash = BootSplashTarget{str(o, "mode"), str(o, "theme"), str(o, "daemonTheme"), str(o, "build"), str(o, "label"),
                                        str(o, "initrds") != QLatin1String("newest"), str(o, "design"), str(o, "stamp")};
    }
    if (const QJsonValue v = json.value(stageKey(Stage::PlasmaSplash)); v.isObject()) {
        const QJsonObject o = v.toObject();
        t.plasmaSplash = PlasmaSplashTarget{str(o, "theme"), str(o, "engine"), str(o, "build"), str(o, "label"), str(o, "design"), str(o, "stamp")};
    }
    return t;
}

Target currentTarget(const SystemState &state, const QString &rootDir)
{
    const QString root = rootDir == QLatin1String("/") ? QString() : rootDir;
    Target t;

    // GRUB: Ekpo's drop-in, if any, holds everything Ekpo set.
    BootMenuTarget menu;
    menu.original = !state.ekpoGrubDropIn;
    if (state.ekpoGrubDropIn) {
        QFile dropIn(root + QStringLiteral("/etc/default/grub.d/zz-ekpo.cfg"));
        const QString text = dropIn.open(QIODevice::ReadOnly) ? QString::fromUtf8(dropIn.readAll()) : QString();
        static const QRegularExpression line(QStringLiteral("^(GRUB_[A-Z_]+)=\"?([^\"\\n]*)\"?$"), QRegularExpression::MultilineOption);
        for (auto it = line.globalMatch(text); it.hasNext();) {
            const auto m = it.next();
            if (m.captured(1) == QLatin1String("GRUB_THEME"))
                menu.theme = QFileInfo(m.captured(2)).dir().dirName();
            else if (m.captured(1) == QLatin1String("GRUB_GFXMODE"))
                menu.gfxmode = m.captured(2);
            else if (m.captured(1) == QLatin1String("GRUB_RECORDFAIL_TIMEOUT"))
                menu.timeout = m.captured(2).toInt();
            else if (m.captured(1) == QLatin1String("GRUB_TIMEOUT_STYLE"))
                menu.timeoutStyle = m.captured(2);
        }
    }
    const QString themePath = state.grub.value(QStringLiteral("GRUB_THEME"));
    menu.label = themePath.isEmpty() ? QStringLiteral("text menu") : QFileInfo(themePath).dir().dirName();
    t.bootMenu = menu;

    // Plymouth: the alternative and plymouthd.conf.
    BootSplashTarget splash;
    const QString selected = QFileInfo(state.plymouthAlternative).dir().dirName();
    splash.mode = state.plymouthAlternativeMode == QLatin1String("auto") ? QStringLiteral("auto") : QStringLiteral("manual");
    splash.theme = splash.mode == QLatin1String("manual") ? selected : QString();
    splash.daemonTheme = state.plymouthDaemonTheme.isEmpty() ? selected : state.plymouthDaemonTheme;
    splash.label = selected;
    for (const SystemState::PlymouthTheme &theme : state.plymouthThemes) {
        if (theme.id == selected)
            splash.label = theme.name;
    }
    t.bootSplash = splash;

    PlasmaSplashTarget plasma{state.splashTheme, state.splashEngine, {}, state.splashTheme, {}, {}};
    for (const SystemState::SplashPackage &p : state.splashPackages) {
        if (p.id == state.splashTheme)
            plasma.label = p.name;
    }
    t.plasmaSplash = plasma;
    return t;
}
