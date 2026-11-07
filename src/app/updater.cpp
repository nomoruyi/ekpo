#include "updater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QVersionNumber>

namespace {

bool restart = false;

QString tr(const char *text)
{
    return QCoreApplication::translate("Updater", text);
}

// "0.5.0" or "v0.5.0"; anything with a suffix ("0.5.0-rc1") isn't a release version.
QVersionNumber parseVersion(QString text)
{
    if (text.startsWith(QLatin1Char('v')))
        text.remove(0, 1);
    qsizetype suffix = 0;
    const QVersionNumber version = QVersionNumber::fromString(text, &suffix);
    return suffix == text.size() ? version : QVersionNumber();
}

QNetworkRequest request(const QUrl &url, const QString &version)
{
    QNetworkRequest r(url);
    r.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Ekpo/%1").arg(version));
    r.setTransferTimeout(30000); // gives up after 30 s without data
    return r;
}

// Why a request to GitHub failed, in words.
QString networkError(QNetworkReply *reply)
{
    switch (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()) {
    case 404:
        return tr("GitHub doesn't show Ekpo's releases. Is the repository private?");
    case 403:
    case 429:
        return tr("GitHub's limit of 60 checks an hour is used up. Try again later.");
    default:
        return tr("No answer from GitHub: %1").arg(reply->errorString());
    }
}

} // namespace

bool isNewerVersion(const QString &a, const QString &b)
{
    const QVersionNumber va = parseVersion(a);
    const QVersionNumber vb = parseVersion(b);
    return !va.isNull() && !vb.isNull() && va > vb;
}

std::optional<Release> parseRelease(const QByteArray &json, QString &error)
{
    QJsonParseError parseError;
    const QJsonObject o = QJsonDocument::fromJson(json, &parseError).object();
    const QString tag = o.value(QStringLiteral("tag_name")).toString();
    if (parseError.error != QJsonParseError::NoError || tag.isEmpty()) {
        error = tr("GitHub's answer isn't a release.");
        return std::nullopt;
    }
    Release release;
    release.version = tag.startsWith(QLatin1Char('v')) ? tag.mid(1) : tag;
    release.notes = o.value(QStringLiteral("body")).toString();
    release.page = QUrl(o.value(QStringLiteral("html_url")).toString());
    const QString debName = QStringLiteral("ekpo_%1_amd64.deb").arg(release.version);
    for (const QJsonValue &asset : o.value(QStringLiteral("assets")).toArray()) {
        const QString name = asset.toObject().value(QStringLiteral("name")).toString();
        const QUrl url(asset.toObject().value(QStringLiteral("browser_download_url")).toString());
        if (name == debName)
            release.deb = url;
        else if (name == QLatin1String("SHA256SUMS"))
            release.sums = url;
    }
    if (release.deb.isEmpty() || release.sums.isEmpty()) {
        error = tr("Release %1 has no %2 or SHA256SUMS.").arg(tag, debName);
        return std::nullopt;
    }
    return release;
}

QString verifyPackage(const QString &debPath, const QByteArray &sums, const QString &version)
{
    const QByteArray name = QFileInfo(debPath).fileName().toUtf8();
    QByteArray expected;
    for (const QByteArray &line : sums.split('\n')) {
        // sha256sum's format: "<hash>  <name>", or "<hash> *<name>" in binary mode.
        const qsizetype space = line.indexOf(' ');
        QByteArray file = line.mid(space + 1).trimmed();
        if (file.startsWith('*'))
            file.remove(0, 1);
        if (space > 0 && file == name)
            expected = line.left(space).toLower();
    }
    if (expected.isEmpty())
        return tr("SHA256SUMS has no line for %1.").arg(QString::fromUtf8(name));
    QFile file(debPath);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
        return tr("Can't read %1.").arg(debPath);
    if (hash.result().toHex() != expected)
        return tr("The download is damaged: its checksum doesn't match SHA256SUMS.");
    QProcess dpkg;
    dpkg.start(QStringLiteral("dpkg-deb"), {QStringLiteral("-f"), debPath, QStringLiteral("Version")});
    if (!dpkg.waitForFinished(10000) || dpkg.exitStatus() != QProcess::NormalExit || dpkg.exitCode() != 0)
        return tr("The download isn't a package dpkg can read.");
    const QString packageVersion = QString::fromUtf8(dpkg.readAllStandardOutput()).trimmed();
    if (packageVersion != version)
        return tr("The package is version %1, not %2.").arg(packageVersion, version);
    return {};
}

UpdateSource updateSource()
{
#ifdef EKPO_SANDBOX
    // Dev builds are never installed: no GitHub unless a test asks, and no pkexec.
    return {QUrl(qEnvironmentVariable("EKPO_UPDATE_URL")), QStringLiteral(EKPO_STUBS_DIR "/install-update"), {}};
#else
    return {QUrl(QStringLiteral("https://api.github.com/repos/nomoruyi/ekpo/releases/latest")), QStringLiteral("/usr/bin/pkexec"),
            {QStringLiteral("/usr/bin/apt-get"), QStringLiteral("install"), QStringLiteral("-y")}};
#endif
}

