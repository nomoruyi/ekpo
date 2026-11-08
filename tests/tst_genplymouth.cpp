#include "gencommon.h"
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
    void spinnerStyles();
    void progressStyles();
    void textEffects();
    void movingImages();
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

void TestGenPlymouth::spinnerStyles()
{
    const auto build = [&](const QJsonObject &more, QStringList &problems) {
        Design d = design();
        QJsonArray elements = d.content.value(QStringLiteral("elements")).toArray();
        elements[2] = element(QStringLiteral("spinner"), 45, 45, 10, 10, more);
        d.content.insert(QStringLiteral("elements"), elements);
        return buildPlymouthSplash(d, QStringLiteral("ekpo-night-v1"), *m_library, m_screen, problems);
    };
    for (const QString &style : ::spinnerStyles) {
        for (const char *speed : {"slow", "normal", "fast"}) {
            QStringList problems;
            QJsonObject more{{QStringLiteral("style"), style}, {QStringLiteral("speed"), QLatin1String(speed)}, {QStringLiteral("color"), QStringLiteral("#ffffff")}};
            if (style == QLatin1String("image"))
                more.insert(QStringLiteral("asset"), m_logo);
            const QList<PayloadFile> files = build(more, problems);
            QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
            QCOMPARE(frames(files, QStringLiteral("throbber-")), 60);
            // Each frame draws something; the loop is 2 s: one turn slow, two normal, three fast.
            const QImage first = QImage::fromData(fileData(files, QStringLiteral("throbber-0001.png")));
            const QImage half = QImage::fromData(fileData(files, QStringLiteral("throbber-0031.png")));
            QCOMPARE(first.size(), QSize(144, 144));
            if (const QString save = qEnvironmentVariable("EKPO_SAVE_PREVIEW"); !save.isEmpty() && QLatin1String(speed) == QLatin1String("slow"))
                QImage::fromData(fileData(files, QStringLiteral("throbber-0005.png"))).save(QString(save).replace(QLatin1String(".png"), QLatin1Char('-') + style + QLatin1String(".png")));
            int drawn = 0;
            for (int y = 0; y < first.height(); ++y)
                for (int x = 0; x < first.width(); ++x)
                    drawn += qAlpha(first.pixel(x, y)) > 128;
            QVERIFY2(drawn > 100, qPrintable(style));
            if (QLatin1String(speed) == QLatin1String("normal"))
                QCOMPARE(half, first);
            else if (style != QLatin1String("pulse") && style != QLatin1String("image")) // the pulse and the square logo look the same half a cycle on
                QVERIFY2(half != first, qPrintable(style + QLatin1Char(' ') + QLatin1String(speed)));
        }
    }
    QStringList problems;
    build({{QStringLiteral("style"), QStringLiteral("wobble")}, {QStringLiteral("speed"), QStringLiteral("warp")}}, problems);
    build({{QStringLiteral("style"), QStringLiteral("image")}}, problems);
    QCOMPARE(problems, (QStringList{QStringLiteral("Element 3 has an unknown spinner style “wobble”."), QStringLiteral("Element 3 has an unknown speed “warp”."),
                                    QStringLiteral("Element 3 has no image to turn.")}));
}

void TestGenPlymouth::progressStyles()
{
    // Styled bars need the script module; its images are put together by splash.script.
    for (const char *style : {"rounded", "glow", "segmented"}) {
        Design d = design();
        QJsonArray elements = d.content.value(QStringLiteral("elements")).toArray();
        QJsonObject bar = elements[3].toObject();
        bar.insert(QStringLiteral("style"), QLatin1String(style));
        bar.insert(QStringLiteral("height"), 1);
        elements[3] = bar;
        d.content.insert(QStringLiteral("elements"), elements);
        QStringList problems;
        PlymouthLayout layout;
        const QList<PayloadFile> files = buildPlymouthSplash(d, QStringLiteral("ekpo-night-v1"), *m_library, m_screen, problems, &layout);
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        QCOMPARE(layout.module, QStringLiteral("script"));
        QCOMPARE(layout.progressStyle, QLatin1String(style));
        QCOMPARE(frames(files, QStringLiteral("frame-")), 60); // the spinner moves to the script too
        const QString script = QString::fromUtf8(fileData(files, QStringLiteral("splash.script")));
        QVERIFY(script.contains(QLatin1String("Plymouth.SetBootProgressFunction(boot_progress_callback);")));
        QVERIFY(script.contains(QStringLiteral("# Plymouth's script module, for a %1 progress bar.").arg(QLatin1String(style))));
        const QImage box = QImage::fromData(fileData(files, QStringLiteral("progress-box.png")));
        QCOMPARE(box.size(), QSize(512, 14));
        const ProgressShape &shape = layout.progressShape;
        if (QLatin1String(style) == QLatin1String("segmented")) {
            QCOMPARE(shape.segments, 10);
            QCOMPARE(shape.gap, 7);
            QCOMPARE(shape.segmentWidth, (512 - 9 * 7) / 10);
            QCOMPARE(QImage::fromData(fileData(files, QStringLiteral("progress-segment.png"))).size(), QSize(shape.segmentWidth, 14));
            QVERIFY(script.contains(QLatin1String("global.progress_segments[9].SetPosition(global.progress_x + 459, global.progress_y, 6);")));
            QCOMPARE(qAlpha(box.pixel(shape.segmentWidth + 1, 7)), 0); // the gap
        } else {
            QCOMPARE(shape.caps, 7);
            QCOMPARE(shape.margin, QLatin1String(style) == QLatin1String("glow") ? 14 : 0);
            const QImage left = QImage::fromData(fileData(files, QStringLiteral("progress-left.png")));
            const QImage middle = QImage::fromData(fileData(files, QStringLiteral("progress-middle.png")));
            QCOMPARE(left.size(), QSize(7 + shape.margin, 14 + 2 * shape.margin));
            QCOMPARE(middle.size(), QSize(1, 14 + 2 * shape.margin));
            QCOMPARE(middle.pixelColor(0, shape.margin + 7), QColor(QStringLiteral("#ff8800")));
            QVERIFY(qAlpha(left.pixel(shape.margin, shape.margin)) < 128); // a round end
            QCOMPARE(qAlpha(box.pixel(0, 0)) < 128, true);
            QVERIFY(script.contains(QStringLiteral("width = Math.Int(512 * progress);")));
        }
    }
    QStringList problems;
    Design d = design();
    QJsonArray elements = d.content.value(QStringLiteral("elements")).toArray();
    QJsonObject bar = elements[3].toObject();
    bar.insert(QStringLiteral("style"), QStringLiteral("zigzag"));
    elements[3] = bar;
    d.content.insert(QStringLiteral("elements"), elements);
    buildPlymouthSplash(d, QStringLiteral("ekpo-night-v1"), *m_library, m_screen, problems);
    QCOMPARE(problems, QStringList{QStringLiteral("Element 4 has an unknown progress style “zigzag”.")});
}

