#include "request.h"
#include "testmain.h"

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QFile>

namespace {

const QByteArray png = QByteArrayLiteral("\x89PNG\r\n\x1a\n....");
const QByteArray pf2 = QByteArrayLiteral("FILE\0\0\0\x04PFF2....");

std::optional<QList<PayloadFile>> parse(const QByteArray &wire, QString *errorOut = nullptr)
{
    QBuffer buffer;
    buffer.setData(wire);
    buffer.open(QIODevice::ReadOnly);
    QString error;
    auto files = readPayload(buffer, error, PayloadLimits{1000, 4});
    if (errorOut)
        *errorOut = error;
    return files;
}

QByteArray wire(const QList<PayloadFile> &files)
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    writePayload(buffer, files);
    return buffer.data();
}

QList<PayloadFile> plymouthTheme(const QString &id, const QByteArray &plymouthFile)
{
    return {{id + QStringLiteral(".plymouth"), plymouthFile}, {QStringLiteral("logo.png"), png}};
}

} // namespace

class TestCommon : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void payloadRoundTrip();
    void payloadRejects_data();
    void payloadRejects();
    void paths_data();
    void paths();
    void values();
    void fileContent();
    void themeTxt_data();
    void themeTxt();
    void installedGrubThemesPass();
    void grubTheme();
    void plymouthTheme_data();
    void plymouthTheme();
    void requestRoundTrip();
    void requestRejects_data();
    void requestRejects();
    void payloadSplitsIntoThemes();
};

void TestCommon::payloadRoundTrip()
{
    const QList<PayloadFile> files = {{QStringLiteral("a.png"), png}, {QStringLiteral("dir/sub/b.txt"), QByteArrayLiteral("x\ny\n")}, {QStringLiteral("empty.txt"), {}}};
    const auto back = parse(wire(files));
    QVERIFY(back);
    QCOMPARE(back->size(), 3);
    for (qsizetype i = 0; i < files.size(); ++i) {
        QCOMPARE(back->at(i).path, files[i].path);
        QCOMPARE(back->at(i).data, files[i].data);
    }
}

void TestCommon::payloadRejects_data()
{
    QTest::addColumn<QByteArray>("wire");
    const QByteArray head = QByteArrayLiteral("EKPO-PAYLOAD 1\n");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("wrong magic") << QByteArrayLiteral("EKPO-PAYLOAD 2\nE\n");
    QTest::newRow("no end marker") << head + "F 1 a.txt\nx";
    QTest::newRow("truncated data") << head + "F 5 a.txt\nab";
    QTest::newRow("size not a number") << head + "F x a.txt\nE\n";
    QTest::newRow("negative size") << head + "F -1 a.txt\nE\n";
    QTest::newRow("leading zero") << head + "F 01 a.txt\nxE\n";
    QTest::newRow("over the byte limit") << head + "F 1001 a.txt\n" + QByteArray(1001, 'x') + "E\n";
    QTest::newRow("over the file limit") << head + "F 0 a\nF 0 b\nF 0 c\nF 0 d\nF 0 e\nE\n";
    QTest::newRow("parent dir") << head + "F 0 ../etc/passwd\nE\n";
    QTest::newRow("absolute") << head + "F 0 /etc/passwd\nE\n";
    QTest::newRow("hidden") << head + "F 0 .bashrc\nE\n";
    QTest::newRow("double slash") << head + "F 0 a//b\nE\n";
    QTest::newRow("dot component") << head + "F 0 a/./b\nE\n";
    QTest::newRow("duplicate") << head + "F 0 a\nF 0 a\nE\n";
    QTest::newRow("file then folder") << head + "F 0 a\nF 0 a/b\nE\n";
    QTest::newRow("folder then file") << head + "F 0 a/b\nF 0 a\nE\n";
    QTest::newRow("invalid utf-8 path") << head + "F 0 a\xff.txt\nE\n";
    QTest::newRow("nul in path") << head + QByteArray("F 0 a\0b\nE\n", 10);
    QTest::newRow("overlong header") << head + "F 0 " + QByteArray(300, 'a') + "\nE\n";
    QTest::newRow("trailing data") << head + "E\nmore";
    QTest::newRow("unknown record") << head + "L 0 link\nE\n";
}

