#include "exporter.h"
#include "golden.h"
#include "testmain.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

class TestExporter : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void namesLicences();
    void exportsPlasmaSplash();
    void exportsBootSplash();
    void exportsBootMenu();
    void writesArchive();
    void exportsMask();
    void exportsLibrary();

private:
    static QStringList paths(const ExportPlan &plan)
    {
        QStringList result;
        for (const PayloadFile &f : plan.files)
            result << f.path;
        return result;
    }
    static QByteArray file(const ExportPlan &plan, const QString &path)
    {
        for (const PayloadFile &f : plan.files)
            if (f.path == path)
                return f.data;
        return {};
    }
    static QString tar(const QStringList &arguments)
    {
        QProcess p;
        p.start(QStringLiteral("tar"), arguments);
        p.waitForFinished();
        return QString::fromUtf8(p.readAllStandardOutput());
    }
    // A saved design; version 1.
    QString save(Stage stage, const QString &name, const QJsonObject &content)
    {
        Design d;
        d.name = name;
        d.stage = stage;
        d.content = content;
        QString error;
        if (!m_library->saveDesign(d, error))
            qFatal("saveDesign: %s", qPrintable(error));
        return d.id;
    }

    std::unique_ptr<QTemporaryDir> m_tmp;
    std::optional<Library> m_library;
    const QSize m_screen{2560, 1440};
};

void TestExporter::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    QString error;
    m_library = Library::open(m_tmp->filePath(QStringLiteral("lib")), error);
    QVERIFY2(m_library, qPrintable(error));
}

void TestExporter::namesLicences()
{
    bool free = false;
    QCOMPARE(licenceName("Copyright 2010 The Noto Authors\nThis Font Software is licensed under the SIL Open Font License, Version 1.1.", &free),
             QStringLiteral("SIL Open Font License"));
    QVERIFY(free);
    QCOMPARE(licenceName("Files: *\nCopyright: 2012 Google\nLicense: OFL-1.1\n", &free), QStringLiteral("SIL Open Font License")); // Debian's copyright file
    QCOMPARE(licenceName("Licensed under the Apache License, Version 2.0", &free), QStringLiteral("Apache License"));
    QCOMPARE(licenceName("GNU LESSER GENERAL PUBLIC LICENSE Version 2.1", &free), QStringLiteral("GNU LGPL"));
    QCOMPARE(licenceName("This work is licensed under CC BY-NC 4.0 (Attribution-NonCommercial)", &free),
             QStringLiteral("Creative Commons, non-commercial or no derivatives"));
    QVERIFY(!free);
    QCOMPARE(licenceName("Copyright 2024 Somebody. All rights reserved.", &free), QString());
    QVERIFY(!free);
}

