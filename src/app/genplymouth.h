#pragma once

#include "library.h"
#include "payload.h"

#include <QColor>
#include <QPointF>
#include <QRect>
#include <QSize>

#include <optional>

// Builds a Plymouth theme (SPEC.md §4.6) from a boot splash design, for one screen
// size. The still parts (background colour or gradient, background image, images,
// texts in any font) are drawn into one background.png, which Plymouth scales to the
// screen. The animation and the progress bar are drawn by Plymouth at their own pixel
// size, on top. Needs a QGuiApplication (fonts).
//
// Design content (positions and sizes in % of the screen, top-left corner):
//   background: {color, color2: a gradient's other end (optional), angle: its direction
//                in degrees as in CSS (180, the default, top to bottom; gencommon.h),
//                image: <asset hash, optional; covers the screen>}
//   elements: [
//     {type: "image", asset, x, y, width, height}                       (aspect ratio kept)
//     {type: "text", text, font: <asset hash, optional>, size: % of screen height,
//      color, align: "left" | "center" | "right", x, y, width, height}
//     {type: "spinner", color, x, y, width, height}                     } one animation,
//     {type: "frames", frames: [<image asset>, …], seconds, x, y, width, height}  } centred in its box
//     {type: "progress", color, background, x, y, width, height}        at most one
//   ]
//   hideMessages: status messages at boot, shutdown and reboot (password prompts
//                 always show)
//
// Module: two-step, whose throbber always loops in 2.0 s; the script module when a
// frames animation loops in any other time (notes/spikes-2026-11-06.md). Frames are
// drawn at their own size, shrunk to fit their box if larger.

// Boot splash themes are built for this screen. ponytail: this machine's mode; the
// probe reads the primary screen's with M3 (SPEC.md §4.5).
inline constexpr QSize builtScreen{2560, 1440};
// What Plymouth may hold in memory before it shows: every frame, decoded (SPEC.md §4.6).
inline constexpr qint64 plymouthMemoryBudget = qint64(300) << 20;

// What Plymouth will draw, for the editor's preview and meters.
struct PlymouthLayout {
    QString module; // "two-step" or "script"
    QStringList frameFiles; // in playing order
    QSize frameSize; // pixels, of the largest frame
    double seconds = 2.0; // one loop
    QPointF centre; // of the animation, fractions of the screen
    std::optional<QRect> progress; // pixels on the built screen
    QColor progressColour;
    QColor progressBackground;
    bool hideMessages = false;
    qint64 bytes = 0; // all files: counts against the 15 MB cap
    qint64 decodedBytes = 0; // background and frames as Plymouth holds them in memory
    qint64 frameBytes = 0; // the animation's share of bytes
    qint64 frameDecodedBytes = 0; // and of decodedBytes
};

// Every problem found goes to problems; the files are only usable without any.
QList<PayloadFile> buildPlymouthSplash(const Design &design, const QString &themeId, const Library &library, QSize screen, QStringList &problems,
                                       PlymouthLayout *layout = nullptr);
