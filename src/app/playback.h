#pragma once

#include "genplymouth.h"
#include "library.h"
#include "model.h"
#include "probe.h"

#include <QVariantList>
#include <QVariantMap>

// "Play the Boot" (SPEC.md §4.13): the boot menu, boot splash and Plasma splash as
// Ekpo previews them, in boot order, for a Mask (its "leave as is" stages and those
// it doesn't set show what is applied now) or, with no slots, the look applied now.
// The built files go to outDir. Each step: {stage, label, kind, seconds} (the first
// also width and height: the screen the boot menu is drawn for) and
//   kind "image":    source (the boot menu, drawn as GRUB draws it)
//   kind "plymouth": layout (for PlymouthPreview.qml)
//   kind "qml":      source (a Plasma splash's Splash.qml)
//   kind "none":     nothing Ekpo can draw (GRUB's text menu, a boot splash it didn't
//                    make), and a note that says so
QVariantList playbackSteps(const Library &library, const SystemState &state, const QString &rootDir, const QString &homeDir,
                           const QMap<Stage, Slot> &maskSlots, const QString &outDir);

// What PlymouthPreview.qml shows of a boot splash built into dir.
QVariantMap plymouthPreviewMap(const PlymouthLayout &layout, const QString &dir);
