#include "updater.h"

#include "testmain.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

// A tiny HTTP server for the update flow: fixed answers per path, a redirect like
// GitHub's asset downloads, or no answer at all ("hold", to cancel a download).
class TestServer : public QTcpServer
{
public:
    struct Answer {
        int status = 200;
        QByteArray body;
        QByteArray location; // for redirects
        bool hold = false;
    };
    QHash<QByteArray, Answer> answers;

    QUrl url(const QString &path) const { return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path)); }

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
            const QByteArray request = socket->property("request").toByteArray() + socket->readAll();
            socket->setProperty("request", request);
            if (!request.contains("\r\n\r\n"))
                return;
            const Answer answer = answers.value(request.split(' ').value(1), Answer{404, "not found", {}, false});
            if (answer.hold)
                return;
            QByteArray head = "HTTP/1.1 " + QByteArray::number(answer.status) + " X\r\nConnection: close\r\nContent-Length: " + QByteArray::number(answer.body.size())
                + "\r\n";
            if (!answer.location.isEmpty())
                head += "Location: " + answer.location + "\r\n";
            socket->write(head + "\r\n" + answer.body);
            socket->disconnectFromHost();
        });
    }
};

class TestUpdater : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void comparesVersions();
    void parsesRelease();
    void verifiesPackage();
    void checksForNewerRelease();
    void downloadsVerifiesAndInstalls();
    void refusesDamagedDownload();
    void reportsFailedInstall();
    void cancelsDownload();

private:
    QString makePackage(const QString &version);
    void serveRelease(TestServer &server, const QString &version, const QByteArray &sums = {});
    Updater *makeUpdater(TestServer &server, const QString &current);

    QTemporaryDir m_dir;
    QByteArray m_package;
};

void TestUpdater::comparesVersions()
{
    QVERIFY(isNewerVersion(QStringLiteral("0.4.1"), QStringLiteral("0.4.0")));
    QVERIFY(isNewerVersion(QStringLiteral("0.10.0"), QStringLiteral("0.9.9")));
    QVERIFY(isNewerVersion(QStringLiteral("v1.0.0"), QStringLiteral("0.4.1")));
    QVERIFY(!isNewerVersion(QStringLiteral("0.4.1"), QStringLiteral("0.4.1")));
    QVERIFY(!isNewerVersion(QStringLiteral("0.4.0"), QStringLiteral("0.4.1")));
    QVERIFY(!isNewerVersion(QStringLiteral("0.5.0-rc1"), QStringLiteral("0.4.1")));
    QVERIFY(!isNewerVersion(QStringLiteral("latest"), QStringLiteral("0.4.1")));
}

