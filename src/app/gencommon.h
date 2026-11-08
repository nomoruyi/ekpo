#pragma once

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QLineF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <optional>

class QPainter;

// Shared by the generators (genksplash, genplymouth) and the editor.

// Where a design element sits, in % of the screen (top-left corner, size).
struct PercentBox {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
};

// Each missing or invalid value (negative sizes too) adds "Element <number> has no valid <key>."
std::optional<PercentBox> readBox(const QJsonObject &element, int number, QStringList &problems);

// o[key] as "#rrggbb" or "#aarrggbb" in lower case; the fallback, after a problem, if
// it isn't one.
QString readColour(const QJsonObject &o, const QString &key, const QString &fallback, QStringList &problems);

// The direction of the background's gradient in degrees (0–360), as in CSS: 180, the
// default, runs from color at the top to color2 at the bottom; 90 from left to right.
double gradientAngle(const QJsonObject &background);
// The line a gradient at that angle runs along in a box of that size, as CSS draws it:
// through the centre and just long enough that the corners get the end colours.
QLineF gradientLine(const QSizeF &size, double degrees);

// A palette (paletteKeys → "#rrggbb") from an image's main colours: background the
// most common one, gradient the next, accent the most vivid, accent2 a vivid one of
// another hue, text whichever reads best on the background.
QMap<QString, QString> paletteFromImage(const QImage &image);

// Spinners (SPEC.md §4.6, §4.7), drawn alike by every splash: genplymouth paints frames
// with paintSpinner, genksplash draws spinnerScript on a QML Canvas. The style "image"
// turns an image instead (not drawn here).
extern const QStringList spinnerStyles; // dots, arc, pulse, bars, orbit, comet, image
// One turn (or pulse) in seconds for "slow", "normal" (the default) or "fast"; 0 if unknown.
double spinnerSeconds(const QString &speed);
// The spinner in a size × size square at phase 0–1 of a turn.
void paintSpinner(QPainter &p, const QString &style, const QColor &colour, double size, double phase);
// The same drawing in JavaScript, for a Canvas with `tint` (color) and `phase` (0–1).
QString spinnerScript(const QString &style);

// Image animations (SPEC.md §4.6, §4.7): fade and slide (up, once), breathe, pulse and
// spin (looping); their length or loop in seconds, 0 if unknown.
extern const QStringList imageAnimations; // fade, breathe, pulse, slide, spin
double imageAnimationSeconds(const QString &animation);

// Text effects (SPEC.md §4.4): e["shadow"], e["outline"], e["glow"], each a colour; an
// invalid colour is off. Sizes follow the font: a shadow 6 % of its pixel size down and
// right, an outline 5 % wide (8 copies around), a glow 30 % wide.
struct TextEffects {
    QColor shadow;
    QColor outline;
    QColor glow;
    bool any() const { return shadow.isValid() || outline.isValid() || glow.isValid(); }
};
TextEffects readTextEffects(const QJsonObject &e, QStringList &problems);
// As QPainter::drawText(rect, flags, text) in colour, with the effects underneath, in
// p's font (its pixel size sets the effects' sizes).
void drawStyledText(QPainter &p, const QRectF &rect, int flags, const QString &text, const QColor &colour, const TextEffects &effects);

// The family of a font file, registered with the application once. Needs a
// QGuiApplication; empty if the file isn't a font.
QString registerFont(const QString &file);
