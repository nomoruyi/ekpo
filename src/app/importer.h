#pragma once

#include "library.h"
#include "probe.h"

#include <utility>

struct ImportResult {
    QList<ImportedTheme> themes; // saved to the library, new or updated
    QStringList errors;
};

// Finds every theme under sourcePath (a folder or an archive, up to three folders
// deep), checks it, and saves a verbatim copy plus a report to the library (SPEC.md
// §4.3). A theme imported before from the same source is updated (Re-import).
// Bundled scripts are never run. rootDir and homeDir locate installed themes and
// Plymouth modules ("/" and the real home outside tests).
ImportResult importThemes(Library &library, const QString &sourcePath, const QString &rootDir, const QString &homeDir);

// Theme folders installed on this system that can be imported, including Plymouth
// themes installed one folder too deep.
QList<std::pair<Stage, QString>> installedThemeFolders(const SystemState &state, const QString &rootDir, const QString &homeDir);
