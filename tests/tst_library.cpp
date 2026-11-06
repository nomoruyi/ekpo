#include "library.h"
#include "testmain.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>

class TestLibrary : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void createsAndReopens();
    void refusesForeignFolder();
    void refusesNewerFormat();
    void personaRoundTrip();
    void unreadablePersonaIsReported();
    void historyKeepsNewest();
    void originalIsCapturedOnce();
    void removedPersonaGoesToTrash();

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
    writeFile(tmp.filePath(QStringLiteral("library.json")), R"({"format": 2})");
    QString error;
    QVERIFY(!Library::open(tmp.path(), error));
    QVERIFY(error.contains(QLatin1String("newer")));
}

void TestLibrary::personaRoundTrip()
{
    QTemporaryDir tmp;
    Library library = openFresh(tmp);
    Persona persona{{}, QStringLiteral("Night"), {{Stage::BootMenu, Slot{Slot::Kind::Import, QStringLiteral("abc")}}, {Stage::PlasmaSplash, Slot{}}}};
    QString error;
    QVERIFY(library.savePersona(persona, error));
    QVERIFY(!persona.id.isEmpty());

    QStringList errors;
    const QList<Persona> loaded = library.personas(errors);
    QVERIFY(errors.isEmpty());
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded[0].id, persona.id);
    QCOMPARE(loaded[0].name, QStringLiteral("Night"));
    QCOMPARE(loaded[0].stageSlots.value(Stage::BootMenu).ref, QStringLiteral("abc"));
    QVERIFY(loaded[0].stageSlots.value(Stage::PlasmaSplash).kind == Slot::Kind::LeaveAsIs);
    QVERIFY(!loaded[0].stageSlots.contains(Stage::BootSplash));

    Persona unnamed;
    QVERIFY(!library.savePersona(unnamed, error));
}

void TestLibrary::unreadablePersonaIsReported()
{
    QTemporaryDir tmp;
    Library library = openFresh(tmp);
    writeFile(library.path() + QStringLiteral("/personas/broken.json"), "{ not json");
    writeFile(library.path() + QStringLiteral("/personas/badslot.json"), R"({"name": "X", "slots": {"bootMenu": {"kind": "import"}}})");
    QStringList errors;
    QVERIFY(library.personas(errors).isEmpty());
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

void TestLibrary::removedPersonaGoesToTrash()
{
    // The trash must be the one inside the repo (dev/env.sh, tests/CMakeLists.txt).
    const QString trash = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash");
    QVERIFY2(trash.startsWith(QStringLiteral(DEV_DIR)), qPrintable(trash));

    QTemporaryDir tmp;
    Library library = openFresh(tmp);
    Persona persona{{}, QStringLiteral("Gone"), {}};
    QString error;
    QVERIFY(library.savePersona(persona, error));
    QString trashed;
    QVERIFY2(library.removePersona(persona.id, error, &trashed), qPrintable(error));
    QVERIFY(trashed.startsWith(trash));
    QVERIFY(QFile::exists(trashed));
    QStringList errors;
    QVERIFY(library.personas(errors).isEmpty());
    QFile::remove(trashed);
    QFile::remove(trash + QStringLiteral("/info/") + QFileInfo(trashed).fileName() + QStringLiteral(".trashinfo"));
}

EKPO_TEST_MAIN(TestLibrary)
#include "tst_library.moc"
