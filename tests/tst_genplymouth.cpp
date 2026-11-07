#include "genplymouth.h"
#include "golden.h"
#include "testmain.h"
#include "validate.h"

#include <functional>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QTemporaryDir>

class TestGenPlymouth : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void twoStepMatchesGolden();
    void drawsTheStillParts();
    void scriptForOtherLoops();
    void reportsProblems();
    void gradientAngle();

private:
    static QJsonObject element(const QString &type, double x, double y, double w, double h, const QJsonObject &more = {})
    {
        QJsonObject e = more;
        e.insert(QStringLiteral("type"), type);
        e.insert(QStringLiteral("x"), x);
        e.insert(QStringLiteral("y"), y);
        e.insert(QStringLiteral("width"), w);
        e.insert(QStringLiteral("height"), h);
        return e;
    }
    // A gradient, a logo, a text, a spinner and a progress bar; messages hidden.
    Design design() const
    {
        Design d;
        d.id = QStringLiteral("00000000-0000-0000-0000-0000000000d2");
        d.stage = Stage::BootSplash;
        d.name = QStringLiteral("Night\nSky"); // the line break must not reach the key file
        d.version = 2;
        d.content = {
            {QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#102030")}, {QStringLiteral("color2"), QStringLiteral("#000000")}}},
            {QStringLiteral("hideMessages"), true},
            {QStringLiteral("elements"),
             QJsonArray{
                 element(QStringLiteral("image"), 45, 30, 10, 10, {{QStringLiteral("asset"), m_logo}}),
                 element(QStringLiteral("text"), 10, 80, 80, 5, {{QStringLiteral("text"), QStringLiteral("Welcome")}, {QStringLiteral("color"), QStringLiteral("#ffffff")}}),
                 element(QStringLiteral("spinner"), 48.5, 60, 3, 5, {{QStringLiteral("color"), QStringLiteral("#ffffff")}}),
                 element(QStringLiteral("progress"), 40, 90, 20, 0.5, {{QStringLiteral("color"), QStringLiteral("#ff8800")}}),
             }},
        };
        return d;
    }
    static QByteArray fileData(const QList<PayloadFile> &files, const QString &path)
    {
        for (const PayloadFile &f : files) {
            if (f.path == path)
                return f.data;
        }
        return {};
    }
    static QStringList paths(const QList<PayloadFile> &files, bool withFrames)
    {
        QStringList out;
        for (const PayloadFile &f : files) {
            if (withFrames || !(f.path.startsWith(QLatin1String("throbber-")) || f.path.startsWith(QLatin1String("frame-"))))
                out << f.path;
        }
        return out;
    }
    static int frames(const QList<PayloadFile> &files, const QString &prefix)
    {
        return int(std::count_if(files.cbegin(), files.cend(), [&](const PayloadFile &f) { return f.path.startsWith(prefix); }));
    }

    std::unique_ptr<QTemporaryDir> m_tmp;
    std::optional<Library> m_library;
    QString m_logo;
    const QSize m_screen{2560, 1440};
};

void TestGenPlymouth::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    QString error;
    m_library = Library::open(m_tmp->filePath(QStringLiteral("lib")), error);
    QVERIFY2(m_library, qPrintable(error));
    const auto logo = m_library->addAsset(QStringLiteral(FIXTURES_DIR "/assets/logo.png"), Asset::Kind::Image, QStringLiteral("Logo"), error);
    QVERIFY2(logo, qPrintable(error));
    m_logo = logo->hash;
}

