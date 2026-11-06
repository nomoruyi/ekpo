#pragma once

#include "request.h"

#include <QJsonObject>

#include <functional>

// Where and how the helper works. The production build fixes root "" (the real
// system) and the system command paths; only the test build fills these from the
// environment (ARCHITECTURE.md §8).
struct HelperEnv {
    QString root; // "" for the real system, a fake root in tests
    QString binDir; // empty: the system's commands; tests: a folder of stubs
    std::function<void(const QString &)> progress; // one line per step, for the app
    std::function<void(const QString &)> log; // every privileged action
};

enum class Outcome {
    Done = 0,
    RolledBack = 1, // a step failed; everything was put back
    Rejected = 2, // refused before any change
    NeedsRecovery = 3, // rollback failed too; the journal stays for "recover"
};

struct HelperResult {
    Outcome outcome = Outcome::Done;
    QString error;
    QJsonObject details; // e.g. the GRUB menu entries after an apply
};

// Runs a parsed and validated request (ARCHITECTURE.md §3): takes the dpkg lock,
// journals the pre-state, installs, configures, rebuilds, verifies, and undoes
// everything if a step fails.
HelperResult runRequest(const HelperEnv &env, const Request &request, const QMap<QString, QList<PayloadFile>> &themes);
