#include "gencommon.h"

#include <QFontDatabase>
#include <QHash>
#include <QJsonValue>
#include <QRegularExpression>

#include <cmath>

std::optional<PercentBox> readBox(const QJsonObject &element, int number, QStringList &problems)
{
    double values[4] = {};
    bool valid = true;
    int i = 0;
    for (const char *key : {"x", "y", "width", "height"}) {
        const QJsonValue v = element.value(QLatin1String(key));
        if (!v.isDouble() || !std::isfinite(v.toDouble()) || (i >= 2 && v.toDouble() < 0)) {
            problems << QStringLiteral("Element %1 has no valid %2.").arg(number).arg(QLatin1String(key));
            valid = false;
        }
        values[i++] = v.toDouble();
    }
    if (!valid)
        return std::nullopt;
    return PercentBox{values[0], values[1], values[2], values[3]};
}

QString readColour(const QJsonObject &o, const QString &key, const QString &fallback, QStringList &problems)
{
    static const QRegularExpression re(QStringLiteral("^#(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
    const QString value = o.value(key).toString(fallback);
    if (re.match(value).hasMatch())
        return value.toLower();
    problems << QStringLiteral("%1 is not a colour.").arg(value);
    return fallback;
}

QString registerFont(const QString &file)
{
    static QHash<QString, QString> families;
    if (const auto known = families.constFind(file); known != families.cend())
        return *known;
    const int id = QFontDatabase::addApplicationFont(file);
    const QString family = id < 0 ? QString() : QFontDatabase::applicationFontFamilies(id).value(0);
    families.insert(file, family);
    return family;
}
