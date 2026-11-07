#include "golden.h"
#include "importer.h"
#include "testmain.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>

namespace {

const QString referenceRoot = QStringLiteral(FIXTURES_DIR "/roots/reference");
const QString referenceHome = referenceRoot + QStringLiteral("/home");

// Everything about an import except its id and time, for golden files.
QJsonObject summary(const QString &source, const ImportResult &result)
{
    QJsonArray themes;
    for (const ImportedTheme &t : result.themes) {
        themes.append(QJsonObject{
            {QStringLiteral("stage"), stageKey(t.stage)},
            {QStringLiteral("name"), t.name},
            {QStringLiteral("installedId"), t.installedId},
            {QStringLiteral("verdict"), verdictKey(t.verdict)},
            {QStringLiteral("report"), QJsonArray::fromStringList(t.report)},
        });
    }
    QStringList errors = result.errors;
    errors.replaceInStrings(QStringLiteral(FIXTURES_DIR), QStringLiteral("<fixtures>")); // keep golden files independent of the checkout path
    return {{QStringLiteral("source"), source}, {QStringLiteral("themes"), themes}, {QStringLiteral("errors"), QJsonArray::fromStringList(errors)}};
}

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
}

} // namespace

class TestImporter : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void fixtureThemes();
    void installedOnReferenceSystem();
    void archive();
    void reimportUpdatesTheSameEntry();
    void linkLeavingTheThemeIsSkipped();
    void twoStepNeedsLockEntryAndBullet();
    void removeMovesToTrash();

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
    std::optional<Library> m_library;
};

void TestImporter::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    QString error;
    m_library = Library::open(m_tmp->filePath(QStringLiteral("lib")), error);
    QVERIFY2(m_library, qPrintable(error));
}

