#include "library.h"
#include "testmain.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonArray>
#include <QTemporaryDir>

#include <algorithm>

class TestLibrary : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void createsAndReopens();
    void refusesForeignFolder();
    void refusesNewerFormat();
    void maskRoundTrip();
    void unreadableMaskIsReported();
    void historyKeepsNewest();
    void originalIsCapturedOnce();
    void removedMaskGoesToTrash();
    void configuredPath();
    void relocate();
    void migratesFormat1();
    void designVersions();
    void draftRecovery();
    void assetsAreStoredOnce();
    void fontWithLicenceNextToIt();
    void notAFontIsRefused();
    void fontFromDebianPackage();
    void resolvesSharedElements();

private:
    static Library openFresh(const QTemporaryDir &tmp)
    {
        QString error;
        auto library = Library::open(tmp.filePath(QStringLiteral("lib")), error);
        if (!library)
            qFatal("open failed: %s", qPrintable(error));
        return *library;
    }
    static void writeFile(const QString &path, const QByteArray &content)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(content);
    }
};

void TestLibrary::createsAndReopens()
{
    QTemporaryDir tmp;
    Library library = openFresh(tmp);
    QVERIFY(QFile::exists(library.path() + QStringLiteral("/library.json")));
    QCOMPARE(library.settings().historySize, 10);

    Settings settings = library.settings();
    settings.technicalDetailsExpanded = true;
    QString error;
    QVERIFY(library.saveSettings(settings, error));

    const auto reopened = Library::open(library.path(), error);
    QVERIFY2(reopened, qPrintable(error));
    QVERIFY(reopened->settings().technicalDetailsExpanded);
}

void TestLibrary::refusesForeignFolder()
{
    QTemporaryDir tmp;
    writeFile(tmp.filePath(QStringLiteral("notes.txt")), "not a library");
    QString error;
    QVERIFY(!Library::open(tmp.path(), error));
    QVERIFY(error.contains(QLatin1String("not a Ekpo library")));
}

void TestLibrary::refusesNewerFormat()
{
    QTemporaryDir tmp;
    writeFile(tmp.filePath(QStringLiteral("library.json")), R"({"format": 3})");
    QString error;
    QVERIFY(!Library::open(tmp.path(), error));
    QVERIFY(error.contains(QLatin1String("newer")));
}

void TestLibrary::maskRoundTrip()
{
    QTemporaryDir tmp;
    Library library = openFresh(tmp);
    Mask mask{{}, QStringLiteral("Night"), {{Stage::BootMenu, Slot{Slot::Kind::Import, QStringLiteral("abc")}}, {Stage::PlasmaSplash, Slot{}}}};
    QString error;
    QVERIFY(library.saveMask(mask, error));
    QVERIFY(!mask.id.isEmpty());

    QStringList errors;
    const QList<Mask> loaded = library.masks(errors);
    QVERIFY(errors.isEmpty());
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded[0].id, mask.id);
    QCOMPARE(loaded[0].name, QStringLiteral("Night"));
    QCOMPARE(loaded[0].stageSlots.value(Stage::BootMenu).ref, QStringLiteral("abc"));
    QVERIFY(loaded[0].stageSlots.value(Stage::PlasmaSplash).kind == Slot::Kind::LeaveAsIs);
    QVERIFY(!loaded[0].stageSlots.contains(Stage::BootSplash));

    Mask unnamed;
    QVERIFY(!library.saveMask(unnamed, error));
}

void TestLibrary::unreadableMaskIsReported()
{
    QTemporaryDir tmp;
    Library library = openFresh(tmp);
    writeFile(library.path() + QStringLiteral("/masks/broken.json"), "{ not json");
    writeFile(library.path() + QStringLiteral("/masks/badslot.json"), R"({"name": "X", "slots": {"bootMenu": {"kind": "import"}}})");
    QStringList errors;
    QVERIFY(library.masks(errors).isEmpty());
    QCOMPARE(errors.size(), 2);
}

