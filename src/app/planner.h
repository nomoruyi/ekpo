#pragma once

#include "library.h"
#include "request.h"
#include "target.h"

// What an apply would do, worked out before anything happens (SPEC.md §4.9). The
// review screen shows it; the executor carries it out.
struct FileDiff {
    QString path;
    QString before; // empty: the file doesn't exist
    QString after; // empty: the file will be deleted
};

struct PlannedStage {
    Stage stage = Stage::BootMenu;
    QString from;
    QString to;
    QStringList actions;
    QList<FileDiff> diffs;
};

struct Plan {
    QString title;
    Target target; // the changed stages' new state
    Target before; // the current state of every stage
    QList<PlannedStage> stages; // only stages that change, in boot order
    std::optional<Request> request; // the helper's part (boot stages)
    QList<PayloadFile> payload; // files for the helper, as <theme id>/<path>
    QMap<QString, QList<PayloadFile>> builds; // new builds to store, by hash
    QStringList commands; // what runs, for the technical details
    QStringList notes;
    QStringList problems; // any problem blocks the apply
    int estimatedSeconds = 0;
    // For the review's "all kernels" switch; set when the boot splash changes.
    bool touchesSplash = false;
    bool allKernels = true;
    QString newestKernel;
    int kernelCount = 0;

    bool canApply() const { return !stages.isEmpty() && problems.isEmpty(); }
};

struct PlanContext {
    const Library &library;
    const SystemState &state;
    QString rootDir = QStringLiteral("/");
    QString homeDir;
    // Boot splash changes: rebuild every kernel's initrd, or only the newest one's.
    // Applies default to the setting, restores to all (INTERVIEW.md, 2026-11-06).
    bool allKernels = true;
};

inline const QString defaultGfxmode = QStringLiteral("2560x1440,1920x1080,1280x720,auto");

Plan planMask(const PlanContext &context, const Mask &mask);
// Restores and Masks both end here. builds holds files of builds not yet in the library.
Plan planTarget(const PlanContext &context, const Target &target, const QString &title, const QMap<QString, QList<PayloadFile>> &builds = {});
// The Target that restores Original, for stages Ekpo has changed.
Target originalTarget(const Library &library);
// Per stage, the state the newest history entry touching it left behind.
Target lastApplied(const Library &library);
// Stages whose current state differs from what Ekpo last applied (SPEC.md §4.12).
QList<Stage> driftedStages(const Library &library, const Target &current);
