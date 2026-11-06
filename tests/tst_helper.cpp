#include "request.h"
#include "sysinfo.h"
#include "testmain.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <fcntl.h>
#include <unistd.h>

namespace {

const QByteArray png = QByteArrayLiteral("\x89PNG\r\n\x1a\n....");
const QString grubId = QStringLiteral("ekpo-night-v1");
const QString plymouthId = QStringLiteral("ekpo-dots-v1");

struct Run {
    int exitCode = -1;
    QJsonObject result;
    QStringList progress;
};

QList<PayloadFile> grubFiles(const QString &id = grubId, const QByteArray &background = png)
{
    return {{id + QStringLiteral("/theme.txt"), QByteArrayLiteral("desktop-image: \"bg.png\"\ntitle-text: \"\"\n")}, {id + QStringLiteral("/bg.png"), background}};
}

QList<PayloadFile> plymouthFiles(const QString &id = plymouthId)
{
    const QByteArray plymouth = QStringLiteral("[Plymouth Theme]\nName=Dots\nModuleName=two-step\n\n[two-step]\nImageDir=/usr/share/plymouth/themes/%1\n").arg(id).toUtf8();
    return {{id + QLatin1Char('/') + id + QStringLiteral(".plymouth"), plymouth}, {id + QStringLiteral("/throbber-0001.png"), png}};
}

QByteArray input(const Request &request, const QList<PayloadFile> &files = {})
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    buffer.write(serializeRequest(request) + '\n');
    if (request.operation == Request::Operation::Apply)
        writePayload(buffer, files);
    return buffer.data();
}

Request grubRequest(const QString &theme = grubId)
{
    Request r;
    if (theme.startsWith(QLatin1String("ekpo-")))
        r.themes = {{theme, ThemeTarget::Grub}};
    r.grub = Request::Grub{false, theme, QStringLiteral("2560x1440,1920x1080,1280x720,auto"), 5, QStringLiteral("menu")};
    return r;
}

Request plymouthRequest(const QString &theme = plymouthId)
{
    Request r;
    if (theme.startsWith(QLatin1String("ekpo-")))
        r.themes = {{theme, ThemeTarget::Plymouth}};
    r.plymouth = Request::Plymouth{QStringLiteral("manual"), theme, theme};
    return r;
}

// The paths whose content differs, for readable failures.
QStringList changedPaths(const QMap<QString, QByteArray> &before, const QMap<QString, QByteArray> &after)
{
    QStringList changed;
    for (auto it = before.cbegin(); it != before.cend(); ++it) {
        if (after.value(it.key(), "<missing>") != it.value())
            changed << it.key();
    }
    for (auto it = after.cbegin(); it != after.cend(); ++it) {
        if (!before.contains(it.key()))
            changed << it.key() + QStringLiteral(" (new)");
    }
    return changed;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestHelper : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void productionHelperRefusesNonRoot();
    void appliesGrubTheme();
    void appliesPlymouthTheme();
    void registersInstalledTheme();
    void usesDracutWhenItBuildsInitrds();
    void reappliesSameVersion();
    void failedRebuildRollsBack();
    void sameIdDifferentContentRollsBack();
    void invalidThemeChangesNothing();
    void lockHeldChangesNothing();
    void failedRollbackKeepsJournalForRecover();
    void grubOriginalRemovesDropIn();
    void uninstall();

private:
    QString at(const QString &abs) const { return m_root + abs; }
    Run run(const QByteArray &stdinData, const QString &helper = QStringLiteral(HELPER_TEST_BIN));
    void runStub(const QString &name, const QStringList &args = {});
    void failNext(const QString &command, bool always = false);
    // Everything in the fake root except logs, locks, failure markers and the
    // alternative's admin file (compared through readAlternative instead).
    QMap<QString, QByteArray> snapshot() const;
    QString alternativeState() const;

    std::unique_ptr<QTemporaryDir> m_tmp;
    QString m_root;
};