void TestGenPlymouth::twoStepMatchesGolden()
{
    QStringList problems;
    const QList<PayloadFile> files = buildPlymouthSplash(design(), QStringLiteral("ekpo-night-v1"), *m_library, m_screen, problems);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    QCOMPARE(paths(files, false), (QStringList{QStringLiteral("background.png"), QStringLiteral("bullet.png"), QStringLiteral("ekpo-night-v1.plymouth"),
                                               QStringLiteral("entry.png"), QStringLiteral("lock.png")}));
    QCOMPARE(frames(files, QStringLiteral("throbber-")), 60);
    QVERIFY(paths(files, true).contains(QStringLiteral("throbber-0060.png")));
    // The spinner fits its 3 % × 5 % box: 72 px square at 1440p.
    QCOMPARE(QImage::fromData(fileData(files, QStringLiteral("throbber-0001.png"))).size(), QSize(72, 72));
    QVERIFY(matchesGolden(QStringLiteral("plymouth-two-step.plymouth"), fileData(files, QStringLiteral("ekpo-night-v1.plymouth"))));
    // What the root helper will check before installing it.
    QCOMPARE(validateTheme(ThemeTarget::Plymouth, QStringLiteral("ekpo-night-v1"), files), QStringList());
}

void TestGenPlymouth::drawsTheStillParts()
{
    QStringList problems;
    const QList<PayloadFile> files = buildPlymouthSplash(design(), QStringLiteral("ekpo-night-v1"), *m_library, m_screen, problems);
    const QImage background = QImage::fromData(fileData(files, QStringLiteral("background.png")));
    QCOMPARE(background.size(), m_screen);
    // The gradient runs from the top colour to the bottom one.
    QCOMPARE(background.pixelColor(0, 0), QColor(QStringLiteral("#102030")));
    QVERIFY(background.pixelColor(0, 1439).blue() < 3);
    // The logo (8×8, scaled into 256×144 px, so 144×144 centred) and the text are drawn in.
    const QImage logo(QStringLiteral(FIXTURES_DIR "/assets/logo.png"));
    QCOMPARE(background.pixelColor(1280, 432 + 72).rgb(), logo.pixelColor(4, 4).rgb());
    QCOMPARE(background.pixelColor(1152 + 20, 432 + 72).red(), background.pixelColor(20, 432 + 72).red()); // beside it: background
    bool text = false;
    for (int x = 256; x < 2304 && !text; ++x)
        text = background.pixelColor(x, 1152 + 36).lightness() > 128;
    QVERIFY(text);
    // The dialog images two-step needs, scaled from 1080p.
    QCOMPARE(QImage::fromData(fileData(files, QStringLiteral("entry.png"))).size(), QSize(400, 43));
    QCOMPARE(QImage::fromData(fileData(files, QStringLiteral("lock.png"))).size(), QSize(32, 40));
}

void TestGenPlymouth::scriptForOtherLoops()
{
    // Three frames in a 3 s loop: two-step can't, the script module can.
    Design d = design();
    QJsonArray elements = d.content.value(QStringLiteral("elements")).toArray();
    elements[2] = element(QStringLiteral("frames"), 40, 50, 20, 20, {{QStringLiteral("frames"), QJsonArray{m_logo, m_logo, m_logo}}, {QStringLiteral("seconds"), 3}});
    d.content.insert(QStringLiteral("elements"), elements);
    d.content.insert(QStringLiteral("hideMessages"), false);

    QStringList problems;
    const QList<PayloadFile> files = buildPlymouthSplash(d, QStringLiteral("ekpo-night-v1"), *m_library, m_screen, problems);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    QCOMPARE(paths(files, true), (QStringList{QStringLiteral("background.png"), QStringLiteral("bullet.png"), QStringLiteral("ekpo-night-v1.plymouth"),
                                              QStringLiteral("entry.png"), QStringLiteral("frame-0001.png"), QStringLiteral("frame-0002.png"),
                                              QStringLiteral("frame-0003.png"), QStringLiteral("lock.png"), QStringLiteral("progress-bar.png"),
                                              QStringLiteral("progress-box.png"), QStringLiteral("splash.script")}));
    // Frames that fit are kept byte for byte (palette PNGs stay small).
    QFile logo(QStringLiteral(FIXTURES_DIR "/assets/logo.png"));
    QVERIFY(logo.open(QIODevice::ReadOnly));
    QCOMPARE(fileData(files, QStringLiteral("frame-0002.png")), logo.readAll());
    QVERIFY(matchesGolden(QStringLiteral("plymouth-script.plymouth"), fileData(files, QStringLiteral("ekpo-night-v1.plymouth"))));
    QVERIFY(matchesGolden(QStringLiteral("plymouth-splash.script"), fileData(files, QStringLiteral("splash.script"))));
    QCOMPARE(validateTheme(ThemeTarget::Plymouth, QStringLiteral("ekpo-night-v1"), files), QStringList());
}

