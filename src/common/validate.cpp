#include "validate.h"

#include <QDir>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

namespace {

const QString plymouthThemesDir = QStringLiteral("/usr/share/plymouth/themes/");
constexpr qint64 grubThemeCap = 64 << 20;
constexpr int themeFileCap = 4096;

// From GRUB 2.14's gfxmenu module (theme_loader.c): anything else is an error at boot.
const QSet<QString> grubGlobals = {
    QStringLiteral("title-text"), QStringLiteral("title-font"), QStringLiteral("title-color"),
    QStringLiteral("message-font"), QStringLiteral("message-color"), QStringLiteral("message-bg-color"),
    QStringLiteral("desktop-image"), QStringLiteral("desktop-image-scale-method"), QStringLiteral("desktop-image-h-align"),
    QStringLiteral("desktop-image-v-align"), QStringLiteral("desktop-color"), QStringLiteral("terminal-box"),
    QStringLiteral("terminal-font"), QStringLiteral("terminal-left"), QStringLiteral("terminal-top"),
    QStringLiteral("terminal-width"), QStringLiteral("terminal-height"), QStringLiteral("terminal-border"),
};
const QSet<QString> grubComponents = {
    QStringLiteral("label"), QStringLiteral("image"), QStringLiteral("progress_bar"), QStringLiteral("circular_progress"),
    QStringLiteral("boot_menu"), QStringLiteral("canvas"), QStringLiteral("hbox"), QStringLiteral("vbox"),
};
// Properties whose value names a file in the theme ('*' patterns allowed).
const QSet<QString> grubFileProperties = {
    QStringLiteral("desktop-image"), QStringLiteral("terminal-box"), QStringLiteral("file"), QStringLiteral("center_bitmap"),
    QStringLiteral("tick_bitmap"), QStringLiteral("bar_style"), QStringLiteral("highlight_style"), QStringLiteral("menu_pixmap_style"),
    QStringLiteral("item_pixmap_style"), QStringLiteral("selected_item_pixmap_style"), QStringLiteral("scrollbar_frame"),
    QStringLiteral("scrollbar_thumb"),
};

class ThemeTxtParser
{
public:
    explicit ThemeTxtParser(const QString &text)
        : m_s(text)
    {
    }

    QStringList run()
    {
        skipBlank(true);
        while (!atEnd()) {
            if (peek() == QLatin1Char('+') ? !component(1) : !global())
                break;
            skipBlank(true);
        }
        return m_errors;
    }

private:
    bool atEnd() const { return m_i >= m_s.size(); }
    QChar peek() const { return atEnd() ? QChar() : m_s[m_i]; }
    void advance()
    {
        if (m_s[m_i] == QLatin1Char('\n'))
            ++m_line;
        ++m_i;
    }
    bool fail(const QString &message)
    {
        m_errors << QStringLiteral("theme.txt line %1: %2").arg(m_line).arg(message);
        return false;
    }

    // Spaces and tabs; with newlines also line breaks and '#' comments.
    void skipBlank(bool newlines)
    {
        while (!atEnd()) {
            const QChar c = peek();
            if (c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == QLatin1Char('\r') || (newlines && c == QLatin1Char('\n'))) {
                advance();
            } else if (newlines && c == QLatin1Char('#')) {
                while (!atEnd() && peek() != QLatin1Char('\n'))
                    advance();
            } else {
                break;
            }
        }
    }

    QString identifier()
    {
        const qsizetype start = m_i;
        while (!atEnd() && (peek().isLetterOrNumber() || peek() == QLatin1Char('-') || peek() == QLatin1Char('_')))
            advance();
        return m_s.mid(start, m_i - start);
    }

    // A quoted string, or a bare value up to the end of the line (global) or to
    // whitespace or '}' (component).
    std::optional<QString> value(bool global)
    {
        if (peek() == QLatin1Char('"')) {
            advance();
            const qsizetype start = m_i;
            while (!atEnd() && peek() != QLatin1Char('"') && peek() != QLatin1Char('\n'))
                advance();
            if (peek() != QLatin1Char('"')) {
                fail(QStringLiteral("unterminated string"));
                return std::nullopt;
            }
            const QString v = m_s.mid(start, m_i - start);
            advance();
            return v;
        }
        const qsizetype start = m_i;
        while (!atEnd() && peek() != QLatin1Char('\n') && (global || (!peek().isSpace() && peek() != QLatin1Char('}'))))
            advance();
        const QString v = m_s.mid(start, m_i - start).trimmed();
        if (v.isEmpty()) {
            fail(QStringLiteral("missing value"));
            return std::nullopt;
        }
        return v;
    }

