#include "bugreport.h"
#include "testmain.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUrlQuery>

// Stands in for the browser: QDesktopServices hands https links to it instead.
class FakeBrowser : public QObject
{
    Q_OBJECT

public:
    QList<QUrl> opened;

public Q_SLOTS:
    void open(const QUrl &url) { opened << url; }
};

class TestBugReport : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void encodesIssueLink();
    void opensIssueInBrowser();
    void savesMarkdownAndScreenshot();
    void keepsRecentMessages();
    void readsHelperLogUnderFakeRoot();
};

void TestBugReport::encodesIssueLink()
{
    const QString title = QStringLiteral("a+b & c=d #1");
    const QString body = QStringLiteral("## What happened\n100% wrong: ü?x=1&body=y");
    const QUrl url = issueUrl(title, body);
    const QString encoded = url.toString(QUrl::FullyEncoded);
    QVERIFY(encoded.startsWith(QStringLiteral("https://github.com/nomoruyi/ekpo/issues/new?labels=bug&title=")));
    QVERIFY(encoded.contains(QStringLiteral("a%2Bb%20%26%20c%3Dd%20%231"))); // '+' must not read as a space
    QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("title"), QUrl::FullyDecoded), title);
    QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("body"), QUrl::FullyDecoded), body);
}

void TestBugReport::opensIssueInBrowser()
{
    FakeBrowser browser;
    QDesktopServices::setUrlHandler(QStringLiteral("https"), &browser, "open");
    QCOMPARE(openIssue(QStringLiteral("Title"), QStringLiteral("Body")), QString());
    QCOMPARE(browser.opened, QList<QUrl>{issueUrl(QStringLiteral("Title"), QStringLiteral("Body"))});

    // Too long for GitHub: nothing opens, and the user is told why.
    QVERIFY(!openIssue(QStringLiteral("Title"), QString(maxIssueUrlLength, QLatin1Char('x'))).isEmpty());
    QCOMPARE(browser.opened.size(), 1);
    QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
}

void TestBugReport::savesMarkdownAndScreenshot()
{
    QVERIFY(localReportsDir().endsWith(QStringLiteral("/bug-reports"))); // a sandbox build
    QTemporaryDir tmp;
    const QString dir = tmp.filePath(QStringLiteral("bug-reports"));
    QImage shot(4, 3, QImage::Format_RGB32);
    shot.fill(Qt::red);
    QString error;
    const QString path = saveReport(dir, QStringLiteral("Crash on Apply!"), QStringLiteral("What happened\n"), shot, error);
    QVERIFY2(!path.isEmpty(), qPrintable(error));
    QVERIFY(path.endsWith(QStringLiteral("-crash-on-apply.md")));
    QFile md(path);
    QVERIFY(md.open(QIODevice::ReadOnly));
    const QString base = QFileInfo(path).completeBaseName();
    QCOMPARE(QString::fromUtf8(md.readAll()),
             QStringLiteral("# Crash on Apply!\n\nWhat happened\n\n## Screenshot\n\n![Screenshot](%1.png)\n").arg(base));
    QCOMPARE(QImage(dir + QLatin1Char('/') + base + QStringLiteral(".png")).size(), QSize(4, 3));

    // Without a screenshot: no image, no section.
    const QString plain = saveReport(dir, QStringLiteral("Plain"), QStringLiteral("Text"), QImage(), error);
    QVERIFY(plain.endsWith(QStringLiteral("-plain.md")));
    QVERIFY(!QFile::exists(plain.chopped(3) + QStringLiteral(".png")));
}

void TestBugReport::keepsRecentMessages()
{
    installMessageLog();
    for (int i = 0; i < 55; ++i)
        qDebug("message %d", i);
    const QStringList messages = recentMessages();
    QCOMPARE(messages.size(), 50);
    QVERIFY(messages.first().endsWith(QStringLiteral(" debug: message 5")));
    QVERIFY(messages.last().endsWith(QStringLiteral(" debug: message 54")));
}

void TestBugReport::readsHelperLogUnderFakeRoot()
{
    QTemporaryDir root;
    QVERIFY(helperLog(root.path(), 20).isEmpty()); // nothing applied yet
    QVERIFY(QDir(root.path()).mkpath(QStringLiteral("var/log")));
    QFile log(root.filePath(QStringLiteral("var/log/ekpo-helper.log")));
    QVERIFY(log.open(QIODevice::WriteOnly));
    for (int i = 1; i <= 25; ++i)
        log.write(QStringLiteral("line %1\n").arg(i).toUtf8());
    log.close();
    const QStringList lines = helperLog(root.path(), 20);
    QCOMPARE(lines.size(), 20);
    QCOMPARE(lines.first(), QStringLiteral("line 6"));
    QCOMPARE(lines.last(), QStringLiteral("line 25"));
}

EKPO_GUI_TEST_MAIN(TestBugReport)
#include "tst_bugreport.moc"
