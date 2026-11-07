#pragma once

#include "library.h"
#include "payload.h"

// Builds a Plasma splash (look-and-feel package with only contents/splash) from a
// Plasma splash design (SPEC.md §4.7). The package is plain QtQuick and keeps
// working without Ekpo.
//
// Design content (positions and sizes in % of the screen, top-left corner):
//   background: {color: "#rrggbb" | "#aarrggbb", color2: a gradient's other end (optional),
//                angle: its direction in degrees as in CSS (180, the default, top to
//                bottom; gencommon.h), image: <asset hash, optional>}
//   fadeIn: milliseconds, 0 = none
//   elements: [
//     {type: "image", asset, x, y, width, height}            (aspect ratio kept)
//     {type: "text", text, font: <asset hash, optional>, size: % of screen height,
//      color, align: "left" | "center" | "right", x, y, width, height}
//     {type: "spinner", color, x, y, width, height}
//     {type: "video", asset: <silent MP4>, fill: "crop" | "fit", x, y, width, height}
//     {type: "frames", frames: [<image asset>, …], seconds, x, y, width, height}  a loop
//     {type: "progress", color, background, x, y, width, height}  follows Plasma's start
//   ]
//   author, licence: package metadata (default nomoruyi, MIT)
//
// Every problem found goes to problems; the files are only usable without any.
// With links (the editor's preview), assets aren't read: links maps their package
// paths to the library files instead.
QList<PayloadFile> buildPlasmaSplash(const Design &design, const QString &packageId, const Library &library, QStringList &problems,
                                     QMap<QString, QString> *links = nullptr);