    void checkFileReference(const QString &property, const QString &v)
    {
        if (!grubFileProperties.contains(property) || v.isEmpty())
            return;
        if (!validRelativePath(QString(v).replace(QLatin1Char('*'), QLatin1Char('x'))))
            fail(QStringLiteral("%1 refers to \"%2\", which isn't a file inside the theme").arg(property, v));
    }

    bool global()
    {
        const QString name = identifier();
        if (name.isEmpty())
            return fail(QStringLiteral("unexpected \"%1\"").arg(peek()));
        skipBlank(false);
        if (peek() != QLatin1Char(':'))
            return fail(QStringLiteral("expected ':' after %1").arg(name));
        advance();
        skipBlank(false);
        const auto v = value(true);
        if (!v)
            return false;
        if (!grubGlobals.contains(name))
            fail(QStringLiteral("unknown global property %1").arg(name));
        checkFileReference(name, *v);
        return true;
    }

    bool component(int depth)
    {
        advance(); // '+'
        skipBlank(true);
        const QString type = identifier();
        if (!grubComponents.contains(type))
            fail(QStringLiteral("unknown component type \"%1\"").arg(type));
        if (depth > 8 || ++m_components > 512)
            return fail(QStringLiteral("too many or too deeply nested components"));
        skipBlank(true);
        if (peek() != QLatin1Char('{'))
            return fail(QStringLiteral("expected '{' after + %1").arg(type));
        advance();
        while (true) {
            skipBlank(true);
            if (atEnd())
                return fail(QStringLiteral("missing '}' for + %1").arg(type));
            if (peek() == QLatin1Char('}')) {
                advance();
                return true;
            }
            if (peek() == QLatin1Char('+')) {
                if (!component(depth + 1))
                    return false;
                continue;
            }
            const QString name = identifier();
            if (name.isEmpty())
                return fail(QStringLiteral("unexpected \"%1\" in + %2").arg(peek()).arg(type));
            skipBlank(false);
            if (peek() != QLatin1Char('='))
                return fail(QStringLiteral("expected '=' after %1").arg(name));
            advance();
            skipBlank(false);
            const auto v = value(false);
            if (!v)
                return false;
            checkFileReference(name, *v);
        }
    }

    QString m_s;
    qsizetype m_i = 0;
    int m_line = 1;
    int m_components = 0;
    QStringList m_errors;
};

std::optional<QString> utf8Text(const QByteArray &data)
{
    if (data.contains('\0'))
        return std::nullopt;
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder(data);
    return decoder.hasError() ? std::nullopt : std::optional(text);
}

// Minimal INI reader for .plymouth files: [section] and key=value lines.
std::optional<QMap<QString, QMap<QString, QString>>> parseIni(const QString &text, QStringList &errors)
{
    QMap<QString, QMap<QString, QString>> sections;
    QString section;
    int lineNo = 0;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        ++lineNo;
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2).trimmed();
            continue;
        }
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0 || section.isEmpty()) {
            errors << QStringLiteral(".plymouth line %1 is not \"key=value\" inside a [section]").arg(lineNo);
            return std::nullopt;
        }
        sections[section][line.left(eq).trimmed()] = line.mid(eq + 1).trimmed();
    }
    return sections;
}

QStringList validatePlymouthFile(const QString &id, const QString &text, const QSet<QString> &paths)
{
    QStringList errors;
    const auto ini = parseIni(text, errors);
    if (!ini)
        return errors;
    const QString module = ini->value(QStringLiteral("Plymouth Theme")).value(QStringLiteral("ModuleName"));
    if (module != QLatin1String("two-step") && module != QLatin1String("script"))
        return {QStringLiteral("ModuleName must be two-step or script, not \"%1\"").arg(module)};

    const QString own = plymouthThemesDir + id;
    const QMap<QString, QString> section = ini->value(module);
    if (QDir::cleanPath(section.value(QStringLiteral("ImageDir"))) != own)
        errors << QStringLiteral("ImageDir must be %1").arg(own);
    if (module == QLatin1String("script")) {
        const QString script = QDir::cleanPath(section.value(QStringLiteral("ScriptFile")));
        const QString rel = script.startsWith(own + QLatin1Char('/')) ? script.mid(own.size() + 1) : QString();
        if (!rel.endsWith(QLatin1String(".script")) || !paths.contains(rel))
            errors << QStringLiteral("ScriptFile must name a .script file in %1").arg(own);
    }
    return errors;
}

} // namespace

bool validPayloadThemeId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^ekpo-[a-z0-9]+(?:-[a-z0-9]+)*-v[1-9][0-9]{0,5}$"));
    return id.size() <= 64 && re.match(id).hasMatch();
}

bool validInstalledThemeId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_+-][A-Za-z0-9._+-]{0,63}$"));
    return re.match(id).hasMatch();
}

