#pragma once

#include "library.h"
#include "payload.h"

#include <QSize>
#include <QString>
#include <QStringList>

// Exports (SPEC.md §4.13, ARCHITECTURE.md §9d): plain packages that work without Ekpo,
// in .tar.gz archives, and the whole library as one archive.

// Something in an export that isn't the owner's own work, for the licence check.
struct ExportItem {
    QString kind; // font, image, video, theme (an imported one)
    QString name;
    QString source; // where it came from
    QString licence; // the licence's name if Ekpo recognises it, else empty
    bool free = false; // a licence that allows sharing
};

// What one export writes: archive paths below a single top folder (the folder name),
// so the Plasma archive installs as it is (System Settings, kpackagetool6).
struct ExportPlan {
    QString folder; // e.g. ekpo-dawn-v2
    QList<PayloadFile> files; // folder/…
    QList<ExportItem> thirdParty;
    QStringList problems;
};

// A design (its newest version) as its theme package with README.md and LICENSE. Boot
// menus and boot splashes are built for screen; boot menus fit entries.
ExportPlan exportDesign(const Library &library, const QString &designId, QSize screen, const QStringList &entries);
// A Mask: a folder per stage with its design built or its imported theme as imported,
// and one README.md.
ExportPlan exportMask(const Library &library, const QString &maskId, QSize screen, const QStringList &entries);

// Writes the files as a .tar.gz (files owned by root, sorted). Empty on success.
QString writeArchive(const QList<PayloadFile> &files, const QString &archive);
// The whole library folder as a .tar.gz, its folder name on top. Empty on success.
QString exportLibrary(const Library &library, const QString &archive);

// The licence a text is ("SIL Open Font License 1.1", "Apache License 2.0", …), and
// whether it allows sharing; empty for one Ekpo doesn't recognise.
QString licenceName(const QByteArray &text, bool *free = nullptr);
