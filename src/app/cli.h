#pragma once

#include <QStringList>

// The command line (SPEC.md §4.15): ekpo status | apply <mask> | restore <entry|original>
// | recover | help. Applies show the same review as the app and ask y/N on stdin.
// Needs a QCoreApplication. Returns the process exit code.
int runCli(const QStringList &args);