void TestHelper::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    m_root = m_tmp->filePath(QStringLiteral("root"));
    QCOMPARE(QProcess::execute(QStringLiteral("cp"), {QStringLiteral("-a"), QStringLiteral(FIXTURES_DIR "/roots/reference"), m_root}), 0);
    QDir().mkpath(at(QStringLiteral("/boot/grub")));
    QDir().mkpath(at(QStringLiteral("/var/log")));
    runStub(QStringLiteral("update-grub"));
    runStub(QStringLiteral("update-initramfs"), {QStringLiteral("-u"), QStringLiteral("-k"), QStringLiteral("all")});
}

Run TestHelper::run(const QByteArray &stdinData, const QString &helper)
{
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("EKPO_HELPER_ROOT"), m_root);
    env.insert(QStringLiteral("EKPO_HELPER_BIN"), QStringLiteral(STUBS_DIR));
    proc.setProcessEnvironment(env);
    proc.start(helper);
    proc.write(stdinData);
    proc.closeWriteChannel();
    if (!proc.waitForFinished(60000))
        qFatal("helper timed out");

    Run r;
    r.exitCode = proc.exitCode();
    for (const QByteArray &line : proc.readAllStandardOutput().split('\n')) {
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        if (o.value(QStringLiteral("event")) == QLatin1String("progress"))
            r.progress << o.value(QStringLiteral("text")).toString();
        else if (o.value(QStringLiteral("event")) == QLatin1String("result"))
            r.result = o;
    }
    if (r.exitCode != r.result.value(QStringLiteral("outcome")).toInt(-1))
        qWarning("exit code %d doesn't match the result line", r.exitCode);
    return r;
}

void TestHelper::runStub(const QString &name, const QStringList &args)
{
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("EKPO_HELPER_ROOT"), m_root);
    proc.setProcessEnvironment(env);
    proc.start(QStringLiteral(STUBS_DIR "/") + name, args);
    QVERIFY(proc.waitForFinished());
    QVERIFY2(proc.exitCode() == 0, proc.readAllStandardError().constData());
}

void TestHelper::failNext(const QString &command, bool always)
{
    QDir().mkpath(at(QStringLiteral("/stub-fail")));
    QFile marker(at(QStringLiteral("/stub-fail/") + command));
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.write(always ? "always" : "once");
}

QMap<QString, QByteArray> TestHelper::snapshot() const
{
    static const QStringList skipped = {QStringLiteral("var/log/"), QStringLiteral("var/lib/dpkg/lock-frontend"), QStringLiteral("stub-fail"),
                                        QStringLiteral("var/lib/dpkg/alternatives/")};
    QMap<QString, QByteArray> files;
    const QDir root(m_root);
    for (QDirIterator it(m_root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories); it.hasNext();) {
        const QFileInfo info = it.nextFileInfo();
        const QString rel = root.relativeFilePath(info.filePath());
        if (std::any_of(skipped.cbegin(), skipped.cend(), [&](const QString &s) { return rel.startsWith(s); }))
            continue;
        if (info.isSymLink())
            files.insert(rel, "-> " + info.readSymLink().toUtf8());
        else if (info.isDir())
            files.insert(rel + QLatin1Char('/'), {});
        else
            files.insert(rel, QCryptographicHash::hash(readAll(info.filePath()), QCryptographicHash::Sha256));
    }
    return files;
}

QString TestHelper::alternativeState() const
{
    const auto alt = readAlternative(m_root, QStringLiteral("default.plymouth"));
    if (!alt)
        return {};
    QStringList choices;
    for (const auto &[path, priority] : alt->choices)
        choices << QStringLiteral("%1=%2").arg(path).arg(priority);
    choices.sort();
    return alt->mode + QLatin1Char(' ') + alt->value + QLatin1Char(' ') + choices.join(QLatin1Char(','));
}

void TestHelper::productionHelperRefusesNonRoot()
{
    if (::geteuid() == 0)
        QSKIP("running as root");
    const auto before = snapshot();
    const Run r = run(input(grubRequest(), grubFiles()), QStringLiteral(HELPER_BIN));
    QCOMPARE(r.exitCode, 2);
    QVERIFY(r.result.value(QStringLiteral("error")).toString().contains(QLatin1String("root")));
    QVERIFY2(changedPaths(before, snapshot()).isEmpty(), qPrintable(changedPaths(before, snapshot()).join(QStringLiteral(", "))));
}

