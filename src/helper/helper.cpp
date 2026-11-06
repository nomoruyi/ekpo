#include "helper.h"

#include "configtext.h"
#include "sysinfo.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStorageInfo>

#include <fcntl.h>
#include <unistd.h>

namespace {

const QString alternativeName = QStringLiteral("default.plymouth");
const QString alternativeLink = QStringLiteral("/usr/share/plymouth/themes/default.plymouth");
const QString grubThemesDir = QStringLiteral("/boot/grub/themes/");
const QString plymouthThemesDir = QStringLiteral("/usr/share/plymouth/themes/");
const QString dropInFile = QStringLiteral("/etc/default/grub.d/zz-ekpo.cfg");
const QString plymouthdConf = QStringLiteral("/etc/plymouth/plymouthd.conf");
const QString txnDir = QStringLiteral("/var/lib/ekpo/txn");
constexpr int registerPriority = 50; // below every distro theme (SPEC decision 3)

// The only programs the helper runs, at fixed paths.
const QMap<QString, QString> commands = {
    {QStringLiteral("update-grub"), QStringLiteral("/usr/sbin/update-grub")},
    {QStringLiteral("update-alternatives"), QStringLiteral("/usr/bin/update-alternatives")},
    {QStringLiteral("update-initramfs"), QStringLiteral("/usr/sbin/update-initramfs")},
    {QStringLiteral("lsinitramfs"), QStringLiteral("/usr/bin/lsinitramfs")},
    {QStringLiteral("dracut"), QStringLiteral("/usr/bin/dracut")},
    {QStringLiteral("lsinitrd"), QStringLiteral("/usr/bin/lsinitrd")},
};

struct StepError {
    QString message;
};

std::optional<QByteArray> readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    return f.readAll();
}

QJsonValue fileContent(const QString &path)
{
    const auto data = readFile(path);
    return data ? QJsonValue(QString::fromUtf8(*data)) : QJsonValue(QJsonValue::Null);
}

// Holds the dpkg frontend lock the way apt does, so no package tool runs meanwhile.
class DpkgLock
{
public:
    explicit DpkgLock(const QString &root)
    {
        m_fd = ::open(QFile::encodeName(root + QStringLiteral("/var/lib/dpkg/lock-frontend")).constData(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0640);
        struct flock fl = {};
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        m_locked = m_fd >= 0 && ::fcntl(m_fd, F_SETLK, &fl) == 0;
    }
    ~DpkgLock()
    {
        if (m_fd >= 0)
            ::close(m_fd);
    }
    bool locked() const { return m_locked; }

private:
    int m_fd = -1;
    bool m_locked = false;
};

class Helper
{
public:
    Helper(const HelperEnv &env)
        : m_env(env)
    {
    }

    HelperResult apply(const Request &request, const QMap<QString, QList<PayloadFile>> &themes);
    HelperResult recover();
    HelperResult uninstall();

private:
    QString at(const QString &abs) const { return m_env.root + abs; }
    void log(const QString &line) const
    {
        if (m_env.log)
            m_env.log(line);
    }
    void progress(const QString &line) const
    {
        if (m_env.progress)
            m_env.progress(line);
    }

    QString run(const QString &command, const QStringList &args, int timeoutMs = 120000) const;
    void writeFile(const QString &abs, const QByteArray &data) const;
    void removeFile(const QString &abs) const;
    void savePlan() const;
    void installTheme(const QString &dir, const QString &id, const QList<PayloadFile> &files);
    void rebuildInitrds() const;
    void setDaemonTheme(const QString &theme) const;
    QJsonObject verify() const;
    bool restore(QStringList &errors);
    QString themeIdOf(const QString &plymouthPath) const { return QFileInfo(plymouthPath).dir().dirName(); }
    // Nothing of Ekpo's stays on the system after an apply, a rollback or a recover.
    void removeJournal() const
    {
        QDir(at(txnDir)).removeRecursively();
        QDir().rmdir(at(QStringLiteral("/var/lib/ekpo")));
    }

