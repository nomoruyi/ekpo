#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSize>
#include <QString>
#include <QStringList>

#include <optional>

// Read-only view of everything that decides the boot look (ARCHITECTURE.md §4, probe).
// Nothing here needs root.
struct SystemState {
    struct Kernel {
        QString version;
        bool hasInitrd = false;
        QDateTime initrdModified;
    };
    struct AlternativeChoice {
        QString path;
        int priority = 0;
    };
    struct PlymouthTheme {
        QString id;
        QString name;
        QString module;
    };
    struct SplashPackage {
        QString id;
        QString name; // metadata.json KPlugin Name
        bool user = false; // in the user's home rather than /usr
    };

    QString initramfsGenerator; // "initramfs-tools", "dracut", or empty if none is installed
    QMap<QString, QString> grub; // effective GRUB_* values, as grub-mkconfig sees them
    bool ekpoGrubDropIn = false;
    QStringList grubThemes;

    QString plymouthAlternative; // selected default.plymouth
    QString plymouthAlternativeMode; // "auto" or "manual"
    QList<AlternativeChoice> plymouthChoices;
    QString plymouthDaemonTheme; // plymouthd.conf [Daemon] Theme=
    QDateTime plymouthSelectedAt; // last change of either input; initrds older than this show an older splash
    QList<PlymouthTheme> plymouthThemes;

    QString splashTheme; // ksplashrc [KSplash] Theme=
    QString splashEngine;
    QList<SplashPackage> splashPackages;

    QList<Kernel> kernels; // oldest first
    bool cmdlineSplash = false;

    // The screen boot menus are built for (SPEC.md §4.5): KScreen's primary output
    // (priority 1) and its current mode. Empty if KScreen can't be asked.
    QString primaryOutput;
    QSize primaryMode;
    // What GRUB handed the kernel on this boot (boot_params screen_info, kept with
    // gfxpayload=keep): its graphics mode, empty if it was text; EFI or BIOS.
    QSize bootMode;
    bool efi = false;
    QDateTime bootedAt; // /proc/stat btime
    // The titles GRUB's menu (and its Advanced options submenu) will show, as the
    // scripts in /etc/grub.d write them: for the entry width (SPEC.md §4.5).
    QStringList menuEntries;
    bool leftoverJournal = false; // an apply was cut off

    QStringList errors; // what couldn't be read

    QJsonObject toJson() const;
};

// rootDir is "/" on the real system, a fixture root in tests. homeDir is the user's home.
// For a fixture root, KScreen's answer comes from <root>/kscreen-doctor.json.
SystemState probeSystem(const QString &rootDir, const QString &homeDir);

// `kscreen-doctor -j`: the enabled output with the lowest priority (1 = primary), and
// the size of its current mode.
std::optional<std::pair<QString, QSize>> primaryScreen(const QByteArray &kscreenJson);
// /sys/kernel/boot_params/data: the linear framebuffer the boot loader set up, if any.
QSize bootFramebuffer(const QByteArray &bootParams, bool &efi);