// Its look-and-feel package, README.md and LICENSE in one folder: the archive installs as
// it is. The font and the image keep their own licences.
void TestExporter::exportsPlasmaSplash()
{
    QDir().mkpath(m_tmp->filePath(QStringLiteral("font")));
    QFile font(m_tmp->filePath(QStringLiteral("font/Mask-Regular.ttf")));
    QVERIFY(font.open(QIODevice::WriteOnly) && font.write(QByteArray("\x00\x01\x00\x00", 4) + "glyphs") > 0);
    font.close();
    QFile ofl(m_tmp->filePath(QStringLiteral("font/OFL.txt")));
    QVERIFY(ofl.open(QIODevice::WriteOnly) && ofl.write("SIL OPEN FONT LICENSE Version 1.1") > 0);
    ofl.close();
    QString error;
    const auto fontAsset = m_library->addFont(font.fileName(), QStringLiteral("Mask Regular"), error);
    QVERIFY2(fontAsset, qPrintable(error));
    const auto image = m_library->addAsset(QStringLiteral(FIXTURES_DIR "/assets/logo.png"), Asset::Kind::Image, QStringLiteral("Logo"), error);
    QVERIFY2(image, qPrintable(error));
    const QString id = save(Stage::PlasmaSplash, QStringLiteral("Dawn"),
                            {{QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#102030")}}},
                             {QStringLiteral("elements"),
                              QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), QStringLiteral("Hi")}, {QStringLiteral("font"), fontAsset->hash},
                                                     {QStringLiteral("x"), 10}, {QStringLiteral("y"), 10}, {QStringLiteral("width"), 80}, {QStringLiteral("height"), 10}},
                                         QJsonObject{{QStringLiteral("type"), QStringLiteral("image")}, {QStringLiteral("asset"), image->hash},
                                                     {QStringLiteral("x"), 40}, {QStringLiteral("y"), 40}, {QStringLiteral("width"), 20}, {QStringLiteral("height"), 20}}}}});

    const ExportPlan plan = exportDesign(*m_library, id, m_screen, {});
    QVERIFY2(plan.problems.isEmpty(), qPrintable(plan.problems.join(QLatin1Char('\n'))));
    QCOMPARE(plan.folder, QStringLiteral("ekpo-dawn-v1"));
    const QStringList files = paths(plan);
    for (const QString &f : files)
        QVERIFY2(f.startsWith(QStringLiteral("ekpo-dawn-v1/")), qPrintable(f)); // one folder on top
    QVERIFY(files.contains(QStringLiteral("ekpo-dawn-v1/metadata.json")));
    QVERIFY(files.contains(QStringLiteral("ekpo-dawn-v1/contents/splash/Splash.qml")));
    QVERIFY(matchesGolden(QStringLiteral("export-README-plasma.md"), file(plan, QStringLiteral("ekpo-dawn-v1/README.md"))));
    const QByteArray licence = file(plan, QStringLiteral("ekpo-dawn-v1/LICENSE"));
    QVERIFY(licence.startsWith("MIT License\n\nCopyright (c) 20"));
    QVERIFY(licence.contains(" nomoruyi\n"));

    QCOMPARE(plan.thirdParty.size(), 2);
    QCOMPARE(plan.thirdParty[0].kind, QStringLiteral("font"));
    QCOMPARE(plan.thirdParty[0].licence, QStringLiteral("SIL Open Font License"));
    QVERIFY(plan.thirdParty[0].free);
    QCOMPARE(plan.thirdParty[1].kind, QStringLiteral("image"));
    QCOMPARE(plan.thirdParty[1].licence, QString());
    QVERIFY(!plan.thirdParty[1].free);
}

// Built for the screen, with the paths of the folder it installs to.
void TestExporter::exportsBootSplash()
{
    const QString id = save(Stage::BootSplash, QStringLiteral("Night"),
                            {{QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#102030")}}},
                             {QStringLiteral("author"), QStringLiteral("Somebody")},
                             {QStringLiteral("licence"), QStringLiteral("CC-BY-4.0")},
                             {QStringLiteral("elements"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("spinner")}, {QStringLiteral("x"), 47.5},
                                                                                 {QStringLiteral("y"), 60}, {QStringLiteral("width"), 5}, {QStringLiteral("height"), 5}}}}});
    const ExportPlan plan = exportDesign(*m_library, id, m_screen, {});
    QVERIFY2(plan.problems.isEmpty(), qPrintable(plan.problems.join(QLatin1Char('\n'))));
    QCOMPARE(plan.folder, QStringLiteral("ekpo-night-v1"));
    QVERIFY(file(plan, QStringLiteral("ekpo-night-v1/ekpo-night-v1.plymouth")).contains("ImageDir=/usr/share/plymouth/themes/ekpo-night-v1"));
    QVERIFY(matchesGolden(QStringLiteral("export-README-bootsplash.md"), file(plan, QStringLiteral("ekpo-night-v1/README.md"))));
    QVERIFY(file(plan, QStringLiteral("ekpo-night-v1/LICENSE")).endsWith(" Somebody\n\nLicensed under CC-BY-4.0.\n"));
}

void TestExporter::exportsBootMenu()
{
    if (QStandardPaths::findExecutable(QStringLiteral("grub-mkfont")).isEmpty())
        QSKIP("grub-mkfont isn't installed (grub-common)");
    const QString id = save(Stage::BootMenu, QStringLiteral("Night Menu"),
                            {{QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#102030")}}},
                             {QStringLiteral("elements"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("menu")}, {QStringLiteral("size"), 2.4},
                                                                                 {QStringLiteral("x"), 30}, {QStringLiteral("y"), 30}, {QStringLiteral("width"), 40},
                                                                                 {QStringLiteral("height"), 40}}}}});
    const ExportPlan plan = exportDesign(*m_library, id, m_screen, {QStringLiteral("Kubuntu"), QStringLiteral("Advanced options for Kubuntu")});
    QVERIFY2(plan.problems.isEmpty(), qPrintable(plan.problems.join(QLatin1Char('\n'))));
    QCOMPARE(plan.folder, QStringLiteral("ekpo-night-menu-v1"));
    QVERIFY(paths(plan).contains(QStringLiteral("ekpo-night-menu-v1/theme.txt")));
    QVERIFY(matchesGolden(QStringLiteral("export-README-bootmenu.md"), file(plan, QStringLiteral("ekpo-night-menu-v1/README.md"))));
}

