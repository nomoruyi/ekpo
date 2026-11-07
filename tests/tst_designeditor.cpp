#include "designeditor.h"
#include "importer.h"
#include "testmain.h"
#include "validate.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

class TestDesignEditor : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void undoAndRedo();
    void dragIsOneUndoStep();
    void savesVersions();
    void recoversDraftOfNewDesign();
    void recoversDraftOfSavedDesign();
    void buildsPreview();
    void snaps();
    void warns();
    void imageKeepsAspectRatio();
    void storesColours();
    void addsVideo();
    void closingCancelsVideo();
    void cancelsVideo();
    void bootSplashPreview();
    void animationFromImages();
    void animationFromVideo();
    void warnsAboutMemory();
    void startersAreTinted();
    void duplicates();
    void sharesLogos();
    void sharesPalettes();
    void reusesPaletteWithSameColours();
    void deletesPalette();
    void fiveColourPalettes();
    void bootMenuPreview();
    void bootMenuStarters();
    void bootMenuIcons();

private:
    static QVariantMap elementAt(const DesignEditor &editor, int index)
    {
        return editor.content().value(QStringLiteral("elements")).toList().value(index).toMap();
    }

    std::unique_ptr<QTemporaryDir> m_tmp;
    std::optional<Library> m_library;
    std::unique_ptr<DesignEditor> m_editor;
};

void TestDesignEditor::init()
{
    m_editor.reset();
    m_tmp = std::make_unique<QTemporaryDir>();
    QString error;
    m_library = Library::open(m_tmp->filePath(QStringLiteral("lib")), error);
    QVERIFY2(m_library, qPrintable(error));
    m_editor = std::make_unique<DesignEditor>();
    m_editor->setLibrary(&*m_library);
}

void TestDesignEditor::undoAndRedo()
{
    DesignEditor &e = *m_editor;
    e.create();
    QVERIFY(e.isOpen());
    QVERIFY(e.dirty()); // never saved
    QVERIFY(!e.canUndo());
    QCOMPARE(e.addElement(QStringLiteral("text")), 0);
    e.setElement(0, {{QStringLiteral("x"), 12.345}});
    QCOMPARE(elementAt(e, 0).value(QStringLiteral("x")).toDouble(), 12.35); // two decimals

    e.undo();
    QCOMPARE(elementAt(e, 0).value(QStringLiteral("x")).toDouble(), 30.0);
    QVERIFY(e.canRedo());
    e.undo();
    QVERIFY(e.content().value(QStringLiteral("elements")).toList().isEmpty());
    QVERIFY(!e.canUndo());
    e.redo();
    e.redo();
    QCOMPARE(elementAt(e, 0).value(QStringLiteral("x")).toDouble(), 12.35);
    QVERIFY(!e.canRedo());

    // A new edit drops what could be redone; a null value removes the key.
    e.undo();
    e.setElement(0, {{QStringLiteral("align"), QVariant()}});
    QVERIFY(!e.canRedo());
    QVERIFY(!elementAt(e, 0).contains(QStringLiteral("align")));
}

void TestDesignEditor::dragIsOneUndoStep()
{
    DesignEditor &e = *m_editor;
    e.create();
    e.addElement(QStringLiteral("spinner"));
    e.beginGesture();
    for (double x : {20.0, 21.0, 22.0})
        e.setElement(0, {{QStringLiteral("x"), x}});
    e.endGesture();
    QCOMPARE(elementAt(e, 0).value(QStringLiteral("x")).toDouble(), 22.0);
    e.undo();
    QCOMPARE(elementAt(e, 0).value(QStringLiteral("x")).toDouble(), 48.5);
}

void TestDesignEditor::savesVersions()
{
    DesignEditor &e = *m_editor;
    QSignalSpy libraryChanged(&e, &DesignEditor::libraryChanged);
    e.create();
    e.setName(QStringLiteral("Dusk"));
    QCOMPARE(e.save(), QString());
    QCOMPARE(e.version(), 1);
    QVERIFY(!e.dirty());
    QVERIFY(!libraryChanged.isEmpty());
    QCOMPARE(e.save(), QString()); // nothing changed: no new version
    QCOMPARE(m_library->designVersions(e.designId()), QList<int>{1});

    e.addElement(QStringLiteral("text"));
    QVERIFY(e.dirty());
    QCOMPARE(e.save(), QString());
    QCOMPARE(e.version(), 2);
    QCOMPARE(m_library->designVersions(e.designId()), (QList<int>{1, 2}));
    QCOMPARE(e.versions().size(), 2);
    QCOMPARE(e.versions().first().toMap().value(QStringLiteral("version")).toInt(), 2); // newest first

    // Opening version 1 is an edit like any other.
    e.restoreVersion(1);
    QVERIFY(e.dirty());
    QVERIFY(e.content().value(QStringLiteral("elements")).toList().isEmpty());
    e.undo();
    QVERIFY(!e.dirty());

    e.setName(QStringLiteral("  "));
    QCOMPARE(e.save(), QStringLiteral("The design needs a name."));
}

