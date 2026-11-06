#include "sysinfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QVersionNumber>

#include <algorithm>

#include <sys/stat.h>

namespace {

QString readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

} // namespace

std::optional<QMap<QString, QString>> effectiveGrub(const QString &root, const QStringList &keys, QString &error)
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
    bash.start(QStringLiteral("/usr/bin/bash"), QStringList{QStringLiteral("-c"), script, QStringLiteral("bash"), root} + keys);
    if (!bash.waitForFinished(5000) || bash.exitStatus() != QProcess::NormalExit || bash.exitCode() != 0) {
        error = QStringLiteral("GRUB settings: %1").arg(QString::fromUtf8(bash.readAllStandardError()).trimmed());
        return std::nullopt;
    }

    QMap<QString, QString> values;
    for (const QByteArray &pair : bash.readAllStandardOutput().split('\0')) {
        const qsizetype eq = pair.indexOf('=');
        if (eq > 0)
            values.insert(QString::fromUtf8(pair.left(eq)), QString::fromUtf8(pair.mid(eq + 1)));
    }
    return values;
}

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

QString packageVersion(const QString &root, const QString &package)
{
    const QStringList stanzas = readFile(root + QStringLiteral("/var/lib/dpkg/status")).split(QStringLiteral("\n\n"));
    for (const QString &stanza : stanzas) {
        if (!stanza.startsWith(QStringLiteral("Package: ") + package + QLatin1Char('\n')) || !stanza.contains(QLatin1String("\nStatus: install ok installed")))
            continue;
        for (const QString &line : stanza.split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1String("Version: ")))
                return line.mid(9).trimmed();
        }
    }
    return {};
}

// Format of /var/lib/dpkg/alternatives/<name>: mode, link, slave name/link pairs, a
// blank line, then per choice its path, priority and one line per slave.
std::optional<Alternative> readAlternative(const QString &root, const QString &name)
{
    const QStringList lines = readFile(root + QStringLiteral("/var/lib/dpkg/alternatives/") + name).split(QLatin1Char('\n'));
    if (lines.size() < 3)
        return std::nullopt;

    Alternative a;
    a.mode = lines[0];
    qsizetype i = 2;
    int slaves = 0;
    for (; i < lines.size() && !lines[i].isEmpty(); i += 2)
        ++slaves;
    for (++i; i + 1 < lines.size() && !lines[i].isEmpty(); i += 2 + slaves)
        a.choices.append({lines[i], lines[i + 1].toInt()});

    const QFileInfo link(root + QStringLiteral("/etc/alternatives/") + name);
    a.value = link.readSymLink();
    // lastModified() follows the link; lstat gives the time the selection changed.
    struct stat st;
    if (::lstat(QFile::encodeName(link.filePath()).constData(), &st) == 0)
        a.selectedAt = QDateTime::fromSecsSinceEpoch(st.st_mtim.tv_sec, QTimeZone::UTC);
    return a;
}

QStringList installedKernels(const QString &root)
{
    QStringList versions;
    for (const QString &name : QDir(root + QStringLiteral("/boot")).entryList({QStringLiteral("vmlinuz-*")}, QDir::Files | QDir::NoSymLinks))
        versions << name.mid(8);
    // 7.0.0-42-generic sorts as 7.0.0.42, so -42 comes after -9.
    const auto key = [](QString v) { return QVersionNumber::fromString(v.replace(QLatin1Char('-'), QLatin1Char('.'))); };
    std::sort(versions.begin(), versions.end(), [&](const QString &a, const QString &b) { return key(a) < key(b); });
    return versions;
}
