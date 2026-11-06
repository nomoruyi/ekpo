// ekpo-helper: the only part of Ekpo that runs as root, started through pkexec.
// Protocol (ARCHITECTURE.md §1-3): stdin carries one JSON request line and, for
// "apply", a payload; stdout carries JSON lines ({"event":"progress"} ...,
// then one {"event":"result"}). The exit code is the Outcome.

#include "helper.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <csignal>
#include <cstdio>

#include <sys/stat.h>
#include <unistd.h>
#ifdef EKPO_HELPER_TESTING
#include <sys/prctl.h>
#else
#include <syslog.h>
#endif

namespace {

void emitLine(const QJsonObject &o)
{
    const QByteArray line = QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout);
    std::fflush(stdout);
}

int finish(Outcome outcome, const QString &error, const QJsonObject &details = {})
{
    emitLine({{QStringLiteral("event"), QStringLiteral("result")},
              {QStringLiteral("outcome"), int(outcome)},
              {QStringLiteral("error"), error},
              {QStringLiteral("details"), details}});
    return int(outcome);
}

std::optional<QByteArray> readRequestLine(QIODevice &in)
{
    QByteArray line;
    char c;
    while (line.size() <= maxRequestBytes && in.getChar(&c)) {
        if (c == '\n')
            return line;
        line.append(c);
    }
    return std::nullopt;
}

} // namespace

int main(int argc, char *argv[])
{
    std::signal(SIGPIPE, SIG_IGN); // the app may close its end; the work still finishes or rolls back
    QCoreApplication app(argc, argv);

    HelperEnv env;
    env.progress = [](const QString &line) { emitLine({{QStringLiteral("event"), QStringLiteral("progress")}, {QStringLiteral("text"), line}}); };
#ifdef EKPO_HELPER_TESTING
    prctl(PR_SET_DUMPABLE, 0);
    env.root = qEnvironmentVariable("EKPO_HELPER_ROOT");
    env.binDir = qEnvironmentVariable("EKPO_HELPER_BIN");
    if (env.root.isEmpty() || env.binDir.isEmpty())
        return finish(Outcome::Rejected, QStringLiteral("test helper needs EKPO_HELPER_ROOT and EKPO_HELPER_BIN"));
    env.log = [root = env.root](const QString &line) {
        QFile log(root + QStringLiteral("/var/log/ekpo-helper.log"));
        if (log.open(QIODevice::Append))
            log.write((QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + QLatin1Char(' ') + line + QLatin1Char('\n')).toUtf8());
    };
#else
    if (::geteuid() != 0)
        return finish(Outcome::Rejected, QStringLiteral("ekpo-helper must run as root (through pkexec)"));
    ::umask(022);
    ::openlog("ekpo-helper", LOG_PID, LOG_AUTHPRIV);
    env.log = [](const QString &line) { ::syslog(LOG_NOTICE, "%s", line.toUtf8().constData()); };
#endif

    QFile in;
    if (!in.open(stdin, QIODevice::ReadOnly))
        return finish(Outcome::Rejected, QStringLiteral("can't read stdin"));
    const auto line = readRequestLine(in);
    if (!line)
        return finish(Outcome::Rejected, QStringLiteral("request: missing or too long"));
    QString error;
    const auto request = parseRequest(*line, error);
    if (!request)
        return finish(Outcome::Rejected, error);

    QMap<QString, QList<PayloadFile>> themes;
    if (request->operation == Request::Operation::Apply) {
        const auto files = readPayload(in, error);
        if (!files)
            return finish(Outcome::Rejected, error);
        QStringList errors;
        const auto split = themesFromPayload(*request, *files, errors);
        if (!split)
            return finish(Outcome::Rejected, errors.join(QStringLiteral("; ")));
        themes = *split;
    }

    const HelperResult result = runRequest(env, *request, themes);
    return finish(result.outcome, result.error, result.details);
}