bool validGfxmode(const QString &mode)
{
    static const QRegularExpression re(QStringLiteral("^(?:auto|[1-9][0-9]{2,4}x[1-9][0-9]{2,4}(?:x(?:8|15|16|24|32))?(?:,[1-9][0-9]{2,4}x[1-9][0-9]{2,4}(?:x(?:8|15|16|24|32))?){0,7}(?:,auto)?)$"));
    return re.match(mode).hasMatch();
}

bool validTimeoutStyle(const QString &style)
{
    return style == QLatin1String("menu") || style == QLatin1String("countdown") || style == QLatin1String("hidden");
}

QStringList checkFile(ThemeTarget target, const PayloadFile &file)
{
    static const QRegularExpression notice(QStringLiteral("^(?:LICEN[CS]E|COPYING|README|AUTHORS)(?:\\.(?:md|txt))?$"), QRegularExpression::CaseInsensitiveOption);
    const QString name = file.path.section(QLatin1Char('/'), -1);
    const QString ext = name.contains(QLatin1Char('.')) ? name.section(QLatin1Char('.'), -1).toLower() : QString();
    const QByteArray &d = file.data;
    const auto bad = [&](const QString &what) { return QStringList{QStringLiteral("%1: %2").arg(file.path, what)}; };

    const bool text = notice.match(name).hasMatch() || ext == QLatin1String("txt")
        || (target == ThemeTarget::Plymouth && (ext == QLatin1String("plymouth") || ext == QLatin1String("script")));
    if (text)
        return utf8Text(d) ? QStringList() : bad(QStringLiteral("not UTF-8 text"));
    if (ext == QLatin1String("png"))
        return d.startsWith("\x89PNG\r\n\x1a\n") ? QStringList() : bad(QStringLiteral("not a PNG image"));
    if (target == ThemeTarget::Grub) {
        if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg"))
            return d.startsWith("\xff\xd8\xff") ? QStringList() : bad(QStringLiteral("not a JPEG image"));
        if (ext == QLatin1String("tga")) {
            static const QByteArray types = QByteArrayLiteral("\x01\x02\x03\x09\x0a\x0b");
            return d.size() >= 18 && types.contains(d[2]) ? QStringList() : bad(QStringLiteral("not a TGA image"));
        }
        if (ext == QLatin1String("pf2"))
            return d.startsWith(QByteArrayLiteral("FILE\0\0\0\x04PFF2")) ? QStringList() : bad(QStringLiteral("not a GRUB .pf2 font"));
    }
    return bad(QStringLiteral("file type not allowed in this theme"));
}

QStringList validateGrubThemeTxt(const QByteArray &text)
{
    const auto decoded = utf8Text(text);
    if (!decoded)
        return {QStringLiteral("theme.txt is not UTF-8 text")};
    return ThemeTxtParser(*decoded).run();
}

QStringList validateTheme(ThemeTarget target, const QString &id, const QList<PayloadFile> &files)
{
    if (!validPayloadThemeId(id))
        return {QStringLiteral("invalid theme id \"%1\"").arg(id)};

    QStringList errors;
    qint64 total = 0;
    QSet<QString> paths;
    for (const PayloadFile &f : files) {
        total += f.data.size();
        if (!validRelativePath(f.path) || paths.contains(f.path))
            errors << QStringLiteral("invalid or repeated path \"%1\"").arg(f.path);
        paths.insert(f.path);
        errors << checkFile(target, f);
    }
    const qint64 cap = target == ThemeTarget::Grub ? grubThemeCap : plymouthThemeCap;
    if (files.size() > themeFileCap || total > cap)
        errors << QStringLiteral("the theme exceeds %1 files or %2 MB").arg(themeFileCap).arg(cap >> 20);

    const QString main = target == ThemeTarget::Grub ? QStringLiteral("theme.txt") : id + QStringLiteral(".plymouth");
    const auto it = std::find_if(files.cbegin(), files.cend(), [&](const PayloadFile &f) { return f.path == main; });
    if (it == files.cend())
        return errors << QStringLiteral("%1 is missing").arg(main);

    if (target == ThemeTarget::Grub) {
        errors << validateGrubThemeTxt(it->data);
    } else {
        const qsizetype plymouthFiles = std::count_if(files.cbegin(), files.cend(), [](const PayloadFile &f) { return f.path.endsWith(QLatin1String(".plymouth")); });
        if (plymouthFiles != 1)
            errors << QStringLiteral("a Plymouth theme must hold exactly one .plymouth file");
        if (const auto text = utf8Text(it->data))
            errors << validatePlymouthFile(id, *text, paths);
    }
    return errors;
}