void TestGenPlymouth::textEffects()
{
    // Black text with a red glow, a green shadow and a blue outline, drawn into background.png.
    Design d = design();
    d.content.insert(QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#ffffff")}});
    QJsonArray elements = d.content.value(QStringLiteral("elements")).toArray();
    elements[1] = element(QStringLiteral("text"), 0, 25, 100, 50,
                          {{QStringLiteral("text"), QStringLiteral("IIII")}, {QStringLiteral("size"), 20}, {QStringLiteral("color"), QStringLiteral("#000000")},
                           {QStringLiteral("glow"), QStringLiteral("#ff0000")}, {QStringLiteral("shadow"), QStringLiteral("#00ff00")},
                           {QStringLiteral("outline"), QStringLiteral("#0000ff")}});
    d.content.insert(QStringLiteral("elements"), elements);
    QStringList problems;
    const QList<PayloadFile> files = buildPlymouthSplash(d, QStringLiteral("ekpo-night-v1"), *m_library, m_screen, problems);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    const QImage image = QImage::fromData(fileData(files, QStringLiteral("background.png")));
    const auto seen = [&](const std::function<bool(const QColor &)> &match) {
        for (int y = 0; y < image.height(); y += 2)
            for (int x = 0; x < image.width(); x += 2)
                if (match(image.pixelColor(x, y)))
                    return true;
        return false;
    };
    QVERIFY(seen([](const QColor &c) { return c.red() > 200 && c.green() < 150 && c.blue() < 150; }));
    QVERIFY(seen([](const QColor &c) { return c.green() > 200 && c.red() < 100 && c.blue() < 100; }));
    QVERIFY(seen([](const QColor &c) { return c.blue() > 200 && c.red() < 100 && c.green() < 100; }));
    QVERIFY(seen([](const QColor &c) { return c.lightness() < 30; }));
}

void TestGenPlymouth::movingImages()
{
    // A moving logo gets its own file and moves in splash.script; a still one stays in background.png.
    for (const QString &animation : ::imageAnimations) {
        Design d = design();
        QJsonArray elements = d.content.value(QStringLiteral("elements")).toArray();
        QJsonObject logo = elements[0].toObject();
        logo.insert(QStringLiteral("animation"), animation);
        elements[0] = logo;
        d.content.insert(QStringLiteral("elements"), elements);
        QStringList problems;
        PlymouthLayout layout;
        const QList<PayloadFile> files = buildPlymouthSplash(d, QStringLiteral("ekpo-night-v1"), *m_library, m_screen, problems, &layout);
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        QCOMPARE(layout.module, QStringLiteral("script"));
        QCOMPARE(layout.images.size(), 1);
        QCOMPARE(layout.images[0].animation, animation);
        QCOMPARE(layout.images[0].centre, QPointF(0.5, 0.35));
        const QImage image = QImage::fromData(fileData(files, QStringLiteral("image-1.png")));
        // The logo fitted to 10 % of the screen's height (144 px); spinning: on a square with room for its corners.
        QCOMPARE(image.size(), animation == QLatin1String("spin") ? QSize(204, 204) : QSize(144, 144));
        const QString script = QString::fromUtf8(fileData(files, QStringLiteral("splash.script")));
        QVERIFY(script.contains(QLatin1String("global.image1 = Image(\"image-1.png\");")));
        QVERIFY(script.contains(QLatin1String("  images_tick(global.ticks / 50);")));
        QVERIFY(script.contains(QLatin1String("fun images_tick (t)")));
        // Not in the background: the logo's middle shows the gradient there.
        const QImage background = QImage::fromData(fileData(files, QStringLiteral("background.png")));
        QVERIFY(background.pixelColor(1280, 504) != QImage::fromData(fileData(files, QStringLiteral("image-1.png"))).pixelColor(image.width() / 2, image.height() / 2));
    }
    Design d = design();
    QJsonArray elements = d.content.value(QStringLiteral("elements")).toArray();
    QJsonObject logo = elements[0].toObject();
    logo.insert(QStringLiteral("animation"), QStringLiteral("wobble"));
    elements[0] = logo;
    d.content.insert(QStringLiteral("elements"), elements);
    QStringList problems;
    buildPlymouthSplash(d, QStringLiteral("ekpo-night-v1"), *m_library, m_screen, problems);
    QCOMPARE(problems, QStringList{QStringLiteral("Element 1 has an unknown animation “wobble”.")});
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
