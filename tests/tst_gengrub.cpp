#include "gengrub.h"
#include "golden.h"
#include "testmain.h"
#include "validate.h"

#include <functional>
#include <QImage>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

class TestGenGrub : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void matchesGoldenAndValidates();
    void menuWidensForLongEntries();
    void tooLongForAnyWidth();
    void selectionStyles();
    void progressStyles();
    void textEffects();
    void findsClippedEntries();
    void rendersPreview();
    void gradientBackground();
    void gradientAngle();
    void builtinIcons();
    void reportsProblems();

private:
    static QJsonObject element(const QString &type, double x, double y, double w, double h, QJsonObject more = {})
    {
        more.insert(QStringLiteral("type"), type);
        more.insert(QStringLiteral("x"), x);
        more.insert(QStringLiteral("y"), y);
        more.insert(QStringLiteral("width"), w);
        more.insert(QStringLiteral("height"), h);
        return more;
    }
    Design design(double menuWidth = 40) const
    {
        Design d;
        d.id = QStringLiteral("00000000-0000-0000-0000-0000000000d3");
        d.stage = Stage::BootMenu;
        d.name = QStringLiteral("Night \"Menu\"");
        d.version = 2;
        d.content = {
            {QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#102030")}}},
            {QStringLiteral("icons"), QJsonObject{{QStringLiteral("kubuntu"), m_logo}}},
            {QStringLiteral("elements"),
             QJsonArray{
                 element(QStringLiteral("image"), 45, 8, 10, 10, {{QStringLiteral("asset"), m_logo}}),
                 element(QStringLiteral("menu"), 50 - menuWidth / 2, 30, menuWidth, 45,
                         {{QStringLiteral("font"), m_font}, {QStringLiteral("size"), 2.2}, {QStringLiteral("color"), QStringLiteral("#c0c0c0")},
                          {QStringLiteral("selectedColor"), QStringLiteral("#ffffff")},
                          {QStringLiteral("selection"), QJsonObject{{QStringLiteral("style"), QStringLiteral("rounded")}, {QStringLiteral("color"), QStringLiteral("#3daee9")}}}}),
                 element(QStringLiteral("countdown"), 30, 80, 40, 4, {{QStringLiteral("font"), m_font}, {QStringLiteral("text"), QStringLiteral("Starting in %d s")}, {QStringLiteral("size"), 1.6}}),
                 element(QStringLiteral("progress"), 40, 85, 20, 0.5, {{QStringLiteral("color"), QStringLiteral("#3daee9")}}),
                 element(QStringLiteral("text"), 10, 92, 80, 4, {{QStringLiteral("font"), m_font}, {QStringLiteral("text"), QStringLiteral("Enter: start · e: edit · c: command line")}, {QStringLiteral("size"), 1.4}}),
             }},
        };
        return d;
    }
    QStringList entries() const
    {
        return {QStringLiteral("Kubuntu"), QStringLiteral("Advanced options for Kubuntu"), QStringLiteral("Kubuntu, with Linux 7.0.0-42-generic (recovery mode)"),
                QStringLiteral("UEFI Firmware Settings")};
    }
    static QByteArray fileData(const QList<PayloadFile> &files, const QString &path)
    {
        for (const PayloadFile &f : files) {
            if (f.path == path)
                return f.data;
        }
        return {};
    }

    std::unique_ptr<QTemporaryDir> m_tmp;
    std::optional<Library> m_library;
    QString m_logo;
    QString m_font;
    const QSize m_screen{2560, 1440};
};

void TestGenGrub::initTestCase()
{
    if (QStandardPaths::findExecutable(QStringLiteral("grub-mkfont")).isEmpty())
        QSKIP("grub-mkfont isn't installed (grub-common)");
}

void TestGenGrub::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    QString error;
    m_library = Library::open(m_tmp->filePath(QStringLiteral("lib")), error);
    QVERIFY2(m_library, qPrintable(error));
    const auto logo = m_library->addAsset(QStringLiteral(FIXTURES_DIR "/assets/logo.png"), Asset::Kind::Image, QStringLiteral("Logo"), error);
    QVERIFY2(logo, qPrintable(error));
    m_logo = logo->hash;
    const auto font = m_library->addAsset(QStringLiteral("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"), Asset::Kind::Font, QStringLiteral("Noto Sans"), error);
    QVERIFY2(font, qPrintable(error));
    m_font = font->hash;
}