void TestLibrary::historyKeepsNewest()
{
    QTemporaryDir tmp;
    Library library = openFresh(tmp);
    QString error;
    Settings settings;
    settings.historySize = 3;
    QVERIFY(library.saveSettings(settings, error));

    for (int i = 1; i <= 5; ++i) {
        HistoryEntry entry{0, QDateTime::currentDateTimeUtc(), QStringLiteral("apply %1").arg(i), {{QStringLiteral("i"), i}}};
        QVERIFY2(library.addHistoryEntry(entry, error), qPrintable(error));
        QCOMPARE(entry.number, i);
    }

    QStringList errors;
    const QList<HistoryEntry> history = library.history(errors);
    QVERIFY(errors.isEmpty());
    QCOMPARE(history.size(), 3);
    QCOMPARE(history.first().number, 5);
    QCOMPARE(history.last().number, 3);
    QCOMPARE(history.last().state.value(QStringLiteral("i")).toInt(), 3);
}

void TestLibrary::originalIsCapturedOnce()
{
    QTemporaryDir tmp;
    Library library = openFresh(tmp);
    QString error;
    QVERIFY(!library.original(Stage::BootSplash));
    QVERIFY(library.saveOriginalIfMissing(Stage::BootSplash, {{QStringLiteral("theme"), QStringLiteral("fallout")}}, error));
    QVERIFY(library.saveOriginalIfMissing(Stage::BootSplash, {{QStringLiteral("theme"), QStringLiteral("later")}}, error));
    QVERIFY(library.saveOriginalIfMissing(Stage::BootMenu, {{QStringLiteral("dropIn"), false}}, error));

    QCOMPARE(library.original(Stage::BootSplash)->value(QStringLiteral("theme")).toString(), QStringLiteral("fallout"));
    QVERIFY(library.original(Stage::BootMenu));
    QVERIFY(!library.original(Stage::PlasmaSplash));
}

void TestLibrary::removedMaskGoesToTrash()
{
    // The trash must be the one inside the repo (dev/env.sh, tests/CMakeLists.txt).
    const QString trash = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash");
    QVERIFY2(trash.startsWith(QStringLiteral(DEV_DIR)), qPrintable(trash));

    QTemporaryDir tmp;
    Library library = openFresh(tmp);
    Mask mask{{}, QStringLiteral("Gone"), {}};
    QString error;
    QVERIFY(library.saveMask(mask, error));
    QString trashed;
    QVERIFY2(library.removeMask(mask.id, error, &trashed), qPrintable(error));
    QVERIFY(trashed.startsWith(trash));
    QVERIFY(QFile::exists(trashed));
    QStringList errors;
    QVERIFY(library.masks(errors).isEmpty());
    QFile::remove(trashed);
    QFile::remove(trash + QStringLiteral("/info/") + QFileInfo(trashed).fileName() + QStringLiteral(".trashinfo"));
}

void TestLibrary::configuredPath()
{
    QCOMPARE(Library::configuredPath(), Library::defaultPath());
    QString error;
    QVERIFY(Library::setConfiguredPath(QStringLiteral("/somewhere/else"), error));
    QCOMPARE(Library::configuredPath(), QStringLiteral("/somewhere/else"));
    QVERIFY(Library::setConfiguredPath(Library::defaultPath(), error)); // back to the default: the key goes away
    QCOMPARE(Library::configuredPath(), Library::defaultPath());
}

void TestLibrary::relocate()
{
    QTemporaryDir tmp;
    QString error;
    auto library = Library::open(tmp.filePath(QStringLiteral("a")), error);
    QVERIFY(library);
    Mask mask{{}, QStringLiteral("Moving"), {}};
    QVERIFY(library->saveMask(mask, error));

    // An empty or missing folder: the library moves.
    const auto moved = Library::relocate(tmp.filePath(QStringLiteral("a")), tmp.filePath(QStringLiteral("b/lib")), error);
    QVERIFY2(moved, qPrintable(error));
    QCOMPARE(*moved, tmp.filePath(QStringLiteral("b/lib")));
    QVERIFY(!QFileInfo::exists(tmp.filePath(QStringLiteral("a"))));
    QStringList errors;
    QCOMPARE(Library::open(*moved, error)->masks(errors).first().name, QStringLiteral("Moving"));

    // A folder with a library: used as it is, nothing moves.
    QVERIFY(Library::open(tmp.filePath(QStringLiteral("c")), error));
    QCOMPARE(*Library::relocate(*moved, tmp.filePath(QStringLiteral("c")), error), tmp.filePath(QStringLiteral("c")));
    QVERIFY(QFileInfo::exists(*moved + QStringLiteral("/library.json")));

    // Other files, or the library's own subfolder: refused.
    QDir().mkpath(tmp.filePath(QStringLiteral("d")));
    writeFile(tmp.filePath(QStringLiteral("d/notes.txt")), "x");
    QVERIFY(!Library::relocate(*moved, tmp.filePath(QStringLiteral("d")), error));
    QVERIFY(!Library::relocate(*moved, *moved + QStringLiteral("/inside"), error));
}

