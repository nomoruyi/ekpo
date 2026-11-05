#include "probe.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QVersionNumber>

#include <algorithm>

namespace {

// GRUB keys that decide the menu's look and reachability.
const QStringList grubKeys = {
    QStringLiteral("GRUB_THEME"),
    QStringLiteral("GRUB_BACKGROUND"),
    QStringLiteral("GRUB_GFXMODE"),
    QStringLiteral("GRUB_GFXPAYLOAD_LINUX"),
    QStringLiteral("GRUB_TERMINAL"),
    QStringLiteral("GRUB_TERMINAL_OUTPUT"),
    QStringLiteral("GRUB_TIMEOUT"),
    QStringLiteral("GRUB_TIMEOUT_STYLE"),
    QStringLiteral("GRUB_RECORDFAIL_TIMEOUT"),
    QStringLiteral("GRUB_DISTRIBUTOR"),
    QStringLiteral("GRUB_DISABLE_OS_PROBER"),
};

QString readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QString iniValue(const QString &path, const QString &group, const QString &key)
{
    return KConfig(path, KConfig::SimpleConfig).group(group).readEntry(key);
}

QStringList subdirs(const QString &path)
{
    return QDir(path).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

// Sources /etc/default/grub and grub.d/*.cfg the way grub-mkconfig does, so
// shell syntax and drop-in overrides resolve exactly as at update-grub time.
QMap<QString, QString> effectiveGrub(const QString &root, QStringList &errors)
{
    static const QString script = QStringLiteral(
        "set -a\n"
        ". \"$1/etc/default/grub\" || exit 3\n"
        "for f in \"$1\"/etc/default/grub.d/*.cfg; do [ -e \"$f\" ] && . \"$f\"; done\n"
        "shift\n"
        "for k; do [ -n \"${!k+x}\" ] && printf '%s=%s\\0' \"$k\" \"${!k}\"; done\n"
        "exit 0\n");

    QProcess bash;
    QProcessEnvironment env; // clean: no GRUB_* leaking in from the caller
    env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/bin:/bin"));
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    bash.setProcessEnvironment(env);
    bash.start(QStringLiteral("/usr/bin/bash"),
               QStringList{QStringLiteral("-c"), script, QStringLiteral("bash"), root} + grubKeys);
    if (!bash.waitForFinished(5000) || bash.exitStatus() != QProcess::NormalExit || bash.exitCode() != 0) {
        errors << QStringLiteral("GRUB settings: %1").arg(QString::fromUtf8(bash.readAllStandardError()).trimmed());
        return {};
    }

    QMap<QString, QString> values;
    for (const QByteArray &pair : bash.readAllStandardOutput().split('\0')) {
        const qsizetype eq = pair.indexOf('=');
        if (eq > 0)
            values.insert(QString::fromUtf8(pair.left(eq)), QString::fromUtf8(pair.mid(eq + 1)));
    }
    return values;
}

// The provider of linux-initramfs-tool decides which generator builds the initrds.
QString initramfsGenerator(const QString &root)
{
    const QStringList stanzas = readFile(root + QStringLiteral("/var/lib/dpkg/status")).split(QStringLiteral("\n\n"));
    for (const QString &stanza : stanzas) {
        QString package;
        bool installed = false;
        bool provides = false;
        for (const QString &line : stanza.split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1String("Package: ")))
                package = line.mid(9).trimmed();
            else if (line.startsWith(QLatin1String("Status: ")))
                installed = line.endsWith(QLatin1String(" installed"));
            else if (line.startsWith(QLatin1String("Provides: ")))
                provides = line.contains(QLatin1String("linux-initramfs-tool"));
        }
        if (installed && provides)
            return package == QLatin1String("initramfs-tools") ? package : QStringLiteral("dracut");
    }
    return {};
}

// Format of /var/lib/dpkg/alternatives/<name>: mode, link, slave name/link pairs, a
// blank line, then per choice its path, priority and one line per slave.
void readPlymouthAlternative(const QString &root, SystemState &s)
{
    const QStringList lines = readFile(root + QStringLiteral("/var/lib/dpkg/alternatives/default.plymouth")).split(QLatin1Char('\n'));
    if (lines.size() < 3) {
        s.errors << QStringLiteral("default.plymouth alternative not found");
        return;
    }
    s.plymouthAlternativeMode = lines[0];
    qsizetype i = 2;
    int slaves = 0;
    for (; i < lines.size() && !lines[i].isEmpty(); i += 2)
        ++slaves;
    for (++i; i + 1 < lines.size() && !lines[i].isEmpty(); i += 2 + slaves)
        s.plymouthChoices.append({lines[i], lines[i + 1].toInt()});

    s.plymouthAlternative = QFileInfo(root + QStringLiteral("/etc/alternatives/default.plymouth")).readSymLink();
}

QVersionNumber kernelVersion(QString version)
{
    return QVersionNumber::fromString(version.replace(QLatin1Char('-'), QLatin1Char('.')));
}

} // namespace

