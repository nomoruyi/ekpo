#pragma once

#include "model.h"
#include "probe.h"

#include <QJsonObject>

#include <optional>

// A desired state per stage. Applies, history entries and Original are all
// expressed as Targets, so restoring anything means applying a Target again.
struct BootMenuTarget {
    bool original = false; // no Ekpo drop-in: /etc/default/grub decides
    QString theme; // folder in /boot/grub/themes
    QString gfxmode;
    std::optional<int> timeout;
    QString timeoutStyle;
    QString build; // library build holding the theme files, when Ekpo installs a copy
    QString label; // what the review and history show

    bool sameState(const BootMenuTarget &o) const
    {
        return original == o.original && (original || (theme == o.theme && gfxmode == o.gfxmode && timeout == o.timeout && timeoutStyle == o.timeoutStyle));
    }
};

struct BootSplashTarget {
    QString mode = QStringLiteral("manual"); // default.plymouth: manual or auto
    QString theme; // folder in /usr/share/plymouth/themes; empty in auto mode
    QString daemonTheme; // plymouthd.conf Theme=
    QString build;
    QString label;
    bool allKernels = true; // how it was applied: every initrd, or only the newest kernel's
    QString design; // the design it was built from, if any
    QString stamp; // Library::stamp of that design as built: another one means "update available"

    bool sameState(const BootSplashTarget &o) const { return mode == o.mode && theme == o.theme && daemonTheme == o.daemonTheme; }
};

struct PlasmaSplashTarget {
    QString theme; // ksplashrc Theme= (package id)
    QString engine; // ksplashrc Engine=; empty = Plasma's default
    QString build; // package files Ekpo copies into ~/.local/share/plasma/look-and-feel/<theme>
    QString label;
    QString design; // as for BootSplashTarget
    QString stamp;

    bool sameState(const PlasmaSplashTarget &o) const { return theme == o.theme && engine == o.engine; }
};

struct Target {
    std::optional<BootMenuTarget> bootMenu;
    std::optional<BootSplashTarget> bootSplash;
    std::optional<PlasmaSplashTarget> plasmaSplash;

    bool isEmpty() const { return !bootMenu && !bootSplash && !plasmaSplash; }
    QJsonObject toJson() const;
    static Target fromJson(const QJsonObject &json);
    // One stage as JSON, for Original (Library::saveOriginalIfMissing).
    QJsonObject stageJson(Stage stage) const;
};

// The current state of every stage as a Target. rootDir is "/" outside tests.
Target currentTarget(const SystemState &state, const QString &rootDir);
