#pragma once

#include <QImage>
#include <QString>
#include <QStringList>
#include <QUrl>

// Bug reports from inside the app: a pre-filled GitHub issue the user submits in the
// browser (no token), or, in sandbox builds, a Markdown file in the repo's bug-reports/.

// A new-issue link for nomoruyi/ekpo with title and body filled in.
QUrl issueUrl(const QString &title, const QString &body);
// GitHub refuses longer links, so a report this long must be shortened first.
constexpr int maxIssueUrlLength = 8000;
// Opens that link in the browser, where the user reads and submits it. Empty on
// success, else why not.
QString openIssue(const QString &title, const QString &body);

// The repo's bug-reports/ folder in sandbox builds; empty in packages.
QString localReportsDir();
// Writes <dir>/<time>-<title>.md (and .png next to it when the screenshot isn't
// null); returns the .md path, or an empty string and error.
QString saveReport(const QString &dir, const QString &title, const QString &body, const QImage &screenshot, QString &error);

// The last Qt messages of this run (kept from installMessageLog() on), oldest first.
void installMessageLog();
QStringList recentMessages();
// The helper's last log lines: the journal (tag ekpo-helper) on the real system, the
// test helper's log file under a fake root.
QStringList helperLog(const QString &rootDir, int lines);