void TestHelper::appliesGrubTheme()
{
    const Run r = run(input(grubRequest(), grubFiles()));
    QVERIFY2(r.exitCode == 0, qPrintable(r.result.value(QStringLiteral("error")).toString()));

    QCOMPARE(readAll(at(QStringLiteral("/etc/default/grub.d/zz-ekpo.cfg"))),
             QByteArrayLiteral("# Written by Ekpo (io.github.nomoruyi.ekpo). Delete this file to return to /etc/default/grub.\n"
                               "GRUB_THEME=\"/boot/grub/themes/ekpo-night-v1/theme.txt\"\n"
                               "GRUB_GFXMODE=\"2560x1440,1920x1080,1280x720,auto\"\n"
                               "GRUB_TIMEOUT=5\nGRUB_RECORDFAIL_TIMEOUT=5\nGRUB_TIMEOUT_STYLE=menu\n"));
    QCOMPARE(readAll(at(QStringLiteral("/boot/grub/themes/ekpo-night-v1/bg.png"))), png);
    QCOMPARE(QFileInfo(at(QStringLiteral("/boot/grub/themes/ekpo-night-v1/bg.png"))).permissions() & 0x0777, QFileDevice::Permissions(0x0644));
    QVERIFY(readAll(at(QStringLiteral("/boot/grub/grub.cfg"))).contains("set theme=($root)/@/boot/grub/themes/ekpo-night-v1/theme.txt"));
    QVERIFY(r.result.value(QStringLiteral("details")).toObject().value(QStringLiteral("menuEntries")).toArray().contains(QStringLiteral("Kubuntu, with Linux 7.0.0-42-generic (recovery mode)")));
    QVERIFY(!QFileInfo::exists(at(QStringLiteral("/var/lib/ekpo/txn"))));
    QVERIFY(readAll(at(QStringLiteral("/var/log/ekpo-helper.log"))).contains("write: /etc/default/grub.d/zz-ekpo.cfg"));
    QVERIFY(!r.progress.isEmpty());
}

void TestHelper::appliesPlymouthTheme()
{
    const Run r = run(input(plymouthRequest(), plymouthFiles()));
    QVERIFY2(r.exitCode == 0, qPrintable(r.result.value(QStringLiteral("error")).toString()));

    const auto alt = readAlternative(m_root, QStringLiteral("default.plymouth"));
    const QString path = QStringLiteral("/usr/share/plymouth/themes/ekpo-dots-v1/ekpo-dots-v1.plymouth");
    QCOMPARE(alt->mode, QStringLiteral("manual"));
    QCOMPARE(alt->value, path);
    QVERIFY(alt->choices.contains(std::pair(path, 50)));
    QCOMPARE(readAll(at(QStringLiteral("/etc/plymouth/plymouthd.conf"))), QByteArrayLiteral("[Daemon]\nTheme=ekpo-dots-v1\n"));
    for (const QString &v : {QStringLiteral("27"), QStringLiteral("42")})
        QVERIFY(readAll(at(QStringLiteral("/boot/initrd.img-7.0.0-%1-generic").arg(v))).contains("usr/share/plymouth/themes/ekpo-dots-v1/ekpo-dots-v1.plymouth"));
}

void TestHelper::registersInstalledTheme()
{
    const Run r = run(input(plymouthRequest(QStringLiteral("spinner"))));
    QVERIFY2(r.exitCode == 0, qPrintable(r.result.value(QStringLiteral("error")).toString()));
    const auto alt = readAlternative(m_root, QStringLiteral("default.plymouth"));
    QVERIFY(alt->choices.contains(std::pair(QStringLiteral("/usr/share/plymouth/themes/spinner/spinner.plymouth"), 50)));
    QCOMPARE(alt->value, QStringLiteral("/usr/share/plymouth/themes/spinner/spinner.plymouth"));
}