void TestImporter::fixtureThemes()
{
    QJsonArray all;
    const QDir themes(QStringLiteral(FIXTURES_DIR "/themes"));
    for (const QString &name : themes.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        all.append(summary(name, importThemes(*m_library, themes.filePath(name), referenceRoot, referenceHome)));
    QVERIFY(matchesGolden(QStringLiteral("import-fixtures.json"), QJsonDocument(all).toJson()));

    // The verbatim copy of the nested theme holds only the theme folder.
    QStringList errors;
    for (const ImportedTheme &t : m_library->imports(errors)) {
        if (t.name == QLatin1String("Nested Fixture")) {
            QVERIFY(QFile::exists(m_library->importFilesPath(t.id) + QStringLiteral("/Nested.plymouth")));
            QVERIFY(!QFile::exists(m_library->importFilesPath(t.id) + QStringLiteral("/install.sh")));
        }
    }
    QVERIFY(errors.isEmpty());
}

void TestImporter::installedOnReferenceSystem()
{
    const SystemState state = probeSystem(referenceRoot, referenceHome);
    QJsonArray all;
    for (const auto &[stage, folder] : installedThemeFolders(state, referenceRoot, referenceHome)) {
        const QString source = QDir(referenceRoot).relativeFilePath(folder);
        all.append(summary(source, importThemes(*m_library, folder, referenceRoot, referenceHome)));
    }
    QVERIFY(matchesGolden(QStringLiteral("import-installed.json"), QJsonDocument(all).toJson()));
}

void TestImporter::archive()
{
    const QString archivePath = m_tmp->filePath(QStringLiteral("grub-ok.tar.gz"));
    QCOMPARE(QProcess::execute(QStringLiteral("tar"), {QStringLiteral("-czf"), archivePath, QStringLiteral("-C"), QStringLiteral(FIXTURES_DIR "/themes"), QStringLiteral("grub-ok")}), 0);

    const ImportResult result = importThemes(*m_library, archivePath, referenceRoot, referenceHome);
    QVERIFY(result.errors.isEmpty());
    QCOMPARE(result.themes.size(), 1);
    QCOMPARE(result.themes[0].sourcePath, QFileInfo(archivePath).canonicalFilePath());
    QVERIFY(result.themes[0].installedId.isEmpty());
    QVERIFY(QFile::exists(m_library->importFilesPath(result.themes[0].id) + QStringLiteral("/background.png")));
    QVERIFY(QDir(m_library->path() + QStringLiteral("/staging")).isEmpty());
}

void TestImporter::reimportUpdatesTheSameEntry()
{
    const QString theme = m_tmp->filePath(QStringLiteral("src/MyTheme"));
    writeFile(theme + QStringLiteral("/theme.txt"), "desktop-image: \"bg.png\"\n");
    const ImportResult first = importThemes(*m_library, theme, referenceRoot, referenceHome);
    QCOMPARE(first.themes.size(), 1);
    QCOMPARE(first.themes[0].verdict, ImportedTheme::Verdict::Warnings);

    writeFile(theme + QStringLiteral("/bg.png"), "png");
    const ImportResult second = importThemes(*m_library, theme, referenceRoot, referenceHome);
    QCOMPARE(second.themes.size(), 1);
    QCOMPARE(second.themes[0].id, first.themes[0].id);
    QCOMPARE(second.themes[0].verdict, ImportedTheme::Verdict::Works);
    QVERIFY(QFile::exists(m_library->importFilesPath(first.themes[0].id) + QStringLiteral("/bg.png")));

    QStringList errors;
    QCOMPARE(m_library->imports(errors).size(), 1);
}

void TestImporter::linkLeavingTheThemeIsSkipped()
{
    const QString theme = m_tmp->filePath(QStringLiteral("src/Linky"));
    writeFile(theme + QStringLiteral("/theme.txt"), "# no images\n");
    writeFile(theme + QStringLiteral("/inside.png"), "png");
    QVERIFY(QFile::link(QStringLiteral("/etc/hostname"), theme + QStringLiteral("/outside.png")));
    QVERIFY(QFile::link(QStringLiteral("inside.png"), theme + QStringLiteral("/alias.png")));

    const ImportResult result = importThemes(*m_library, theme, referenceRoot, referenceHome);
    QCOMPARE(result.themes.size(), 1);
    const QString files = m_library->importFilesPath(result.themes[0].id);
    QVERIFY(!QFile::exists(files + QStringLiteral("/outside.png")));
    QVERIFY(QFileInfo(files + QStringLiteral("/alias.png")).isFile());
    QVERIFY(!QFileInfo(files + QStringLiteral("/alias.png")).isSymLink());
    QVERIFY(result.themes[0].report.contains(QStringLiteral("Not copied: the link outside.png points outside the theme folder or to a folder.")));
}

void TestImporter::twoStepNeedsLockEntryAndBullet()
{
    using Verdict = ImportedTheme::Verdict;
    const QString pluginDir = QStringLiteral("/usr/lib/x86_64-linux-gnu/plymouth");
    const QString themesDir = QStringLiteral("/usr/share/plymouth/themes");
    const auto plymouthFile = [](const QString &imageDir) {
        return QStringLiteral("[Plymouth Theme]\nName=Bare\nModuleName=two-step\n\n[two-step]\nImageDir=%1\n").arg(imageDir).toUtf8();
    };

    // A copy gets ImageDir rewritten to its own folder, so the images must be in the theme.
    const QString copy = m_tmp->filePath(QStringLiteral("src/Bare"));
    writeFile(copy + QStringLiteral("/Bare.plymouth"), plymouthFile(themesDir + QStringLiteral("/Bare")));
    writeFile(copy + QStringLiteral("/lock.png"), "png");
    ImportResult result = importThemes(*m_library, copy, referenceRoot, referenceHome);
    QCOMPARE(result.themes.size(), 1);
    QCOMPARE(result.themes[0].verdict, Verdict::Broken);
    QVERIFY(result.themes[0].report.contains(
        QStringLiteral("two-step shows no splash without lock.png, entry.png and bullet.png; this theme lacks entry.png and bullet.png.")));
    writeFile(copy + QStringLiteral("/entry.png"), "png");
    writeFile(copy + QStringLiteral("/bullet.png"), "png");
    QCOMPARE(importThemes(*m_library, copy, referenceRoot, referenceHome).themes.value(0).verdict, Verdict::Works);

    // Without ImageDir, two-step looks for "(null)/lock.png".
    writeFile(copy + QStringLiteral("/Bare.plymouth"), "[Plymouth Theme]\nName=Bare\nModuleName=two-step\n");
    result = importThemes(*m_library, copy, referenceRoot, referenceHome);
    QCOMPARE(result.themes.value(0).verdict, Verdict::Broken);
    QVERIFY(result.themes[0].report.contains(QStringLiteral("A two-step theme needs an ImageDir.")));

    // A theme applied in place loads its images from ImageDir, here another theme's folder.
    const QString root = m_tmp->filePath(QStringLiteral("root"));
    writeFile(root + pluginDir + QStringLiteral("/two-step.so"), "so");
    const QString shared = root + themesDir + QStringLiteral("/shared");
    writeFile(shared + QStringLiteral("/lock.png"), "png");
    writeFile(shared + QStringLiteral("/entry.png"), "png");
    writeFile(shared + QStringLiteral("/bullet.png"), "png");
    const QString pointer = root + themesDir + QStringLiteral("/pointer");
    writeFile(pointer + QStringLiteral("/pointer.plymouth"), plymouthFile(themesDir + QStringLiteral("//shared")));
    result = importThemes(*m_library, pointer, root, referenceHome);
    QCOMPARE(result.themes.size(), 1);
    QCOMPARE(result.themes[0].installedId, QStringLiteral("pointer"));
    QCOMPARE(result.themes[0].verdict, Verdict::Warnings);

    QVERIFY(QFile::remove(shared + QStringLiteral("/bullet.png")));
    result = importThemes(*m_library, pointer, root, referenceHome);
    QCOMPARE(result.themes[0].verdict, Verdict::Broken);
    QVERIFY(result.themes[0].report.contains(
        QStringLiteral("two-step shows no splash without lock.png, entry.png and bullet.png; /usr/share/plymouth/themes/shared lacks bullet.png.")));
}

void TestImporter::removeMovesToTrash()
{
    const ImportResult result = importThemes(*m_library, QStringLiteral(FIXTURES_DIR "/themes/grub-ok"), referenceRoot, referenceHome);
    QCOMPARE(result.themes.size(), 1);
    QString error;
    QString trashed;
    QVERIFY2(m_library->removeImport(result.themes[0].id, error, &trashed), qPrintable(error));
    QVERIFY(trashed.startsWith(QStringLiteral(DEV_DIR)));
    QVERIFY(QDir(trashed).removeRecursively());
    const QString info = QFileInfo(trashed).path() + QStringLiteral("/../info/") + QFileInfo(trashed).fileName() + QStringLiteral(".trashinfo");
    QFile::remove(info);
    QStringList errors;
    QVERIFY(m_library->imports(errors).isEmpty());
}

EKPO_TEST_MAIN(TestImporter)
#include "tst_importer.moc"