void TestCommon::payloadRejects()
{
    QFETCH(QByteArray, wire);
    QString error;
    QVERIFY(!parse(wire, &error));
    QVERIFY(!error.isEmpty());
}

void TestCommon::paths_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("valid");
    QTest::newRow("plain") << QStringLiteral("theme.txt") << true;
    QTest::newRow("nested") << QStringLiteral("icons/kubuntu.png") << true;
    QTest::newRow("space inside") << QStringLiteral("DejaVu Sans 16.pf2") << true;
    QTest::newRow("space at start") << QStringLiteral(" a.png") << false;
    QTest::newRow("space at end") << QStringLiteral("a.png ") << false;
    QTest::newRow("too deep") << QStringLiteral("a/b/c/d/e/f") << false;
    QTest::newRow("dotdot") << QStringLiteral("..") << false;
    QTest::newRow("backslash") << QStringLiteral("a\\b") << false;
    QTest::newRow("newline") << QStringLiteral("a\nb") << false;
    QTest::newRow("unicode") << QStringLiteral("bildä.png") << false;
    QTest::newRow("empty") << QString() << false;
}

void TestCommon::paths()
{
    QFETCH(QString, path);
    QFETCH(bool, valid);
    QCOMPARE(validRelativePath(path), valid);
}

void TestCommon::values()
{
    QVERIFY(validPayloadThemeId(QStringLiteral("ekpo-night-v3")));
    QVERIFY(validPayloadThemeId(QStringLiteral("ekpo-a-b-c-v12")));
    QVERIFY(!validPayloadThemeId(QStringLiteral("ekpo-night")));
    QVERIFY(!validPayloadThemeId(QStringLiteral("ekpo--x-v1")));
    QVERIFY(!validPayloadThemeId(QStringLiteral("ekpo-Night-v1")));
    QVERIFY(!validPayloadThemeId(QStringLiteral("fallout")));
    QVERIFY(!validPayloadThemeId(QStringLiteral("ekpo-x-v0")));

    QVERIFY(validInstalledThemeId(QStringLiteral("fallout")));
    QVERIFY(validInstalledThemeId(QStringLiteral("org.kde.breeze.desktop")));
    QVERIFY(!validInstalledThemeId(QStringLiteral("..")));
    QVERIFY(!validInstalledThemeId(QStringLiteral(".hidden")));
    QVERIFY(!validInstalledThemeId(QStringLiteral("a/b")));
    QVERIFY(!validInstalledThemeId(QStringLiteral("a b")));

    QVERIFY(validGfxmode(QStringLiteral("auto")));
    QVERIFY(validGfxmode(QStringLiteral("2560x1440,1920x1080,1280x720,auto")));
    QVERIFY(validGfxmode(QStringLiteral("1920x1080x32")));
    QVERIFY(!validGfxmode(QStringLiteral("")));
    QVERIFY(!validGfxmode(QStringLiteral("1920x1080;rm -rf /")));
    QVERIFY(!validGfxmode(QStringLiteral("1920x1080\nGRUB_CMDLINE_LINUX=x")));
    QVERIFY(!validGfxmode(QStringLiteral("auto,1920x1080")));
    QVERIFY(!validGfxmode(QStringLiteral("$(id)")));

    QVERIFY(validTimeoutStyle(QStringLiteral("menu")));
    QVERIFY(!validTimeoutStyle(QStringLiteral("menu\"")));
}

