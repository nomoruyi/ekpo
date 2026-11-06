#include "probe.h"

#include "sysinfo.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
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

    QString grubError;
    if (const auto grub = effectiveGrub(root, grubKeys, grubError))
        s.grub = *grub;
    else
        s.errors << grubError;
    s.ekpoGrubDropIn = QFileInfo::exists(root + QStringLiteral("/etc/default/grub.d/zz-ekpo.cfg"));
    for (const QString &id : subdirs(root + QStringLiteral("/boot/grub/themes"))) {
        if (QFileInfo::exists(root + QStringLiteral("/boot/grub/themes/") + id + QStringLiteral("/theme.txt")))
            s.grubThemes << id;
    }

    if (const auto alt = readAlternative(root, QStringLiteral("default.plymouth"))) {
        s.plymouthAlternativeMode = alt->mode;
        s.plymouthAlternative = alt->value;
        s.plymouthSelectedAt = alt->selectedAt;
        for (const auto &[path, priority] : alt->choices)
            s.plymouthChoices.append({path, priority});
    } else {
        s.errors << QStringLiteral("default.plymouth alternative not found");
    }
    const QString daemonConf = root + QStringLiteral("/etc/plymouth/plymouthd.conf");
    s.plymouthDaemonTheme = iniValue(daemonConf, QStringLiteral("Daemon"), QStringLiteral("Theme"));
    if (const QFileInfo conf(daemonConf); conf.exists())
        s.plymouthSelectedAt = std::max(s.plymouthSelectedAt, conf.lastModified(QTimeZone::UTC));
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
            if (QFileInfo::exists(dir + id + QStringLiteral("/contents/splash/Splash.qml"))) {
                QFile metadata(dir + id + QStringLiteral("/metadata.json"));
                const QString name = metadata.open(QIODevice::ReadOnly)
                    ? QJsonDocument::fromJson(metadata.readAll()).object().value(QStringLiteral("KPlugin")).toObject().value(QStringLiteral("Name")).toString()
                    : QString();
                s.splashPackages.append({id, name.isEmpty() ? id : name, user});
            }
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
    s.leftoverJournal = QFileInfo::exists(root + QStringLiteral("/var/lib/ekpo/txn"));
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
        splashes.append(QJsonObject{{QStringLiteral("id"), p.id}, {QStringLiteral("name"), p.name}, {QStringLiteral("user"), p.user}});
    QJsonArray kernelList;
    for (const Kernel &k : kernels) {
        kernelList.append(QJsonObject{{QStringLiteral("version"), k.version},
                                      {QStringLiteral("initrdModified"), k.hasInitrd ? k.initrdModified.toString(Qt::ISODate) : QString()}});
    }

    return QJsonObject{
        {QStringLiteral("initramfsGenerator"), initramfsGenerator},
        {QStringLiteral("grub"), QJsonObject{
                                     {QStringLiteral("values"), grubValues},
                                     {QStringLiteral("ekpoDropIn"), ekpoGrubDropIn},
                                     {QStringLiteral("themes"), QJsonArray::fromStringList(grubThemes)},
                                 }},
        {QStringLiteral("plymouth"), QJsonObject{
                                         {QStringLiteral("alternative"), plymouthAlternative},
                                         {QStringLiteral("alternativeMode"), plymouthAlternativeMode},
                                         {QStringLiteral("choices"), choices},
                                         {QStringLiteral("daemonTheme"), plymouthDaemonTheme},
                                         {QStringLiteral("selectedAt"), plymouthSelectedAt.toString(Qt::ISODate)},
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
