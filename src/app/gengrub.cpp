#include "gengrub.h"

#include "gencommon.h"
#include "pf2.h"

#include <QBuffer>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QLinearGradient>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace {

QByteArray png(const QImage &image)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return data;
}

// theme.txt strings have no escapes: no quotes, no line breaks.
QString themeString(QString text)
{
    static const QRegularExpression control(QStringLiteral("[\\x00-\\x1f\\x7f]+"));
    return QLatin1Char('"') + text.replace(control, QStringLiteral(" ")).replace(QLatin1Char('"'), QChar(0x201D)) + QLatin1Char('"');
}

QString rgb(const QString &colour)
{
    return QColor(colour).name(QColor::HexRgb); // GRUB has no alpha in theme colours
}

QRect pixelRect(const PercentBox &b, QSize screen)
{
    return QRectF(screen.width() * b.x / 100, screen.height() * b.y / 100, screen.width() * b.width / 100, screen.height() * b.height / 100).toRect();
}

// fontconfig's choice for sans-serif, for elements without a font of their own.
QString defaultFontFile()
{
    static QString file;
    if (file.isEmpty()) {
        QProcess fcMatch;
        fcMatch.start(QStringLiteral("fc-match"), {QStringLiteral("--format=%{file}"), QStringLiteral("sans-serif")});
        if (fcMatch.waitForFinished(5000) && fcMatch.exitCode() == 0)
            file = QString::fromUtf8(fcMatch.readAllStandardOutput()).trimmed();
    }
    return file;
}

// The selected entry's box as GRUB's 9-slice pixmap style (select_*.png): corners,
// edges GRUB stretches, and the centre. border = the width of each side.
QList<PayloadFile> selectionImages(const QString &style, const QColor &colour, int border)
{
    const int s = 2 * border + 1;
    QImage box(s, s, QImage::Format_ARGB32_Premultiplied);
    box.fill(Qt::transparent);
    QPainter p(&box);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    if (style == QLatin1String("rounded")) {
        p.setBrush(colour);
        p.drawRoundedRect(QRectF(0, 0, s, s), border, border);
    } else if (style == QLatin1String("glow")) {
        // Fading in towards a translucent fill.
        for (int k = 0; k < border; ++k) {
            QColor ring = colour;
            ring.setAlphaF(colour.alphaF() * 0.6 * (k + 1) / border);
            p.setBrush(ring);
            QPainterPath path;
            path.addRoundedRect(QRectF(k, k, s - 2 * k, s - 2 * k), border - k, border - k);
            path.addRoundedRect(QRectF(k + 1, k + 1, s - 2 * k - 2, s - 2 * k - 2), std::max(0, border - k - 1), std::max(0, border - k - 1));
            path.setFillRule(Qt::OddEvenFill);
            p.drawPath(path);
        }
        QColor fill = colour;
        fill.setAlphaF(colour.alphaF() * 0.35);
        p.setBrush(fill);
        p.drawRect(QRectF(border, border, 1, 1));
    } else { // flat
        p.fillRect(box.rect(), colour);
    }
    p.end();
    const int b = border;
    const QList<std::pair<const char *, QRect>> pieces = {
        {"nw", {0, 0, b, b}}, {"n", {b, 0, 1, b}}, {"ne", {b + 1, 0, b, b}}, {"w", {0, b, b, 1}}, {"c", {b, b, 1, 1}},
        {"e", {b + 1, b, b, 1}}, {"sw", {0, b + 1, b, b}}, {"s", {b, b + 1, 1, b}}, {"se", {b + 1, b + 1, b, b}},
    };
    QList<PayloadFile> files;
    for (const auto &[name, rect] : pieces)
        files << PayloadFile{QStringLiteral("select_%1.png").arg(QLatin1String(name)), png(box.copy(rect))};
    return files;
}