void TestCommon::fileContent()
{
    const auto ok = [](ThemeTarget t, const QString &path, const QByteArray &data) { return checkFile(t, {path, data}).isEmpty(); };
    QVERIFY(ok(ThemeTarget::Grub, QStringLiteral("bg.png"), png));
    QVERIFY(ok(ThemeTarget::Grub, QStringLiteral("bg.JPG"), QByteArrayLiteral("\xff\xd8\xff\xe0")));
    QVERIFY(ok(ThemeTarget::Grub, QStringLiteral("font.pf2"), pf2));
    QVERIFY(ok(ThemeTarget::Grub, QStringLiteral("LICENSE"), QByteArrayLiteral("MIT")));
    QVERIFY(ok(ThemeTarget::Plymouth, QStringLiteral("x.script"), QByteArrayLiteral("Window.SetBackgroundTopColor(0, 0, 0);")));

    QVERIFY(!ok(ThemeTarget::Grub, QStringLiteral("bg.png"), QByteArrayLiteral("#!/bin/sh")));
    QVERIFY(!ok(ThemeTarget::Grub, QStringLiteral("font.pf2"), QByteArrayLiteral("FILE")));
    QVERIFY(!ok(ThemeTarget::Grub, QStringLiteral("notes.txt"), QByteArray("a\0b", 3)));
    QVERIFY(!ok(ThemeTarget::Grub, QStringLiteral("notes.txt"), QByteArrayLiteral("\xff\xfe")));
    QVERIFY(!ok(ThemeTarget::Grub, QStringLiteral("install.sh"), QByteArrayLiteral("echo")));
    QVERIFY(!ok(ThemeTarget::Grub, QStringLiteral("x.script"), QByteArrayLiteral("text")));
    QVERIFY(!ok(ThemeTarget::Plymouth, QStringLiteral("font.pf2"), pf2));
    QVERIFY(!ok(ThemeTarget::Plymouth, QStringLiteral("bg.jpg"), QByteArrayLiteral("\xff\xd8\xff\xe0")));
    QVERIFY(!ok(ThemeTarget::Plymouth, QStringLiteral("noext"), QByteArrayLiteral("x")));
}

void TestCommon::themeTxt_data()
{
    QTest::addColumn<QByteArray>("text");
    QTest::addColumn<int>("errors");
    QTest::newRow("globals and nesting") << QByteArrayLiteral(
        "# comment\n"
        "title-text: \"\"\n"
        "desktop-image: \"bg.png\"\n"
        "terminal-left: 10%\n"
        "+ vbox {\n"
        "  left = 50%-100 top = 20%\n"
        "  + label { text = \"Hello\" color = \"#ffffff\" }\n"
        "  + boot_menu\n  {\n    menu_pixmap_style = \"menu_*.png\"\n    item_align = \"center\"\n  }\n"
        "}\n") << 0;
    QTest::newRow("unknown global") << QByteArrayLiteral("title-txt: \"x\"\n") << 1;
    QTest::newRow("unknown component") << QByteArrayLiteral("+ widget { }\n") << 1;
    QTest::newRow("missing brace") << QByteArrayLiteral("+ label { text = \"x\"\n") << 1;
    QTest::newRow("unterminated string") << QByteArrayLiteral("title-text: \"x\n") << 1;
    QTest::newRow("missing colon") << QByteArrayLiteral("title-text \"x\"\n") << 1;
    QTest::newRow("missing value") << QByteArrayLiteral("+ label { text = }\n") << 1;
    QTest::newRow("image outside theme") << QByteArrayLiteral("desktop-image: \"../../etc/shadow.png\"\n") << 1;
    QTest::newRow("absolute image") << QByteArrayLiteral("+ image { file = \"/boot/x.png\" }\n") << 1;
    QTest::newRow("too deep") << QByteArray("+ vbox {").repeated(10) + QByteArray("}").repeated(10) << 1;
    QTest::newRow("not utf-8") << QByteArrayLiteral("title-text: \"\xff\"\n") << 1;
}

void TestCommon::themeTxt()
{
    QFETCH(QByteArray, text);
    QFETCH(int, errors);
    const QStringList found = validateGrubThemeTxt(text);
    QVERIFY2(found.size() == errors, qPrintable(found.join(QLatin1Char('\n'))));
}