void requestRestart()
{
    restart = true;
    QCoreApplication::quit();
}

bool restartRequested()
{
    return restart;
}

QString restartPath()
{
    // apt replaced the running binary, so the link reads "/usr/bin/ekpo (deleted)".
    QString path = QFile::symLinkTarget(QStringLiteral("/proc/self/exe"));
    if (path.endsWith(QLatin1String(" (deleted)")))
        path.chop(10);
    return path;
}

Updater::Updater(const UpdateSource &source, const QString &currentVersion, const QString &cacheDir, QObject *parent)
    : QObject(parent)
    , m_source(source)
    , m_currentVersion(currentVersion)
    , m_cacheDir(cacheDir)
    , m_network(new QNetworkAccessManager(this))
{
}

Updater::~Updater()
{
    if (m_reply)
        m_reply->abort();
}

bool Updater::busy() const
{
    return m_updating || m_reply;
}

void Updater::check()
{
    if (!canCheck() || busy())
        return;
    QNetworkRequest r = request(m_source.releaseUrl, m_currentVersion);
    r.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply *reply = m_network->get(r);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_reply.clear();
        if (reply->error() != QNetworkReply::NoError)
            return Q_EMIT checked(false, {}, networkError(reply));
        QString error;
        const std::optional<Release> release = parseRelease(reply->readAll(), error);
        if (!release)
            return Q_EMIT checked(false, {}, error);
        Q_EMIT checked(isNewerVersion(release->version, m_currentVersion), *release, {});
    });
}

void Updater::update(const Release &release)
{
    if (busy())
        return;
    m_release = release;
    m_updating = true;
    m_sums.clear();
    QDir(m_cacheDir).removeRecursively(); // earlier downloads
    if (!QDir().mkpath(m_cacheDir))
        return fail(tr("Can't create %1.").arg(m_cacheDir));
    Q_EMIT progress(tr("Downloading the checksums…"), -1);
    QNetworkReply *reply = m_network->get(request(release.sums, m_currentVersion));
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_reply.clear();
        if (reply->error() == QNetworkReply::OperationCanceledError)
            return fail({});
        if (reply->error() != QNetworkReply::NoError)
            return fail(networkError(reply));
        m_sums = reply->readAll();
        downloadPackage();
    });
}

void Updater::downloadPackage()
{
    m_file.setFileName(QDir(m_cacheDir).filePath(m_release.deb.fileName()));
    if (!m_file.open(QIODevice::WriteOnly))
        return fail(tr("Can't write %1.").arg(m_file.fileName()));
    QNetworkReply *reply = m_network->get(request(m_release.deb, m_currentVersion));
    m_reply = reply;
    Q_EMIT progress(tr("Downloading Ekpo %1…").arg(m_release.version), -1);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] { m_file.write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        Q_EMIT progress(tr("Downloading Ekpo %1…").arg(m_release.version), total > 0 ? double(received) / double(total) : -1);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_reply.clear();
        m_file.write(reply->readAll());
        m_file.close();
        if (reply->error() == QNetworkReply::OperationCanceledError)
            return fail({});
        if (reply->error() != QNetworkReply::NoError)
            return fail(networkError(reply));
        Q_EMIT progress(tr("Checking the download…"), -1);
        if (const QString error = verifyPackage(m_file.fileName(), m_sums, m_release.version); !error.isEmpty())
            return fail(error);
        install();
    });
}

void Updater::install()
{
    Q_EMIT progress(tr("Installing Ekpo %1…").arg(m_release.version), -1);
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QStringList output = QString::fromUtf8(m_process->readAll()).trimmed().split(QLatin1Char('\n'));
        m_process->deleteLater();
        m_process = nullptr;
        if (status == QProcess::NormalExit && code == 0) {
            m_updating = false;
            return Q_EMIT finished(true, {});
        }
        if (status == QProcess::NormalExit && (code == 126 || code == 127)) // pkexec: dismissed or not allowed
            return fail(tr("The password prompt was cancelled. Nothing was installed."));
        fail(tr("Installing the update failed (exit code %1):\n%2").arg(code).arg(output.mid(qMax(0, output.size() - 6)).join(QLatin1Char('\n'))));
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_process->deleteLater();
        m_process = nullptr;
        fail(tr("Couldn't start %1.").arg(m_source.installer));
    });
    m_process->start(m_source.installer, m_source.installerArguments + QStringList{m_file.fileName()});
}

void Updater::cancel()
{
    if (m_reply)
        m_reply->abort();
}

void Updater::fail(const QString &error)
{
    m_updating = false;
    if (m_file.isOpen())
        m_file.close();
    Q_EMIT finished(false, error);
}