void TestGenGrub::matchesGoldenAndValidates()
{
    QStringList problems;
    GrubLayout layout;
    const QList<PayloadFile> files = buildBootMenu(design(), *m_library, m_screen, entries(), problems, &layout);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    QStringList paths;
    for (const PayloadFile &f : files)
        paths << f.path;
    // One .pf2 per size: menu 31 px, countdown 23 px, help line 20 px.
    QCOMPARE(paths, (QStringList{QStringLiteral("f1-20.pf2"), QStringLiteral("f1-23.pf2"), QStringLiteral("f1-31.pf2"), QStringLiteral("icons/kubuntu.png"),
                                 QStringLiteral("image-1.png"), QStringLiteral("item_c.png"), QStringLiteral("item_e.png"), QStringLiteral("item_n.png"),
                                 QStringLiteral("item_ne.png"), QStringLiteral("item_nw.png"), QStringLiteral("item_s.png"), QStringLiteral("item_se.png"),
                                 QStringLiteral("item_sw.png"), QStringLiteral("item_w.png"), QStringLiteral("select_c.png"), QStringLiteral("select_e.png"), QStringLiteral("select_n.png"),
                                 QStringLiteral("select_ne.png"), QStringLiteral("select_nw.png"), QStringLiteral("select_s.png"), QStringLiteral("select_se.png"),
                                 QStringLiteral("select_sw.png"), QStringLiteral("select_w.png"), QStringLiteral("theme.txt")}));
    QVERIFY(layout.fits);
    QCOMPARE(layout.itemFontPixels, 31);
    QVERIFY(matchesGolden(QStringLiteral("grub-theme.txt"), fileData(files, QStringLiteral("theme.txt"))));
    // What the root helper checks before installing it (src/common/validate.cpp).
    QCOMPARE(validateTheme(ThemeTarget::Grub, QStringLiteral("ekpo-night-v1"), files), QStringList());
    // The logo is scaled to the size it is shown at: 144 px square in a 256×144 box.
    QCOMPARE(QImage::fromData(fileData(files, QStringLiteral("image-1.png"))).size(), QSize(144, 144));
}

void TestGenGrub::menuWidensForLongEntries()
{
    QStringList problems;
    GrubLayout layout;
    buildBootMenu(design(10), *m_library, m_screen, entries(), problems, &layout); // 256 px wide: too narrow
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    QCOMPARE(layout.longestEntry, QStringLiteral("Kubuntu, with Linux 7.0.0-42-generic (recovery mode)"));
    QVERIFY(layout.menu.width() >= layout.neededWidth);
    QVERIFY(layout.neededWidth > 256);
    QVERIFY(qAbs(layout.menu.x() + layout.menu.width() / 2.0 - 1280) <= 0.5); // around its centre
    QVERIFY(layout.fits);

    // A wide menu stays as designed.
    buildBootMenu(design(80), *m_library, m_screen, entries(), problems, &layout);
    QCOMPARE(layout.menu.width(), 2048);
}

void TestGenGrub::tooLongForAnyWidth()
{
    QStringList problems;
    GrubLayout layout;
    buildBootMenu(design(), *m_library, m_screen, {QString(200, QLatin1Char('W'))}, problems, &layout);
    QVERIFY(!layout.fits);
    QCOMPARE(layout.menu, QRect(0, 432, 2560, 648)); // as wide as the screen, still too narrow
}

void TestGenGrub::selectionStyles()
{
    for (const char *style : {"flat", "rounded", "glow", "outline", "underline", "bar"}) {
        Design d = design();
        QJsonArray elements = d.content.value(QStringLiteral("elements")).toArray();
        QJsonObject menu = elements[1].toObject();
        menu.insert(QStringLiteral("selection"), QJsonObject{{QStringLiteral("style"), QLatin1String(style)}, {QStringLiteral("color"), QStringLiteral("#ff0000")}});
        elements[1] = menu;
        d.content.insert(QStringLiteral("elements"), elements);
        QStringList problems;
        const QList<PayloadFile> files = buildBootMenu(d, *m_library, m_screen, entries(), problems);
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        // GRUB stretches the 1-pixel centre and edges; the corners are the border's size.
        const QImage centre = QImage::fromData(fileData(files, QStringLiteral("select_c.png")));
        const QImage corner = QImage::fromData(fileData(files, QStringLiteral("select_nw.png")));
        QCOMPARE(centre.size(), QSize(1, 1));
        QCOMPARE(QImage::fromData(fileData(files, QStringLiteral("select_w.png"))).height(), 1);
        QCOMPARE(corner.width(), QImage::fromData(fileData(files, QStringLiteral("select_e.png"))).width());
        // The other entries' boxes: as big, and clear.
        const QImage itemCorner = QImage::fromData(fileData(files, QStringLiteral("item_nw.png")));
        QCOMPARE(itemCorner.size(), corner.size());
        QCOMPARE(qAlpha(itemCorner.pixel(0, 0)), 0);
        const QLatin1String name(style);
        if (name == QLatin1String("outline") || name == QLatin1String("underline")) {
            QCOMPARE(qAlpha(centre.pixel(0, 0)), 0); // no fill
            const QImage edge = QImage::fromData(fileData(files, name == QLatin1String("outline") ? QStringLiteral("select_n.png") : QStringLiteral("select_s.png")));
            QVERIFY(edge.pixelColor(0, 1).red() > 200 && qAlpha(edge.pixel(0, 1)) > 200); // the line along the edge
        } else {
            QCOMPARE(centre.pixelColor(0, 0).red(), 255);
        }
        if (name == QLatin1String("flat"))
            QCOMPARE(corner.size(), QSize(1, 1));
        else if (name == QLatin1String("bar")) // full colour on the left, faint elsewhere
            QVERIFY(qAlpha(QImage::fromData(fileData(files, QStringLiteral("select_w.png"))).pixel(0, 0)) == 255 && qAlpha(centre.pixel(0, 0)) < 128);
        else if (name != QLatin1String("underline")) // the outermost corner pixel stays clear
            QVERIFY(corner.width() > 1 && qAlpha(corner.pixel(0, 0)) < 128);
    }
}