// Fonts: one .pf2 per font file and pixel size, named "Ekpo F<n> <style> <size>".
class Fonts
{
public:
    Fonts(const Library &library, const QMap<QString, Asset> &assets, QStringList &problems)
        : m_library(library)
        , m_assets(assets)
        , m_problems(problems)
    {
    }

    // The font for o["font"] at pixels, or nothing after a problem.
    const Pf2Font *get(const QJsonObject &o, int pixels)
    {
        QString file = defaultFontFile();
        if (const QString hash = o.value(QStringLiteral("font")).toString(); !hash.isEmpty()) {
            const auto it = m_assets.constFind(hash);
            if (it == m_assets.cend() || it->kind != Asset::Kind::Font) {
                m_problems << QStringLiteral("The design uses a font that is no longer in the library.");
                return nullptr;
            }
            file = m_library.assetPath(*it);
        }
        if (file.isEmpty()) {
            m_problems << QStringLiteral("No font found for text without one (fc-match sans-serif).");
            return nullptr;
        }
        const QString key = QStringLiteral("%1@%2").arg(file).arg(pixels);
        if (const auto it = m_built.constFind(key); it != m_built.cend())
            return &*it;
        if (!m_families.contains(file))
            m_families.insert(file, QStringLiteral("Ekpo F%1").arg(m_families.size() + 1));
        QString error;
        // The editor rebuilds on every change: grub-mkfont runs once per font and size.
        static QHash<QString, QByteArray> made;
        const QString madeKey = key + QLatin1Char('|') + m_families.value(file);
        if (!made.contains(madeKey))
            made.insert(madeKey, makePf2(file, pixels, m_families.value(file), error));
        const QByteArray data = made.value(madeKey);
        const auto font = data.isEmpty() ? std::nullopt : Pf2Font::read(data, error);
        if (!font) {
            m_problems << error;
            return nullptr;
        }
        // 00_header loads *.pf2 from the theme folder itself (CLAUDE.md).
        files << PayloadFile{QStringLiteral("%1-%2.pf2").arg(m_families.value(file).section(QLatin1Char(' '), 1).toLower()).arg(pixels), data};
        return &*m_built.insert(key, *font);
    }

    QList<PayloadFile> files;

private:
    const Library &m_library;
    const QMap<QString, Asset> &m_assets;
    QStringList &m_problems;
    QMap<QString, QString> m_families; // font file → family
    QMap<QString, Pf2Font> m_built;
};

// The width a menu needs for text: GRUB starts it at padding + selection border +
// icon + space and ends it before width − padding − selection border, with 2 px
// kept for the scrollbar's pad (gui_list.c, GRUB 2.14).
int rowWidth(int textWidth, int padding, int border, int icon, int iconSpace)
{
    return 2 * padding + 2 * border + icon + iconSpace + textWidth + 2;
}

} // namespace

QStringList clippedEntries(const QList<PayloadFile> &theme, const QStringList &entries)
{
    QByteArray text;
    int border = 0;
    QList<Pf2Font> fonts;
    for (const PayloadFile &f : theme) {
        QString error;
        if (f.path == QLatin1String("theme.txt"))
            text = f.data;
        else if (f.path == QLatin1String("select_w.png"))
            border = QImage::fromData(f.data).width();
        else if (f.path.endsWith(QLatin1String(".pf2")))
            if (const auto font = Pf2Font::read(f.data, error))
                fonts << *font;
    }
    const qsizetype start = text.indexOf("+ boot_menu {");
    const QString menu = QString::fromUtf8(text.mid(start, text.indexOf('}', start) - start));
    const auto value = [&](const char *key) {
        static const QRegularExpression line(QStringLiteral("^\\s*(\\w+)\\s*=\\s*\"?([^\"\\n]*)\"?\\s*$"), QRegularExpression::MultilineOption);
        for (auto it = line.globalMatch(menu); it.hasNext();) {
            const auto m = it.next();
            if (m.captured(1) == QLatin1String(key))
                return m.captured(2);
        }
        return QString();
    };
    const auto font = std::find_if(fonts.cbegin(), fonts.cend(), [&](const Pf2Font &f) { return f.name == value("item_font"); });
    if (start < 0 || font == fonts.cend())
        return {};
    const int width = value("width").toInt();
    QStringList clipped;
    for (const QString &entry : entries) {
        if (rowWidth(font->textWidth(entry), value("item_padding").toInt(), border, value("icon_width").toInt(), value("item_icon_space").toInt()) > width)
            clipped << entry;
    }
    return clipped;
}