void TestDesignEditor::recoversDraftOfNewDesign()
{
    DesignEditor &e = *m_editor;
    e.create();
    e.addElement(QStringLiteral("text"));
    e.flushDraft();
    const QString id = e.designId();
    QVERIFY(!id.isEmpty());

    // As if the app had crashed: another editor finds the draft.
    DesignEditor other;
    other.setLibrary(&*m_library);
    QCOMPARE(other.open(id), QString());
    QVERIFY(other.recovered());
    QCOMPARE(other.version(), 0);
    QCOMPARE(other.content(), e.content());

    other.discard();
    QVERIFY(!other.isOpen());
    QStringList errors;
    QVERIFY(m_library->drafts(errors).isEmpty());
    QVERIFY(!QFileInfo::exists(m_tmp->filePath(QStringLiteral("lib/designs/") + id)));
}

void TestDesignEditor::recoversDraftOfSavedDesign()
{
    DesignEditor &e = *m_editor;
    e.create();
    QCOMPARE(e.save(), QString());
    const QString id = e.designId();
    e.addElement(QStringLiteral("spinner"));
    e.flushDraft();

    DesignEditor other;
    other.setLibrary(&*m_library);
    QCOMPARE(other.open(id), QString());
    QVERIFY(other.recovered());
    QVERIFY(other.dirty());
    QCOMPARE(other.content().value(QStringLiteral("elements")).toList().size(), 1);
    other.discard(); // back to version 1
    QVERIFY(other.isOpen());
    QVERIFY(!other.dirty());
    QVERIFY(other.content().value(QStringLiteral("elements")).toList().isEmpty());
    QStringList errors;
    QVERIFY(m_library->drafts(errors).isEmpty());

    // Undoing back to the saved version removes the draft.
    e.flushDraft();
    QCOMPARE(m_library->drafts(errors).size(), 1);
    e.undo();
    e.flushDraft();
    QVERIFY(m_library->drafts(errors).isEmpty());
}