// The validator must accept the hand-installed themes on this machine (read-only).
void TestCommon::installedGrubThemesPass()
{
    const QStringList themes = QDir(QStringLiteral("/boot/grub/themes")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (themes.isEmpty())
        QSKIP("no GRUB themes installed");
    for (const QString &theme : themes) {
        QFile file(QStringLiteral("/boot/grub/themes/%1/theme.txt").arg(theme));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QStringList errors = validateGrubThemeTxt(file.readAll());
        QVERIFY2(errors.isEmpty(), qPrintable(theme + QStringLiteral(": ") + errors.join(QLatin1Char('\n'))));
    }
}

void TestCommon::grubTheme()
{
    const QString id = QStringLiteral("ekpo-night-v1");
    const QList<PayloadFile> good = {{QStringLiteral("theme.txt"), QByteArrayLiteral("desktop-image: \"bg.png\"\n")}, {QStringLiteral("bg.png"), png}, {QStringLiteral("f/font.pf2"), pf2}};
    QVERIFY2(validateTheme(ThemeTarget::Grub, id, good).isEmpty(), qPrintable(validateTheme(ThemeTarget::Grub, id, good).join(QLatin1Char('\n'))));
    QVERIFY(!validateTheme(ThemeTarget::Grub, QStringLiteral("hollow-grub"), good).isEmpty());
    QVERIFY(!validateTheme(ThemeTarget::Grub, id, {{QStringLiteral("bg.png"), png}}).isEmpty());
    QVERIFY(!validateTheme(ThemeTarget::Grub, id, good + QList<PayloadFile>{{QStringLiteral("run.sh"), QByteArrayLiteral("x")}}).isEmpty());
    QVERIFY(!validateTheme(ThemeTarget::Grub, id, good + QList<PayloadFile>{{QStringLiteral("huge.png"), png + QByteArray(64 << 20, 'x')}}).isEmpty());
}

void TestCommon::plymouthTheme_data()
{
    QTest::addColumn<QByteArray>("file");
    QTest::addColumn<bool>("valid");
    QTest::newRow("two-step") << QByteArrayLiteral("[Plymouth Theme]\nName=Night\nModuleName=two-step\n\n[two-step]\nImageDir=/usr/share/plymouth/themes/ekpo-night-v1\n") << true;
    QTest::newRow("script") << QByteArrayLiteral("[Plymouth Theme]\nModuleName=script\n[script]\nImageDir=/usr/share/plymouth/themes//ekpo-night-v1\nScriptFile=/usr/share/plymouth/themes/ekpo-night-v1/night.script\n") << true;
    QTest::newRow("text module") << QByteArrayLiteral("[Plymouth Theme]\nModuleName=text\n") << false;
    QTest::newRow("ImageDir elsewhere") << QByteArrayLiteral("[Plymouth Theme]\nModuleName=two-step\n[two-step]\nImageDir=/etc\n") << false;
    QTest::newRow("ImageDir escapes") << QByteArrayLiteral("[Plymouth Theme]\nModuleName=two-step\n[two-step]\nImageDir=/usr/share/plymouth/themes/ekpo-night-v1/../../../../etc\n") << false;
    QTest::newRow("missing ImageDir") << QByteArrayLiteral("[Plymouth Theme]\nModuleName=two-step\n") << false;
    QTest::newRow("script file missing") << QByteArrayLiteral("[Plymouth Theme]\nModuleName=script\n[script]\nImageDir=/usr/share/plymouth/themes/ekpo-night-v1\nScriptFile=/usr/share/plymouth/themes/ekpo-night-v1/other.script\n") << false;
    QTest::newRow("script outside") << QByteArrayLiteral("[Plymouth Theme]\nModuleName=script\n[script]\nImageDir=/usr/share/plymouth/themes/ekpo-night-v1\nScriptFile=/tmp/night.script\n") << false;
    QTest::newRow("garbage line") << QByteArrayLiteral("[Plymouth Theme]\nModuleName=two-step\nnonsense\n") << false;
}

void TestCommon::plymouthTheme()
{
    QFETCH(QByteArray, file);
    QFETCH(bool, valid);
    const QString id = QStringLiteral("ekpo-night-v1");
    QList<PayloadFile> files = ::plymouthTheme(id, file);
    files.append({QStringLiteral("night.script"), QByteArrayLiteral("// script")});
    const QStringList errors = validateTheme(ThemeTarget::Plymouth, id, files);
    QVERIFY2(errors.isEmpty() == valid, qPrintable(errors.join(QLatin1Char('\n'))));

    if (valid) {
        // Misnamed or doubled .plymouth files are rejected too.
        QList<PayloadFile> renamed = files;
        renamed[0].path = QStringLiteral("other.plymouth");
        QVERIFY(!validateTheme(ThemeTarget::Plymouth, id, renamed).isEmpty());
        QVERIFY(!validateTheme(ThemeTarget::Plymouth, id, files + QList<PayloadFile>{{QStringLiteral("sub/x.plymouth"), file}}).isEmpty());
    }
}

void TestCommon::requestRoundTrip()
{
    Request r;
    r.themes = {{QStringLiteral("ekpo-night-v2"), ThemeTarget::Plymouth}};
    r.grub = Request::Grub{false, QStringLiteral("hollow-grub"), QStringLiteral("2560x1440,auto"), 5, QStringLiteral("menu")};
    r.plymouth = Request::Plymouth{QStringLiteral("manual"), QStringLiteral("ekpo-night-v2"), QStringLiteral("ekpo-night-v2")};
    const QByteArray json = serializeRequest(r);
    QVERIFY(!json.contains('\n'));

    QString error;
    const auto back = parseRequest(json, error);
    QVERIFY2(back, qPrintable(error));
    QCOMPARE(serializeRequest(*back), json);
    QCOMPARE(back->grub->timeout, 5);

    QVERIFY(parseRequest(R"({"format":1,"operation":"apply","grub":{"original":true}})", error));
    QVERIFY(parseRequest(R"({"format":1,"operation":"apply","plymouth":{"alternativeMode":"auto","daemonTheme":"fallout"}})", error));
    QVERIFY(parseRequest(R"({"format":1,"operation":"recover"})", error));
    const auto keep = parseRequest(R"({"format":1,"operation":"apply","grub":{"original":true},"keep":["ekpo-a-v1"]})", error);
    QVERIFY2(keep, qPrintable(error));
    QCOMPARE(*keep->keep, QStringList{QStringLiteral("ekpo-a-v1")});
    const auto newest = parseRequest(R"({"format":1,"operation":"apply","plymouth":{"alternativeMode":"manual","theme":"fallout","daemonTheme":"fallout","initrds":"newest"}})", error);
    QVERIFY2(newest, qPrintable(error));
    QVERIFY(!newest->plymouth->allKernels);
    QVERIFY(serializeRequest(*newest).contains("\"initrds\":\"newest\""));
    QVERIFY(parseRequest(R"({"format":1,"operation":"apply","plymouth":{"alternativeMode":"manual","theme":"fallout","daemonTheme":"fallout"}})", error)->plymouth->allKernels);
}

void TestCommon::requestRejects_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::newRow("not json") << QByteArrayLiteral("{");
    QTest::newRow("array") << QByteArrayLiteral("[]");
    QTest::newRow("wrong format") << QByteArrayLiteral(R"({"format":2,"operation":"recover"})");
    QTest::newRow("unknown key") << QByteArrayLiteral(R"({"format":1,"operation":"recover","debug":true})");
    QTest::newRow("unknown operation") << QByteArrayLiteral(R"({"format":1,"operation":"shell"})");
    QTest::newRow("recover with changes") << QByteArrayLiteral(R"({"format":1,"operation":"recover","grub":{"original":true}})");
    QTest::newRow("apply without changes") << QByteArrayLiteral(R"({"format":1,"operation":"apply"})");
    QTest::newRow("original plus theme") << QByteArrayLiteral(R"({"format":1,"operation":"apply","grub":{"original":true,"theme":"x"}})");
    QTest::newRow("original false") << QByteArrayLiteral(R"({"format":1,"operation":"apply","grub":{"original":false}})");
    QTest::newRow("theme path") << QByteArrayLiteral(R"({"format":1,"operation":"apply","grub":{"theme":"../../etc"}})");
    QTest::newRow("payload id not sent") << QByteArrayLiteral(R"({"format":1,"operation":"apply","grub":{"theme":"ekpo-night-v1"}})");
    QTest::newRow("payload of other target") << QByteArrayLiteral(R"({"format":1,"operation":"apply","themes":[{"id":"ekpo-night-v1","target":"plymouth"}],"grub":{"theme":"ekpo-night-v1"}})");
    QTest::newRow("unused payload theme") << QByteArrayLiteral(R"({"format":1,"operation":"apply","themes":[{"id":"ekpo-night-v1","target":"grub"}],"grub":{"original":true}})");
    QTest::newRow("repeated theme") << QByteArrayLiteral(R"({"format":1,"operation":"apply","themes":[{"id":"ekpo-a-v1","target":"grub"},{"id":"ekpo-a-v1","target":"grub"}],"grub":{"theme":"ekpo-a-v1"}})");
    QTest::newRow("bad gfxmode") << QByteArrayLiteral(R"({"format":1,"operation":"apply","grub":{"gfxmode":"1x1\nGRUB_X=1"}})");
    QTest::newRow("timeout too big") << QByteArrayLiteral(R"({"format":1,"operation":"apply","grub":{"timeout":601}})");
    QTest::newRow("timeout fraction") << QByteArrayLiteral(R"({"format":1,"operation":"apply","grub":{"timeout":1.5}})");
    QTest::newRow("timeout string") << QByteArrayLiteral(R"({"format":1,"operation":"apply","grub":{"timeout":"5"}})");
    QTest::newRow("theme as number") << QByteArrayLiteral(R"({"format":1,"operation":"apply","grub":{"theme":5}})");
    QTest::newRow("manual without theme") << QByteArrayLiteral(R"({"format":1,"operation":"apply","plymouth":{"alternativeMode":"manual","daemonTheme":"fallout"}})");
    QTest::newRow("auto with theme") << QByteArrayLiteral(R"({"format":1,"operation":"apply","plymouth":{"alternativeMode":"auto","theme":"fallout","daemonTheme":"fallout"}})");
    QTest::newRow("bad daemon theme") << QByteArrayLiteral(R"({"format":1,"operation":"apply","plymouth":{"alternativeMode":"auto","daemonTheme":"x\nThemeDir=/tmp"}})");
    QTest::newRow("keep with a bad id") << QByteArrayLiteral(R"({"format":1,"operation":"apply","grub":{"original":true},"keep":["../x"]})");
    QTest::newRow("keep not a list") << QByteArrayLiteral(R"({"format":1,"operation":"apply","grub":{"original":true},"keep":"ekpo-a-v1"})");
    QTest::newRow("keep on recover") << QByteArrayLiteral(R"({"format":1,"operation":"recover","keep":[]})");
    QTest::newRow("bad initrd scope") << QByteArrayLiteral(R"({"format":1,"operation":"apply","plymouth":{"alternativeMode":"manual","theme":"fallout","daemonTheme":"fallout","initrds":"oldest"}})");
    QTest::newRow("oversized") << QByteArray(maxRequestBytes + 1, ' ');
}

