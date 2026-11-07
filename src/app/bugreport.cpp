#include "bugreport.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSysInfo>

#include <algorithm>

QStringList systemBasics(const QString &rootDir)
{
    QString osName;
    QFile osRelease(rootDir + QStringLiteral("/etc/os-release"));
    if (osRelease.open(QIODevice::ReadOnly)) {
        static const QRegularExpression pretty(QStringLiteral("^PRETTY_NAME=\"?([^\"\\n]*)"), QRegularExpression::MultilineOption);
        osName = pretty.match(QString::fromUtf8(osRelease.readAll())).captured(1);
    }
    return {QStringLiteral("Ekpo %1").arg(QCoreApplication::applicationVersion()),
            QStringLiteral("System: %1, kernel %2").arg(osName.isEmpty() ? QSysInfo::prettyProductName() : osName, QSysInfo::kernelVersion())};
}

QUrl issueUrl(const QString &title, const QString &body)
{
    // Every character outside A-Z a-z 0-9 - . _ ~ encoded: '+' would read as a space.
    const auto encode = [](const QString &s) { return QString::fromLatin1(QUrl::toPercentEncoding(s)); };
    return QUrl(QStringLiteral("https://github.com/nomoruyi/ekpo/issues/new?labels=bug&title=") + encode(title) + QStringLiteral("&body=")
                + encode(body));
}

QString fitIssueBody(const QString &title, const QString &body)
{
    const auto fits = [&title](const QString &b) { return issueUrl(title, b).toString(QUrl::FullyEncoded).size() <= maxIssueUrlLength; };
    if (fits(body))
        return body;
    const QString open = QStringLiteral("## Log\n```\n");
    const qsizetype start = body.indexOf(open);
    const qsizetype end = start < 0 ? -1 : body.indexOf(QStringLiteral("\n```"), start + open.size());
    if (end < 0)
        return body;
    QStringList lines = body.mid(start + open.size(), end - start - open.size()).split(QLatin1Char('\n'));
    // The part headers ("Ekpo:", "ekpo-helper:") and the blank line between them stay.
    const auto isHeader = [](const QString &line) {
        return line.isEmpty() || line == QLatin1String("Ekpo:") || line == QLatin1String("ekpo-helper:");
    };
    const auto rebuilt = [&](int dropped) {
        const QString note = QCoreApplication::translate("BugReport", "(%n older log lines left out to fit GitHub's link length)", "", dropped);
        return body.left(start + open.size()) + note + QLatin1Char('\n') + lines.join(QLatin1Char('\n')) + body.mid(end);
    };
    for (int dropped = 1;; ++dropped) {
        const qsizetype oldest = std::distance(lines.cbegin(), std::find_if_not(lines.cbegin(), lines.cend(), isHeader));
        if (oldest >= lines.size())
            return body; // nothing left to drop
        lines.removeAt(oldest);
        if (const QString shorter = rebuilt(dropped); fits(shorter))
            return shorter;
    }
}

QString openIssue(const QString &title, const QString &body)
{
    const QUrl url = issueUrl(title, body);
    const qsizetype length = url.toString(QUrl::FullyEncoded).size();
    if (length > maxIssueUrlLength)
        return QCoreApplication::translate("BugReport", "The report is too long for a GitHub link (%1 of %2 characters once encoded). Shorten it, for example the log.")
            .arg(length)
            .arg(maxIssueUrlLength);
    if (!QDesktopServices::openUrl(url))
        return QCoreApplication::translate("BugReport", "No browser could open the GitHub page.");
    return {};
}

QString localReportsDir()
{
#ifdef EKPO_BUG_REPORTS_DIR
    return QStringLiteral(EKPO_BUG_REPORTS_DIR);
#else
    return {};
#endif
}

QString saveReport(const QString &dir, const QString &title, const QString &body, const QImage &screenshot, QString &error)
{
    if (!QDir().mkpath(dir)) {
        error = QStringLiteral("Can't create %1").arg(dir);
        return {};
    }
    static const QRegularExpression nonWord(QStringLiteral("[^a-z0-9]+"));
    QString slug = title.toLower().replace(nonWord, QStringLiteral("-")).left(50);
    while (slug.startsWith(QLatin1Char('-')))
        slug.remove(0, 1);
    while (slug.endsWith(QLatin1Char('-')))
        slug.chop(1);
    // ponytail: two reports with the same title in the same second share a name; the second wins.
    const QString base = dir + QLatin1Char('/') + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HHmmss"))
        + (slug.isEmpty() ? QString() : QLatin1Char('-') + slug);
    QString text = QStringLiteral("# %1\n\n%2\n").arg(title, body.trimmed());
    if (!screenshot.isNull()) {
        if (!screenshot.save(base + QStringLiteral(".png"))) {
            error = QStringLiteral("Can't write %1.png").arg(base);
            return {};
        }
        text += QStringLiteral("\n## Screenshot\n\n![Screenshot](%1.png)\n").arg(QFileInfo(base).fileName());
    }
    QSaveFile file(base + QStringLiteral(".md"));
    if (!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0 || !file.commit()) {
        error = QStringLiteral("%1: %2").arg(file.fileName(), file.errorString());
        return {};
    }
    return file.fileName();
}

namespace
{
constexpr int keptMessages = 50;
QMutex messagesMutex;
QStringList messages;
QtMessageHandler previousHandler = nullptr;

void logMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    static const char *const names[] = {"debug", "warning", "critical", "fatal", "info"}; // QtMsgType order
    {
        QMutexLocker lock(&messagesMutex);
        messages << QTime::currentTime().toString(QStringLiteral("HH:mm:ss ")) + QLatin1String(names[type]) + QStringLiteral(": ") + message;
        if (messages.size() > keptMessages)
            messages.removeFirst();
    }
    previousHandler(type, context, message); // prints; QT_FATAL_WARNINGS still applies
}
} // namespace

void installMessageLog()
{
    previousHandler = qInstallMessageHandler(logMessage);
}

QStringList recentMessages()
{
    QMutexLocker lock(&messagesMutex);
    return messages;
}

QStringList helperLog(const QString &rootDir, int lines)
{
    QByteArray text;
    if (rootDir == QLatin1String("/")) {
        QProcess journal;
        journal.start(QStringLiteral("journalctl"), {QStringLiteral("-t"), QStringLiteral("ekpo-helper"), QStringLiteral("-n"), QString::number(lines),
                                                     QStringLiteral("-o"), QStringLiteral("short-iso"), QStringLiteral("--no-pager"), QStringLiteral("-q")});
        if (!journal.waitForFinished(3000)) {
            journal.kill();
            journal.waitForFinished();
            return {};
        }
        text = journal.readAllStandardOutput();
    } else {
        QFile file(rootDir + QStringLiteral("/var/log/ekpo-helper.log"));
        if (!file.open(QIODevice::ReadOnly))
            return {};
        text = file.readAll();
    }
    const QStringList all = QString::fromUtf8(text).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    return all.mid(qMax(0, all.size() - lines));
}