void TestDesignEditor::buildsPreview()
{
    DesignEditor &e = *m_editor;
    e.create();
    e.setName(QStringLiteral("Preview"));
    e.flushPreview();
    const QString first = e.previewPackage();
    QVERIFY(QFileInfo::exists(first + QStringLiteral("/metadata.json")));
    QCOMPARE(e.previewUrl(), QUrl::fromLocalFile(first + QStringLiteral("/contents/splash/Splash.qml")));
    QFile qml(e.previewUrl().toLocalFile());
    QVERIFY(qml.open(QIODevice::ReadOnly));
    QVERIFY(qml.readAll().contains("design \"Preview\""));

    // The build on screen is kept until the next one replaces it.
    e.addElement(QStringLiteral("text"));
    e.flushPreview();
    QVERIFY(QFileInfo::exists(first));
    e.addElement(QStringLiteral("spinner"));
    e.flushPreview();
    QVERIFY(!QFileInfo::exists(first));
    QCOMPARE(QDir(QFileInfo(first).path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 2);
    QVERIFY(e.problems().isEmpty());
}

void TestDesignEditor::snaps()
{
    DesignEditor &e = *m_editor;
    e.create();
    e.addElement(QStringLiteral("text"), {{QStringLiteral("x"), 10}, {QStringLiteral("y"), 10}, {QStringLiteral("width"), 20}, {QStringLiteral("height"), 10}});
    e.addElement(QStringLiteral("text"), {{QStringLiteral("x"), 60}, {QStringLiteral("y"), 60}, {QStringLiteral("width"), 10}, {QStringLiteral("height"), 4}});

    // Left edge onto the other element's right edge (30); top far from anything.
    QVariantMap s = e.snapMove(1, 29.6, 63, 0.5, 0.5);
    QCOMPARE(s.value(QStringLiteral("x")).toDouble(), 30.0);
    QCOMPARE(s.value(QStringLiteral("guideX")).toDouble(), 30.0);
    QCOMPARE(s.value(QStringLiteral("y")).toDouble(), 63.0);
    QCOMPARE(s.value(QStringLiteral("guideY")).toDouble(), -1.0);
    // Centre onto the screen centre.
    s = e.snapMove(1, 44.8, 47.7, 0.5, 0.5);
    QCOMPARE(s.value(QStringLiteral("x")).toDouble(), 45.0);
    QCOMPARE(s.value(QStringLiteral("y")).toDouble(), 48.0);
    // Right edge onto the screen edge when resizing.
    s = e.snapResize(1, 39.7, 4, 0.5, 0.5);
    QCOMPARE(s.value(QStringLiteral("width")).toDouble(), 40.0);
    QCOMPARE(s.value(QStringLiteral("guideX")).toDouble(), 100.0);
    QCOMPARE(s.value(QStringLiteral("height")).toDouble(), 4.0);
}

void TestDesignEditor::warns()
{
    DesignEditor &e = *m_editor;
    e.create();
    const QVariantMap logo = e.addImage(QUrl::fromLocalFile(QStringLiteral(FIXTURES_DIR "/assets/logo.png")));
    QVERIFY(logo.contains(QStringLiteral("hash")));
    e.addElement(QStringLiteral("image"), {{QStringLiteral("asset"), logo.value(QStringLiteral("hash"))},
                                           {QStringLiteral("x"), 90}, {QStringLiteral("y"), 0}, {QStringLiteral("width"), 20}, {QStringLiteral("height"), 20}});
    e.addElement(QStringLiteral("text"), {{QStringLiteral("text"), QStringLiteral("Far too big")}, {QStringLiteral("size"), 20},
                                          {QStringLiteral("x"), 0}, {QStringLiteral("y"), 0}, {QStringLiteral("width"), 10}, {QStringLiteral("height"), 5}});
    e.addElement(QStringLiteral("text"), {{QStringLiteral("text"), QStringLiteral("tiny")}, {QStringLiteral("size"), 1}});
    e.flushPreview();
    const QString warnings = e.warnings().join(QLatin1Char('\n'));
    QVERIFY2(warnings.contains(QStringLiteral("The image “logo” reaches past the edge of the screen.")), qPrintable(warnings));
    QVERIFY2(warnings.contains(QStringLiteral("1440p: The image “logo” (8×8 px) is enlarged ×36.0 and looks blurry.")), qPrintable(warnings));
    for (const char *r : {"1440p", "1080p", "720p"})
        QVERIFY2(warnings.contains(QStringLiteral("%1: The text “Far too big” overflows its box by").arg(QLatin1String(r))), qPrintable(warnings));
    QVERIFY2(warnings.contains(QStringLiteral("720p: The text “tiny” is only 7 px high and hard to read.")), qPrintable(warnings));
    QVERIFY2(!warnings.contains(QStringLiteral("“tiny” overflows")), qPrintable(warnings));
    QVERIFY(e.problems().isEmpty());
}

void TestDesignEditor::imageKeepsAspectRatio()
{
    DesignEditor &e = *m_editor;
    e.create();
    const QVariantMap logo = e.addImage(QUrl::fromLocalFile(QStringLiteral(FIXTURES_DIR "/assets/logo.png")));
    QCOMPARE(e.addElement(QStringLiteral("image"), {{QStringLiteral("asset"), logo.value(QStringLiteral("hash"))}}), 0);
    // A square image: 20 % of the width is 35.56 % of the height at 16:9.
    const QVariantMap image = elementAt(e, 0);
    QCOMPARE(image.value(QStringLiteral("width")).toDouble(), 20.0);
    QCOMPARE(image.value(QStringLiteral("height")).toDouble(), 35.56);
    QCOMPARE(image.value(QStringLiteral("x")).toDouble(), 40.0);
    QCOMPARE(image.value(QStringLiteral("y")).toDouble(), 32.22);

    QCOMPARE(e.addImage(QUrl::fromLocalFile(QStringLiteral(FIXTURES_DIR "/library/library.json"))).value(QStringLiteral("error")).toString(),
             QStringLiteral("library.json isn't an image Ekpo can read."));
}

void TestDesignEditor::storesColours()
{
    DesignEditor &e = *m_editor;
    e.create();
    e.addElement(QStringLiteral("spinner"));
    e.setElement(0, {{QStringLiteral("color"), QColor(255, 0, 0, 128)}});
    QCOMPARE(elementAt(e, 0).value(QStringLiteral("color")).toString(), QStringLiteral("#80ff0000"));
    e.setBackground({{QStringLiteral("color"), QColor(QStringLiteral("#123456"))}});
    QCOMPARE(e.content().value(QStringLiteral("background")).toMap().value(QStringLiteral("color")).toString(), QStringLiteral("#123456"));
}

void TestDesignEditor::addsVideo()
{
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
        QSKIP("ffmpeg isn't installed");
    DesignEditor &e = *m_editor;
    e.create();
    e.addElement(QStringLiteral("text"));
    QSignalSpy finished(&e, &DesignEditor::conversionFinished);
    const QString clip = QStringLiteral(FIXTURES_DIR "/assets/clip.mp4");
    QCOMPARE(e.addVideo(QUrl::fromLocalFile(clip)), QString());
    QCOMPARE(e.conversionName(), QStringLiteral("clip"));
    QVERIFY(e.conversionProgress() >= 0);
    QCOMPARE(e.addVideo(QUrl::fromLocalFile(clip)), QStringLiteral("Wait until “clip” is converted."));
    QVERIFY(finished.wait(60000));
    QCOMPARE(finished.first().first().toString(), QString());
    QCOMPARE(e.conversionProgress(), -1.0);

    // The video goes first, under the text, and fills the screen.
    const QVariantMap video = elementAt(e, 0);
    QCOMPARE(video.value(QStringLiteral("type")).toString(), QStringLiteral("video"));
    QCOMPARE(video.value(QStringLiteral("width")).toDouble(), 100.0);
    QCOMPARE(elementAt(e, 1).value(QStringLiteral("type")).toString(), QStringLiteral("text"));
    const QString hash = video.value(QStringLiteral("asset")).toString();
    QStringList errors;
    const QList<Asset> assets = m_library->assets(errors);
    QCOMPARE(assets.size(), 1);
    QCOMPARE(assets.first().kind, Asset::Kind::Video);
    QCOMPARE(assets.first().source, clip); // the original, not the converted copy

    // The preview links the stored video instead of copying it.
    e.flushPreview();
    const QFileInfo link(e.previewPackage() + QStringLiteral("/contents/splash/videos/%1.mp4").arg(hash));
    QVERIFY(link.isSymLink());
    QCOMPARE(link.symLinkTarget(), m_library->assetPath(assets.first()));
    QVERIFY(e.problems().isEmpty());
}

void TestDesignEditor::closingCancelsVideo()
{
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
        QSKIP("ffmpeg isn't installed");
    DesignEditor &e = *m_editor;
    e.create();
    QSignalSpy finished(&e, &DesignEditor::conversionFinished);
    QCOMPARE(e.addVideo(QUrl::fromLocalFile(QStringLiteral(FIXTURES_DIR "/assets/clip.mp4"))), QString());
    e.create(); // closes the first design
    QCOMPARE(finished.size(), 1);
    QCOMPARE(e.conversionProgress(), -1.0);
    QStringList errors;
    QVERIFY(m_library->assets(errors).isEmpty());
    QVERIFY(e.content().value(QStringLiteral("elements")).toList().isEmpty());
}

void TestDesignEditor::cancelsVideo()
{
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
        QSKIP("ffmpeg isn't installed");
    DesignEditor &e = *m_editor;
    e.create();
    QSignalSpy finished(&e, &DesignEditor::conversionFinished);
    QCOMPARE(e.addVideo(QUrl::fromLocalFile(QStringLiteral(FIXTURES_DIR "/assets/clip.mp4"))), QString());
    e.cancelConversion();
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.first().first().toString(), QString());
    QCOMPARE(e.conversionProgress(), -1.0);
    QStringList errors;
    QVERIFY(m_library->assets(errors).isEmpty());
    QVERIFY(e.content().value(QStringLiteral("elements")).toList().isEmpty());
}