void TestHelper::usesDracutWhenItBuildsInitrds()
{
    QFile status(at(QStringLiteral("/var/lib/dpkg/status")));
    QVERIFY(status.open(QIODevice::WriteOnly));
    status.write("Package: dracut\nStatus: install ok installed\nProvides: linux-initramfs-tool\n\nPackage: initramfs-tools\nStatus: deinstall ok config-files\n");
    status.close();
    failNext(QStringLiteral("update-initramfs"), true); // must not be used

    const Run r = run(input(plymouthRequest(), plymouthFiles()));
    QVERIFY2(r.exitCode == 0, qPrintable(r.result.value(QStringLiteral("error")).toString()));
    QVERIFY(readAll(at(QStringLiteral("/var/log/ekpo-helper.log"))).contains("dracut --regenerate-all --force"));
    QVERIFY(readAll(at(QStringLiteral("/boot/initrd.img-7.0.0-42-generic"))).contains("ekpo-dots-v1/ekpo-dots-v1.plymouth"));
}

void TestHelper::reappliesSameVersion()
{
    QCOMPARE(run(input(grubRequest(), grubFiles())).exitCode, 0);
    const Run again = run(input(grubRequest(), grubFiles()));
    QCOMPARE(again.exitCode, 0);
    QVERIFY(again.progress.contains(QStringLiteral("ekpo-night-v1 is already installed")));
}

void TestHelper::failedRebuildRollsBack()
{
    const auto before = snapshot();
    const QString altBefore = alternativeState();
    failNext(QStringLiteral("update-initramfs"));

    const Run r = run(input(plymouthRequest(), plymouthFiles()));
    QCOMPARE(r.exitCode, 1);
    QVERIFY(r.result.value(QStringLiteral("error")).toString().contains(QLatin1String("update-initramfs")));
    QCOMPARE(alternativeState(), altBefore);
    QVERIFY2(changedPaths(before, snapshot()).isEmpty(), qPrintable(changedPaths(before, snapshot()).join(QStringLiteral(", "))));
}

void TestHelper::sameIdDifferentContentRollsBack()
{
    QCOMPARE(run(input(grubRequest(), grubFiles())).exitCode, 0);
    const auto before = snapshot();
    const Run r = run(input(grubRequest(), grubFiles(grubId, png + "changed")));
    QCOMPARE(r.exitCode, 1);
    QVERIFY(r.result.value(QStringLiteral("error")).toString().contains(QLatin1String("different content")));
    QVERIFY2(changedPaths(before, snapshot()).isEmpty(), qPrintable(changedPaths(before, snapshot()).join(QStringLiteral(", "))));
}

void TestHelper::invalidThemeChangesNothing()
{
    const auto before = snapshot();
    QList<PayloadFile> files = grubFiles();
    files[0].data = "title-txt: \"x\"\n";
    const Run r = run(input(grubRequest(), files));
    QCOMPARE(r.exitCode, 2);
    QVERIFY(r.result.value(QStringLiteral("error")).toString().contains(QLatin1String("unknown global property")));
    QVERIFY2(changedPaths(before, snapshot()).isEmpty(), qPrintable(changedPaths(before, snapshot()).join(QStringLiteral(", "))));

    QCOMPARE(run("{\"format\":1,\"operation\":\"apply\",\"grub\":{\"theme\":\"../../etc\"}}\n").exitCode, 2);
    QCOMPARE(run("not json\n").exitCode, 2);
    QVERIFY2(changedPaths(before, snapshot()).isEmpty(), qPrintable(changedPaths(before, snapshot()).join(QStringLiteral(", "))));
}

void TestHelper::lockHeldChangesNothing()
{
    const auto before = snapshot();
    const int fd = ::open(QFile::encodeName(at(QStringLiteral("/var/lib/dpkg/lock-frontend"))).constData(), O_RDWR | O_CREAT, 0640);
    struct flock fl = {};
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    QCOMPARE(::fcntl(fd, F_SETLK, &fl), 0);

    const Run r = run(input(grubRequest(), grubFiles()));
    ::close(fd);
    QCOMPARE(r.exitCode, 2);
    QVERIFY(r.result.value(QStringLiteral("error")).toString().contains(QLatin1String("package manager")));
    QVERIFY2(changedPaths(before, snapshot()).isEmpty(), qPrintable(changedPaths(before, snapshot()).join(QStringLiteral(", "))));
}

