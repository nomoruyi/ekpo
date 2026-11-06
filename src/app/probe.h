#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

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
    bool personaeGrubDropIn = false;
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
    bool leftoverJournal = false; // an apply was cut off

    QStringList errors; // what couldn't be read

    QJsonObject toJson() const;
};

// rootDir is "/" on the real system, a fixture root in tests. homeDir is the user's home.
SystemState probeSystem(const QString &rootDir, const QString &homeDir);