void TestDesignEditor::bootSplashPreview()
{
    DesignEditor &e = *m_editor;
    e.create(QStringLiteral("bootSplash"));
    QCOMPARE(e.stage(), QStringLiteral("bootSplash"));
    e.addElement(QStringLiteral("spinner"));
    QCOMPARE(e.addElement(QStringLiteral("progress")), 1);
    e.flushPreview();
    QVERIFY2(e.problems().isEmpty(), qPrintable(e.problems().join(QLatin1Char('\n'))));
    QVERIFY(e.previewUrl().isEmpty()); // the Plymouth preview draws from the built files
    const QVariantMap p = e.plymouth();
    QCOMPARE(p.value(QStringLiteral("module")).toString(), QStringLiteral("two-step"));
    QVERIFY(QFileInfo::exists(p.value(QStringLiteral("background")).toUrl().toLocalFile()));
    const QVariantList frames = p.value(QStringLiteral("frames")).toList();
    QCOMPARE(frames.size(), 60);
    QVERIFY(QFileInfo::exists(frames.last().toUrl().toLocalFile()));
    QCOMPARE(p.value(QStringLiteral("progress")).toMap().value(QStringLiteral("width")).toInt(), 512);
    // The meters: background (twice) and 60 frames of 76 px in memory.
    QCOMPARE(p.value(QStringLiteral("memory")).toDouble(), double(2 * 2560 * 1440 * 4 + 60 * 76 * 76 * 4));
    QVERIFY(p.value(QStringLiteral("bytes")).toDouble() > 0);

    // Saved and opened again as a boot splash.
    QCOMPARE(e.save(), QString());
    const QString id = e.designId();
    e.close();
    QCOMPARE(e.open(id), QString());
    QCOMPARE(e.stage(), QStringLiteral("bootSplash"));
}

void TestDesignEditor::animationFromImages()
{
    DesignEditor &e = *m_editor;
    e.create(QStringLiteral("bootSplash"));
    // Sorted as people number files: 2 before 10.
    QDir().mkpath(m_tmp->filePath(QStringLiteral("frames")));
    QImage frame(16, 9, QImage::Format_RGB32);
    QList<QUrl> files;
    for (int n : {10, 2, 1}) {
        frame.fill(QColor(n * 20, 0, 0));
        const QString path = m_tmp->filePath(QStringLiteral("frames/f%1.png").arg(n));
        QVERIFY(frame.save(path));
        files << QUrl::fromLocalFile(path);
    }
    QCOMPARE(e.addImageFrames(files), QString());
    const QVariantMap animation = elementAt(e, 0);
    QCOMPARE(animation.value(QStringLiteral("type")).toString(), QStringLiteral("frames"));
    QCOMPARE(animation.value(QStringLiteral("seconds")).toDouble(), 2.0); // up to 60 frames: two-step's loop
    QCOMPARE(animation.value(QStringLiteral("width")).toDouble(), 20.0); // 16:9 frames on a 16:9 screen
    QCOMPARE(animation.value(QStringLiteral("height")).toDouble(), 20.0);
    const QStringList hashes = animation.value(QStringLiteral("frames")).toStringList();
    QCOMPARE(hashes.size(), 3);
    QCOMPARE(e.assets().value(hashes.first()).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("frames 0001"));
    QStringList errors;
    for (const Asset &a : m_library->assets(errors)) {
        if (a.hash == hashes[1])
            QCOMPARE(QFileInfo(a.source).fileName(), QStringLiteral("f2.png"));
    }

    // Replacing keeps the element and its box.
    QCOMPARE(e.addImageFrames({files.last()}, 0), QString());
    QCOMPARE(elementAt(e, 0).value(QStringLiteral("frames")).toList().size(), 1);
    QCOMPARE(e.content().value(QStringLiteral("elements")).toList().size(), 1);
}

