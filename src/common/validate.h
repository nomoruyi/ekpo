#pragma once

#include "payload.h"

#include <QStringList>

// Checks the root helper runs on everything it receives, before its first write
// (ARCHITECTURE.md §2). Each function returns the problems found; empty means valid.

enum class ThemeTarget { Grub, Plymouth };

inline constexpr qint64 plymouthThemeCap = 15 << 20; // copied into every initrd (SPEC.md §4.6)

// Folder name of a theme Ekpo installs: ekpo-<slug>-v<n>.
bool validPayloadThemeId(const QString &id);
// Folder name of a theme already installed (e.g. fallout, hollow-grub).
bool validInstalledThemeId(const QString &id);
// GRUB_GFXMODE: "auto" or WxH[xD] modes separated by commas, optionally ending in ",auto".
bool validGfxmode(const QString &mode);
bool validTimeoutStyle(const QString &style); // menu, countdown, hidden

// A file's type is decided by its extension and must match its content: image
// headers, the .pf2 signature, UTF-8 text without NUL bytes.
QStringList checkFile(ThemeTarget target, const PayloadFile &file);

// theme.txt against GRUB 2.14's grammar and its known global properties and
// component types (unknown component properties are ignored by GRUB, so allowed).
// Image references must be relative paths inside the theme.
QStringList validateGrubThemeTxt(const QByteArray &text);

// The whole theme: size and file-count caps, allowed file types, theme.txt or
// <id>.plymouth with ImageDir/ScriptFile pointing into the theme's own folder.
// Paths in files are relative to the theme folder.
QStringList validateTheme(ThemeTarget target, const QString &id, const QList<PayloadFile> &files);
