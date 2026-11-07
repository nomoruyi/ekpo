#pragma once

#include <QJsonObject>
#include <QLineF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <optional>

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

// The family of a font file, registered with the application once. Needs a
// QGuiApplication; empty if the file isn't a font.
QString registerFont(const QString &file);