void TestDesignEditor::animationFromVideo()
{
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
        QSKIP("ffmpeg isn't installed");
    DesignEditor &e = *m_editor;
    e.create(QStringLiteral("bootSplash"));
    e.flushPreview();
    const QUrl clip = QUrl::fromLocalFile(QStringLiteral(FIXTURES_DIR "/assets/clip.mp4")); // 64×36, 1 s at 10 fps
    const QVariantMap plan = e.videoPlan(clip);
    QCOMPARE(plan.value(QStringLiteral("fps")).toDouble(), 10.0);
    // A new box is 20 % of the width: 512 px at 1440p.
    QCOMPARE(plan.value(QStringLiteral("frameWidth")).toInt(), 512);
    QCOMPARE(plan.value(QStringLiteral("frameHeight")).toInt(), 288);
    QCOMPARE(plan.value(QStringLiteral("memoryLeft")).toDouble(), double(plymouthMemoryBudget - 2 * 2560 * 1440 * 4));
    QVERIFY(plan.value(QStringLiteral("bytesLeft")).toDouble() < plymouthThemeCap);

    QSignalSpy measured(&e, &DesignEditor::framesMeasured);
    e.measureFrames(clip, 128, 72, 10);
    QVERIFY(measured.wait(60000));
    QVERIFY(measured.first().first().toDouble() > 0);

    QSignalSpy finished(&e, &DesignEditor::conversionFinished);
    QCOMPARE(e.addVideoFrames(clip, 128, 72, 5, 1), QString());
    QVERIFY(finished.wait(60000));
    QCOMPARE(finished.first().first().toString(), QString());
    const QVariantMap animation = elementAt(e, 0);
    QCOMPARE(animation.value(QStringLiteral("frames")).toList().size(), 5);
    QCOMPARE(animation.value(QStringLiteral("seconds")).toDouble(), 1.0);
    e.flushPreview();
    QCOMPARE(e.plymouth().value(QStringLiteral("module")).toString(), QStringLiteral("script")); // a 1 s loop
    QVERIFY2(e.problems().isEmpty(), qPrintable(e.problems().join(QLatin1Char('\n'))));
}

void TestDesignEditor::warnsAboutMemory()
{
    DesignEditor &e = *m_editor;
    e.create(QStringLiteral("bootSplash"));
    // 60 frames of 1108 px square: about 280 MB, plus the background.
    e.addElement(QStringLiteral("spinner"), {{QStringLiteral("x"), 0}, {QStringLiteral("y"), 0}, {QStringLiteral("width"), 50}, {QStringLiteral("height"), 77}});
    e.flushPreview();
    QVERIFY2(e.warnings().join(QLatin1Char('\n')).contains(QStringLiteral("Plymouth holds 3")), qPrintable(e.warnings().join(QLatin1Char('\n'))));
}

void TestDesignEditor::startersAreTinted()
{
    DesignEditor &e = *m_editor;
    e.create(QStringLiteral("plasmaSplash"), QStringLiteral("welcome"), QColor(QStringLiteral("#e01b24")));
    QCOMPARE(elementAt(e, 1).value(QStringLiteral("color")).toString(), QStringLiteral("#e01b24")); // the spinner
    const QColor background(e.content().value(QStringLiteral("background")).toMap().value(QStringLiteral("color")).toString());
    QVERIFY(qAbs(background.hslHue() - QColor(QStringLiteral("#e01b24")).hslHue()) <= 2); // a dark shade of the accent (8-bit rounding)
    QVERIFY(background.lightness() < 40);

    e.create(QStringLiteral("bootSplash"), QStringLiteral("progress"), QColor(QStringLiteral("#3daee9")));
    QCOMPARE(elementAt(e, 1).value(QStringLiteral("type")).toString(), QStringLiteral("progress"));
    QCOMPARE(elementAt(e, 1).value(QStringLiteral("color")).toString(), QStringLiteral("#3daee9"));
    e.flushPreview();
    QVERIFY2(e.problems().isEmpty(), qPrintable(e.problems().join(QLatin1Char('\n'))));

    e.create(QStringLiteral("bootSplash")); // blank
    QVERIFY(e.content().value(QStringLiteral("elements")).toList().isEmpty());
}