void TestLibrary::migratesFormat1()
{
    QTemporaryDir tmp;
    writeFile(tmp.filePath(QStringLiteral("library.json")), R"({"format": 1, "settings": {"historySize": 4}})");
    QDir(tmp.path()).mkpath(QStringLiteral("masks"));
    QString error;
    const auto library = Library::open(tmp.path(), error);
    QVERIFY2(library, qPrintable(error));
    QCOMPARE(library->settings().historySize, 4);
    QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("designs"))).isDir());
    QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("assets"))).isDir());
    QFile manifest(tmp.filePath(QStringLiteral("library.json")));
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    QVERIFY(manifest.readAll().contains("\"format\": 2"));
}

void TestLibrary::designVersions()
{
    QTemporaryDir tmp;
    Library library = openFresh(tmp);
    QString error;
    Design design;
    design.stage = Stage::PlasmaSplash;
    QVERIFY(!library.saveDesign(design, error)); // needs a name

    design.name = QStringLiteral("Dawn");
    design.content = {{QStringLiteral("background"), QStringLiteral("#102030")}};
    QVERIFY2(library.saveDesign(design, error), qPrintable(error));
    QVERIFY(!design.id.isEmpty());
    QCOMPARE(design.version, 1);
    QVERIFY(design.savedAt.isValid());

    design.content = {{QStringLiteral("background"), QStringLiteral("#ffffff")}};
    QVERIFY(library.saveDesign(design, error));
    QCOMPARE(design.version, 2);

    QCOMPARE(library.designVersions(design.id), (QList<int>{1, 2}));
    const auto current = library.design(design.id, 0, error);
    QVERIFY2(current, qPrintable(error));
    QCOMPARE(current->version, 2);
    QCOMPARE(current->stage, Stage::PlasmaSplash);
    QCOMPARE(current->content, design.content);
    QCOMPARE(library.design(design.id, 1, error)->content.value(QStringLiteral("background")).toString(), QStringLiteral("#102030"));
    QVERIFY(!library.design(design.id, 3, error));

    QStringList errors;
    const QList<Design> all = library.designs(errors);
    QVERIFY(errors.isEmpty());
    QCOMPARE(all.size(), 1);
    QCOMPARE(all[0].name, QStringLiteral("Dawn"));
}

void TestLibrary::draftRecovery()
{
    QTemporaryDir tmp;
    Library library = openFresh(tmp);
    QString error;
    QStringList errors;

    // A new design that was never saved: the draft gets an id, but it isn't a design yet.
    Design fresh;
    fresh.stage = Stage::BootSplash;
    fresh.name = QStringLiteral("Unsaved");
    QVERIFY2(library.saveDraft(fresh, error), qPrintable(error));
    QVERIFY(!fresh.id.isEmpty());
    QVERIFY(library.designs(errors).isEmpty());
    QList<Design> drafts = library.drafts(errors);
    QCOMPARE(drafts.size(), 1);
    QCOMPARE(drafts[0].id, fresh.id);
    QCOMPARE(drafts[0].version, 0);

    // Saving makes it a version and clears the draft.
    QVERIFY(library.saveDesign(fresh, error));
    QVERIFY(library.drafts(errors).isEmpty());

    // A draft of a saved design keeps the version it started from.
    fresh.content = {{QStringLiteral("x"), 1}};
    QVERIFY(library.saveDraft(fresh, error));
    drafts = library.drafts(errors);
    QCOMPARE(drafts.size(), 1);
    QCOMPARE(drafts[0].version, 1);
    QCOMPARE(drafts[0].content, fresh.content);
    library.clearDraft(fresh.id);
    QVERIFY(library.drafts(errors).isEmpty());
    QVERIFY(errors.isEmpty());
}

