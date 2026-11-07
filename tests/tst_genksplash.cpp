#include "genksplash.h"
#include "golden.h"
#include "testmain.h"

#include <algorithm>

#include <functional>
#include <QQuickView>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>

class TestGenKSplash : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void matchesGoldenFiles();
    void loadsInQtQuick();
    void reportsProblems();
    void linksAssetsForPreview();
    void gradientAngle();

private:
    // A video, background image, logo, two texts (one with a bundled font), a spinner.
    Design design(bool withFont) const
    {
        QJsonArray elements{
            QJsonObject{{QStringLiteral("type"), QStringLiteral("video")}, {QStringLiteral("asset"), m_clip}, {QStringLiteral("fill"), QStringLiteral("fit")},
                        {QStringLiteral("x"), 0}, {QStringLiteral("y"), 0}, {QStringLiteral("width"), 100}, {QStringLiteral("height"), 100}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("image")}, {QStringLiteral("asset"), m_logo},
                        {QStringLiteral("x"), 40}, {QStringLiteral("y"), 30}, {QStringLiteral("width"), 20}, {QStringLiteral("height"), 20}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), QStringLiteral("Say \"hi\"\nto Ekpo")},
                        {QStringLiteral("size"), 4}, {QStringLiteral("color"), QStringLiteral("#FFEEDD")}, {QStringLiteral("align"), QStringLiteral("left")},
                        {QStringLiteral("x"), 10}, {QStringLiteral("y"), 60}, {QStringLiteral("width"), 80}, {QStringLiteral("height"), 10}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), QStringLiteral("Starting")},
                        {QStringLiteral("x"), 0}, {QStringLiteral("y"), 90}, {QStringLiteral("width"), 100}, {QStringLiteral("height"), 5}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("spinner")}, {QStringLiteral("color"), QStringLiteral("#80ffffff")},
                        {QStringLiteral("x"), 47.5}, {QStringLiteral("y"), 75}, {QStringLiteral("width"), 5}, {QStringLiteral("height"), 5}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("progress")}, {QStringLiteral("color"), QStringLiteral("#3daee9")},
                        {QStringLiteral("x"), 40}, {QStringLiteral("y"), 85}, {QStringLiteral("width"), 20}, {QStringLiteral("height"), 0.5}},
            QJsonObject{{QStringLiteral("type"), QStringLiteral("frames")}, {QStringLiteral("frames"), QJsonArray{m_logo, m_logo}}, {QStringLiteral("seconds"), 1},
                        {QStringLiteral("x"), 5}, {QStringLiteral("y"), 5}, {QStringLiteral("width"), 10}, {QStringLiteral("height"), 10}},
        };
        if (withFont) {
            QJsonObject text = elements[2].toObject();
            text.insert(QStringLiteral("font"), m_font);
            elements[2] = text;
        }
        Design d;
        d.id = QStringLiteral("00000000-0000-0000-0000-0000000000d1");
        d.stage = Stage::PlasmaSplash;
        d.name = QStringLiteral("Dawn");
        d.version = 3;
        d.content = {
            {QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#102030")}, {QStringLiteral("color2"), QStringLiteral("#000000")},
                                                       {QStringLiteral("image"), m_logo}}},
            {QStringLiteral("fadeIn"), 400},
            {QStringLiteral("elements"), elements},
        };
        return d;
    }
    static QString pathsOf(const QList<PayloadFile> &files)
    {
        QStringList paths;
        for (const PayloadFile &f : files)
            paths << f.path;
        return paths.join(QLatin1Char('\n'));
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
    QString m_clip;
};

