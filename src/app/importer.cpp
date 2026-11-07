#include "importer.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>

namespace {

using Verdict = ImportedTheme::Verdict;

const QString grubThemesDir = QStringLiteral("/boot/grub/themes");
const QString plymouthThemesDir = QStringLiteral("/usr/share/plymouth/themes");
const QString plymouthPluginDir = QStringLiteral("/usr/lib/x86_64-linux-gnu/plymouth"); // plymouth --get-splash-plugin-path
const QString systemLookAndFeelDir = QStringLiteral("/usr/share/plasma/look-and-feel");
const QString userLookAndFeelDir = QStringLiteral("/.local/share/plasma/look-and-feel");

struct Finding {
    ImportedTheme theme;
    QString root;

    void add(Verdict level, const QString &line)
    {
        theme.verdict = std::max(theme.verdict, level);
        theme.report << line;
    }
    void note(const QString &line) { theme.report << line; }
};

QString canonical(const QString &path)
{
    const QString c = QFileInfo(path).canonicalFilePath();
    return c.isEmpty() ? QDir::cleanPath(path) : c;
}

bool isInside(const QString &path, const QString &dir)
{
    return canonical(path).startsWith(canonical(dir) + QLatin1Char('/'));
}

bool isArchive(const QString &path)
{
    static const QStringList suffixes = {
        QStringLiteral(".tar"), QStringLiteral(".tar.gz"), QStringLiteral(".tgz"), QStringLiteral(".tar.xz"),
        QStringLiteral(".txz"), QStringLiteral(".tar.zst"), QStringLiteral(".tar.bz2"), QStringLiteral(".zip"),
    };
    return QFileInfo(path).isFile()
        && std::any_of(suffixes.cbegin(), suffixes.cend(), [&](const QString &s) { return path.endsWith(s, Qt::CaseInsensitive); });
}

// GNU tar and unzip both refuse absolute paths and ".." members by default; links
// that leave the theme folder are dropped later when the theme is copied.
bool extract(const QString &archive, const QString &target, QString &error)
{
    QProcess proc;
    QProcessEnvironment env;
    env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/bin:/bin"));
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    proc.setProcessEnvironment(env);
    if (archive.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive))
        proc.start(QStringLiteral("/usr/bin/unzip"), {QStringLiteral("-q"), QStringLiteral("-n"), archive, QStringLiteral("-d"), target});
    else
        proc.start(QStringLiteral("/usr/bin/tar"), {QStringLiteral("-xf"), archive, QStringLiteral("-C"), target,
                                                     QStringLiteral("--no-same-owner"), QStringLiteral("--no-same-permissions")});
    if (!proc.waitForFinished(120000) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        error = QStringLiteral("Can't unpack %1: %2").arg(QFileInfo(archive).fileName(), QString::fromUtf8(proc.readAllStandardError()).trimmed());
        return false;
    }
    return true;
}

QList<std::pair<Stage, QString>> findThemeRoots(const QString &dir, int depth = 0)
{
    const QDir d(dir);
    if (d.exists(QStringLiteral("theme.txt")))
        return {{Stage::BootMenu, dir}};
    if (!d.entryList({QStringLiteral("*.plymouth")}, QDir::Files).isEmpty())
        return {{Stage::BootSplash, dir}};
    if (d.exists(QStringLiteral("contents/splash/Splash.qml")))
        return {{Stage::PlasmaSplash, dir}};
    if (depth == 3)
        return {};
    QList<std::pair<Stage, QString>> roots;
    for (const QString &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name))
        roots += findThemeRoots(d.filePath(sub), depth + 1);
    return roots;
}