void TestDesignEditor::duplicates()
{
    DesignEditor &e = *m_editor;
    e.create(QStringLiteral("plasmaSplash"), QStringLiteral("accent"));
    e.setName(QStringLiteral("Dawn"));
    QCOMPARE(e.save(), QString());
    const QVariantMap content = e.content();
    QCOMPARE(e.duplicate(e.designId()), QString());
    QCOMPARE(e.name(), QStringLiteral("Copy of Dawn"));
    QCOMPARE(e.version(), 0);
    QVERIFY(e.designId().isEmpty());
    QCOMPARE(e.content(), content);
}

void TestDesignEditor::sharesLogos()
{
    DesignEditor &e = *m_editor;
    const auto addLogo = [&](const QString &file) {
        return e.addImage(QUrl::fromLocalFile(file)).value(QStringLiteral("hash")).toString();
    };
    e.create();
    const QString red = addLogo(QStringLiteral(FIXTURES_DIR "/assets/logo.png"));
    e.addElement(QStringLiteral("image"), {{QStringLiteral("asset"), red}});
    QCOMPARE(e.shareLogo(0, QStringLiteral("Brand")), QString());
    const QString id = elementAt(e, 0).value(QStringLiteral("shared")).toString();
    QCOMPARE(e.shared().value(id).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Brand"));
    QCOMPARE(e.save(), QString());
    const QString first = e.designId();

    // A second design links the same logo.
    e.create();
    QCOMPARE(e.addSharedLogo(id), 0);
    QCOMPARE(e.save(), QString());
    QCOMPARE(e.shared().value(id).toMap().value(QStringLiteral("uses")).toInt(), 2);

    // Changing it changes both.
    QImage blue(8, 8, QImage::Format_RGB32);
    blue.fill(Qt::blue);
    QVERIFY(blue.save(m_tmp->filePath(QStringLiteral("blue.png"))));
    const QString blueHash = addLogo(m_tmp->filePath(QStringLiteral("blue.png")));
    QCOMPARE(e.setSharedLogo(id, blueHash), QString());
    QStringList problems;
    for (const QString &designId : {first, e.designId()}) {
        QString error;
        const Design resolved = m_library->resolved(*m_library->design(designId, 0, error), problems);
        QCOMPARE(resolved.content.value(QStringLiteral("elements")).toArray().at(0).toObject().value(QStringLiteral("asset")).toString(), blueHash);
    }

    // Unlinked, it keeps the image it shows now.
    e.unlink(0);
    QVERIFY(!elementAt(e, 0).contains(QStringLiteral("shared")));
    QCOMPARE(elementAt(e, 0).value(QStringLiteral("asset")).toString(), blueHash);
}

void TestDesignEditor::sharesPalettes()
{
    DesignEditor &e = *m_editor;
    e.create();
    e.setBackground({{QStringLiteral("color"), QStringLiteral("#102030")}});
    e.addElement(QStringLiteral("text"), {{QStringLiteral("color"), QStringLiteral("#ffeedd")}});
    e.addElement(QStringLiteral("spinner"), {{QStringLiteral("color"), QStringLiteral("#ff8800")}});
    QCOMPARE(e.newPalette(QStringLiteral("Night")), QString());
    // The design's colours now come from the palette.
    const QString id = e.content().value(QStringLiteral("palette")).toString();
    QCOMPARE(e.shared().value(id).toMap().value(QStringLiteral("colours")).toMap().value(QStringLiteral("accent")).toString(), QStringLiteral("#ff8800"));
    QCOMPARE(e.content().value(QStringLiteral("background")).toMap().value(QStringLiteral("color")).toString(), QStringLiteral("@background"));
    QCOMPARE(elementAt(e, 0).value(QStringLiteral("color")).toString(), QStringLiteral("@text"));
    QCOMPARE(elementAt(e, 1).value(QStringLiteral("color")).toString(), QStringLiteral("@accent"));

    // Changing the palette changes what is built.
    QCOMPARE(e.setPaletteColour(QStringLiteral("accent"), QColor(Qt::blue)), QString());
    e.flushPreview();
    QFile qml(e.previewUrl().toLocalFile());
    QVERIFY(qml.open(QIODevice::ReadOnly));
    QVERIFY(qml.readAll().contains("\"#0000ff\""));

    // Unlinked, the palette's colours become the design's own.
    e.linkPalette({});
    QVERIFY(!e.content().contains(QStringLiteral("palette")));
    QCOMPARE(elementAt(e, 1).value(QStringLiteral("color")).toString(), QStringLiteral("#0000ff"));
    QCOMPARE(e.content().value(QStringLiteral("background")).toMap().value(QStringLiteral("color")).toString(), QStringLiteral("#102030"));
}

// "New from This Design" again with the same colours links the palette it made before
// (issue #2: several clicks left identical palettes).
void TestDesignEditor::reusesPaletteWithSameColours()
{
    DesignEditor &e = *m_editor;
    e.create();
    e.setBackground({{QStringLiteral("color"), QStringLiteral("#102030")}});
    QCOMPARE(e.newPalette({}), QString());
    const QString first = e.content().value(QStringLiteral("palette")).toString();
    QCOMPARE(e.newPalette({}), QString());
    QCOMPARE(e.newPalette({}), QString());
    QCOMPARE(e.shared().size(), 1);
    QCOMPARE(e.content().value(QStringLiteral("palette")).toString(), first);
    // Other colours make another palette, under its own name.
    e.linkPalette({});
    e.setBackground({{QStringLiteral("color"), QStringLiteral("#ff0000")}});
    QCOMPARE(e.newPalette({}), QString());
    QCOMPARE(e.shared().size(), 2);
    QStringList names;
    for (const QVariant &v : e.shared())
        names << v.toMap().value(QStringLiteral("name")).toString();
    names.sort();
    QCOMPARE(names, (QStringList{QStringLiteral("Palette of New splash"), QStringLiteral("Palette of New splash 2")}));
}

// Deleting a palette: the design keeps its colours; refused while a saved version or
// another design still links it.
void TestDesignEditor::deletesPalette()
{
    DesignEditor &e = *m_editor;
    e.create();
    e.setBackground({{QStringLiteral("color"), QStringLiteral("#102030")}});
    e.addElement(QStringLiteral("spinner"), {{QStringLiteral("color"), QStringLiteral("#ff8800")}});
    QCOMPARE(e.newPalette({}), QString());
    const QString id = e.content().value(QStringLiteral("palette")).toString();
    QCOMPARE(e.deletePalette(id), QString());
    QVERIFY(e.shared().isEmpty());
    QVERIFY(!e.content().contains(QStringLiteral("palette")));
    QCOMPARE(elementAt(e, 0).value(QStringLiteral("color")).toString(), QStringLiteral("#ff8800"));
    QCOMPARE(e.content().value(QStringLiteral("background")).toMap().value(QStringLiteral("color")).toString(), QStringLiteral("#102030"));

    // Saved while linked: version 1 needs it.
    QCOMPARE(e.newPalette({}), QString());
    const QString linked = e.content().value(QStringLiteral("palette")).toString();
    QCOMPARE(e.save(), QString());
    const QString error = e.deletePalette(linked);
    QVERIFY(error.contains(QStringLiteral("version 1")));
    QCOMPARE(e.shared().size(), 1);
    QCOMPARE(e.content().value(QStringLiteral("palette")).toString(), linked); // nothing changed
}

// Palettes have five colours (issue #3): a gradient's second colour and a second accent
// join background, accent and text. Palettes made with three fill those two in.
void TestDesignEditor::fiveColourPalettes()
{
    DesignEditor &e = *m_editor;
    e.create();
    e.setBackground({{QStringLiteral("color"), QStringLiteral("#102030")}, {QStringLiteral("color2"), QStringLiteral("#000010")}});
    e.addElement(QStringLiteral("spinner"), {{QStringLiteral("color"), QStringLiteral("#ff8800")}});
    e.addElement(QStringLiteral("progress"), {{QStringLiteral("color"), QStringLiteral("#00ff88")}});
    QCOMPARE(e.newPalette({}), QString());
    const QString id = e.content().value(QStringLiteral("palette")).toString();
    const QVariantMap colours = e.shared().value(id).toMap().value(QStringLiteral("colours")).toMap();
    QCOMPARE(colours.value(QStringLiteral("gradient")).toString(), QStringLiteral("#000010"));
    QCOMPARE(colours.value(QStringLiteral("accent")).toString(), QStringLiteral("#ff8800"));
    QCOMPARE(colours.value(QStringLiteral("accent2")).toString(), QStringLiteral("#00ff88"));
    QCOMPARE(e.content().value(QStringLiteral("background")).toMap().value(QStringLiteral("color2")).toString(), QStringLiteral("@gradient"));
    QCOMPARE(elementAt(e, 0).value(QStringLiteral("color")).toString(), QStringLiteral("@accent"));
    QCOMPARE(elementAt(e, 1).value(QStringLiteral("color")).toString(), QStringLiteral("@accent2"));
    QCOMPARE(e.setPaletteColour(QStringLiteral("accent2"), QColor(Qt::blue)), QString());

    // A palette saved with three colours.
    const std::optional<SharedElement> old = SharedElement::fromJson(
        QStringLiteral("p"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("palette")}, {QStringLiteral("name"), QStringLiteral("Old")},
                                         {QStringLiteral("colours"), QJsonObject{{QStringLiteral("background"), QStringLiteral("#111111")},
                                                                                 {QStringLiteral("accent"), QStringLiteral("#222222")},
                                                                                 {QStringLiteral("text"), QStringLiteral("#333333")}}}});
    QVERIFY(old);
    QCOMPARE(old->colours.value(QStringLiteral("gradient")), QStringLiteral("#111111"));
    QCOMPARE(old->colours.value(QStringLiteral("accent2")), QStringLiteral("#222222"));
}

