#include "payload.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

namespace {

const QByteArray magic = QByteArrayLiteral("EKPO-PAYLOAD 1\n");

// Reads one '\n'-terminated line of at most maxLength bytes; anything else fails.
std::optional<QByteArray> readLine(QIODevice &in, int maxLength)
{
    QByteArray line;
    while (line.size() <= maxLength) {
        char c;
        if (!in.getChar(&c) && !(in.waitForReadyRead(-1) && in.getChar(&c)))
            return std::nullopt;
        line.append(c);
        if (c == '\n')
            return line;
    }
    return std::nullopt;
}

bool readExactly(QIODevice &in, qint64 size, QByteArray &data)
{
    data.clear();
    data.reserve(size);
    while (data.size() < size) {
        const QByteArray chunk = in.read(size - data.size());
        if (chunk.isEmpty() && !in.waitForReadyRead(-1))
            return false;
        data.append(chunk);
    }
    return true;
}

} // namespace

bool validRelativePath(const QString &path)
{
    static const QRegularExpression component(QStringLiteral("^[A-Za-z0-9_@+-](?:[A-Za-z0-9._@+ -]{0,98}[A-Za-z0-9._@+-])?$"));
    if (path.isEmpty() || path.size() > 200)
        return false;
    const QStringList parts = path.split(QLatin1Char('/'));
    if (parts.size() > 5)
        return false;
    return std::all_of(parts.cbegin(), parts.cend(), [](const QString &part) { return component.match(part).hasMatch(); });
}

bool writePayload(QIODevice &out, const QList<PayloadFile> &files)
{
    if (out.write(magic) != magic.size())
        return false;
    for (const PayloadFile &f : files) {
        const QByteArray header = "F " + QByteArray::number(f.data.size()) + ' ' + f.path.toUtf8() + '\n';
        if (out.write(header) != header.size() || out.write(f.data) != f.data.size())
            return false;
    }
    return out.write("E\n", 2) == 2;
}

std::optional<QList<PayloadFile>> readPayload(QIODevice &in, QString &error, const PayloadLimits &limits)
{
    if (readLine(in, magic.size()) != magic) {
        error = QStringLiteral("payload: bad header");
        return std::nullopt;
    }

    static const QRegularExpression fileHeader(QStringLiteral("^F (0|[1-9][0-9]{0,10}) (.+)\n$"));
    QList<PayloadFile> files;
    QSet<QString> paths;
    QSet<QString> folders;
    qint64 total = 0;
    while (true) {
        const auto line = readLine(in, 256);
        if (!line) {
            error = QStringLiteral("payload: truncated or overlong record header");
            return std::nullopt;
        }
        if (*line == "E\n")
            break;

        QStringDecoder utf8(QStringDecoder::Utf8);
        const QString text = utf8(*line);
        const auto match = fileHeader.match(text);
        if (utf8.hasError() || !match.hasMatch()) {
            error = QStringLiteral("payload: malformed record header");
            return std::nullopt;
        }
        const qint64 size = match.captured(1).toLongLong();
        const QString path = match.captured(2);
        if (!validRelativePath(path)) {
            error = QStringLiteral("payload: invalid path \"%1\"").arg(path);
            return std::nullopt;
        }
        // A path must not repeat, nor be both a file and a folder.
        const QStringList parts = path.split(QLatin1Char('/'));
        QString prefix;
        for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
            prefix += (i ? QStringLiteral("/") : QString()) + parts[i];
            if (paths.contains(prefix)) {
                error = QStringLiteral("payload: \"%1\" is both a file and a folder").arg(prefix);
                return std::nullopt;
            }
            folders.insert(prefix);
        }
        if (paths.contains(path) || folders.contains(path)) {
            error = QStringLiteral("payload: \"%1\" appears twice").arg(path);
            return std::nullopt;
        }
        if (files.size() >= limits.maxFiles || size > limits.maxTotalBytes - total) {
            error = QStringLiteral("payload: more than %1 files or %2 bytes").arg(limits.maxFiles).arg(limits.maxTotalBytes);
            return std::nullopt;
        }

        PayloadFile f{path, {}};
        if (!readExactly(in, size, f.data)) {
            error = QStringLiteral("payload: \"%1\" is truncated").arg(path);
            return std::nullopt;
        }
        total += size;
        paths.insert(path);
        files.append(f);
    }

    char extra;
    if (in.getChar(&extra) || (in.waitForReadyRead(0) && in.getChar(&extra))) {
        error = QStringLiteral("payload: data after the end marker");
        return std::nullopt;
    }
    return files;
}