void checkGrub(Finding &f)
{
    const QDir root(f.root);
    f.theme.name = root.dirName();

    QFile file(root.filePath(QStringLiteral("theme.txt")));
    if (!file.open(QIODevice::ReadOnly)) {
        f.add(Verdict::Broken, QStringLiteral("theme.txt can't be read."));
        return;
    }
    static const QRegularExpression quotedImage(QStringLiteral("\"([^\"]+\\.(?:png|jpe?g|tga))\""), QRegularExpression::CaseInsensitiveOption);
    for (auto it = quotedImage.globalMatch(QString::fromUtf8(file.readAll())); it.hasNext();) {
        const QString ref = it.next().captured(1);
        const QFileInfo refInfo(root.filePath(ref));
        const bool found = ref.contains(QLatin1Char('*'))
            ? !QDir(refInfo.path()).entryList({refInfo.fileName()}, QDir::Files).isEmpty()
            : refInfo.isFile();
        if (!found)
            f.add(Verdict::Warnings, QStringLiteral("theme.txt refers to %1, which isn't in the theme.").arg(ref));
    }

    int loadable = 0;
    for (QDirIterator it(f.root, {QStringLiteral("*.pf2")}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
        const QString rel = root.relativeFilePath(it.next());
        const QString dir = QFileInfo(rel).path();
        if (dir == QLatin1String(".") || dir == QLatin1String("f"))
            ++loadable;
        else
            f.add(Verdict::Warnings, QStringLiteral("The font %1 won't load: GRUB loads fonts only from the theme folder and f/.").arg(rel));
    }
    if (loadable == 0)
        f.note(QStringLiteral("No fonts of its own that GRUB loads; the menu uses GRUB's built-in font."));
}

// ImageDir and ScriptFile hold absolute paths, normally inside the theme's own install
// folder /usr/share/plymouth/themes/<folder name>.
void checkPlymouthPath(Finding &f, const QString &key, const QString &value, bool mustBeFile, const QString &rootDir)
{
    const QString path = QDir::cleanPath(value);
    const QString own = plymouthThemesDir + QLatin1Char('/') + QDir(f.root).dirName();
    if (path == own || path.startsWith(own + QLatin1Char('/'))) {
        const QFileInfo inTheme(f.root + path.mid(own.size()));
        if (mustBeFile ? !inTheme.isFile() : !inTheme.isDir())
            f.add(Verdict::Broken, QStringLiteral("%1 points to %2, which is missing from the theme.").arg(key, path));
    } else if (path.startsWith(plymouthThemesDir + QLatin1Char('/')) && QFileInfo::exists(rootDir + path)) {
        f.add(Verdict::Warnings, QStringLiteral("%1 uses %2 from another installed theme; it works only while that theme stays installed.").arg(key, path));
    } else {
        f.add(Verdict::Broken, QStringLiteral("%1 points to %2, which isn't part of the theme.").arg(key, path));
    }
}

void checkPlymouth(Finding &f, const QString &rootDir)
{
    const QDir root(f.root);
    QString fileName = root.dirName() + QStringLiteral(".plymouth");
    if (!root.exists(fileName)) {
        fileName = root.entryList({QStringLiteral("*.plymouth")}, QDir::Files, QDir::Name).value(0);
        f.add(Verdict::Repaired, QStringLiteral("%1 doesn't match the folder name; it installs as a cleaned-up copy.").arg(fileName));
    }
    const KConfig config(root.filePath(fileName), KConfig::SimpleConfig);
    const KConfigGroup header = config.group(QStringLiteral("Plymouth Theme"));
    f.theme.name = header.readEntry("Name", root.dirName());
    const QString module = header.readEntry("ModuleName");
    if (module.isEmpty()) {
        f.add(Verdict::Broken, QStringLiteral("%1 names no ModuleName.").arg(fileName));
        return;
    }
    if (!QFileInfo::exists(rootDir + plymouthPluginDir + QLatin1Char('/') + module + QStringLiteral(".so")))
        f.add(Verdict::Broken, QStringLiteral("It needs the Plymouth module \"%1\", which isn't installed.").arg(module));

    const KConfigGroup section = config.group(module);
    const QString imageDir = section.readEntry("ImageDir");
    if (!imageDir.isEmpty())
        checkPlymouthPath(f, QStringLiteral("ImageDir"), imageDir, false, rootDir);
    if (module == QLatin1String("two-step") && imageDir.isEmpty()) {
        f.add(Verdict::Broken, QStringLiteral("A two-step theme needs an ImageDir."));
    } else if (module == QLatin1String("two-step")) {
        // Without these, show_splash_screen() or the entry fails and Plymouth falls back
        // to text. Applied in place, two-step reads ImageDir; a copy gets ImageDir
        // rewritten to its own folder (normalizedPlymouth in planner.cpp).
        const bool inPlace = !f.theme.installedId.isEmpty();
        const QDir images(inPlace ? rootDir + QDir::cleanPath(imageDir) : f.root);
        QStringList missing;
        for (const QString &name : {QStringLiteral("lock.png"), QStringLiteral("entry.png"), QStringLiteral("bullet.png")}) {
            if (!images.exists(name))
                missing << name;
        }
        if (!missing.isEmpty()) {
            const QString last = missing.takeLast();
            const QString list = missing.isEmpty() ? last : missing.join(QStringLiteral(", ")) + QStringLiteral(" and ") + last;
            f.add(Verdict::Broken, QStringLiteral("two-step shows no splash without lock.png, entry.png and bullet.png; %1 lacks %2.")
                                       .arg(inPlace ? QDir::cleanPath(imageDir) : QStringLiteral("this theme"), list));
        }
    }
    if (module == QLatin1String("script")) {
        const QString scriptFile = section.readEntry("ScriptFile");
        if (scriptFile.isEmpty())
            f.add(Verdict::Broken, QStringLiteral("A script theme needs a ScriptFile."));
        else
            checkPlymouthPath(f, QStringLiteral("ScriptFile"), scriptFile, true, rootDir);
        f.note(QStringLiteral("Its script runs at every boot (Plymouth's script language can't read files or run programs)."));
    }

    if (f.theme.installedId.isEmpty() && isInside(f.root, rootDir + plymouthThemesDir))
        f.add(Verdict::Repaired, QStringLiteral("It's installed one folder deeper than Plymouth looks; it installs as a cleaned-up copy."));
}

void checkPlasmaSplash(Finding &f)
{
    const QDir root(f.root);
    f.theme.name = root.dirName();
    QFile metadata(root.filePath(QStringLiteral("metadata.json")));
    if (!metadata.open(QIODevice::ReadOnly)) {
        f.add(Verdict::Broken, QStringLiteral("There is no metadata.json; Plasma 6 needs one (metadata.desktop is the Plasma 5 format)."));
        return;
    }
    const QJsonObject plugin = QJsonDocument::fromJson(metadata.readAll()).object().value(QStringLiteral("KPlugin")).toObject();
    const QString id = plugin.value(QStringLiteral("Id")).toString();
    f.theme.name = plugin.value(QStringLiteral("Name")).toString(root.dirName());
    if (id.isEmpty()) {
        f.add(Verdict::Broken, QStringLiteral("metadata.json has no KPlugin Id."));
        return;
    }
    if (id != root.dirName())
        f.add(Verdict::Repaired, QStringLiteral("The folder name doesn't match the package ID %1; it installs as a cleaned-up copy.").arg(id));

    QFile splash(root.filePath(QStringLiteral("contents/splash/Splash.qml")));
    if (splash.open(QIODevice::ReadOnly) && splash.readAll().contains("import org.kde.plasma"))
        f.add(Verdict::Warnings, QStringLiteral("Splash.qml uses Plasma modules, which can break after a Plasma upgrade."));
    f.note(QStringLiteral("Splash.qml is code that runs at every login; Ekpo doesn't sandbox it."));
}

QString installedId(Stage stage, const QString &root, const QString &rootDir, const QString &homeDir)
{
    const QString parent = canonical(QFileInfo(root).path());
    const QString name = QDir(root).dirName();
    switch (stage) {
    case Stage::BootMenu:
        return parent == canonical(rootDir + grubThemesDir) ? name : QString();
    case Stage::BootSplash:
        return parent == canonical(rootDir + plymouthThemesDir) && QFileInfo::exists(root + QLatin1Char('/') + name + QStringLiteral(".plymouth"))
            ? name
            : QString();
    case Stage::PlasmaSplash:
        return parent == canonical(rootDir + systemLookAndFeelDir) || parent == canonical(homeDir + userLookAndFeelDir) ? name : QString();
    }
    return {};
}

} // namespace