void TestLibrary::assetsAreStoredOnce()
{
    QTemporaryDir tmp;
    Library library = openFresh(tmp);
    writeFile(tmp.filePath(QStringLiteral("logo.PNG")), "first");
    writeFile(tmp.filePath(QStringLiteral("copy.png")), "first");
    writeFile(tmp.filePath(QStringLiteral("other.png")), "second");
    QString error;

    const auto a = library.addAsset(tmp.filePath(QStringLiteral("logo.PNG")), Asset::Kind::Image, QStringLiteral("Logo"), error);
    QVERIFY2(a, qPrintable(error));
    QCOMPARE(a->suffix, QStringLiteral("png"));
    QCOMPARE(a->source, tmp.filePath(QStringLiteral("logo.PNG")));
    const auto again = library.addAsset(tmp.filePath(QStringLiteral("copy.png")), Asset::Kind::Image, QStringLiteral("Copy"), error);
    QCOMPARE(again->hash, a->hash);
    QCOMPARE(again->name, QStringLiteral("Logo")); // the first registration stays
    QVERIFY(library.addAsset(tmp.filePath(QStringLiteral("other.png")), Asset::Kind::Image, QStringLiteral("Other"), error));
    QVERIFY(!library.addAsset(tmp.filePath(QStringLiteral("missing.png")), Asset::Kind::Image, QStringLiteral("Missing"), error));

    QStringList errors;
    const QList<Asset> assets = library.assets(errors);
    QVERIFY(errors.isEmpty());
    QCOMPARE(assets.size(), 2);
    QFile stored(library.assetPath(*a));
    QVERIFY(stored.open(QIODevice::ReadOnly));
    QCOMPARE(stored.readAll(), QByteArray("first"));
    QCOMPARE(QFileInfo(stored).fileName(), a->hash + QStringLiteral(".png"));
}

void TestLibrary::fontWithLicenceNextToIt()
{
    QTemporaryDir tmp;
    Library library = openFresh(tmp);
    QDir(tmp.path()).mkpath(QStringLiteral("fonts"));
    writeFile(tmp.filePath(QStringLiteral("fonts/Mask-Regular.ttf")), QByteArray("\x00\x01\x00\x00", 4) + "glyphs");
    writeFile(tmp.filePath(QStringLiteral("fonts/OFL.txt")), "SIL Open Font License 1.1");
    QString error;

    const auto font = library.addFont(tmp.filePath(QStringLiteral("fonts/Mask-Regular.ttf")), QStringLiteral("Mask Regular"), error);
    QVERIFY2(font, qPrintable(error));
    QCOMPARE(font->kind, Asset::Kind::Font);
    QVERIFY(!font->licence.isEmpty());
    QStringList errors;
    QList<Asset> assets = library.assets(errors);
    const auto licence = std::find_if(assets.cbegin(), assets.cend(), [&](const Asset &a) { return a.hash == font->licence; });
    QVERIFY(licence != assets.cend());
    QCOMPARE(licence->kind, Asset::Kind::Licence);
    QFile text(library.assetPath(*licence));
    QVERIFY(text.open(QIODevice::ReadOnly));
    QCOMPARE(text.readAll(), QByteArray("SIL Open Font License 1.1"));

    // No licence anywhere: the font is still added, without one.
    QDir(tmp.path()).mkpath(QStringLiteral("bare"));
    writeFile(tmp.filePath(QStringLiteral("bare/Plain.otf")), "OTTOplain");
    const auto plain = library.addFont(tmp.filePath(QStringLiteral("bare/Plain.otf")), QStringLiteral("Plain"), error);
    QVERIFY2(plain, qPrintable(error));
    QVERIFY(plain->licence.isEmpty());
}

void TestLibrary::notAFontIsRefused()
{
    QTemporaryDir tmp;
    Library library = openFresh(tmp);
    writeFile(tmp.filePath(QStringLiteral("fake.ttf")), "\x89PNG\r\n");
    QString error;
    QVERIFY(!library.addFont(tmp.filePath(QStringLiteral("fake.ttf")), QStringLiteral("Fake"), error));
    QVERIFY(error.contains(QLatin1String("not a TrueType or OpenType font")));
    QStringList errors;
    QVERIFY(library.assets(errors).isEmpty());
}