    const HelperEnv &m_env;
    QJsonObject m_plan;
    std::optional<Request> m_request;
};

QString Helper::run(const QString &command, const QStringList &args, int timeoutMs) const
{
    const QString program = m_env.binDir.isEmpty() ? commands.value(command) : m_env.binDir + QLatin1Char('/') + command;
    log(QStringLiteral("run: %1 %2").arg(program, args.join(QLatin1Char(' '))));
    QProcess proc;
    QProcessEnvironment env;
    env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/sbin:/usr/bin:/sbin:/bin"));
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    if (!m_env.root.isEmpty())
        env.insert(QStringLiteral("EKPO_HELPER_ROOT"), m_env.root); // test stubs only
    proc.setProcessEnvironment(env);
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start(program, args);
    if (!proc.waitForFinished(timeoutMs) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        const QString stderrText = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        throw StepError{QStringLiteral("%1 failed%2").arg(command, stderrText.isEmpty() ? QString() : QStringLiteral(": ") + stderrText.right(500))};
    }
    return QString::fromUtf8(proc.readAllStandardOutput());
}

// Atomic replace; a link at the target is replaced, never followed.
void Helper::writeFile(const QString &abs, const QByteArray &data) const
{
    log(QStringLiteral("write: %1").arg(abs));
    QDir().mkpath(QFileInfo(at(abs)).path());
    QSaveFile file(at(abs));
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        throw StepError{QStringLiteral("can't write %1: %2").arg(abs, file.errorString())};
    QFile::setPermissions(at(abs), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
}

void Helper::removeFile(const QString &abs) const
{
    if (!QFileInfo(at(abs)).exists() && !QFileInfo(at(abs)).isSymLink())
        return;
    log(QStringLiteral("remove: %1").arg(abs));
    if (!QFile::remove(at(abs)))
        throw StepError{QStringLiteral("can't remove %1").arg(abs)};
}

void Helper::savePlan() const
{
    QSaveFile file(at(txnDir + QStringLiteral("/plan.json")));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(m_plan).toJson()) < 0 || !file.commit())
        throw StepError{QStringLiteral("can't write the journal")};
}