void TestHelper::failedRollbackKeepsJournalForRecover()
{
    const auto before = snapshot();
    const QString altBefore = alternativeState();
    failNext(QStringLiteral("update-grub"), true);

    const Run r = run(input(plymouthRequest(), plymouthFiles()));
    QCOMPARE(r.exitCode, 3);
    QVERIFY(QFileInfo::exists(at(QStringLiteral("/var/lib/ekpo/txn/plan.json"))));

    // Nothing else runs while the journal is there.
    const Run blocked = run(input(grubRequest(), grubFiles()));
    QCOMPARE(blocked.exitCode, 2);
    QVERIFY(blocked.result.value(QStringLiteral("error")).toString().contains(QLatin1String("recover")));

    QVERIFY(QFile::remove(at(QStringLiteral("/stub-fail/update-grub"))));
    Request recover;
    recover.operation = Request::Operation::Recover;
    const Run recovered = run(input(recover));
    QVERIFY2(recovered.exitCode == 0, qPrintable(recovered.result.value(QStringLiteral("error")).toString()));
    QVERIFY(recovered.result.value(QStringLiteral("details")).toObject().value(QStringLiteral("recovered")).toBool());
    QCOMPARE(alternativeState(), altBefore);
    QVERIFY2(changedPaths(before, snapshot()).isEmpty(), qPrintable(changedPaths(before, snapshot()).join(QStringLiteral(", "))));

    // A second recover finds nothing to do.
    QCOMPARE(run(input(recover)).result.value(QStringLiteral("details")).toObject().value(QStringLiteral("recovered")).toBool(true), false);
}

void TestHelper::grubOriginalRemovesDropIn()
{
    QCOMPARE(run(input(grubRequest(), grubFiles())).exitCode, 0);
    Request original;
    original.grub = Request::Grub{true, {}, {}, {}, {}};
    QCOMPARE(run(input(original)).exitCode, 0);
    QVERIFY(!QFileInfo::exists(at(QStringLiteral("/etc/default/grub.d/zz-ekpo.cfg"))));
    QVERIFY(readAll(at(QStringLiteral("/boot/grub/grub.cfg"))).contains("set theme=($root)/@/boot/grub/themes/hollow-grub/theme.txt"));
}

void TestHelper::uninstall()
{
    Request both = grubRequest();
    both.themes << Request::Theme{plymouthId, ThemeTarget::Plymouth};
    both.plymouth = plymouthRequest().plymouth;
    QCOMPARE(run(input(both, grubFiles() + plymouthFiles())).exitCode, 0);

    Request uninstall;
    uninstall.operation = Request::Operation::Uninstall;
    const Run refused = run(input(uninstall));
    QCOMPARE(refused.exitCode, 2);
    QVERIFY(refused.result.value(QStringLiteral("error")).toString().contains(QLatin1String("Original")));

    Request original;
    original.grub = Request::Grub{true, {}, {}, {}, {}};
    original.plymouth = Request::Plymouth{QStringLiteral("manual"), QStringLiteral("fallout"), QStringLiteral("fallout")};
    QCOMPARE(run(input(original)).exitCode, 0);

    const Run done = run(input(uninstall));
    QVERIFY2(done.exitCode == 0, qPrintable(done.result.value(QStringLiteral("error")).toString()));
    QVERIFY(!QFileInfo::exists(at(QStringLiteral("/boot/grub/themes/ekpo-night-v1"))));
    QVERIFY(!QFileInfo::exists(at(QStringLiteral("/usr/share/plymouth/themes/ekpo-dots-v1"))));
    QVERIFY(!QFileInfo::exists(at(QStringLiteral("/var/lib/ekpo"))));
    QVERIFY(!alternativeState().contains(QLatin1String("ekpo-")));
    QVERIFY(alternativeState().startsWith(QLatin1String("manual /usr/share/plymouth/themes/fallout/fallout.plymouth")));
}

EKPO_TEST_MAIN(TestHelper)
#include "tst_helper.moc"