QList<PayloadFile> buildBootMenu(const Design &design, const Library &library, QSize screen, const QStringList &entries, QStringList &problems,
                                 GrubLayout *layout, bool images)
{
    if (layout)
        *layout = GrubLayout{};
    QStringList errors;
    QMap<QString, Asset> assets;
    for (const Asset &a : library.assets(errors))
        assets.insert(a.hash, a);
    problems << errors;
    const auto imageFile = [&](const QString &hash) -> QString {
        const auto it = assets.constFind(hash);
        if (it != assets.cend() && it->kind == Asset::Kind::Image)
            return library.assetPath(*it);
        problems << QStringLiteral("The design uses an image that is no longer in the library.");
        return {};
    };
    const int H = screen.height();
    const auto textPixels = [&](const QJsonObject &e, int number) {
        const double size = e.value(QStringLiteral("size")).toDouble(3);
        if (!(size > 0))
            problems << QStringLiteral("Element %1 has no valid text size.").arg(number);
        return std::max(8, int(H * size / 100));
    };

    QList<PayloadFile> files;
    Fonts fonts(library, assets, problems);
    const QJsonObject &content = design.content;
    const QJsonObject background = content.value(QStringLiteral("background")).toObject();
    QString theme = QStringLiteral("# Generated by Ekpo from the design %1, version %2, for %3x%4.\n"
                                   "# The terminal keeps GRUB's own font (Unifont).\n\n"
                                   "title-text: \"\"\n"
                                   "desktop-color: \"%5\"\n")
                        .arg(themeString(design.name), QString::number(design.version), QString::number(screen.width()), QString::number(H),
                             rgb(readColour(background, QStringLiteral("color"), QStringLiteral("#000000"), problems)));
    // GRUB fills with one colour: a gradient or an image becomes background.png, built
    // at the screen's size so GRUB has nothing to scale.
    const QString hash = background.value(QStringLiteral("image")).toString();
    const bool gradient = background.contains(QStringLiteral("color2"));
    if (images && (gradient || !hash.isEmpty())) {
        QImage canvas(screen, QImage::Format_RGB32);
        QPainter p(&canvas);
        const QColor top(readColour(background, QStringLiteral("color"), QStringLiteral("#000000"), problems));
        QLinearGradient fill(0, 0, 0, H);
        fill.setColorAt(0, top);
        fill.setColorAt(1, gradient ? QColor(readColour(background, QStringLiteral("color2"), QStringLiteral("#000000"), problems)) : top);
        p.fillRect(canvas.rect(), fill);
        if (const QString file = hash.isEmpty() ? QString() : imageFile(hash); !file.isEmpty()) {
            QImage image = QImageReader(file).read();
            if (image.isNull()) {
                problems << QStringLiteral("The background image can't be read.");
            } else {
                image = image.scaled(screen, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                p.drawImage(QPoint((screen.width() - image.width()) / 2, (H - image.height()) / 2), image);
            }
        }
        p.end();
        files << PayloadFile{QStringLiteral("background.png"), png(canvas)};
        theme += QStringLiteral("desktop-image: \"background.png\"\ndesktop-image-scale-method: \"stretch\"\n");
    }

    static const QStringList types = {QStringLiteral("menu"), QStringLiteral("text"), QStringLiteral("countdown"), QStringLiteral("progress"),
                                      QStringLiteral("image")};
    static const QMap<QString, QString> aligns = {{QStringLiteral("left"), QStringLiteral("left")},
                                                  {QStringLiteral("center"), QStringLiteral("center")},
                                                  {QStringLiteral("right"), QStringLiteral("right")}};
    QString components;
    int menus = 0, imageCount = 0, number = 0;
    for (const QJsonValue &value : content.value(QStringLiteral("elements")).toArray()) {
        const QJsonObject e = value.toObject();
        const QString type = e.value(QStringLiteral("type")).toString();
        ++number;
        if (!types.contains(type)) {
            problems << QStringLiteral("Element %1 has an unknown type “%2”.").arg(number).arg(type);
            continue;
        }
        const auto box = readBox(e, number, problems);
        if (!box)
            continue;
        QRect rect = pixelRect(*box, screen);
        const auto place = [](const QRect &r) {
            return QStringLiteral("    left = %1\n    top = %2\n    width = %3\n    height = %4\n").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
        };

        if (type == QLatin1String("menu")) {
            if (++menus > 1) {
                problems << QStringLiteral("Element %1 is a second menu; a boot menu has one.").arg(number);
                continue;
            }
            int pixels = textPixels(e, number);
            const QString sizeKey = QStringLiteral("%1x%2").arg(screen.width()).arg(H);
            if (const int override = content.value(QStringLiteral("sizeOverrides")).toObject().value(sizeKey).toInt(); override > 0)
                pixels = override;
            const Pf2Font *font = fonts.get(e, pixels);
            if (!font)
                continue;
            // Spacing from the font's line height; GRUB's own defaults are for 1080p.
            const int line = font->ascent + font->descent;
            const int itemHeight = int(std::lround(line * 1.4)), padding = int(std::lround(line * 0.25)), spacing = int(std::lround(line * 0.2));
            const int icon = line, iconSpace = int(std::lround(line * 0.4));
            const QJsonObject selection = e.value(QStringLiteral("selection")).toObject();
            const QString style = selection.value(QStringLiteral("style")).toString(QStringLiteral("rounded"));
            if (style != QLatin1String("flat") && style != QLatin1String("rounded") && style != QLatin1String("glow"))
                problems << QStringLiteral("Element %1 has an unknown selection style “%2”.").arg(number).arg(style);
            const int border = style == QLatin1String("flat") ? 1 : style == QLatin1String("glow") ? int(std::lround(itemHeight * 0.35))
                                                                                                   : int(std::lround(itemHeight * 0.25));
            files << selectionImages(style, QColor(readColour(selection, QStringLiteral("color"), QStringLiteral("#40ffffff"), problems)), std::max(1, border));

            // Text starts at padding + selection border + icon + space and must end
            // before width − padding − selection border (gui_list.c, GRUB 2.14);
            // four more digits leave room for longer kernel versions.
            int widest = 0;
            QString longest;
            for (const QString &entry : entries) {
                if (const int w = font->textWidth(entry); w > widest) {
                    widest = w;
                    longest = entry;
                }
            }
            const int needed = rowWidth(widest + font->textWidth(QStringLiteral("0000")), padding, border, icon, iconSpace);
            bool fits = true;
            if (rect.width() < needed) {
                const double centre = rect.x() + rect.width() / 2.0;
                rect.setWidth(std::min(needed, screen.width()));
                rect.moveLeft(std::clamp(int(std::lround(centre - rect.width() / 2.0)), 0, screen.width() - rect.width()));
                fits = needed <= screen.width();
            }
            if (layout) {
                layout->menu = rect;
                layout->neededWidth = needed;
                layout->longestEntry = longest;
                layout->fits = fits;
                layout->itemFontPixels = pixels;
            }

            const QString itemColour = rgb(readColour(e, QStringLiteral("color"), QStringLiteral("#cccccc"), problems));
            const QString selectedColour = rgb(readColour(e, QStringLiteral("selectedColor"), QStringLiteral("#ffffff"), problems));
            components += QStringLiteral("\n+ boot_menu {\n") + place(rect)
                + QStringLiteral("    item_font = %1\n    selected_item_font = %1\n    item_color = \"%2\"\n    selected_item_color = \"%3\"\n"
                                 "    item_height = %4\n    item_padding = %5\n    item_spacing = %6\n"
                                 "    icon_width = %7\n    icon_height = %7\n    item_icon_space = %8\n"
                                 "    selected_item_pixmap_style = \"select_*.png\"\n    scrollbar = false\n}\n")
                      .arg(themeString(font->name), itemColour, selectedColour, QString::number(itemHeight), QString::number(padding),
                           QString::number(spacing), QString::number(icon), QString::number(iconSpace));

            // Icons: GRUB shows icons/<class>.png for the first class of an entry it finds.
            const QJsonObject icons = images ? content.value(QStringLiteral("icons")).toObject() : QJsonObject();
            for (auto it = icons.constBegin(); it != icons.constEnd(); ++it) {
                static const QRegularExpression className(QStringLiteral("^[a-z0-9_+-]+$"));
                if (!className.match(it.key()).hasMatch()) {
                    problems << QStringLiteral("“%1” isn't a menu entry class.").arg(it.key());
                    continue;
                }
                QImage image;
                if (const QString value = it->toString(); value.startsWith(QLatin1String("builtin:"))) {
                    image = builtinIcon(value.mid(8), icon, QColor(itemColour));
                    if (image.isNull())
                        problems << QStringLiteral("There is no built-in icon “%1”.").arg(value.mid(8));
                } else if (const QString file = imageFile(value); !file.isEmpty()) {
                    image = QImageReader(file).read().scaled(icon, icon, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                }
                if (!image.isNull())
                    files << PayloadFile{QStringLiteral("icons/%1.png").arg(it.key()), png(image)};
            }
        } else if (type == QLatin1String("text") || type == QLatin1String("countdown")) {
            const Pf2Font *font = fonts.get(e, textPixels(e, number));
            if (!font)
                continue;
            const QString align = e.value(QStringLiteral("align")).toString(QStringLiteral("center"));
            if (!aligns.contains(align))
                problems << QStringLiteral("Element %1 has an unknown alignment “%2”.").arg(number).arg(align);
            QString text = e.value(QStringLiteral("text")).toString();
            if (type == QLatin1String("countdown") && !text.contains(QLatin1String("%d")))
                text = QStringLiteral("%d s");
            if (layout && font->textWidth(QString(text).replace(QLatin1String("%d"), QLatin1String("30"))) > rect.width())
                layout->clippedTexts << text;
            components += QStringLiteral("\n+ label {\n") + (type == QLatin1String("countdown") ? QStringLiteral("    id = \"__timeout__\"\n") : QString())
                + place(rect)
                + QStringLiteral("    text = %1\n    font = %2\n    color = \"%3\"\n    align = \"%4\"\n}\n")
                      .arg(themeString(text), themeString(font->name), rgb(readColour(e, QStringLiteral("color"), QStringLiteral("#ffffff"), problems)),
                           aligns.value(align, QStringLiteral("center")));
        } else if (type == QLatin1String("progress")) {
            const QString bar = rgb(readColour(e, QStringLiteral("color"), QStringLiteral("#ffffff"), problems));
            const QString back = rgb(readColour(e, QStringLiteral("background"), QStringLiteral("#404040"), problems));
            components += QStringLiteral("\n+ progress_bar {\n    id = \"__timeout__\"\n") + place(rect)
                + QStringLiteral("    fg_color = \"%1\"\n    bg_color = \"%2\"\n    border_color = \"%2\"\n    text = \"\"\n}\n").arg(bar, back);
        } else if (images) { // image: scaled here to the size it is shown at
            const QString file = imageFile(e.value(QStringLiteral("asset")).toString());
            QImageReader reader(file);
            if (file.isEmpty())
                continue;
            QSize size = reader.size();
            if (size.isEmpty()) {
                problems << QStringLiteral("Element %1's image can't be read.").arg(number);
                continue;
            }
            size.scale(rect.size(), Qt::KeepAspectRatio);
            reader.setScaledSize(size);
            const QImage image = reader.read();
            const QRect shown(rect.center().x() - size.width() / 2, rect.center().y() - size.height() / 2, size.width(), size.height());
            const QString name = QStringLiteral("image-%1.png").arg(++imageCount);
            files << PayloadFile{name, png(image)};
            components += QStringLiteral("\n+ image {\n") + place(shown) + QStringLiteral("    file = \"%1\"\n}\n").arg(name);
        }
    }
    if (menus == 0)
        problems << QStringLiteral("A boot menu design needs a menu.");

    files << fonts.files << PayloadFile{QStringLiteral("theme.txt"), (theme + components).toUtf8()};
    std::sort(files.begin(), files.end(), [](const PayloadFile &a, const PayloadFile &b) { return a.path < b.path; });
    return files;
}

namespace {

// A component of a theme.txt Ekpo wrote: "+ type {" then "key = value" lines.
struct Component {
    QString type;
    QMap<QString, QString> values;
    int number(const char *key) const { return values.value(QLatin1String(key)).toInt(); }
    QRect rect() const { return {number("left"), number("top"), number("width"), number("height")}; }
};

QString classOf(const QString &entry)
{
    // GRUB shows icons/<class>.png; Ubuntu's scripts give Linux entries the
    // distributor's name, os-prober "windows", the firmware entry "efi".
    if (entry.startsWith(QLatin1String("Advanced options")))
        return {};
    if (entry.startsWith(QLatin1String("Windows")))
        return QStringLiteral("windows");
    if (entry.startsWith(QLatin1String("UEFI Firmware")))
        return QStringLiteral("efi");
    if (entry.startsWith(QLatin1String("Memory test")))
        return QStringLiteral("memtest86");
    static const QRegularExpression notClass(QStringLiteral("[^a-z0-9_]"));
    return entry.section(QLatin1Char(' '), 0, 0).toLower().remove(notClass);
}

} // namespace

QImage renderBootMenu(const QList<PayloadFile> &theme, QSize screen, const QStringList &entries, int seconds)
{
    QMap<QString, QByteArray> files;
    QMap<QString, Pf2Font> fonts;
    for (const PayloadFile &f : theme) {
        files.insert(f.path, f.data);
        QString error;
        if (f.path.endsWith(QLatin1String(".pf2")))
            if (const auto font = Pf2Font::read(f.data, error))
                fonts.insert(font->name, *font);
    }
    const QString text = QString::fromUtf8(files.value(QStringLiteral("theme.txt")));
    const auto global = [&](const QString &key) {
        const QRegularExpression line(QStringLiteral("^%1:\\s*\"([^\"]*)\"").arg(QRegularExpression::escape(key)), QRegularExpression::MultilineOption);
        return line.match(text).captured(1);
    };
    QList<Component> components;
    static const QRegularExpression block(QStringLiteral("^\\+ (\\w+) \\{\\n(.*?)^\\}"), QRegularExpression::MultilineOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression pair(QStringLiteral("^\\s*(\\w+) = \"?([^\"\\n]*)\"?$"), QRegularExpression::MultilineOption);
    for (auto it = block.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        Component c{m.captured(1), {}};
        for (auto p = pair.globalMatch(m.captured(2)); p.hasNext();) {
            const auto v = p.next();
            c.values.insert(v.captured(1), v.captured(2));
        }
        components << c;
    }

    QImage image(screen, QImage::Format_RGB32);
    image.fill(QColor(global(QStringLiteral("desktop-color"))));
    QPainter p(&image);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (const QString file = global(QStringLiteral("desktop-image")); files.contains(file))
        p.drawImage(QRect(QPoint(), screen), QImage::fromData(files.value(file)));
    const auto drawText = [&](const Pf2Font &font, const QString &string, const QColor &colour, const QRect &box, int x) {
        // One line, clipped to its box like GRUB's viewports, centred vertically.
        p.save();
        p.setClipRect(box);
        p.drawImage(QPoint(x, box.y() + (box.height() - font.ascent - font.descent) / 2), font.render(string, colour));
        p.restore();
    };

    for (const Component &c : std::as_const(components)) {
        const QRect r = c.rect();
        if (c.type == QLatin1String("image")) {
            p.drawImage(r, QImage::fromData(files.value(c.values.value(QStringLiteral("file")))));
        } else if (c.type == QLatin1String("progress_bar")) {
            p.fillRect(r, QColor(c.values.value(QStringLiteral("bg_color"))));
            p.fillRect(QRect(r.topLeft(), QSize(r.width() / 2, r.height())), QColor(c.values.value(QStringLiteral("fg_color"))));
        } else if (c.type == QLatin1String("label")) {
            const auto font = fonts.constFind(c.values.value(QStringLiteral("font")));
            if (font == fonts.cend())
                continue;
            const QString string = QString(c.values.value(QStringLiteral("text"))).replace(QLatin1String("%d"), QString::number(seconds));
            const int width = font->textWidth(string);
            const QString align = c.values.value(QStringLiteral("align"));
            const int x = align == QLatin1String("left") ? r.x() : align == QLatin1String("right") ? r.right() + 1 - width : r.x() + (r.width() - width) / 2;
            drawText(*font, string, QColor(c.values.value(QStringLiteral("color"))), r, x);
        } else if (c.type == QLatin1String("boot_menu")) {
            const auto font = fonts.constFind(c.values.value(QStringLiteral("item_font")));
            if (font == fonts.cend())
                continue;
            const int pad = c.number("item_padding"), height = c.number("item_height"), spacing = c.number("item_spacing");
            const int icon = c.number("icon_width"), iconSpace = c.number("item_icon_space");
            const QImage w = QImage::fromData(files.value(QStringLiteral("select_w.png")));
            const int border = w.width();
            // The top level: kernel entries sit in the "Advanced options" submenu.
            QStringList shown;
            const bool submenu = std::any_of(entries.cbegin(), entries.cend(), [](const QString &e) { return e.startsWith(QLatin1String("Advanced options")); });
            for (const QString &entry : entries) {
                if (!(submenu && entry.contains(QLatin1String(", with Linux "))))
                    shown << entry;
            }
            int top = r.y() + pad;
            for (qsizetype i = 0; i < shown.size() && top + height <= r.bottom() + 1 - pad; ++i) {
                const QRect row(r.x() + pad, top, r.width() - 2 * pad, height);
                if (i == 0 && border > 0) { // the selected entry's 9-slice box
                    const auto piece = [&](const char *name) { return QImage::fromData(files.value(QStringLiteral("select_%1.png").arg(QLatin1String(name)))); };
                    const int b = border;
                    p.drawImage(QRect(row.x(), row.y(), b, b), piece("nw"));
                    p.drawImage(QRect(row.x() + b, row.y(), row.width() - 2 * b, b), piece("n"));
                    p.drawImage(QRect(row.right() + 1 - b, row.y(), b, b), piece("ne"));
                    p.drawImage(QRect(row.x(), row.y() + b, b, row.height() - 2 * b), piece("w"));
                    p.drawImage(QRect(row.x() + b, row.y() + b, row.width() - 2 * b, row.height() - 2 * b), piece("c"));
                    p.drawImage(QRect(row.right() + 1 - b, row.y() + b, b, row.height() - 2 * b), piece("e"));
                    p.drawImage(QRect(row.x(), row.bottom() + 1 - b, b, b), piece("sw"));
                    p.drawImage(QRect(row.x() + b, row.bottom() + 1 - b, row.width() - 2 * b, b), piece("s"));
                    p.drawImage(QRect(row.right() + 1 - b, row.bottom() + 1 - b, b, b), piece("se"));
                }
                const QString iconFile = QStringLiteral("icons/%1.png").arg(classOf(shown[i]));
                if (files.contains(iconFile))
                    p.drawImage(QPoint(row.x() + border, row.y() + (height - icon) / 2), QImage::fromData(files.value(iconFile)));
                const int textLeft = row.x() + border + icon + iconSpace;
                const QRect textBox(textLeft, row.y(), row.right() + 1 - border - textLeft, height);
                const QString colour = c.values.value(i == 0 ? QStringLiteral("selected_item_color") : QStringLiteral("item_color"));
                drawText(*font, shown[i], QColor(colour), textBox, textLeft);
                top += height + spacing;
            }
        }
    }
    return image;
}

QStringList menuClasses(const QStringList &entries)
{
    QStringList classes;
    for (const QString &entry : entries) {
        if (const QString c = classOf(entry); !c.isEmpty() && !classes.contains(c))
            classes << c;
    }
    return classes;
}

QString builtinIconFor(const QString &entryClass)
{
    static const QMap<QString, QString> icons = {
        {QStringLiteral("windows"), QStringLiteral("windows")}, {QStringLiteral("efi"), QStringLiteral("firmware")},
        {QStringLiteral("memtest86"), QStringLiteral("memory")},
    };
    return entryClass.isEmpty() ? QString() : icons.value(entryClass, QStringLiteral("os")); // any other class is a system
}

QImage builtinIcon(const QString &name, int size, const QColor &colour)
{
    // Drawn on a 24-unit grid, in strokes and fills of one colour.
    static const QStringList names = {QStringLiteral("os"), QStringLiteral("windows"), QStringLiteral("firmware"), QStringLiteral("memory")};
    if (!names.contains(name) || size < 1)
        return {};
    QImage icon(size, size, QImage::Format_ARGB32_Premultiplied);
    icon.fill(Qt::transparent);
    QPainter p(&icon);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 24.0, size / 24.0);
    QPen pen(colour, 2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    if (name == QLatin1String("os")) { // a screen with a prompt
        p.drawRoundedRect(QRectF(3, 4, 18, 13), 2, 2);
        p.drawPolyline(QList<QPointF>{{7, 8.5}, {10, 10.5}, {7, 12.5}});
        p.drawLine(QPointF(12, 12.5), QPointF(16, 12.5));
        p.drawLine(QPointF(9, 20.5), QPointF(15, 20.5));
    } else if (name == QLatin1String("windows")) { // four panes
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        for (const QPointF &at : {QPointF(4, 4), QPointF(13, 4), QPointF(4, 13), QPointF(13, 13)})
            p.drawRoundedRect(QRectF(at, QSizeF(7, 7)), 1, 1);
    } else if (name == QLatin1String("firmware")) { // a chip with pins
        p.drawRoundedRect(QRectF(7, 7, 10, 10), 1.5, 1.5);
        for (const double at : {9.5, 12.0, 14.5}) {
            p.drawLine(QPointF(at, 3.5), QPointF(at, 6));
            p.drawLine(QPointF(at, 18), QPointF(at, 20.5));
            p.drawLine(QPointF(3.5, at), QPointF(6, at));
            p.drawLine(QPointF(18, at), QPointF(20.5, at));
        }
    } else { // memory: a module with chips and contacts
        p.drawRoundedRect(QRectF(2.5, 7, 19, 9), 1.5, 1.5);
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        for (const double at : {5.0, 10.5, 16.0})
            p.drawRect(QRectF(at, 9.5, 3, 4));
        p.setPen(pen);
        for (const double at : {5.0, 8.0, 11.0, 14.0, 17.0})
            p.drawLine(QPointF(at, 18.5), QPointF(at, 20.5));
    }
    p.end();
    return icon;
}