void TestGenGrub::progressStyles()
{
    // The countdown bar (element 3) as a ring of dots, and with round ends.
    for (const char *style : {"ring", "rounded"}) {
        Design d = design();
        QJsonArray elements = d.content.value(QStringLiteral("elements")).toArray();
        QJsonObject bar = elements[3].toObject();
        QCOMPARE(bar.value(QStringLiteral("type")).toString(), QStringLiteral("progress"));
        bar.insert(QStringLiteral("style"), QLatin1String(style));
        bar.insert(QStringLiteral("height"), 10);
        elements[3] = bar;
        d.content.insert(QStringLiteral("elements"), elements);
        QStringList problems;
        const QList<PayloadFile> files = buildBootMenu(d, *m_library, m_screen, entries(), problems);
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        const QString theme = QString::fromUtf8(fileData(files, QStringLiteral("theme.txt")));
        QCOMPARE(validateTheme(ThemeTarget::Grub, QStringLiteral("ekpo-night-v1"), files), QStringList());
        const QImage image = renderBootMenu(files, m_screen, entries());
        if (const QString save = qEnvironmentVariable("EKPO_SAVE_PREVIEW"); !save.isEmpty())
            image.save(QString(save).replace(QLatin1String(".png"), QStringLiteral("-%1.png").arg(QLatin1String(style))));
        if (QLatin1String(style) == QLatin1String("ring")) {
            QVERIFY(theme.contains(QLatin1String("+ circular_progress {\n    id = \"__timeout__\"")));
            QVERIFY(theme.contains(QLatin1String("ticks_disappear = true")));
            QVERIFY(!theme.contains(QLatin1String("progress_bar")));
            const QImage tick = QImage::fromData(fileData(files, QStringLiteral("progress-1-tick.png")));
            QCOMPARE(tick.pixelColor(tick.width() / 2, tick.height() / 2), QColor(QStringLiteral("#3daee9")));
            // Half the time gone: the dots right of the top are gone, those on the left stay.
            static const QRegularExpression place(QStringLiteral("circular_progress \\{[^}]*left = (\\d+)\\n    top = (\\d+)\\n    width = (\\d+)"));
            const auto m = place.match(theme);
            QVERIFY(m.hasMatch());
            const int left = m.captured(1).toInt(), top = m.captured(2).toInt(), side = m.captured(3).toInt();
            const int radius = side / 2 - tick.width() / 2 - 1;
            const auto dotAt = [&](int x) {
                for (int y = top + side / 2 - 2 * tick.height(); y <= top + side / 2 + 2 * tick.height(); ++y)
                    if (image.pixelColor(x, y) == QColor(QStringLiteral("#3daee9")))
                        return true;
                return false;
            };
            QVERIFY(dotAt(left + side / 2 - radius));
            QVERIFY(!dotAt(left + side / 2 + radius));
        } else {
            QVERIFY(theme.contains(QLatin1String("bar_style = \"progress-1-track_*.png\"")));
            QVERIFY(theme.contains(QLatin1String("highlight_overlay = true")));
            const QImage corner = QImage::fromData(fileData(files, QStringLiteral("progress-1-fill_nw.png")));
            QVERIFY(corner.width() > 1 && qAlpha(corner.pixel(0, 0)) < 128); // round
        }
    }
}

