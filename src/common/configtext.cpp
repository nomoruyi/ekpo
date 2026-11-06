#include "configtext.h"

#include <QStringList>

QByteArray grubDropInText(const Request::Grub &grub)
{
    QStringList lines{QStringLiteral("# Written by Ekpo (io.github.nomoruyi.ekpo). Delete this file to return to /etc/default/grub.")};
    if (!grub.theme.isEmpty())
        lines << QStringLiteral("GRUB_THEME=\"/boot/grub/themes/%1/theme.txt\"").arg(grub.theme);
    if (!grub.gfxmode.isEmpty())
        lines << QStringLiteral("GRUB_GFXMODE=\"%1\"").arg(grub.gfxmode);
    if (grub.timeout) {
        // With /boot on btrfs only the recordfail timeout counts (CLAUDE.md); set both.
        lines << QStringLiteral("GRUB_TIMEOUT=%1").arg(*grub.timeout);
        lines << QStringLiteral("GRUB_RECORDFAIL_TIMEOUT=%1").arg(*grub.timeout);
    }
    if (!grub.timeoutStyle.isEmpty())
        lines << QStringLiteral("GRUB_TIMEOUT_STYLE=%1").arg(grub.timeoutStyle);
    return (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
}

QByteArray withDaemonTheme(const QByteArray &conf, const QString &theme)
{
    const QStringList lines = QString::fromUtf8(conf).split(QLatin1Char('\n'));
    QStringList out;
    QString section;
    bool written = false;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('['))) {
            if (section == QLatin1String("Daemon") && !written) {
                out << QStringLiteral("Theme=") + theme;
                written = true;
            }
            section = trimmed.mid(1).chopped(1).trimmed();
        } else if (section == QLatin1String("Daemon") && trimmed.startsWith(QLatin1String("Theme")) && trimmed.mid(5).trimmed().startsWith(QLatin1Char('='))) {
            if (!written)
                out << QStringLiteral("Theme=") + theme;
            written = true;
            continue;
        }
        out << line;
    }
    while (!out.isEmpty() && out.last().isEmpty())
        out.removeLast();
    if (!written) {
        if (section != QLatin1String("Daemon"))
            out << QStringLiteral("[Daemon]");
        out << QStringLiteral("Theme=") + theme;
    }
    return (out.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
}