void TestGenKSplash::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    QString error;
    m_library = Library::open(m_tmp->filePath(QStringLiteral("lib")), error);
    QVERIFY2(m_library, qPrintable(error));
    const auto logo = m_library->addAsset(QStringLiteral(FIXTURES_DIR "/assets/logo.png"), Asset::Kind::Image, QStringLiteral("Logo"), error);
    QVERIFY2(logo, qPrintable(error));
    m_logo = logo->hash;
    const auto clip = m_library->addAsset(QStringLiteral(FIXTURES_DIR "/assets/clip.mp4"), Asset::Kind::Video, QStringLiteral("Clip"), error);
    QVERIFY2(clip, qPrintable(error));
    m_clip = clip->hash;

    QDir(m_tmp->path()).mkpath(QStringLiteral("fonts"));
    for (const auto &[name, data] : {std::pair{QStringLiteral("fonts/Mask.ttf"), QByteArray("\x00\x01\x00\x00glyphs", 10)},
                                     std::pair{QStringLiteral("fonts/OFL.txt"), QByteArray("SIL Open Font License 1.1")}}) {
        QSaveFile f(m_tmp->filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit());
    }
    const auto font = m_library->addFont(m_tmp->filePath(QStringLiteral("fonts/Mask.ttf")), QStringLiteral("Mask"), error);
    QVERIFY2(font, qPrintable(error));
    m_font = font->hash;
}

void TestGenKSplash::matchesGoldenFiles()
{
    QStringList problems;
    const QList<PayloadFile> files = buildPlasmaSplash(design(true), QStringLiteral("ekpo-dawn-v2"), *m_library, problems);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    // The logo is bundled once although two elements use it; the font carries its licence.
    QCOMPARE(pathsOf(files), QStringLiteral("contents/splash/Splash.qml\n"
                                            "contents/splash/fonts/%1.licence.txt\n"
                                            "contents/splash/fonts/%1.ttf\n"
                                            "contents/splash/images/%2.png\n"
                                            "contents/splash/videos/%3.mp4\n"
                                            "metadata.json")
                                 .arg(m_font, m_logo, m_clip));
    QCOMPARE(fileData(files, QStringLiteral("contents/splash/fonts/%1.licence.txt").arg(m_font)), QByteArray("SIL Open Font License 1.1"));
    QVERIFY(matchesGolden(QStringLiteral("ksplash-metadata.json"), fileData(files, QStringLiteral("metadata.json"))));
    QVERIFY(matchesGolden(QStringLiteral("ksplash-Splash.qml"), fileData(files, QStringLiteral("contents/splash/Splash.qml"))));
}

void TestGenKSplash::loadsInQtQuick()
{
    // Without the font: the test font isn't a real one, and FontLoader would warn.
    QStringList problems;
    const QList<PayloadFile> files = buildPlasmaSplash(design(false), QStringLiteral("ekpo-dawn-v1"), *m_library, problems);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    const QString package = m_tmp->filePath(QStringLiteral("package"));
    for (const PayloadFile &f : files) {
        QSaveFile out(package + QLatin1Char('/') + f.path);
        QVERIFY(QDir().mkpath(QFileInfo(out.fileName()).path()));
        QVERIFY(out.open(QIODevice::WriteOnly) && out.write(f.data) == f.data.size() && out.commit());
    }

    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(package + QStringLiteral("/contents/splash/Splash.qml")));
    std::unique_ptr<QObject> splash(component.create());
    QVERIFY2(splash, qPrintable(component.errorString()));
    QVERIFY(splash->setProperty("width", 2560));
    QVERIFY(splash->setProperty("height", 1440));
    QVERIFY(splash->setProperty("stage", 2)); // ksplashqml sets it as Plasma starts
    QCoreApplication::processEvents();

    // The video's relative source resolves inside the package.
    const QList<QObject *> objects = splash->findChildren<QObject *>();
    const auto player = std::find_if(objects.cbegin(), objects.cend(), [](QObject *o) { return o->metaObject()->indexOfProperty("videoOutput") >= 0; });
    QVERIFY(player != objects.cend());
    QCOMPARE((*player)->property("source").toUrl(), QUrl::fromLocalFile(package + QStringLiteral("/contents/splash/videos/%1.mp4").arg(m_clip)));
    // And it plays: the first frame reaches the VideoOutput (no QtMultimedia headers needed).
    const auto output = std::find_if(objects.cbegin(), objects.cend(), [](QObject *o) { return o->metaObject()->indexOfProperty("videoSink") >= 0; });
    QVERIFY(output != objects.cend());
    QObject *sink = (*output)->property("videoSink").value<QObject *>();
    QVERIFY(sink);
    QSignalSpy frames(sink, SIGNAL(videoFrameChanged(QVideoFrame)));
    QVERIFY(frames.isValid());
    QVERIFY(frames.wait(5000));
}

