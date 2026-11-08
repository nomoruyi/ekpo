#pragma once

#include "payload.h"
#include "validate.h"

#include <QMap>

#include <optional>

// What the app asks the root helper to do (ARCHITECTURE.md §2). Sent on stdin as
// one JSON line, followed by the payload. Every field is checked on parsing;
// unknown keys are errors.
struct Request {
    enum class Operation { Apply, Recover, Uninstall, Prune };

    struct Theme {
        QString id; // payload files are sent as <id>/<path>
        ThemeTarget target = ThemeTarget::Grub;
    };
    struct Grub {
        bool original = false; // remove Ekpo's drop-in; nothing else may be set
        QString theme; // installed or payload id; empty = no GRUB_THEME line
        QString gfxmode; // empty = not set
        std::optional<int> timeout; // seconds, 0–600
        QString timeoutStyle; // empty = not set
    };
    struct Plymouth {
        QString alternativeMode; // "manual" selects theme; "auto" lets priorities decide
        QString theme; // installed or payload id; required for manual, empty for auto
        QString daemonTheme; // plymouthd.conf [Daemon] Theme=
        bool allKernels = true; // rebuild every initrd, or only the newest kernel's ("initrds": "newest")
    };

    Operation operation = Operation::Apply;
    QList<Theme> themes;
    // Apply and prune: Ekpo's theme folders (ekpo-*) still needed by history. After a
    // successful apply, or for prune alone, the helper removes the others that aren't
    // in use. Absent (apply): remove nothing.
    std::optional<QStringList> keep;
    std::optional<Grub> grub;
    std::optional<Plymouth> plymouth;
};

inline constexpr int maxRequestBytes = 64 * 1024;

std::optional<Request> parseRequest(const QByteArray &json, QString &error);
QByteArray serializeRequest(const Request &request); // one line, no trailing newline

// Splits the payload into the request's themes (stripping the <id>/ prefix) and
// validates each theme. Fails on files of unlisted themes and on listed themes
// without files.
std::optional<QMap<QString, QList<PayloadFile>>> themesFromPayload(const Request &request, const QList<PayloadFile> &files, QStringList &errors);