void TestGenGrub::textEffects()
{
    // GRUB's labels are plain: text with an outline is drawn into background.png.
    Design d = design();
    QJsonArray elements = d.content.value(QStringLiteral("elements")).toArray();
    QJsonObject help = elements[4].toObject();
    QCOMPARE(help.value(QStringLiteral("type")).toString(), QStringLiteral("text"));
    help.insert(QStringLiteral("outline"), QStringLiteral("#ff0000"));
    help.insert(QStringLiteral("size"), 4);
    elements[4] = help;
    d.content.insert(QStringLiteral("elements"), elements);
    QStringList problems;
    const QList<PayloadFile> files = buildBootMenu(d, *m_library, m_screen, entries(), problems);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    const QString theme = QString::fromUtf8(fileData(files, QStringLiteral("theme.txt")));
    QVERIFY(!theme.contains(QLatin1String("Enter: start")));
    QVERIFY(theme.contains(QLatin1String("desktop-image: \"background.png\"")));
    QCOMPARE(validateTheme(ThemeTarget::Grub, QStringLiteral("ekpo-night-v1"), files), QStringList());
    const QImage background = QImage::fromData(fileData(files, QStringLiteral("background.png")));
    QCOMPARE(background.size(), m_screen);
    QCOMPARE(background.pixelColor(5, 5), QColor(QStringLiteral("#102030")));
    int red = 0, white = 0;
    for (int y = int(1440 * 0.92); y < int(1440 * 0.96); ++y)
        for (int x = 256; x < 2304; ++x) {
            const QColor c = background.pixelColor(x, y);
            red += c.red() > 200 && c.green() < 80;
            white += c.lightness() > 230;
        }
    QVERIFY(red > 50);
    QVERIFY(white > 50);
}

void TestGenGrub::findsClippedEntries()
{
    // The theme as built fits the expected entries; a longer one from the real grub.cfg doesn't.
    QStringList problems;
    const QList<PayloadFile> files = buildBootMenu(design(), *m_library, m_screen, entries(), problems);
    QVERIFY(clippedEntries(files, entries()).isEmpty());
    const QString custom = QStringLiteral("My custom entry with a very long title that nobody expected when the theme was built, far too long");
    QCOMPARE(clippedEntries(files, entries() << custom), QStringList{custom});
    QVERIFY(clippedEntries({}, {custom}).isEmpty()); // no theme to measure against
}

void TestGenGrub::rendersPreview()
{
    QStringList problems;
    GrubLayout layout;
    const QList<PayloadFile> files = buildBootMenu(design(), *m_library, m_screen, entries(), problems, &layout);
    const QImage image = renderBootMenu(files, m_screen, entries());
    if (const QString save = qEnvironmentVariable("EKPO_SAVE_PREVIEW"); !save.isEmpty())
        image.save(save);
    QCOMPARE(image.size(), m_screen);
    QCOMPARE(image.pixelColor(5, 5), QColor(QStringLiteral("#102030"))); // desktop-color
    // The first entry is selected: the selection's colour fills its row's middle, below
    // the box's top border (14 px for a 55 px rounded entry).
    const int firstRow = layout.menu.y() + 10 + 14 + 55 / 2;
    QCOMPARE(image.pixelColor(layout.menu.right() - 40, firstRow), QColor(QStringLiteral("#3daee9")));
    // Text in the item colour somewhere on the second row (not selected).
    bool text = false;
    for (int x = layout.menu.x(); x < layout.menu.right() && !text; ++x)
        for (int y = firstRow + 55; y < firstRow + 55 + 30 && !text; ++y)
            text = image.pixelColor(x, y).red() > 150;
    QVERIFY(text);
}

