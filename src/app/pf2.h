#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QString>

#include <optional>

// GRUB's .pf2 bitmap fonts (ARCHITECTURE.md §4 `gen`): built from TrueType/OpenType
// fonts with grub-mkfont, read back for text widths (the entry-width check, SPEC.md
// §4.5) and for drawing the preview with GRUB's own glyphs.
//
// The format: "FILE" "PFF2", then sections of a 4-byte name and a 4-byte big-endian
// length (NAME, FAMI, PTSZ, MAXW, MAXH, ASCE, DESC, CHIX, ...), then DATA. CHIX holds
// 9 bytes per glyph (code point, storage flags, file offset); each glyph is width,
// height, x and y offset, advance (16 bits each, big-endian) and a 1-bit bitmap.
struct Pf2Font {
    struct Glyph {
        int width = 0;
        int height = 0;
        int xOffset = 0; // from the pen position on the baseline to the bitmap's left
        int yOffset = 0; // and to its bottom
        int advance = 0;
        QByteArray bitmap; // rows packed without padding, most significant bit first
    };

    QString name; // what theme.txt refers to, e.g. "Ekpo Item Regular 24"
    QString family;
    int pointSize = 0;
    int maxWidth = 0;
    int maxHeight = 0;
    int ascent = 0;
    int descent = 0;
    QHash<char32_t, Glyph> glyphs;

    static std::optional<Pf2Font> read(const QByteArray &data, QString &error);
    // As GRUB measures it: the advances added up. A character the font lacks counts
    // as maxWidth (GRUB draws a fallback glyph there).
    int textWidth(const QString &text) const;
    // The text as GRUB draws it, on a transparent image ascent + descent high.
    QImage render(const QString &text, const QColor &colour) const;
};

// grub-mkfont: fontFile at pixelSize with the characters a boot menu needs (Latin,
// punctuation, arrows); the file's name becomes "<name> <style> <size>". The line
// height (ascent + descent) comes from the tallest glyph, accents included, so it is
// more than pixelSize. Empty, with error, if it fails.
QByteArray makePf2(const QString &fontFile, int pixelSize, const QString &name, QString &error);