void TestCommon::requestRejects()
{
    QFETCH(QByteArray, json);
    QString error;
    QVERIFY(!parseRequest(json, error));
    QVERIFY(!error.isEmpty());
}

void TestCommon::payloadSplitsIntoThemes()
{
    Request r;
    r.themes = {{QStringLiteral("ekpo-night-v1"), ThemeTarget::Grub}};
    r.grub = Request::Grub{false, QStringLiteral("ekpo-night-v1"), {}, {}, {}};
    const QList<PayloadFile> files = {{QStringLiteral("ekpo-night-v1/theme.txt"), QByteArrayLiteral("title-text: \"\"\n")}, {QStringLiteral("ekpo-night-v1/bg.png"), png}};

    QStringList errors;
    const auto themes = themesFromPayload(r, files, errors);
    QVERIFY2(themes, qPrintable(errors.join(QLatin1Char('\n'))));
    QCOMPARE(themes->value(QStringLiteral("ekpo-night-v1")).first().path, QStringLiteral("theme.txt"));

    errors.clear();
    QVERIFY(!themesFromPayload(r, files + QList<PayloadFile>{{QStringLiteral("ekpo-other-v1/theme.txt"), {}}}, errors));
    errors.clear();
    QVERIFY(!themesFromPayload(r, {}, errors));
    errors.clear();
    QVERIFY(!themesFromPayload(r, {{QStringLiteral("loose.txt"), {}}}, errors));
}

EKPO_TEST_MAIN(TestCommon)
#include "tst_common.moc"
