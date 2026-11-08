#include "exporter.h"

#include "gengrub.h"
#include "genksplash.h"
#include "genplymouth.h"
#include "planner.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <algorithm>

namespace {

const QString homepage = QStringLiteral("https://github.com/nomoruyi/ekpo");

QString stageFolder(Stage stage)
{
    switch (stage) {
    case Stage::BootMenu:
        return QStringLiteral("boot-menu");
    case Stage::BootSplash:
        return QStringLiteral("boot-splash");
    case Stage::PlasmaSplash:
        break;
    }
    return QStringLiteral("plasma-splash");
}

QString stageWords(Stage stage)
{
    switch (stage) {
    case Stage::BootMenu:
        return QStringLiteral("boot menu (GRUB theme)");
    case Stage::BootSplash:
        return QStringLiteral("boot splash (Plymouth theme)");
    case Stage::PlasmaSplash:
        break;
    }
    return QStringLiteral("Plasma splash (look-and-feel package)");
}

// How to install the theme in folder id by hand (SPEC.md §4.13: no Ekpo needed).
QString installSteps(Stage stage, const QString &id, const QString &name, QSize screen)
{
    switch (stage) {
    case Stage::BootMenu:
        return QStringLiteral("Built for a %1×%2 screen. On Debian, Ubuntu and their relatives:\n\n"
                              "```sh\n"
                              "sudo cp -r %3 /boot/grub/themes/\n"
                              "printf 'GRUB_THEME=\"/boot/grub/themes/%3/theme.txt\"\\nGRUB_GFXMODE=\"%1x%2,auto\"\\n' | sudo tee /etc/default/grub.d/zz-%3.cfg\n"
                              "sudo update-grub\n"
                              "```\n\n"
                              "Elsewhere, set GRUB_THEME and GRUB_GFXMODE like that in /etc/default/grub and rebuild grub.cfg "
                              "(grub-mkconfig or grub2-mkconfig). To undo: delete the .cfg file and the folder, then run update-grub again.\n")
            .arg(screen.width())
            .arg(screen.height())
            .arg(id);
    case Stage::BootSplash:
        return QStringLiteral("Built for a %1×%2 screen. On Debian, Ubuntu and their relatives:\n\n"
                              "```sh\n"
                              "sudo cp -r %3 /usr/share/plymouth/themes/\n"
                              "sudo update-alternatives --install /usr/share/plymouth/themes/default.plymouth default.plymouth "
                              "/usr/share/plymouth/themes/%3/%3.plymouth 100\n"
                              "sudo update-alternatives --set default.plymouth /usr/share/plymouth/themes/%3/%3.plymouth\n"
                              "sudo update-initramfs -u -k all\n"
                              "```\n\n"
                              "If /etc/plymouth/plymouthd.conf names a theme (`Theme=`), set it to %3 too. On Fedora, Arch and others: "
                              "`sudo plymouth-set-default-theme -R %3`. The kernel command line needs `splash` for a graphical boot splash.\n")
            .arg(screen.width())
            .arg(screen.height())
            .arg(id);
    case Stage::PlasmaSplash:
        break;
    }
    return QStringLiteral("In System Settings: Colours & Themes → Splash Screen → Install from File, and choose this archive; then pick "
                          "“%1” there. Or:\n\n"
                          "```sh\n"
                          "kpackagetool6 -t Plasma/LookAndFeel -i %2.tar.gz\n"
                          "```\n")
        .arg(name, id);
}

QString kindWord(const QString &kind)
{
    return kind == QLatin1String("theme") ? QStringLiteral("theme") : kind;
}

// "## Not by <author>": what keeps its own licence.
QString thirdPartySection(const QList<ExportItem> &items, const QString &author)
{
    if (items.isEmpty())
        return {};
    QString text = QStringLiteral("\n## Not by %1\n\nThese parts keep their own licences:\n\n").arg(author);
    for (const ExportItem &item : items)
        text += QStringLiteral("- The %1 “%2”: %3\n").arg(kindWord(item.kind), item.name, item.licence.isEmpty() ? QStringLiteral("licence unknown") : item.licence);
    return text;
}

QString licenceText(const QString &licence, const QString &author, int year)
{
    if (licence.compare(QLatin1String("MIT"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("MIT License\n\n"
                              "Copyright (c) %1 %2\n\n"
                              "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
                              "of this software and associated documentation files (the \"Software\"), to deal\n"
                              "in the Software without restriction, including without limitation the rights\n"
                              "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n"
                              "copies of the Software, and to permit persons to whom the Software is\n"
                              "furnished to do so, subject to the following conditions:\n\n"
                              "The above copyright notice and this permission notice shall be included in all\n"
                              "copies or substantial portions of the Software.\n\n"
                              "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
                              "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
                              "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
                              "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
                              "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
                              "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
                              "SOFTWARE.\n")
            .arg(year)
            .arg(author);
    return QStringLiteral("Copyright (c) %1 %2\n\nLicensed under %3.\n").arg(year).arg(author, licence);
}

// The fonts, images and videos a resolved design uses.
QList<ExportItem> thirdPartyOf(const Library &library, const Design &design)
{
    QStringList errors;
    const QList<Asset> assets = library.assets(errors);
    QHash<QString, Asset> byHash;
    for (const Asset &a : assets)
        byHash.insert(a.hash, a);
    QList<ExportItem> items;
    const QSet<QString> used = stringsIn(design.content);
    for (const QString &s : used) {
        const auto it = byHash.constFind(s);
        if (it == byHash.cend() || it->kind == Asset::Kind::Licence)
            continue;
        ExportItem item;
        item.kind = it->kind == Asset::Kind::Font ? QStringLiteral("font") : it->kind == Asset::Kind::Video ? QStringLiteral("video") : QStringLiteral("image");
        item.name = it->name;
        item.source = it->source;
        if (const auto licence = byHash.constFind(it->licence); licence != byHash.cend()) {
            QFile text(library.assetPath(*licence));
            if (text.open(QIODevice::ReadOnly))
                item.licence = licenceName(text.readAll(), &item.free);
        }
        items << item;
    }
    std::sort(items.begin(), items.end(), [](const ExportItem &a, const ExportItem &b) { return std::tie(a.kind, a.name) < std::tie(b.kind, b.name); });
    return items;
}

QList<PayloadFile> themeFiles(const Library &library, const Design &design, const QString &id, QSize screen, const QStringList &entries, QStringList &problems)
{
    switch (design.stage) {
    case Stage::BootMenu:
        return buildBootMenu(design, library, screen, entries, problems);
    case Stage::BootSplash:
        return buildPlymouthSplash(design, id, library, screen, problems);
    case Stage::PlasmaSplash:
        break;
    }
    return buildPlasmaSplash(design, id, library, problems);
}

QList<PayloadFile> filesIn(const QString &dir)
{
    QList<PayloadFile> files;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile f(it.next());
        if (f.open(QIODevice::ReadOnly))
            files << PayloadFile{QDir(dir).relativeFilePath(f.fileName()), f.readAll()};
    }
    std::sort(files.begin(), files.end(), [](const PayloadFile &a, const PayloadFile &b) { return a.path < b.path; });
    return files;
}

QString runTar(const QStringList &arguments, const QString &archive)
{
    const QString part = archive + QStringLiteral(".part");
    QFile::remove(part);
    QProcess tar;
    tar.start(QStringLiteral("tar"), QStringList{QStringLiteral("--sort=name"), QStringLiteral("--owner=0"), QStringLiteral("--group=0"),
                                                 QStringLiteral("--numeric-owner"), QStringLiteral("-czf"), part}
                                         + arguments);
    if (!tar.waitForFinished(600000) || tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0) {
        const QString error = QString::fromUtf8(tar.readAllStandardError()).trimmed();
        QFile::remove(part);
        return QStringLiteral("tar couldn't write the archive%1").arg(error.isEmpty() ? QStringLiteral(".") : QStringLiteral(": ") + error);
    }
    QFile::remove(archive);
    if (!QFile::rename(part, archive)) {
        QFile::remove(part);
        return QStringLiteral("Can't write %1.").arg(archive);
    }
    return {};
}

} // namespace

QString licenceName(const QByteArray &text, bool *free)
{
    struct Known {
        QString pattern;
        QString name;
        bool free;
    };
    // Narrower names first: LGPL before GPL, the restricted Creative Commons ones first.
    static const QList<Known> known = {
        {QStringLiteral("NonCommercial|NoDerivatives|CC-BY-NC|CC-BY-ND"), QStringLiteral("Creative Commons, non-commercial or no derivatives"), false},
        {QStringLiteral("SIL OPEN FONT LICENSE|\\bOFL(-1\\.[01])?\\b"), QStringLiteral("SIL Open Font License"), true},
        {QStringLiteral("UBUNTU FONT LICEN[CS]E"), QStringLiteral("Ubuntu Font Licence"), true},
        {QStringLiteral("Apache License|\\bApache-2\\.0\\b"), QStringLiteral("Apache License"), true},
        {QStringLiteral("GNU LESSER GENERAL PUBLIC LICENSE|\\bLGPL"), QStringLiteral("GNU LGPL"), true},
        {QStringLiteral("GNU GENERAL PUBLIC LICENSE|\\bGPL-[23]"), QStringLiteral("GNU GPL"), true},
        {QStringLiteral("Permission is hereby granted, free of charge|\\bMIT\\b|\\bExpat\\b"), QStringLiteral("MIT"), true},
        {QStringLiteral("Bitstream Vera"), QStringLiteral("Bitstream Vera Fonts licence"), true},
        {QStringLiteral("Creative Commons Attribution|\\bCC-BY\\b"), QStringLiteral("Creative Commons Attribution"), true},
        {QStringLiteral("\\bCC0\\b|public domain"), QStringLiteral("public domain"), true},
    };
    const QString s = QString::fromUtf8(text);
    for (const Known &k : known) {
        if (QRegularExpression(k.pattern, QRegularExpression::CaseInsensitiveOption).match(s).hasMatch()) {
            if (free)
                *free = k.free;
            return k.name;
        }
    }
    if (free)
        *free = false;
    return {};
}

ExportPlan exportDesign(const Library &library, const QString &designId, QSize screen, const QStringList &entries)
{
    ExportPlan plan;
    QString error;
    const std::optional<Design> saved = library.design(designId, 0, error);
    if (!saved) {
        plan.problems << QStringLiteral("Save the design before exporting it.");
        return plan;
    }
    const Design design = library.resolved(*saved, plan.problems);
    plan.folder = QStringLiteral("ekpo-%1-v%2").arg(slug(design.name)).arg(design.version);
    plan.thirdParty = thirdPartyOf(library, design);
    for (const PayloadFile &f : themeFiles(library, design, plan.folder, screen, entries, plan.problems))
        plan.files << PayloadFile{plan.folder + QLatin1Char('/') + f.path, f.data};

    const QString author = design.content.value(QStringLiteral("author")).toString(QStringLiteral("nomoruyi"));
    const QString licence = design.content.value(QStringLiteral("licence")).toString(QStringLiteral("MIT"));
    const QString readme = QStringLiteral("# %1\n\nA %2 made with [Ekpo](%3), from version %4 of the design.\n\nBy %5, under the %6 licence (LICENSE).\n%7\n## Install\n\n%8")
                               .arg(design.name, stageWords(design.stage), homepage, QString::number(design.version), author, licence,
                                    thirdPartySection(plan.thirdParty, author), installSteps(design.stage, plan.folder, design.name, screen));
    const int year = design.savedAt.isValid() ? design.savedAt.date().year() : QDate::currentDate().year();
    plan.files << PayloadFile{plan.folder + QStringLiteral("/README.md"), readme.toUtf8()}
               << PayloadFile{plan.folder + QStringLiteral("/LICENSE"), licenceText(licence, author, year).toUtf8()};
    return plan;
}

ExportPlan exportMask(const Library &library, const QString &maskId, QSize screen, const QStringList &entries)
{
    ExportPlan plan;
    QStringList errors;
    const QList<Mask> masks = library.masks(errors);
    const auto mask = std::find_if(masks.cbegin(), masks.cend(), [&](const Mask &m) { return m.id == maskId; });
    if (mask == masks.cend()) {
        plan.problems << QStringLiteral("The Mask is no longer in the library.");
        return plan;
    }
    plan.folder = QStringLiteral("ekpo-mask-%1").arg(slug(mask->name));
    const QList<ImportedTheme> imports = library.imports(errors);
    QString parts;
    for (auto it = mask->stageSlots.cbegin(); it != mask->stageSlots.cend(); ++it) {
        const QString dir = plan.folder + QLatin1Char('/') + stageFolder(it.key()) + QLatin1Char('/');
        if (it.value().kind == Slot::Kind::Design) {
            const ExportPlan part = exportDesign(library, it.value().ref, screen, entries);
            plan.problems << part.problems;
            plan.thirdParty << part.thirdParty;
            for (const PayloadFile &f : part.files)
                plan.files << PayloadFile{dir + f.path, f.data};
            parts += QStringLiteral("- `%1/%2`: the %3; its README.md says how to install it.\n").arg(stageFolder(it.key()), part.folder, stageWords(it.key()));
            continue;
        }
        if (it.value().kind != Slot::Kind::Import)
            continue;
        const auto theme = std::find_if(imports.cbegin(), imports.cend(), [&](const ImportedTheme &t) { return t.id == it.value().ref; });
        if (theme == imports.cend()) {
            plan.problems << QStringLiteral("The Mask uses an imported theme that is no longer in the library.");
            continue;
        }
        // Its own folder name: a Plymouth theme names its folder in its paths.
        const QString name = QFileInfo(theme->sourcePath).fileName().isEmpty() ? slug(theme->name) : QFileInfo(theme->sourcePath).fileName();
        const QList<PayloadFile> files = filesIn(library.importFilesPath(theme->id));
        ExportItem item{QStringLiteral("theme"), theme->name, theme->sourcePath, {}, false};
        for (const PayloadFile &f : files) {
            plan.files << PayloadFile{dir + name + QLatin1Char('/') + f.path, f.data};
            if (item.licence.isEmpty() && QRegularExpression(QStringLiteral("^(LICEN[CS]E|COPYING)"), QRegularExpression::CaseInsensitiveOption).match(QFileInfo(f.path).fileName()).hasMatch())
                item.licence = licenceName(f.data, &item.free);
        }
        plan.thirdParty << item;
        parts += QStringLiteral("- `%1/%2`: the %3, the imported theme “%4” by its own author, as imported; install it as its author describes.\n")
                     .arg(stageFolder(it.key()), name, stageWords(it.key()), theme->name);
    }
    const QString readme = QStringLiteral("# Mask “%1”\n\nThe look of several boot stages, put together and exported with [Ekpo](%2):\n\n%3")
                               .arg(mask->name, homepage, parts);
    plan.files << PayloadFile{plan.folder + QStringLiteral("/README.md"), readme.toUtf8()};
    return plan;
}

QString writeArchive(const QList<PayloadFile> &files, const QString &archive)
{
    QTemporaryDir dir;
    if (!dir.isValid())
        return QStringLiteral("Can't create a temporary folder.");
    QStringList tops;
    for (const PayloadFile &f : files) {
        const QString path = dir.filePath(f.path);
        QDir().mkpath(QFileInfo(path).path());
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(f.data) != f.data.size())
            return QStringLiteral("Can't write %1.").arg(path);
        if (const QString top = f.path.section(QLatin1Char('/'), 0, 0); !tops.contains(top))
            tops << top;
    }
    return runTar(QStringList{QStringLiteral("-C"), dir.path()} + tops, archive);
}

QString exportLibrary(const Library &library, const QString &archive)
{
    const QFileInfo folder(library.path());
    // tar would read the archive it is writing.
    if (QFileInfo(archive).absoluteFilePath().startsWith(folder.absoluteFilePath() + QLatin1Char('/')))
        return QStringLiteral("Save the archive outside the library folder.");
    return runTar({QStringLiteral("-C"), folder.absolutePath(), folder.fileName()}, archive);
}
