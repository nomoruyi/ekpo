#include "probe.h"

#include "sysinfo.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTimeZone>
#include <QVersionNumber>
#include <QtEndian>

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
    QStringLiteral("GRUB_DISABLE_RECOVERY"),
    QStringLiteral("GRUB_DISABLE_SUBMENU"),
};


QString readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// The titles Ubuntu's GRUB scripts give their entries (10_linux, 30_os-prober,
// 30_uefi-firmware), newest kernel first. 20_memtest86+ needs an image in /boot,
// 40/41_custom are free text; the helper checks the real grub.cfg after update-grub.
QStringList expectedMenuEntries(const SystemState &s, const QString &root)
{
    const auto runs = [&](const QString &script) {
        const QFileInfo f(root + QStringLiteral("/etc/grub.d/") + script);
        return f.isFile() && f.isExecutable();
    };
    const auto yes = [&](const char *key) {
        const QString v = s.grub.value(QLatin1String(key));
        return v == QLatin1String("true") || v == QLatin1String("y");
    };
    QStringList entries;
    const QString os = s.grub.value(QStringLiteral("GRUB_DISTRIBUTOR")).isEmpty() ? QStringLiteral("GNU/Linux") : s.grub.value(QStringLiteral("GRUB_DISTRIBUTOR"));
    if (runs(QStringLiteral("10_linux")) && !s.kernels.isEmpty()) {
        entries << os;
        if (!yes("GRUB_DISABLE_SUBMENU"))
            entries << QStringLiteral("Advanced options for %1").arg(os);
        for (auto k = s.kernels.crbegin(); k != s.kernels.crend(); ++k) {
            entries << QStringLiteral("%1, with Linux %2").arg(os, k->version);
            if (!yes("GRUB_DISABLE_RECOVERY"))
                entries << QStringLiteral("%1, with Linux %2 (recovery mode)").arg(os, k->version);
        }
    }
    // os-prober finds Windows through its boot manager on the ESP.
    if (runs(QStringLiteral("30_os-prober")) && s.grub.value(QStringLiteral("GRUB_DISABLE_OS_PROBER")) == QLatin1String("false")
        && QFileInfo::exists(root + QStringLiteral("/boot/efi/EFI/Microsoft/Boot/bootmgfw.efi"))) {
        QString esp = QStringLiteral("/dev/nvme0n1p1"); // as long as a typical ESP's name
        for (const QString &line : readFile(root + QStringLiteral("/proc/mounts")).split(QLatin1Char('\n'))) {
            if (line.section(QLatin1Char(' '), 1, 1) == QLatin1String("/boot/efi"))
                esp = line.section(QLatin1Char(' '), 0, 0);
        }
        entries << QStringLiteral("Windows Boot Manager (on %1)").arg(esp);
    }
    if (runs(QStringLiteral("30_uefi-firmware")) && s.efi)
        entries << QStringLiteral("UEFI Firmware Settings");
    return entries;
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

    // The screen: KScreen on the real system, its saved answer in a fixture.
    QByteArray kscreen;
    if (root.isEmpty()) {
        QProcess doctor;
        // The command line runs offscreen; kscreen-doctor needs the session's own platform.
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("QT_QPA_PLATFORM"));
        doctor.setProcessEnvironment(env);
        doctor.start(QStringLiteral("kscreen-doctor"), {QStringLiteral("-j")});
        if (doctor.waitForFinished(5000) && doctor.exitStatus() == QProcess::NormalExit && doctor.exitCode() == 0)
            kscreen = doctor.readAllStandardOutput();
        else
            doctor.kill();
    } else {
        QFile saved(root + QStringLiteral("/kscreen-doctor.json"));
        if (saved.open(QIODevice::ReadOnly))
            kscreen = saved.readAll();
    }
    if (const auto screen = primaryScreen(kscreen)) {
        s.primaryOutput = screen->first;
        s.primaryMode = screen->second;
    }
    QFile bootParams(root + QStringLiteral("/sys/kernel/boot_params/data"));
    if (bootParams.open(QIODevice::ReadOnly))
        s.bootMode = bootFramebuffer(bootParams.read(64), s.efi);
    s.efi = s.efi || QFileInfo::exists(root + QStringLiteral("/sys/firmware/efi"));
    for (const QString &line : readFile(root + QStringLiteral("/proc/stat")).split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("btime ")))
            s.bootedAt = QDateTime::fromSecsSinceEpoch(line.mid(6).trimmed().toLongLong(), QTimeZone::UTC);
    }
    s.menuEntries = expectedMenuEntries(s, root);
    s.leftoverJournal = QFileInfo::exists(root + QStringLiteral("/var/lib/ekpo/txn"));
    return s;
}

std::optional<std::pair<QString, QSize>> primaryScreen(const QByteArray &kscreenJson)
{
    std::optional<std::pair<QString, QSize>> best;
    int bestPriority = 0;
    for (const QJsonValue &v : QJsonDocument::fromJson(kscreenJson).object().value(QStringLiteral("outputs")).toArray()) {
        const QJsonObject o = v.toObject();
        const int priority = o.value(QStringLiteral("priority")).toInt();
        if (!o.value(QStringLiteral("enabled")).toBool() || priority < 1 || (best && priority >= bestPriority))
            continue;
        for (const QJsonValue &m : o.value(QStringLiteral("modes")).toArray()) {
            const QJsonObject mode = m.toObject();
            if (mode.value(QStringLiteral("id")) != o.value(QStringLiteral("currentModeId")))
                continue;
            const QJsonObject size = mode.value(QStringLiteral("size")).toObject();
            const QSize pixels(size.value(QStringLiteral("width")).toInt(), size.value(QStringLiteral("height")).toInt());
            if (!pixels.isEmpty()) {
                best = std::pair{o.value(QStringLiteral("name")).toString(), pixels};
                bestPriority = priority;
            }
        }
    }
    return best;
}

QSize bootFramebuffer(const QByteArray &bootParams, bool &efi)
{
    // struct screen_info at the start of boot_params: orig_video_isVGA at 0x0f
    // (0x23 VESA, 0x70 EFI), lfb_width and lfb_height at 0x12 and 0x14, little-endian.
    efi = false;
    if (bootParams.size() < 0x16)
        return {};
    const quint8 type = quint8(bootParams[0x0f]);
    efi = type == 0x70;
    if (type != 0x70 && type != 0x23)
        return {};
    return {qFromLittleEndian<quint16>(bootParams.constData() + 0x12), qFromLittleEndian<quint16>(bootParams.constData() + 0x14)};
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
        {QStringLiteral("screen"), QJsonObject{
                                       {QStringLiteral("primaryOutput"), primaryOutput},
                                       {QStringLiteral("primaryMode"), QStringLiteral("%1x%2").arg(primaryMode.width()).arg(primaryMode.height())},
                                       {QStringLiteral("bootMode"), QStringLiteral("%1x%2").arg(bootMode.width()).arg(bootMode.height())},
                                       {QStringLiteral("efi"), efi},
                                       {QStringLiteral("bootedAt"), bootedAt.toString(Qt::ISODate)},
                                   }},
        {QStringLiteral("menuEntries"), QJsonArray::fromStringList(menuEntries)},
        {QStringLiteral("leftoverJournal"), leftoverJournal},
        {QStringLiteral("errors"), QJsonArray::fromStringList(errors)},
    };
}
