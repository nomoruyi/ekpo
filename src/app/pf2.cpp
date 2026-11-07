#include "pf2.h"

#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>

#include <algorithm>

namespace {

// Latin-1 and Latin Extended-A, dashes, quotes, bullet and ellipsis, guillemets, the
// euro sign, arrows and the triangles menus use.
const QString ranges = QStringLiteral("0x20-0x7E,0xA0-0x17F,0x2010-0x2027,0x2030-0x203A,0x20AC-0x20AC,0x2190-0x2193,0x25B2-0x25BC");

} // namespace

std::optional<Pf2Font> Pf2Font::read(const QByteArray &data, QString &error)
{
    const auto fits = [&](qint64 at, qint64 size) { return at >= 0 && size >= 0 && at + size <= data.size(); };
    const auto u16 = [&](qint64 at) { return qFromBigEndian<quint16>(data.constData() + at); };
    const auto s16 = [&](qint64 at) { return qFromBigEndian<qint16>(data.constData() + at); };
    const auto u32 = [&](qint64 at) { return qFromBigEndian<quint32>(data.constData() + at); };

    if (!data.startsWith(QByteArrayLiteral("FILE\0\0\0\x04PFF2"))) {
        error = QStringLiteral("not a GRUB .pf2 font");
        return std::nullopt;
    }
    Pf2Font font;
    QByteArray index;
    qint64 pos = 12;
    while (fits(pos, 8)) {
        const QByteArray section = data.mid(pos, 4);
        const qint64 length = u32(pos + 4);
        pos += 8;
        if (section == "DATA") // glyphs follow, found through CHIX
            break;
        if (!fits(pos, length)) {
            error = QStringLiteral("the %1 section is cut off").arg(QString::fromLatin1(section));
            return std::nullopt;
        }
        const QByteArray body = data.mid(pos, length);
        const auto text = [&] { return QString::fromUtf8(body.constData(), qstrnlen(body.constData(), body.size())); };
        const int number = length >= 2 ? u16(pos) : 0;
        if (section == "NAME")
            font.name = text();
        else if (section == "FAMI")
            font.family = text();
        else if (section == "PTSZ")
            font.pointSize = number;
        else if (section == "MAXW")
            font.maxWidth = number;
        else if (section == "MAXH")
            font.maxHeight = number;
        else if (section == "ASCE")
            font.ascent = number;
        else if (section == "DESC")
            font.descent = number;
        else if (section == "CHIX")
            index = body;
        pos += length;
    }
    if (index.isEmpty() || index.size() % 9 != 0) {
        error = QStringLiteral("the character index is missing or damaged");
        return std::nullopt;
    }
    for (qsizetype i = 0; i < index.size(); i += 9) {
        const char32_t code = qFromBigEndian<quint32>(index.constData() + i);
        const qint64 at = qFromBigEndian<quint32>(index.constData() + i + 5);
        if ((quint8(index[i + 4]) & 0x07) != 0) // compressed glyphs: grub-mkfont never writes them
            continue;
        if (!fits(at, 10)) {
            error = QStringLiteral("a glyph lies outside the file");
            return std::nullopt;
        }
        Glyph g{u16(at), u16(at + 2), s16(at + 4), s16(at + 6), s16(at + 8), {}};
        const qint64 bytes = (qint64(g.width) * g.height + 7) / 8;
        if (!fits(at + 10, bytes)) {
            error = QStringLiteral("a glyph's bitmap lies outside the file");
            return std::nullopt;
        }
        g.bitmap = data.mid(at + 10, bytes);
        font.glyphs.insert(code, g);
    }
    if (font.name.isEmpty() || font.glyphs.isEmpty()) {
        error = QStringLiteral("the font has no name or no glyphs");
        return std::nullopt;
    }
    return font;
}

int Pf2Font::textWidth(const QString &text) const
{
    int width = 0;
    for (const char32_t c : text.toUcs4()) {
        const auto it = glyphs.constFind(c);
        width += it == glyphs.cend() ? maxWidth : it->advance;
    }
    return width;
}

QImage Pf2Font::render(const QString &text, const QColor &colour) const
{
    QImage image(std::max(1, textWidth(text)), std::max(1, ascent + descent), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    const QRgb ink = qPremultiply(colour.rgba());
    int pen = 0;
    for (const char32_t c : text.toUcs4()) {
        const auto it = glyphs.constFind(c);
        if (it == glyphs.cend()) {
            pen += maxWidth;
            continue;
        }
        const Glyph &g = *it;
        // GRUB: bitmap left = pen + x offset, top = baseline − height − y offset.
        const int left = pen + g.xOffset, top = ascent - g.height - g.yOffset;
        for (int y = 0; y < g.height; ++y) {
            for (int x = 0; x < g.width; ++x) {
                const qint64 bit = qint64(y) * g.width + x;
                const int px = left + x, py = top + y;
                if ((quint8(g.bitmap[bit / 8]) & (0x80 >> (bit % 8))) && px >= 0 && py >= 0 && px < image.width() && py < image.height())
                    image.setPixel(px, py, ink);
            }
        }
        pen += g.advance;
    }
    return image;
}

QByteArray makePf2(const QString &fontFile, int pixelSize, const QString &name, QString &error)
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        error = QStringLiteral("Can't create a temporary folder: %1").arg(dir.errorString());
        return {};
    }
    const QString output = dir.filePath(QStringLiteral("font.pf2"));
    QProcess mkfont;
    mkfont.start(QStringLiteral("grub-mkfont"), {QStringLiteral("-s"), QString::number(pixelSize), QStringLiteral("-n"), name, QStringLiteral("-r"), ranges,
                                                 QStringLiteral("-o"), output, fontFile});
    if (!mkfont.waitForFinished(60000) || mkfont.exitStatus() != QProcess::NormalExit || mkfont.exitCode() != 0) {
        error = mkfont.error() == QProcess::FailedToStart
            ? QStringLiteral("grub-mkfont isn't installed (package grub-common).")
            : QStringLiteral("grub-mkfont failed on %1: %2").arg(fontFile, QString::fromUtf8(mkfont.readAllStandardError()).trimmed());
        mkfont.kill();
        return {};
    }
    // grub-mkfont exits 0 even when it can't read the font, and writes an empty one.
    QFile in(output);
    const QByteArray data = in.open(QIODevice::ReadOnly) ? in.readAll() : QByteArray();
    QString why;
    if (!Pf2Font::read(data, why)) {
        const QString message = QString::fromUtf8(mkfont.readAllStandardError()).trimmed();
        error = QStringLiteral("grub-mkfont made no usable font from %1: %2").arg(fontFile, message.isEmpty() ? why : message);
        return {};
    }
    return data;
}