void TestUpdater::parsesRelease()
{
    QFile file(QStringLiteral(FIXTURES_DIR "/updates/release.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString error;
    const std::optional<Release> release = parseRelease(file.readAll(), error);
    QVERIFY2(release, qPrintable(error));
    QCOMPARE(release->version, QStringLiteral("99.0.0"));
    QCOMPARE(release->deb.fileName(), QStringLiteral("ekpo_99.0.0_amd64.deb"));
    QCOMPARE(release->sums.fileName(), QStringLiteral("SHA256SUMS"));
    QVERIFY(release->notes.contains(QStringLiteral("Boot Menu")));
    QCOMPARE(release->page, QUrl(QStringLiteral("https://github.com/nomoruyi/ekpo/releases/tag/v99.0.0")));

    QVERIFY(!parseRelease(R"({"tag_name": "v1.0.0", "assets": [{"name": "SHA256SUMS", "browser_download_url": "x"}]})", error));
    QVERIFY(error.contains(QStringLiteral("ekpo_1.0.0_amd64.deb")));
    QVERIFY(!parseRelease("<html>rate limited</html>", error));
}

void TestUpdater::verifiesPackage()
{
    const QString deb = makePackage(QStringLiteral("0.5.0"));
    const QByteArray hash = QCryptographicHash::hash(m_package, QCryptographicHash::Sha256).toHex();
    QCOMPARE(verifyPackage(deb, hash + "  ekpo_0.5.0_amd64.deb\n", QStringLiteral("0.5.0")), QString());
    QCOMPARE(verifyPackage(deb, "abc  other.deb\n" + hash + " *ekpo_0.5.0_amd64.deb\n", QStringLiteral("0.5.0")), QString()); // binary-mode line
    QVERIFY(verifyPackage(deb, QByteArray(64, '0') + "  ekpo_0.5.0_amd64.deb\n", QStringLiteral("0.5.0")).contains(QStringLiteral("checksum")));
    QVERIFY(verifyPackage(deb, hash + "  ekpo_0.4.0_amd64.deb\n", QStringLiteral("0.5.0")).contains(QStringLiteral("no line")));
    QVERIFY(verifyPackage(deb, hash + "  ekpo_0.5.0_amd64.deb\n", QStringLiteral("0.6.0")).contains(QStringLiteral("version 0.5.0")));
}

void TestUpdater::checksForNewerRelease()
{
    TestServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    serveRelease(server, QStringLiteral("0.5.0"));
    for (const auto &[current, newer] : {std::pair{"0.4.1", true}, std::pair{"0.5.0", false}}) {
        std::unique_ptr<Updater> updater(makeUpdater(server, QString::fromLatin1(current)));
        bool done = false, found = false;
        Release release;
        QString error;
        connect(updater.get(), &Updater::checked, this, [&](bool f, const Release &r, const QString &e) {
            done = true;
            found = f;
            release = r;
            error = e;
        });
        updater->check();
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QCOMPARE(error, QString());
        QCOMPARE(found, newer);
        QCOMPARE(release.version, QStringLiteral("0.5.0"));
    }
    // A private repository answers 404.
    server.answers.remove("/latest");
    std::unique_ptr<Updater> updater(makeUpdater(server, QStringLiteral("0.4.1")));
    QString error;
    connect(updater.get(), &Updater::checked, this, [&](bool, const Release &, const QString &e) { error = e; });
    updater->check();
    QTRY_VERIFY_WITH_TIMEOUT(!error.isEmpty(), 5000);
    QVERIFY(error.contains(QStringLiteral("private")));
}

void TestUpdater::downloadsVerifiesAndInstalls()
{
    TestServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    serveRelease(server, QStringLiteral("0.5.0"));
    const QString log = m_dir.filePath(QStringLiteral("installed.log"));
    QFile::remove(log);
    qputenv("EKPO_STUB_INSTALL_LOG", log.toUtf8());
    std::unique_ptr<Updater> updater(makeUpdater(server, QStringLiteral("0.4.1")));
    QStringList steps;
    bool done = false, ok = false;
    QString error;
    connect(updater.get(), &Updater::progress, this, [&](const QString &text, double) {
        if (steps.isEmpty() || steps.last() != text)
            steps << text;
    });
    connect(updater.get(), &Updater::finished, this, [&](bool o, const QString &e) {
        done = true;
        ok = o;
        error = e;
    });
    updater->update(Release{QStringLiteral("0.5.0"), {}, {}, server.url(QStringLiteral("/download/ekpo_0.5.0_amd64.deb")),
                            server.url(QStringLiteral("/download/SHA256SUMS"))});
    QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    QVERIFY2(ok, qPrintable(error));
    QVERIFY(!updater->busy());
    QCOMPARE(steps, (QStringList{QStringLiteral("Downloading the checksums…"), QStringLiteral("Downloading Ekpo 0.5.0…"),
                                 QStringLiteral("Checking the download…"), QStringLiteral("Installing Ekpo 0.5.0…")}));
    QFile installed(log);
    QVERIFY(installed.open(QIODevice::ReadOnly));
    const QString path = QString::fromUtf8(installed.readAll()).trimmed();
    QCOMPARE(path, m_dir.filePath(QStringLiteral("cache/ekpo_0.5.0_amd64.deb")));
    QFile downloaded(path); // followed the redirect to the real file
    QVERIFY(downloaded.open(QIODevice::ReadOnly));
    QCOMPARE(downloaded.readAll(), m_package);
}

void TestUpdater::refusesDamagedDownload()
{
    TestServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    serveRelease(server, QStringLiteral("0.5.0"), QByteArray(64, 'f') + "  ekpo_0.5.0_amd64.deb\n");
    const QString log = m_dir.filePath(QStringLiteral("installed.log"));
    QFile::remove(log);
    qputenv("EKPO_STUB_INSTALL_LOG", log.toUtf8());
    std::unique_ptr<Updater> updater(makeUpdater(server, QStringLiteral("0.4.1")));
    bool done = false, ok = true;
    QString error;
    connect(updater.get(), &Updater::finished, this, [&](bool o, const QString &e) {
        done = true;
        ok = o;
        error = e;
    });
    updater->update(Release{QStringLiteral("0.5.0"), {}, {}, server.url(QStringLiteral("/download/ekpo_0.5.0_amd64.deb")),
                            server.url(QStringLiteral("/download/SHA256SUMS"))});
    QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    QVERIFY(!ok);
    QVERIFY(error.contains(QStringLiteral("checksum")));
    QVERIFY(!QFile::exists(log)); // nothing was installed
}

void TestUpdater::reportsFailedInstall()
{
    TestServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    serveRelease(server, QStringLiteral("0.5.0"));
    qputenv("EKPO_STUB_INSTALL_LOG", m_dir.filePath(QStringLiteral("installed.log")).toUtf8());
    qputenv("EKPO_STUB_INSTALL_FAIL", "1");
    std::unique_ptr<Updater> updater(makeUpdater(server, QStringLiteral("0.4.1")));
    bool done = false, ok = true;
    QString error;
    connect(updater.get(), &Updater::finished, this, [&](bool o, const QString &e) {
        done = true;
        ok = o;
        error = e;
    });
    updater->update(Release{QStringLiteral("0.5.0"), {}, {}, server.url(QStringLiteral("/download/ekpo_0.5.0_amd64.deb")),
                            server.url(QStringLiteral("/download/SHA256SUMS"))});
    QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    qunsetenv("EKPO_STUB_INSTALL_FAIL");
    QVERIFY(!ok);
    QVERIFY(error.contains(QStringLiteral("exit code 100")));
    QVERIFY(error.contains(QStringLiteral("injected failure")));
}

void TestUpdater::cancelsDownload()
{
    TestServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    serveRelease(server, QStringLiteral("0.5.0"));
    server.answers["/files/ekpo_0.5.0_amd64.deb"].hold = true;
    std::unique_ptr<Updater> updater(makeUpdater(server, QStringLiteral("0.4.1")));
    bool done = false, ok = true;
    QString error = QStringLiteral("unset");
    connect(updater.get(), &Updater::progress, this, [&](const QString &text, double) {
        if (text.startsWith(QStringLiteral("Downloading Ekpo")))
            QTimer::singleShot(200, updater.get(), &Updater::cancel);
    });
    connect(updater.get(), &Updater::finished, this, [&](bool o, const QString &e) {
        done = true;
        ok = o;
        error = e;
    });
    updater->update(Release{QStringLiteral("0.5.0"), {}, {}, server.url(QStringLiteral("/download/ekpo_0.5.0_amd64.deb")),
                            server.url(QStringLiteral("/download/SHA256SUMS"))});
    QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
    QVERIFY(!ok);
    QCOMPARE(error, QString()); // cancelled, not failed
    QVERIFY(!updater->busy());
}

// A minimal .deb (dpkg-deb only reads its control fields).
QString TestUpdater::makePackage(const QString &version)
{
    const QString root = m_dir.filePath(QStringLiteral("package"));
    QDir(root).removeRecursively();
    QDir().mkpath(root + QStringLiteral("/DEBIAN"));
    QFile control(root + QStringLiteral("/DEBIAN/control"));
    if (!control.open(QIODevice::WriteOnly))
        return {};
    control.write(QStringLiteral("Package: ekpo\nVersion: %1\nArchitecture: amd64\nMaintainer: Test <test@example.org>\nDescription: test package\n")
                      .arg(version)
                      .toUtf8());
    control.close();
    const QString deb = m_dir.filePath(QStringLiteral("ekpo_%1_amd64.deb").arg(version));
    QProcess dpkg;
    dpkg.start(QStringLiteral("dpkg-deb"), {QStringLiteral("--root-owner-group"), QStringLiteral("--build"), root, deb});
    dpkg.waitForFinished();
    QFile file(deb);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    m_package = file.readAll();
    return deb;
}

// The release JSON, SHA256SUMS, and the package behind a redirect, like GitHub serves them.
void TestUpdater::serveRelease(TestServer &server, const QString &version, const QByteArray &sums)
{
    const QString name = QStringLiteral("ekpo_%1_amd64.deb").arg(version);
    makePackage(version);
    const QByteArray goodSums = QCryptographicHash::hash(m_package, QCryptographicHash::Sha256).toHex() + "  " + name.toUtf8() + "\n";
    server.answers["/download/SHA256SUMS"] = {200, sums.isEmpty() ? goodSums : sums, {}, false};
    server.answers["/download/" + name.toUtf8()] = {302, {}, server.url(QStringLiteral("/files/") + name).toEncoded(), false};
    server.answers["/files/" + name.toUtf8()] = {200, m_package, {}, false};
    server.answers["/latest"] = {200,
                                 QStringLiteral(R"({"tag_name": "v%1", "body": "notes", "html_url": "https://example.org",
                                    "assets": [{"name": "%2", "browser_download_url": "%3"}, {"name": "SHA256SUMS", "browser_download_url": "%4"}]})")
                                     .arg(version, name, server.url(QStringLiteral("/download/") + name).toString(),
                                          server.url(QStringLiteral("/download/SHA256SUMS")).toString())
                                     .toUtf8(),
                                 {},
                                 false};
}

Updater *TestUpdater::makeUpdater(TestServer &server, const QString &current)
{
    return new Updater({server.url(QStringLiteral("/latest")), QStringLiteral(STUBS_DIR "/install-update"), {}}, current,
                       m_dir.filePath(QStringLiteral("cache")));
}

EKPO_TEST_MAIN(TestUpdater)
#include "tst_updater.moc"