void TestLibrary::fontFromDebianPackage()
{
    // A packaged font: its licence is the package's copyright file. Brackets in the
    // name must not act as a dpkg-query pattern.
    QString file;
    for (const QString &candidate : {QStringLiteral("/usr/share/fonts/truetype/ubuntu/Ubuntu[wdth,wght].ttf"),
                                     QStringLiteral("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf")}) {
        if (QFile::exists(candidate)) {
            file = candidate;
            break;
        }
    }
    if (file.isEmpty())
        QSKIP("Neither fonts-ubuntu nor fonts-noto-core is installed.");

    QTemporaryDir tmp;
    Library library = openFresh(tmp);
    QString error;
    const auto font = library.addFont(file, QStringLiteral("System font"), error);
    QVERIFY2(font, qPrintable(error));
    QVERIFY(!font->licence.isEmpty());
    QStringList errors;
    const QList<Asset> assets = library.assets(errors);
    const auto licence = std::find_if(assets.cbegin(), assets.cend(), [&](const Asset &a) { return a.hash == font->licence; });
    QVERIFY(licence != assets.cend());
    QVERIFY(licence->source.startsWith(QLatin1String("/usr/share/doc/")));
    QVERIFY(licence->source.endsWith(QLatin1String("/copyright")));
}

void TestLibrary::resolvesSharedElements()
{
    QTemporaryDir tmp;
    Library library = openFresh(tmp);
    QString error;
    const auto image = library.addAsset(QStringLiteral(FIXTURES_DIR "/assets/logo.png"), Asset::Kind::Image, QStringLiteral("Logo"), error);
    QVERIFY2(image, qPrintable(error));
    SharedElement logo{{}, SharedElement::Kind::Logo, QStringLiteral("Brand"), image->hash, {}};
    SharedElement palette{{}, SharedElement::Kind::Palette, QStringLiteral("Night"), {},
                          {{QStringLiteral("background"), QStringLiteral("#101010")}, {QStringLiteral("accent"), QStringLiteral("#ff0000")}, {QStringLiteral("text"), QStringLiteral("#ffffff")}}};
    QVERIFY(library.saveSharedElement(logo, error));
    QVERIFY(library.saveSharedElement(palette, error));
    QStringList errors;
    QCOMPARE(library.sharedElements(errors).size(), 2);
    QCOMPARE(library.sharedElements(errors).first().name, QStringLiteral("Brand")); // sorted by name

    Design design;
    design.stage = Stage::PlasmaSplash;
    design.name = QStringLiteral("Dusk");
    design.content = {
        {QStringLiteral("palette"), palette.id},
        {QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("@background")}}},
        {QStringLiteral("elements"),
         QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("image")}, {QStringLiteral("shared"), logo.id}},
                    QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), QStringLiteral("@home")}, {QStringLiteral("color"), QStringLiteral("@text")}},
                    QJsonObject{{QStringLiteral("type"), QStringLiteral("spinner")}, {QStringLiteral("color"), QStringLiteral("@accent")}}}},
    };
    QStringList problems;
    const Design resolved = library.resolved(design, problems);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    QCOMPARE(resolved.content.value(QStringLiteral("background")).toObject().value(QStringLiteral("color")).toString(), QStringLiteral("#101010"));
    const QJsonArray elements = resolved.content.value(QStringLiteral("elements")).toArray();
    QCOMPARE(elements[0].toObject().value(QStringLiteral("asset")).toString(), image->hash);
    QCOMPARE(elements[1].toObject().value(QStringLiteral("text")).toString(), QStringLiteral("@home")); // only colours are palette colours
    QCOMPARE(elements[1].toObject().value(QStringLiteral("color")).toString(), QStringLiteral("#ffffff"));
    QCOMPARE(elements[2].toObject().value(QStringLiteral("color")).toString(), QStringLiteral("#ff0000"));

    // The stamp follows what is built: the palette, not the version.
    const QString stamp = Library::stamp(resolved);
    design.version = 7;
    QCOMPARE(Library::stamp(library.resolved(design, problems)), stamp);
    palette.colours.insert(QStringLiteral("accent"), QStringLiteral("#00ff00"));
    QVERIFY(library.saveSharedElement(palette, error));
    QVERIFY(Library::stamp(library.resolved(design, problems)) != stamp);

    // What can go missing.
    design.content.insert(QStringLiteral("palette"), QStringLiteral("gone"));
    QJsonArray broken = design.content.value(QStringLiteral("elements")).toArray();
    broken[0] = QJsonObject{{QStringLiteral("type"), QStringLiteral("image")}, {QStringLiteral("shared"), QStringLiteral("gone")}};
    design.content.insert(QStringLiteral("elements"), broken);
    problems.clear();
    library.resolved(design, problems);
    QCOMPARE(problems, (QStringList{QStringLiteral("The design's palette is no longer in the library."),
                                    QStringLiteral("The design uses a shared logo that is no longer in the library.")}));
}

EKPO_TEST_MAIN(TestLibrary)
#include "tst_library.moc"