SystemState probeSystem(const QString &rootDir, const QString &homeDir)
{
    const QString root = rootDir == QLatin1String("/") ? QString() : rootDir;
    SystemState s;

    s.initramfsGenerator = initramfsGenerator(root);

    s.grub = effectiveGrub(root, s.errors);
    s.personaeGrubDropIn = QFileInfo::exists(root + QStringLiteral("/etc/default/grub.d/zz-personae.cfg"));
    for (const QString &id : subdirs(root + QStringLiteral("/boot/grub/themes"))) {
        if (QFileInfo::exists(root + QStringLiteral("/boot/grub/themes/") + id + QStringLiteral("/theme.txt")))
            s.grubThemes << id;
    }

    readPlymouthAlternative(root, s);
    s.plymouthDaemonTheme = iniValue(root + QStringLiteral("/etc/plymouth/plymouthd.conf"), QStringLiteral("Daemon"), QStringLiteral("Theme"));
    const QString plymouthDir = root + QStringLiteral("/usr/share/plymouth/themes/");
    for (const QString &id : subdirs(plymouthDir)) {
        const QString file = plymouthDir + id + QLatin1Char('/') + id + QStringLiteral(".plymouth");
        if (QFileInfo::exists(file)) {
            s.plymouthThemes.append({id,
                                     iniValue(file, QStringLiteral("Plymouth Theme"), QStringLiteral("Name")),
                                     iniValue(file, QStringLiteral("Plymouth Theme"), QStringLiteral("ModuleName"))});
        }
    }

    const KConfigGroup ksplash = KConfig(homeDir + QStringLiteral("/.config/ksplashrc"), KConfig::SimpleConfig).group(QStringLiteral("KSplash"));
    s.splashTheme = ksplash.readEntry("Theme");
    s.splashEngine = ksplash.readEntry("Engine");
    const QList<std::pair<QString, bool>> lnfDirs = {
        {homeDir + QStringLiteral("/.local/share/plasma/look-and-feel/"), true},
        {root + QStringLiteral("/usr/share/plasma/look-and-feel/"), false},
    };
    for (const auto &[dir, user] : lnfDirs) {
        for (const QString &id : subdirs(dir)) {
            if (QFileInfo::exists(dir + id + QStringLiteral("/contents/splash/Splash.qml")))
                s.splashPackages.append({id, user});
        }
    }

    const QDir boot(root + QStringLiteral("/boot"));
    for (const QString &name : boot.entryList({QStringLiteral("vmlinuz-*")}, QDir::Files | QDir::NoSymLinks)) {
        SystemState::Kernel k;
        k.version = name.mid(8);
        const QFileInfo initrd(boot.filePath(QStringLiteral("initrd.img-") + k.version));
        k.hasInitrd = initrd.exists();
        if (k.hasInitrd)
            k.initrdModified = initrd.lastModified(QTimeZone::UTC);
        s.kernels.append(k);
    }
    std::sort(s.kernels.begin(), s.kernels.end(), [](const auto &a, const auto &b) {
        return kernelVersion(a.version) < kernelVersion(b.version);
    });

    s.cmdlineSplash = readFile(root + QStringLiteral("/proc/cmdline")).split(QLatin1Char(' '), Qt::SkipEmptyParts).contains(QLatin1String("splash"));
    s.leftoverJournal = QFileInfo::exists(root + QStringLiteral("/var/lib/personae/txn"));
    return s;
}

QJsonObject SystemState::toJson() const
{
    QJsonObject grubValues;
    for (auto it = grub.cbegin(); it != grub.cend(); ++it)
        grubValues.insert(it.key(), it.value());

    QJsonArray choices;
    for (const AlternativeChoice &c : plymouthChoices)
        choices.append(QJsonObject{{QStringLiteral("path"), c.path}, {QStringLiteral("priority"), c.priority}});
    QJsonArray themes;
    for (const PlymouthTheme &t : plymouthThemes)
        themes.append(QJsonObject{{QStringLiteral("id"), t.id}, {QStringLiteral("name"), t.name}, {QStringLiteral("module"), t.module}});
    QJsonArray splashes;
    for (const SplashPackage &p : splashPackages)
        splashes.append(QJsonObject{{QStringLiteral("id"), p.id}, {QStringLiteral("user"), p.user}});
    QJsonArray kernelList;
    for (const Kernel &k : kernels) {
        kernelList.append(QJsonObject{{QStringLiteral("version"), k.version},
                                      {QStringLiteral("initrdModified"), k.hasInitrd ? k.initrdModified.toString(Qt::ISODate) : QString()}});
    }

    return QJsonObject{
        {QStringLiteral("initramfsGenerator"), initramfsGenerator},
        {QStringLiteral("grub"), QJsonObject{
                                     {QStringLiteral("values"), grubValues},
                                     {QStringLiteral("personaeDropIn"), personaeGrubDropIn},
                                     {QStringLiteral("themes"), QJsonArray::fromStringList(grubThemes)},
                                 }},
        {QStringLiteral("plymouth"), QJsonObject{
                                         {QStringLiteral("alternative"), plymouthAlternative},
                                         {QStringLiteral("alternativeMode"), plymouthAlternativeMode},
                                         {QStringLiteral("choices"), choices},
                                         {QStringLiteral("daemonTheme"), plymouthDaemonTheme},
                                         {QStringLiteral("themes"), themes},
                                     }},
        {QStringLiteral("plasmaSplash"), QJsonObject{
                                             {QStringLiteral("theme"), splashTheme},
                                             {QStringLiteral("engine"), splashEngine},
                                             {QStringLiteral("packages"), splashes},
                                         }},
        {QStringLiteral("kernels"), kernelList},
        {QStringLiteral("cmdlineSplash"), cmdlineSplash},
        {QStringLiteral("leftoverJournal"), leftoverJournal},
        {QStringLiteral("errors"), QJsonArray::fromStringList(errors)},
    };
}