void TestGenGrub::gradientBackground()
{
    // GRUB fills with one colour: the gradient is drawn into background.png.
    Design d = design();
    d.content.insert(QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#ff0000")}, {QStringLiteral("color2"), QStringLiteral("#0000ff")}});
    QStringList problems;
    const QList<PayloadFile> files = buildBootMenu(d, *m_library, m_screen, entries(), problems);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    QByteArray data, theme;
    for (const PayloadFile &f : files) {
        if (f.path == QLatin1String("background.png"))
            data = f.data;
        else if (f.path == QLatin1String("theme.txt"))
            theme = f.data;
    }
    const QImage background = QImage::fromData(data);
    QCOMPARE(background.size(), m_screen);
    QCOMPARE(background.pixelColor(0, 0), QColor(QStringLiteral("#ff0000")));
    QVERIFY(background.pixelColor(0, 1439).blue() > 250 && background.pixelColor(0, 1439).red() < 5);
    QVERIFY(theme.contains("desktop-image: \"background.png\""));
    QCOMPARE(validateTheme(ThemeTarget::Grub, QStringLiteral("ekpo-night-v1"), files), QStringList());
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

void TestGenGrub::gradientAngle()
{
    checkAngles([this](double angle) {
        Design d = design();
        d.content.insert(QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#ff0000")}, {QStringLiteral("color2"), QStringLiteral("#0000ff")},
                                                                   {QStringLiteral("angle"), angle}});
        QStringList problems;
        for (const PayloadFile &f : buildBootMenu(d, *m_library, m_screen, entries(), problems)) {
            if (f.path == QLatin1String("background.png"))
                return QImage::fromData(f.data);
        }
        return QImage();
    });
}

void TestGenGrub::builtinIcons()
{
    QCOMPARE(builtinIconFor(QStringLiteral("kubuntu")), QStringLiteral("os"));
    QCOMPARE(builtinIconFor(QStringLiteral("efi")), QStringLiteral("firmware"));
    QCOMPARE(builtinIconFor(QString()), QString()); // "Advanced options": no icon
    Design d = design();
    d.content.insert(QStringLiteral("icons"), QJsonObject{{QStringLiteral("kubuntu"), QStringLiteral("builtin:os")}, {QStringLiteral("windows"), QStringLiteral("builtin:windows")},
                                                          {QStringLiteral("efi"), QStringLiteral("builtin:firmware")}, {QStringLiteral("memtest86"), QStringLiteral("builtin:memory")}});
    QStringList problems;
    const QList<PayloadFile> files = buildBootMenu(d, *m_library, m_screen, entries(), problems);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    if (const QString save = qEnvironmentVariable("EKPO_SAVE_PREVIEW"); !save.isEmpty())
        renderBootMenu(files, m_screen, entries() << QStringLiteral("Windows Boot Manager (on /dev/sda1)") << QStringLiteral("Memory test (memtest86+x64.efi)")).save(save);
    for (const char *c : {"kubuntu", "windows", "efi", "memtest86"}) {
        QImage icon;
        for (const PayloadFile &f : files) {
            if (f.path == QStringLiteral("icons/%1.png").arg(QLatin1String(c)))
                icon = QImage::fromData(f.data);
        }
        // As tall as the menu's text line, drawn in the menu's text colour.
        QVERIFY2(icon.width() > 30 && icon.width() == icon.height(), c);
        bool ink = false;
        for (int y = 0; y < icon.height() && !ink; ++y)
            for (int x = 0; x < icon.width() && !ink; ++x)
                ink = icon.pixelColor(x, y) == QColor(QStringLiteral("#c0c0c0"));
        QVERIFY2(ink, c);
    }
    d.content.insert(QStringLiteral("icons"), QJsonObject{{QStringLiteral("kubuntu"), QStringLiteral("builtin:penguin")}});
    buildBootMenu(d, *m_library, m_screen, entries(), problems);
    QCOMPARE(problems, QStringList{QStringLiteral("There is no built-in icon “penguin”.")});
}

void TestGenGrub::reportsProblems()
{
    Design d = design();
    QJsonArray elements = d.content.value(QStringLiteral("elements")).toArray();
    elements.append(elements[1]); // a second menu
    elements.append(element(QStringLiteral("clock"), 0, 0, 1, 1));
    elements.append(element(QStringLiteral("text"), 0, 0, 1, 1, {{QStringLiteral("font"), QStringLiteral("gone")}}));
    d.content.insert(QStringLiteral("elements"), elements);
    d.content.insert(QStringLiteral("icons"), QJsonObject{{QStringLiteral("Bad Class"), m_logo}});
    QStringList problems;
    buildBootMenu(d, *m_library, m_screen, entries(), problems);
    QCOMPARE(problems, (QStringList{QStringLiteral("“Bad Class” isn't a menu entry class."), QStringLiteral("Element 6 is a second menu; a boot menu has one."),
                                    QStringLiteral("Element 7 has an unknown type “clock”."),
                                    QStringLiteral("The design uses a font that is no longer in the library.")}));

    d.content.insert(QStringLiteral("elements"), QJsonArray{});
    problems.clear();
    buildBootMenu(d, *m_library, m_screen, entries(), problems);
    QCOMPARE(problems, QStringList{QStringLiteral("A boot menu design needs a menu.")});
}

EKPO_GUI_TEST_MAIN(TestGenGrub)
#include "tst_gengrub.moc"
