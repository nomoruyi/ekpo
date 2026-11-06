#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QList>
#include <QString>

#include <optional>

// Files the app sends to the root helper on stdin (ARCHITECTURE.md §2). Wire format:
//   "EKPO-PAYLOAD 1\n", then per file "F <size> <path>\n" followed by <size> bytes,
//   then "E\n".
// There are no modes, owners, links or folders: the helper writes regular 0644
// files only, and folders follow from the paths.
struct PayloadFile {
    QString path; // relative, see validRelativePath()
    QByteArray data;
};

struct PayloadLimits {
    qint64 maxTotalBytes = 80 << 20; // a GRUB theme (64 MB) plus a Plymouth theme (15 MB)
    int maxFiles = 8192;
};

// 1–5 components of letters, digits, spaces and ._@+- ; no component starts with
// '.' or a space, so ".", ".." and hidden files are impossible.
bool validRelativePath(const QString &path);

bool writePayload(QIODevice &out, const QList<PayloadFile> &files);
// Reads and checks a whole payload; nothing is returned unless all of it is valid.
std::optional<QList<PayloadFile>> readPayload(QIODevice &in, QString &error, const PayloadLimits &limits = {});