ImportResult importThemes(Library &library, const QString &sourcePath, const QString &rootDir, const QString &homeDir)
{
    const QString root = rootDir == QLatin1String("/") ? QString() : rootDir;
    ImportResult result;

    QString searchDir = canonical(sourcePath);
    const bool archive = isArchive(sourcePath);
    const QString staging = library.path() + QStringLiteral("/staging/") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (archive) {
        QString error;
        if (!QDir().mkpath(staging) || !extract(sourcePath, staging, error)) {
            result.errors << (error.isEmpty() ? QStringLiteral("Can't create %1").arg(staging) : error);
            QDir(staging).removeRecursively();
            return result;
        }
        searchDir = staging;
    } else if (!QFileInfo(searchDir).isDir()) {
        result.errors << QStringLiteral("%1 is neither a folder nor a supported archive.").arg(sourcePath);
        return result;
    }

    const auto roots = findThemeRoots(searchDir);
    if (roots.isEmpty())
        result.errors << QStringLiteral("No theme found in %1 (looked for theme.txt, *.plymouth and contents/splash/Splash.qml, up to three folders deep).").arg(sourcePath);

    QStringList loadErrors;
    const QList<ImportedTheme> existing = library.imports(loadErrors);
    for (const auto &[stage, themeRoot] : roots) {
        Finding f;
        f.root = themeRoot;
        f.theme.stage = stage;
        f.theme.sourcePath = archive ? canonical(sourcePath) : themeRoot;
        f.theme.installedId = archive ? QString() : installedId(stage, themeRoot, root, homeDir);
        f.theme.importedAt = QDateTime::currentDateTimeUtc();
        switch (stage) {
        case Stage::BootMenu:
            checkGrub(f);
            break;
        case Stage::BootSplash:
            checkPlymouth(f, root);
            break;
        case Stage::PlasmaSplash:
            checkPlasmaSplash(f);
            break;
        }

        // A cleaned-up copy can't be selected in place.
        if (f.theme.verdict == Verdict::Repaired)
            f.theme.installedId.clear();

        // Files next to a nested theme (install scripts, READMEs) stay behind.
        if (themeRoot != searchDir) {
            for (const QString &left : QDir(QFileInfo(themeRoot).path()).entryList(QDir::Files | QDir::Hidden, QDir::Name))
                f.note(QStringLiteral("Not imported: %1 (Ekpo never runs bundled scripts).").arg(left));
        }

        for (const ImportedTheme &old : existing) {
            if (old.stage == stage && old.sourcePath == f.theme.sourcePath && old.name == f.theme.name)
                f.theme.id = old.id;
        }

        QString error;
        if (library.saveImport(f.theme, themeRoot, error))
            result.themes << f.theme;
        else
            result.errors << error;
    }

    if (archive)
        QDir(staging).removeRecursively();
    return result;
}