void TestDesignEditor::bootMenuPreview()
{
    if (QStandardPaths::findExecutable(QStringLiteral("grub-mkfont")).isEmpty())
        QSKIP("grub-mkfont isn't installed (grub-common)");
    DesignEditor &e = *m_editor;
    e.create(QStringLiteral("bootMenu"));
    QCOMPARE(e.stage(), QStringLiteral("bootMenu"));
    QCOMPARE(elementAt(e, 0).value(QStringLiteral("type")).toString(), QStringLiteral("menu")); // blank still has one
    e.setMenuEntries({QStringLiteral("Kubuntu"), QStringLiteral("Advanced options for Kubuntu"), QStringLiteral("Windows Boot Manager (on /dev/nvme1n1p1)")});
    QCOMPARE(e.iconClasses(), (QStringList{QStringLiteral("kubuntu"), QStringLiteral("windows")}));

    // The preview is the theme drawn as GRUB will draw it, at the editor's resolution.
    e.flushPreview();
    QVERIFY2(e.problems().isEmpty(), qPrintable(e.problems().join(QLatin1Char('\n'))));
    const QString preview = e.bootMenu().value(QStringLiteral("preview")).toUrl().toLocalFile();
    QCOMPARE(QImage(preview).size(), QSize(2560, 1440));
    e.setPreviewScreen(1280, 720);
    e.flushPreview();
    QCOMPARE(QImage(e.bootMenu().value(QStringLiteral("preview")).toUrl().toLocalFile()).size(), QSize(1280, 720));

    // A narrow menu widens (a note); a text too long for its box is cut off (a warning).
    e.setElement(0, {{QStringLiteral("x"), 45}, {QStringLiteral("width"), 10}});
    e.addElement(QStringLiteral("text"), {{QStringLiteral("text"), QString(60, QLatin1Char('W'))}, {QStringLiteral("x"), 45}, {QStringLiteral("width"), 10}});
    e.flushPreview();
    const QString warnings = e.warnings().join(QLatin1Char('\n'));
    QVERIFY2(warnings.contains(QStringLiteral("The menu widens to ")), qPrintable(warnings));
    QVERIFY2(warnings.contains(QStringLiteral("so “Windows Boot Manager (on /dev/nvme1n1p1)” fits.")), qPrintable(warnings));
    QVERIFY2(warnings.contains(QStringLiteral("720p: GRUB cuts off “WWW")), qPrintable(warnings));

    // Saved and opened again as a boot menu.
    QCOMPARE(e.save(), QString());
    const QString id = e.designId();
    e.close();
    QCOMPARE(e.open(id), QString());
    QCOMPARE(e.stage(), QStringLiteral("bootMenu"));
}