void TestGenPlymouth::reportsProblems()
{
    Design d = design();
    QJsonArray elements = d.content.value(QStringLiteral("elements")).toArray();
    elements.append(element(QStringLiteral("spinner"), 0, 0, 1, 1));
    elements.append(element(QStringLiteral("progress"), 0, 0, 1, 1));
    elements.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("video")}});
    elements.append(element(QStringLiteral("image"), 0, 0, 1, 1, {{QStringLiteral("asset"), QStringLiteral("gone")}}));
    d.content.insert(QStringLiteral("elements"), elements);
    d.content.insert(QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("navy")}});

    QStringList problems;
    buildPlymouthSplash(d, QStringLiteral("ekpo-night-v1"), *m_library, m_screen, problems);
    QCOMPARE(problems, (QStringList{
                           QStringLiteral("navy is not a colour."),
                           QStringLiteral("Element 5 is a second animation; a splash has one."),
                           QStringLiteral("Element 6 is a second progress bar; a splash has one."),
                           QStringLiteral("Element 7 has an unknown type “video”."),
                           QStringLiteral("The design uses an image that is no longer in the library."),
                       }));

    // Frames without any, or with a loop of no length.
    d = design();
    elements = d.content.value(QStringLiteral("elements")).toArray();
    elements[2] = element(QStringLiteral("frames"), 40, 50, 20, 20, {{QStringLiteral("seconds"), 0}});
    d.content.insert(QStringLiteral("elements"), elements);
    problems.clear();
    buildPlymouthSplash(d, QStringLiteral("ekpo-night-v1"), *m_library, m_screen, problems);
    QCOMPARE(problems, (QStringList{QStringLiteral("Element 3 has no frames."), QStringLiteral("Element 3 has no valid loop length.")}));
}


// 90°: from left to right; 45°: the corners get exactly the end colours, as in CSS.
static void checkAngles(const std::function<QImage(double)> &render)
{
    const auto red = [](const QColor &c) { return c.red() > 245 && c.blue() < 10; };
    const auto blue = [](const QColor &c) { return c.blue() > 245 && c.red() < 10; };
    QImage image = render(90);
    QVERIFY(!image.isNull());
    const int w = image.width(), h = image.height();
    QVERIFY2(red(image.pixelColor(0, h / 2)), qPrintable(image.pixelColor(0, h / 2).name()));
    QVERIFY2(blue(image.pixelColor(w - 1, h / 2)), qPrintable(image.pixelColor(w - 1, h / 2).name()));
    QVERIFY(red(image.pixelColor(0, 0)) && red(image.pixelColor(0, h - 1))); // the same down a column
    image = render(45);
    QVERIFY2(red(image.pixelColor(0, h - 1)), qPrintable(image.pixelColor(0, h - 1).name()));
    QVERIFY2(blue(image.pixelColor(w - 1, 0)), qPrintable(image.pixelColor(w - 1, 0).name()));
    const QColor middle = image.pixelColor(w / 2, h / 2);
    QVERIFY2(middle.red() > 100 && middle.blue() > 100, qPrintable(middle.name()));
}

void TestGenPlymouth::gradientAngle()
{
    checkAngles([this](double angle) {
        Design d = design();
        d.content.insert(QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#ff0000")}, {QStringLiteral("color2"), QStringLiteral("#0000ff")},
                                                                   {QStringLiteral("angle"), angle}});
        QStringList problems;
        for (const PayloadFile &f : buildPlymouthSplash(d, QStringLiteral("ekpo-night-v1"), *m_library, m_screen, problems)) {
            if (f.path == QLatin1String("background.png"))
                return QImage::fromData(f.data);
        }
        return QImage();
    });
}

EKPO_GUI_TEST_MAIN(TestGenPlymouth)
#include "tst_genplymouth.moc"
