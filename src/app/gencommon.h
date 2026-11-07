#pragma once

#include <QJsonObject>
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

// The family of a font file, registered with the application once. Needs a
// QGuiApplication; empty if the file isn't a font.
QString registerFont(const QString &file);