void TestDesignEditor::bootMenuStarters()
{
    DesignEditor &e = *m_editor;
    for (const char *starter : {"centred", "side"}) {
        e.create(QStringLiteral("bootMenu"), QLatin1String(starter), QColor(QStringLiteral("#e01b24")));
        const QVariantList elements = e.content().value(QStringLiteral("elements")).toList();
        const auto menu = std::find_if(elements.cbegin(), elements.cend(), [](const QVariant &v) { return v.toMap().value(QStringLiteral("type")) == QLatin1String("menu"); });
        QVERIFY(menu != elements.cend());
        QCOMPARE(menu->toMap().value(QStringLiteral("selection")).toMap().value(QStringLiteral("color")).toString(), QStringLiteral("#e01b24"));
    }
}

void TestDesignEditor::bootMenuIcons()
{
    DesignEditor &e = *m_editor;
    e.create(QStringLiteral("bootMenu"));
    e.setMenuEntries({QStringLiteral("Kubuntu"), QStringLiteral("Windows Boot Manager (on /dev/sda1)"), QStringLiteral("UEFI Firmware Settings")});
    e.useBuiltinIcons();
    QCOMPARE(e.content().value(QStringLiteral("icons")).toMap(),
             (QVariantMap{{QStringLiteral("kubuntu"), QStringLiteral("builtin:os")}, {QStringLiteral("windows"), QStringLiteral("builtin:windows")},
                          {QStringLiteral("efi"), QStringLiteral("builtin:firmware")}}));

    // From an imported theme: the icons it has replace those; the rest stay.
    const ImportResult imported = importThemes(*m_library, QStringLiteral(FIXTURES_DIR "/themes/grub-icons"), QStringLiteral("/"), QDir::homePath());
    QCOMPARE(imported.themes.size(), 1);
    QCOMPARE(e.useImportIcons(imported.themes.first().id), QString());
    const QVariantMap icons = e.content().value(QStringLiteral("icons")).toMap();
    QCOMPARE(e.assets().value(icons.value(QStringLiteral("kubuntu")).toString()).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("kubuntu icon"));
    QVERIFY(e.assets().contains(icons.value(QStringLiteral("windows")).toString()));
    QCOMPARE(icons.value(QStringLiteral("efi")).toString(), QStringLiteral("builtin:firmware"));

    e.setMenuEntries({QStringLiteral("Memory test (memtest86+x64.efi)")});
    QCOMPARE(e.useImportIcons(imported.themes.first().id), QStringLiteral("That theme has no icons for memtest86."));
}

EKPO_GUI_TEST_MAIN(TestDesignEditor)
#include "tst_designeditor.moc"