// Writes the theme into a hidden folder next to its destination (same file system),
// then renames it into place. Files are created new (O_EXCL) and never follow links.
void Helper::installTheme(const QString &dir, const QString &id, const QList<PayloadFile> &files)
{
    const QString dest = dir + id;
    if (QFileInfo(at(dest)).exists() || QFileInfo(at(dest)).isSymLink()) {
        // Re-applying the same version is fine; a different one under the same id is not.
        for (const PayloadFile &f : files) {
            if (readFile(at(dest + QLatin1Char('/') + f.path)) != f.data)
                throw StepError{QStringLiteral("%1 already exists with different content").arg(dest)};
        }
        progress(QStringLiteral("%1 is already installed").arg(id));
        return;
    }

    const QString stage = dir + QStringLiteral(".ekpo-stage-") + id;
    QJsonArray installed = m_plan.value(QStringLiteral("installed")).toArray();
    installed << stage << dest;
    m_plan.insert(QStringLiteral("installed"), installed);
    savePlan();

    progress(QStringLiteral("Installing %1").arg(dest));
    log(QStringLiteral("install: %1 (%2 files)").arg(dest).arg(files.size()));
    QDir(at(stage)).removeRecursively();
    if (!QDir().mkpath(at(stage)))
        throw StepError{QStringLiteral("can't create %1").arg(stage)};
    for (const PayloadFile &f : files) {
        const QString target = at(stage + QLatin1Char('/') + f.path);
        if (!QDir().mkpath(QFileInfo(target).path()))
            throw StepError{QStringLiteral("can't create a folder for %1").arg(f.path)};
        const int fd = ::open(QFile::encodeName(target).constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
        if (fd < 0)
            throw StepError{QStringLiteral("can't create %1").arg(f.path)};
        qsizetype written = 0;
        while (written < f.data.size()) {
            const ssize_t n = ::write(fd, f.data.constData() + written, size_t(f.data.size() - written));
            if (n <= 0)
                break;
            written += n;
        }
        const bool ok = written == f.data.size() && ::fsync(fd) == 0;
        ::close(fd);
        if (!ok)
            throw StepError{QStringLiteral("can't write %1").arg(f.path)};
    }
    for (QDirIterator it(at(stage), QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories); it.hasNext();)
        QFile::setPermissions(it.next(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                             | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther);
    QFile::setPermissions(at(stage), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                         | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther);
    if (::rename(QFile::encodeName(at(stage)).constData(), QFile::encodeName(at(dest)).constData()) != 0)
        throw StepError{QStringLiteral("can't move %1 into place").arg(id)};
}

// Rebuilds every initrd, or only the one of the kernel recorded in the journal, so a
// rollback touches exactly what the apply touched.
void Helper::rebuildInitrds() const
{
    const bool dracut = m_plan.value(QStringLiteral("generator")).toString() == QLatin1String("dracut");
    const QString kernel = m_plan.value(QStringLiteral("kernel")).toString();
    if (kernel.isEmpty()) {
        progress(QStringLiteral("Rebuilding the initrds of all kernels (about 40 s)"));
        if (dracut)
            run(QStringLiteral("dracut"), {QStringLiteral("--regenerate-all"), QStringLiteral("--force")}, 900000);
        else
            run(QStringLiteral("update-initramfs"), {QStringLiteral("-u"), QStringLiteral("-k"), QStringLiteral("all")}, 900000);
        return;
    }
    progress(QStringLiteral("Rebuilding the initrd of %1 (about 8 s)").arg(kernel));
    // Plain "dracut --force" would build the running kernel's, which needn't be the newest.
    if (dracut)
        run(QStringLiteral("dracut"), {QStringLiteral("--force"), QStringLiteral("/boot/initrd.img-") + kernel, kernel}, 900000);
    else
        run(QStringLiteral("update-initramfs"), {QStringLiteral("-u"), QStringLiteral("-k"), kernel}, 900000);
}

void Helper::setDaemonTheme(const QString &theme) const
{
    writeFile(plymouthdConf, withDaemonTheme(readFile(at(plymouthdConf)).value_or(QByteArray()), theme));
}

QJsonObject Helper::verify() const
{
    progress(QStringLiteral("Checking the result"));
    const QString cfg = QString::fromUtf8(readFile(at(QStringLiteral("/boot/grub/grub.cfg"))).value_or(QByteArray()));
    if (cfg.trimmed().isEmpty())
        throw StepError{QStringLiteral("grub.cfg is missing or empty")};

    if (m_request->grub && !m_request->grub->original && !m_request->grub->theme.isEmpty()) {
        const QString expected = grubThemesDir + m_request->grub->theme + QStringLiteral("/theme.txt");
        static const QRegularExpression themeLine(QStringLiteral("^\\s*set theme=(.*)$"), QRegularExpression::MultilineOption);
        bool found = false;
        for (auto it = themeLine.globalMatch(cfg); it.hasNext();)
            found = found || it.next().captured(1).contains(expected);
        if (!found)
            throw StepError{QStringLiteral("grub.cfg doesn't load %1").arg(expected)};
    }

    if (m_request->plymouth) {
        const auto alt = readAlternative(m_env.root, alternativeName);
        const QString id = alt ? themeIdOf(alt->value) : QString();
        const QString needle = QStringLiteral("usr/share/plymouth/themes/%1/%1.plymouth").arg(id);
        const bool dracut = m_plan.value(QStringLiteral("generator")).toString() == QLatin1String("dracut");
        QStringList missing;
        const QDir boot(at(QStringLiteral("/boot")));
        const QString kernel = m_plan.value(QStringLiteral("kernel")).toString();
        const QStringList initrds = kernel.isEmpty() ? boot.entryList({QStringLiteral("initrd.img-*")}, QDir::Files | QDir::NoSymLinks)
                                                     : QStringList{QStringLiteral("initrd.img-") + kernel};
        for (const QString &initrd : initrds) {
            if (!run(dracut ? QStringLiteral("lsinitrd") : QStringLiteral("lsinitramfs"), {QStringLiteral("/boot/") + initrd}).contains(needle))
                missing << initrd;
        }
        if (id.isEmpty() || !missing.isEmpty())
            throw StepError{QStringLiteral("the splash \"%1\" is missing from: %2").arg(id, missing.join(QStringLiteral(", ")))};
    }

    static const QRegularExpression entry(QStringLiteral("^\\s*(?:menuentry|submenu)\\s+'([^']*)'"), QRegularExpression::MultilineOption);
    QJsonArray entries;
    for (auto it = entry.globalMatch(cfg); it.hasNext();)
        entries << it.next().captured(1);
    return {{QStringLiteral("menuEntries"), entries}};
}

HelperResult Helper::apply(const Request &request, const QMap<QString, QList<PayloadFile>> &themes)
{
    m_request = request;
    const auto payloadThemeIds = themes.keys();
    const auto exists = [&](const QString &id, ThemeTarget target) {
        if (payloadThemeIds.contains(id))
            return true;
        return target == ThemeTarget::Grub ? QFileInfo::exists(at(grubThemesDir + id + QStringLiteral("/theme.txt")))
                                           : QFileInfo::exists(at(plymouthThemesDir + id + QLatin1Char('/') + id + QStringLiteral(".plymouth")));
    };
    if (request.grub && !request.grub->theme.isEmpty() && !exists(request.grub->theme, ThemeTarget::Grub))
        return {Outcome::Rejected, QStringLiteral("GRUB theme %1 isn't installed").arg(request.grub->theme), {}};
    if (request.plymouth && ((!request.plymouth->theme.isEmpty() && !exists(request.plymouth->theme, ThemeTarget::Plymouth))
                             || !exists(request.plymouth->daemonTheme, ThemeTarget::Plymouth)))
        return {Outcome::Rejected, QStringLiteral("a Plymouth theme of the request isn't installed"), {}};

    const QString generator = initramfsGenerator(m_env.root);
    if (request.plymouth && generator.isEmpty())
        return {Outcome::Rejected, QStringLiteral("no initramfs generator found"), {}};
    // "Newest only" is pinned to a version now, so rebuild, verify and rollback agree.
    const QStringList kernels = installedKernels(m_env.root);
    const QString newestKernel = kernels.isEmpty() ? QString() : kernels.last();
    if (request.plymouth && !request.plymouth->allKernels && newestKernel.isEmpty())
        return {Outcome::Rejected, QStringLiteral("no kernel found in /boot"), {}};

    qint64 payloadBytes = 0;
    for (const auto &files : themes) {
        for (const PayloadFile &f : files)
            payloadBytes += f.data.size();
    }
    const qint64 needed = payloadBytes + (request.plymouth ? 512LL << 20 : 16LL << 20);
    if (QStorageInfo(at(QStringLiteral("/boot"))).bytesAvailable() < needed)
        return {Outcome::Rejected, QStringLiteral("not enough free space in /boot (%1 MB needed)").arg(needed >> 20), {}};

    const auto alt = readAlternative(m_env.root, alternativeName);
    QJsonArray choices;
    for (const auto &c : alt ? alt->choices : QList<std::pair<QString, int>>())
        choices << c.first;
    m_plan = {
        {QStringLiteral("request"), QJsonDocument::fromJson(serializeRequest(request)).object()},
        {QStringLiteral("generator"), generator},
        {QStringLiteral("kernel"), request.plymouth && !request.plymouth->allKernels ? newestKernel : QString()},
        {QStringLiteral("grub"), bool(request.grub)},
        {QStringLiteral("plymouth"), bool(request.plymouth)},
        {QStringLiteral("installed"), QJsonArray()},
        {QStringLiteral("registered"), QJsonArray()},
        {QStringLiteral("pre"),
         QJsonObject{
             {QStringLiteral("dropIn"), fileContent(at(dropInFile))},
             {QStringLiteral("plymouthdConf"), fileContent(at(plymouthdConf))},
             {QStringLiteral("alternativeMode"), alt ? alt->mode : QString()},
             {QStringLiteral("alternativeValue"), alt ? alt->value : QString()},
             {QStringLiteral("choices"), choices},
         }},
    };

    if (!QDir().mkpath(at(txnDir))) {
        return {Outcome::Rejected, QStringLiteral("can't create the journal folder"), {}};
    }
    QFile::setPermissions(at(txnDir), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    log(QStringLiteral("apply: %1").arg(QString::fromUtf8(serializeRequest(request))));

    try {
        savePlan();
        for (const Request::Theme &t : request.themes)
            installTheme(t.target == ThemeTarget::Grub ? grubThemesDir : plymouthThemesDir, t.id, themes.value(t.id));

        if (request.grub) {
            progress(QStringLiteral("Writing the GRUB settings"));
            if (request.grub->original) {
                removeFile(dropInFile);
            } else {
                writeFile(dropInFile, grubDropInText(*request.grub));
            }
        }

        if (request.plymouth) {
            progress(QStringLiteral("Selecting the boot splash"));
            if (request.plymouth->alternativeMode == QLatin1String("manual")) {
                const QString path = plymouthThemesDir + request.plymouth->theme + QLatin1Char('/') + request.plymouth->theme + QStringLiteral(".plymouth");
                if (!choices.contains(path)) {
                    QJsonArray registered = m_plan.value(QStringLiteral("registered")).toArray();
                    registered << path;
                    m_plan.insert(QStringLiteral("registered"), registered);
                    savePlan();
                    run(QStringLiteral("update-alternatives"), {QStringLiteral("--install"), alternativeLink, alternativeName, path, QString::number(registerPriority)});
                }
                run(QStringLiteral("update-alternatives"), {QStringLiteral("--set"), alternativeName, path});
            } else {
                run(QStringLiteral("update-alternatives"), {QStringLiteral("--auto"), alternativeName});
            }
            setDaemonTheme(request.plymouth->daemonTheme);
            rebuildInitrds();
        }

        progress(QStringLiteral("Updating grub.cfg"));
        run(QStringLiteral("update-grub"), {});
        const QJsonObject details = verify();
        removeJournal();
        log(QStringLiteral("apply: done"));
        return {Outcome::Done, {}, details};
    } catch (const StepError &e) {
        log(QStringLiteral("apply failed: %1; rolling back").arg(e.message));
        progress(QStringLiteral("Failed: %1. Putting everything back").arg(e.message));
        QStringList errors;
        if (restore(errors))
            return {Outcome::RolledBack, e.message, {}};
        return {Outcome::NeedsRecovery, e.message + QStringLiteral("; rollback failed: ") + errors.join(QStringLiteral("; ")), {}};
    }
}

// Puts back the journal's pre-state. Each part is attempted even if an earlier one
// fails, and the journal is kept unless all succeed. Safe to run more than once.
bool Helper::restore(QStringList &errors)
{
    const QJsonObject pre = m_plan.value(QStringLiteral("pre")).toObject();
    const bool grub = m_plan.value(QStringLiteral("grub")).toBool();
    const bool plymouth = m_plan.value(QStringLiteral("plymouth")).toBool();
    const auto attempt = [&](const auto &step) {
        try {
            step();
        } catch (const StepError &e) {
            errors << e.message;
        }
    };

    attempt([&] {
        if (pre.value(QStringLiteral("dropIn")).isString())
            writeFile(dropInFile, pre.value(QStringLiteral("dropIn")).toString().toUtf8());
        else
            removeFile(dropInFile);
    });
    if (plymouth) {
        attempt([&] {
            if (pre.value(QStringLiteral("plymouthdConf")).isString())
                writeFile(plymouthdConf, pre.value(QStringLiteral("plymouthdConf")).toString().toUtf8());
            else
                removeFile(plymouthdConf);
        });
        for (const QJsonValue &path : m_plan.value(QStringLiteral("registered")).toArray())
            attempt([&] { run(QStringLiteral("update-alternatives"), {QStringLiteral("--remove"), alternativeName, path.toString()}); });
        attempt([&] {
            const QString value = pre.value(QStringLiteral("alternativeValue")).toString();
            if (pre.value(QStringLiteral("alternativeMode")).toString() == QLatin1String("manual") && !value.isEmpty())
                run(QStringLiteral("update-alternatives"), {QStringLiteral("--set"), alternativeName, value});
            else
                run(QStringLiteral("update-alternatives"), {QStringLiteral("--auto"), alternativeName});
        });
    }
    for (const QJsonValue &dir : m_plan.value(QStringLiteral("installed")).toArray()) {
        attempt([&] {
            const QString path = at(dir.toString());
            if (QFileInfo::exists(path) && !QDir(path).removeRecursively())
                throw StepError{QStringLiteral("can't remove %1").arg(dir.toString())};
        });
    }
    if (grub || plymouth)
        attempt([&] { run(QStringLiteral("update-grub"), {}); });
    if (plymouth)
        attempt([&] { rebuildInitrds(); });

    if (!errors.isEmpty())
        return false;
    removeJournal();
    log(QStringLiteral("restored the state from before the apply"));
    return true;
}

HelperResult Helper::recover()
{
    const auto plan = readFile(at(txnDir + QStringLiteral("/plan.json")));
    if (!plan) {
        removeJournal(); // cut off before the journal was written: nothing changed yet
        return {Outcome::Done, {}, {{QStringLiteral("recovered"), false}}};
    }
    m_plan = QJsonDocument::fromJson(*plan).object();
    log(QStringLiteral("recover: rolling back an interrupted apply"));
    progress(QStringLiteral("Rolling back an interrupted apply"));
    QStringList errors;
    if (restore(errors))
        return {Outcome::Done, {}, {{QStringLiteral("recovered"), true}}};
    return {Outcome::NeedsRecovery, errors.join(QStringLiteral("; ")), {}};
}

// Removes what Ekpo installed. The app restores Original first, so nothing of Ekpo's
// may still be selected.
HelperResult Helper::uninstall()
{
    QString error;
    const auto grub = effectiveGrub(m_env.root, {QStringLiteral("GRUB_THEME")}, error);
    const auto alt = readAlternative(m_env.root, alternativeName);
    if (QFileInfo::exists(at(dropInFile)) || !grub || grub->value(QStringLiteral("GRUB_THEME")).contains(QStringLiteral("/themes/ekpo-"))
        || (alt && themeIdOf(alt->value).startsWith(QStringLiteral("ekpo-"))))
        return {Outcome::Rejected, QStringLiteral("Ekpo's settings are still active; restore Original first"), {}};

    try {
        progress(QStringLiteral("Removing Ekpo's themes"));
        for (const auto &c : alt ? alt->choices : QList<std::pair<QString, int>>()) {
            if (themeIdOf(c.first).startsWith(QStringLiteral("ekpo-")))
                run(QStringLiteral("update-alternatives"), {QStringLiteral("--remove"), alternativeName, c.first});
        }
        for (const QString &dir : {grubThemesDir, plymouthThemesDir}) {
            for (const QString &name : QDir(at(dir)).entryList({QStringLiteral("ekpo-*"), QStringLiteral(".ekpo-stage-*")}, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
                log(QStringLiteral("remove: %1%2").arg(dir, name));
                if (!QDir(at(dir + name)).removeRecursively())
                    throw StepError{QStringLiteral("can't remove %1%2").arg(dir, name)};
            }
        }
        QDir(at(QStringLiteral("/var/lib/ekpo"))).removeRecursively();
    } catch (const StepError &e) {
        return {Outcome::RolledBack, e.message, {}}; // nothing selected changed; a retry finishes the job
    }
    log(QStringLiteral("uninstall: done"));
    return {};
}

} // namespace

HelperResult runRequest(const HelperEnv &env, const Request &request, const QMap<QString, QList<PayloadFile>> &themes)
{
    const DpkgLock lock(env.root);
    if (!lock.locked())
        return {Outcome::Rejected, QStringLiteral("A package manager is running; try again when it has finished."), {}};

    Helper helper(env);
    const bool leftover = QFileInfo::exists(env.root + txnDir);
    switch (request.operation) {
    case Request::Operation::Recover:
        return helper.recover();
    case Request::Operation::Apply:
    case Request::Operation::Uninstall:
        if (leftover)
            return {Outcome::Rejected, QStringLiteral("An earlier apply was interrupted; recover first."), {}};
        return request.operation == Request::Operation::Apply ? helper.apply(request, themes) : helper.uninstall();
    }
    return {Outcome::Rejected, QStringLiteral("unknown operation"), {}};
}