QList<std::pair<Stage, QString>> installedThemeFolders(const SystemState &state, const QString &rootDir, const QString &homeDir)
{
    const QString root = rootDir == QLatin1String("/") ? QString() : rootDir;
    QList<std::pair<Stage, QString>> folders;
    for (const QString &id : state.grubThemes)
        folders.append({Stage::BootMenu, root + grubThemesDir + QLatin1Char('/') + id});

    // Graphical themes only; text-mode themes are Plymouth's fallbacks.
    QStringList listed;
    for (const SystemState::PlymouthTheme &t : state.plymouthThemes) {
        listed << t.id;
        if (t.module == QLatin1String("two-step") || t.module == QLatin1String("script"))
            folders.append({Stage::BootSplash, root + plymouthThemesDir + QLatin1Char('/') + t.id});
    }
    const QDir plymouthDir(root + plymouthThemesDir);
    for (const QString &dir : plymouthDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name)) {
        if (listed.contains(dir))
            continue;
        const auto nested = findThemeRoots(plymouthDir.filePath(dir));
        if (std::any_of(nested.cbegin(), nested.cend(), [](const auto &r) { return r.first == Stage::BootSplash; }))
            folders.append({Stage::BootSplash, plymouthDir.filePath(dir)});
    }

    for (const SystemState::SplashPackage &p : state.splashPackages)
        folders.append({Stage::PlasmaSplash, (p.user ? homeDir + userLookAndFeelDir : root + systemLookAndFeelDir) + QLatin1Char('/') + p.id});
    return folders;
}
