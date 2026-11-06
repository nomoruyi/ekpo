#pragma once

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

// Read-only views of system state that the app's probe and the root helper share.
// root is "" for the real system, or a fake root in tests.

// GRUB_* values as grub-mkconfig sees them: /etc/default/grub, then grub.d/*.cfg,
// sourced by bash with a clean environment. Only keys that are set are returned.
std::optional<QMap<QString, QString>> effectiveGrub(const QString &root, const QStringList &keys, QString &error);

// "initramfs-tools", "dracut", or empty: the installed provider of linux-initramfs-tool.
QString initramfsGenerator(const QString &root);
// The installed version of a dpkg package, or empty.
QString packageVersion(const QString &root, const QString &package);

// A dpkg alternative: /var/lib/dpkg/alternatives/<name> plus /etc/alternatives/<name>.
struct Alternative {
    QString mode; // "auto" or "manual"
    QString value; // the selected path (the link target)
    QList<std::pair<QString, int>> choices; // path, priority
    QDateTime selectedAt; // when the link last changed
};
std::optional<Alternative> readAlternative(const QString &root, const QString &name);

// Kernel versions with a /boot/vmlinuz-<version>, oldest first.
QStringList installedKernels(const QString &root);