void TestExporter::writesArchive()
{
    const QString id = save(Stage::PlasmaSplash, QStringLiteral("Dawn"), {{QStringLiteral("elements"), QJsonArray()}});
    const ExportPlan plan = exportDesign(*m_library, id, m_screen, {});
    const QString archive = m_tmp->filePath(QStringLiteral("dawn.tar.gz"));
    QCOMPARE(writeArchive(plan.files, archive), QString());
    QVERIFY(!QFile::exists(archive + QStringLiteral(".part")));
    const QString listing = tar({QStringLiteral("-tvzf"), archive, QStringLiteral("--numeric-owner")});
    QVERIFY2(listing.contains(QStringLiteral("ekpo-dawn-v1/metadata.json")), qPrintable(listing));
    QVERIFY2(listing.contains(QStringLiteral(" 0/0 ")), qPrintable(listing)); // root's, as installed themes are
}

// A folder per stage: the design built, the imported theme as imported.
void TestExporter::exportsMask()
{
    const QString design = save(Stage::PlasmaSplash, QStringLiteral("Dawn"), {{QStringLiteral("elements"), QJsonArray()}});
    ImportedTheme theme;
    theme.stage = Stage::BootSplash;
    theme.name = QStringLiteral("Dots");
    theme.sourcePath = QStringLiteral(FIXTURES_DIR "/themes/plymouth-two-step/Dots");
    QString error;
    QVERIFY2(m_library->saveImport(theme, theme.sourcePath, error), qPrintable(error));
    Mask mask;
    mask.name = QStringLiteral("Retro");
    mask.stageSlots.insert(Stage::PlasmaSplash, Slot{Slot::Kind::Design, design});
    mask.stageSlots.insert(Stage::BootSplash, Slot{Slot::Kind::Import, theme.id});
    QVERIFY2(m_library->saveMask(mask, error), qPrintable(error));

    const ExportPlan plan = exportMask(*m_library, mask.id, m_screen, {});
    QVERIFY2(plan.problems.isEmpty(), qPrintable(plan.problems.join(QLatin1Char('\n'))));
    QCOMPARE(plan.folder, QStringLiteral("ekpo-mask-retro"));
    const QStringList files = paths(plan);
    QVERIFY(files.contains(QStringLiteral("ekpo-mask-retro/plasma-splash/ekpo-dawn-v1/metadata.json")));
    QVERIFY(files.contains(QStringLiteral("ekpo-mask-retro/plasma-splash/ekpo-dawn-v1/README.md")));
    QVERIFY(files.contains(QStringLiteral("ekpo-mask-retro/boot-splash/Dots/Dots.plymouth")));
    QVERIFY(matchesGolden(QStringLiteral("export-README-mask.md"), file(plan, QStringLiteral("ekpo-mask-retro/README.md"))));
    QCOMPARE(plan.thirdParty.size(), 1);
    QCOMPARE(plan.thirdParty.first().kind, QStringLiteral("theme"));
    QCOMPARE(plan.thirdParty.first().name, QStringLiteral("Dots"));
}

void TestExporter::exportsLibrary()
{
    save(Stage::PlasmaSplash, QStringLiteral("Dawn"), {{QStringLiteral("elements"), QJsonArray()}});
    const QString archive = m_tmp->filePath(QStringLiteral("library.tar.gz"));
    QCOMPARE(exportLibrary(*m_library, archive), QString());
    const QString listing = tar({QStringLiteral("-tzf"), archive});
    QVERIFY2(listing.contains(QStringLiteral("lib/library.json")), qPrintable(listing));
    QVERIFY(listing.contains(QStringLiteral("lib/designs/")));
    // Not into the library itself: tar would read what it writes.
    QCOMPARE(exportLibrary(*m_library, m_library->path() + QStringLiteral("/backup.tar.gz")), QStringLiteral("Save the archive outside the library folder."));
}

EKPO_GUI_TEST_MAIN(TestExporter)
#include "tst_exporter.moc"