void TestGenKSplash::linksAssetsForPreview()
{
    QStringList problems;
    QMap<QString, QString> links;
    const QList<PayloadFile> files = buildPlasmaSplash(design(true), QStringLiteral("ekpo-preview"), *m_library, problems, &links);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    QCOMPARE(pathsOf(files), QStringLiteral("contents/splash/Splash.qml\nmetadata.json"));
    QCOMPARE(links.keys(), (QStringList{QStringLiteral("contents/splash/fonts/%1.licence.txt").arg(m_font), QStringLiteral("contents/splash/fonts/%1.ttf").arg(m_font),
                                        QStringLiteral("contents/splash/images/%1.png").arg(m_logo), QStringLiteral("contents/splash/videos/%1.mp4").arg(m_clip)}));
    QVERIFY(QFileInfo(links.value(QStringLiteral("contents/splash/videos/%1.mp4").arg(m_clip))).isFile());
}

void TestGenKSplash::reportsProblems()
{
    Design d = design(true);
    QJsonArray elements = d.content.value(QStringLiteral("elements")).toArray();
    elements.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("gif")}});
    elements.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("spinner")}, {QStringLiteral("color"), QStringLiteral("red")},
                                {QStringLiteral("x"), 0}, {QStringLiteral("y"), 0}, {QStringLiteral("width"), -1}, {QStringLiteral("height"), 1}});
    elements.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("image")}, {QStringLiteral("asset"), QStringLiteral("gone")},
                                {QStringLiteral("x"), 0}, {QStringLiteral("y"), 0}, {QStringLiteral("width"), 1}, {QStringLiteral("height"), 1}});
    d.content.insert(QStringLiteral("elements"), elements);

    QStringList problems;
    buildPlasmaSplash(d, QStringLiteral("ekpo-dawn-v1"), *m_library, problems);
    QCOMPARE(problems, (QStringList{
                           QStringLiteral("Element 8 has an unknown type “gif”."),
                           QStringLiteral("Element 9 has no valid width."),
                           QStringLiteral("red is not a colour."),
                           QStringLiteral("The design uses an image that is no longer in the library."),
                       }));
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

// Plain QtQuick has no angled gradients: the generated QML turns a rectangle.
void TestGenKSplash::gradientAngle()
{
    checkAngles([this](double angle) {
        Design d;
        d.name = QStringLiteral("Angle");
        d.stage = Stage::PlasmaSplash;
        d.content = QJsonObject{{QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#ff0000")},
                                                                           {QStringLiteral("color2"), QStringLiteral("#0000ff")}, {QStringLiteral("angle"), angle}}},
                                {QStringLiteral("elements"), QJsonArray()}};
        QStringList problems;
        const QString package = m_tmp->filePath(QStringLiteral("angle-%1").arg(angle));
        for (const PayloadFile &f : buildPlasmaSplash(d, QStringLiteral("ekpo-angle-v1"), *m_library, problems)) {
            QSaveFile out(package + QLatin1Char('/') + f.path);
            QDir().mkpath(QFileInfo(out.fileName()).path());
            if (!out.open(QIODevice::WriteOnly) || out.write(f.data) != f.data.size() || !out.commit())
                return QImage();
        }
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(400, 200);
        view.setSource(QUrl::fromLocalFile(package + QStringLiteral("/contents/splash/Splash.qml")));
        view.show();
        if (!QTest::qWaitForWindowExposed(&view))
            return QImage();
        return view.grabWindow();
    });
}

EKPO_GUI_TEST_MAIN(TestGenKSplash)
#include "tst_genksplash.moc"
