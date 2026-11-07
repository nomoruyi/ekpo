#pragma once

#include "library.h"
#include "payload.h"

#include <QRect>
#include <QSize>
#include <QStringList>

// Builds a GRUB theme (SPEC.md §4.5) from a boot menu design, for one screen size:
// theme.txt, .pf2 fonts at the pixel sizes that screen needs, the background and
// images scaled to their size, 9-slice images for the selected entry, and icons.
// Needs a QGuiApplication (drawing) and grub-mkfont.
//
// Design content (positions and sizes in % of the screen, top-left corner; text
// sizes in % of the screen height):
//   background: {color, color2: a gradient's other end (optional), angle: its direction
//                in degrees as in CSS (180, the default, top to bottom; gencommon.h),
//                image: <asset hash, optional; covers the screen>}
//   elements: [
//     {type: "menu", font, size, color, selectedColor,
//      selection: {style: "flat" | "rounded" | "glow", color}, x, y, width, height}   exactly one
//     {type: "text", text, font, size, color, align, x, y, width, height}   one line, e.g. the help line
//     {type: "countdown", text: "… %d …", font, size, color, align, x, y, width, height}
//     {type: "progress", color, background, x, y, width, height}   the countdown as a bar
//     {type: "image", asset, x, y, width, height}   aspect ratio kept, e.g. a logo
//   ]
//   icons: {<menu entry class, e.g. "kubuntu", "windows", "efi">: <image asset> | "builtin:<name>"}
//          built-in: Ekpo's own monochrome icons in the menu's text colour
//   sizeOverrides: {"<W>x<H>": <menu font size in pixels>}   per screen size
// A font is an asset hash; without one, fontconfig's sans-serif.
//
// GRUB draws each entry on one line and clips it (SPEC.md §4.5): the menu widens,
// around its centre, until the longest entry fits with room for longer kernel
// versions; if even the screen width isn't enough, layout.fits is false.
struct GrubLayout {
    QRect menu; // pixels, after widening
    int neededWidth = 0; // for the longest entry
    QString longestEntry;
    bool fits = true;
    int itemFontPixels = 0;
    QStringList clippedTexts; // labels GRUB cuts off (one line, clipped to their box)
};

// images = false skips the background, logos and icons (layout checks only).
QList<PayloadFile> buildBootMenu(const Design &design, const Library &library, QSize screen, const QStringList &entries, QStringList &problems,
                                 GrubLayout *layout = nullptr, bool images = true);

// The editor's preview (SPEC.md §4.5): a theme Ekpo built, drawn the way GRUB lays it
// out (gfxmenu, GRUB 2.14) in its own .pf2 fonts. The menu lists entries' top level
// (kernel entries sit in "Advanced options"), the first one selected; countdowns show seconds.
QImage renderBootMenu(const QList<PayloadFile> &theme, QSize screen, const QStringList &entries, int seconds = 10);

// The classes GRUB looks up icons/<class>.png for, for these entries ("kubuntu",
// "windows", "efi", ...), in the order they first appear.
QStringList menuClasses(const QStringList &entries);

// Ekpo's built-in icon for an entry class (SPEC.md §4.5): "os" (Linux systems),
// "windows", "firmware" or "memory"; empty if there is none.
QString builtinIconFor(const QString &entryClass);
// That icon, size pixels square, in colour.
QImage builtinIcon(const QString &name, int size, const QColor &colour);

// The entries GRUB cuts off in a theme Ekpo built (its theme.txt, fonts and
// selection images), e.g. the titles of the real grub.cfg after an apply.
QStringList clippedEntries(const QList<PayloadFile> &theme, const QStringList &entries);
