#include "request.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cmath>

namespace {

bool onlyKeys(const QJsonObject &o, const QSet<QString> &allowed, const QString &where, QString &error)
{
    for (const QString &key : o.keys()) {
        if (!allowed.contains(key)) {
            error = QStringLiteral("request: unknown key \"%1\" in %2").arg(key, where);
            return false;
        }
    }
    return true;
}

// Strings must be strings: a number or null where text is expected is an error,
// not an empty value.
bool readString(const QJsonObject &o, const QString &key, QString &out, QString &error)
{
    const QJsonValue v = o.value(key);
    if (v.isUndefined())
        return true;
    if (!v.isString()) {
        error = QStringLiteral("request: \"%1\" must be a string").arg(key);
        return false;
    }
    out = v.toString();
    return true;
}

QString targetKey(ThemeTarget target)
{
    return target == ThemeTarget::Grub ? QStringLiteral("grub") : QStringLiteral("plymouth");
}

// A theme reference is either a payload theme of the right target or an installed id.
bool validThemeRef(const QString &ref, ThemeTarget target, const QList<Request::Theme> &themes)
{
    for (const Request::Theme &t : themes) {
        if (t.id == ref)
            return t.target == target;
    }
    return validInstalledThemeId(ref) && !validPayloadThemeId(ref);
}

} // namespace

std::optional<Request> parseRequest(const QByteArray &json, QString &error)
{
    if (json.size() > maxRequestBytes) {
        error = QStringLiteral("request: too large");
        return std::nullopt;
    }
    // Keeps the first, most specific error.
    const auto fail = [&error](const QString &message) {
        if (error.isEmpty())
            error = message;
        return std::nullopt;
    };
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject()) {
        error = QStringLiteral("request: not a JSON object (%1)").arg(parseError.errorString());
        return std::nullopt;
    }
    const QJsonObject o = doc.object();
    if (!onlyKeys(o, {QStringLiteral("format"), QStringLiteral("operation"), QStringLiteral("themes"), QStringLiteral("grub"), QStringLiteral("plymouth"), QStringLiteral("keep")},
                  QStringLiteral("request"), error))
        return std::nullopt;
    if (o.value(QStringLiteral("format")) != QJsonValue(1)) {
        error = QStringLiteral("request: unsupported format");
        return std::nullopt;
    }

    Request r;
    QString op;
    if (!readString(o, QStringLiteral("operation"), op, error))
        return std::nullopt;
    if (op == QLatin1String("apply"))
        r.operation = Request::Operation::Apply;
    else if (op == QLatin1String("recover"))
        r.operation = Request::Operation::Recover;
    else if (op == QLatin1String("uninstall"))
        r.operation = Request::Operation::Uninstall;
    else {
        error = QStringLiteral("request: unknown operation \"%1\"").arg(op);
        return std::nullopt;
    }

    const bool hasChanges = o.contains(QStringLiteral("themes")) || o.contains(QStringLiteral("grub")) || o.contains(QStringLiteral("plymouth")) || o.contains(QStringLiteral("keep"));
    if (r.operation != Request::Operation::Apply) {
        if (hasChanges) {
            error = QStringLiteral("request: %1 takes no themes or changes").arg(op);
            return std::nullopt;
        }
        return r;
    }

    if (o.contains(QStringLiteral("themes")) && !o.value(QStringLiteral("themes")).isArray()) {
        error = QStringLiteral("request: \"themes\" must be a list");
        return std::nullopt;
    }
    QSet<QString> ids;
    for (const QJsonValue &v : o.value(QStringLiteral("themes")).toArray()) {
        const QJsonObject t = v.toObject();
        Request::Theme theme;
        QString target;
        if (!v.isObject() || !onlyKeys(t, {QStringLiteral("id"), QStringLiteral("target")}, QStringLiteral("themes"), error)
            || !readString(t, QStringLiteral("id"), theme.id, error) || !readString(t, QStringLiteral("target"), target, error))
            return fail(QStringLiteral("request: themes must be objects"));
        if (!validPayloadThemeId(theme.id) || ids.contains(theme.id)) {
            error = QStringLiteral("request: invalid or repeated theme id \"%1\"").arg(theme.id);
            return std::nullopt;
        }
        if (target != QLatin1String("grub") && target != QLatin1String("plymouth")) {
            error = QStringLiteral("request: unknown theme target \"%1\"").arg(target);
            return std::nullopt;
        }
        theme.target = target == QLatin1String("grub") ? ThemeTarget::Grub : ThemeTarget::Plymouth;
        ids.insert(theme.id);
        r.themes.append(theme);
    }

    if (o.contains(QStringLiteral("grub"))) {
        const QJsonObject g = o.value(QStringLiteral("grub")).toObject();
        if (!o.value(QStringLiteral("grub")).isObject()
            || !onlyKeys(g, {QStringLiteral("original"), QStringLiteral("theme"), QStringLiteral("gfxmode"), QStringLiteral("timeout"), QStringLiteral("timeoutStyle")}, QStringLiteral("grub"), error))
            return fail(QStringLiteral("request: \"grub\" must be an object"));
        Request::Grub grub;
        if (g.contains(QStringLiteral("original"))) {
            if (g.value(QStringLiteral("original")) != QJsonValue(true) || g.size() != 1) {
                error = QStringLiteral("request: grub.original must be true and stand alone");
                return std::nullopt;
            }
            grub.original = true;
        } else {
            if (!readString(g, QStringLiteral("theme"), grub.theme, error) || !readString(g, QStringLiteral("gfxmode"), grub.gfxmode, error)
                || !readString(g, QStringLiteral("timeoutStyle"), grub.timeoutStyle, error))
                return std::nullopt;
            if (!grub.theme.isEmpty() && !validThemeRef(grub.theme, ThemeTarget::Grub, r.themes)) {
                error = QStringLiteral("request: invalid GRUB theme \"%1\"").arg(grub.theme);
                return std::nullopt;
            }
            if (!grub.gfxmode.isEmpty() && !validGfxmode(grub.gfxmode)) {
                error = QStringLiteral("request: invalid gfxmode \"%1\"").arg(grub.gfxmode);
                return std::nullopt;
            }
            if (!grub.timeoutStyle.isEmpty() && !validTimeoutStyle(grub.timeoutStyle)) {
                error = QStringLiteral("request: invalid timeout style \"%1\"").arg(grub.timeoutStyle);
                return std::nullopt;
            }
            if (g.contains(QStringLiteral("timeout"))) {
                const QJsonValue t = g.value(QStringLiteral("timeout"));
                const double seconds = t.toDouble(-1);
                if (!t.isDouble() || seconds != std::floor(seconds) || seconds < 0 || seconds > 600) {
                    error = QStringLiteral("request: timeout must be a whole number of seconds from 0 to 600");
                    return std::nullopt;
                }
                grub.timeout = int(seconds);
            }
        }
        r.grub = grub;
    }

    if (o.contains(QStringLiteral("plymouth"))) {
        const QJsonObject p = o.value(QStringLiteral("plymouth")).toObject();
        Request::Plymouth plymouth;
        if (!o.value(QStringLiteral("plymouth")).isObject()
            || !onlyKeys(p, {QStringLiteral("alternativeMode"), QStringLiteral("theme"), QStringLiteral("daemonTheme"), QStringLiteral("initrds")}, QStringLiteral("plymouth"), error)
            || !readString(p, QStringLiteral("alternativeMode"), plymouth.alternativeMode, error) || !readString(p, QStringLiteral("theme"), plymouth.theme, error)
            || !readString(p, QStringLiteral("daemonTheme"), plymouth.daemonTheme, error))
            return fail(QStringLiteral("request: \"plymouth\" must be an object"));
        const bool manual = plymouth.alternativeMode == QLatin1String("manual");
        if (!manual && plymouth.alternativeMode != QLatin1String("auto")) {
            error = QStringLiteral("request: alternativeMode must be manual or auto");
            return std::nullopt;
        }
        if (manual ? !validThemeRef(plymouth.theme, ThemeTarget::Plymouth, r.themes) : !plymouth.theme.isEmpty()) {
            error = QStringLiteral("request: manual mode needs a valid theme, auto mode none");
            return std::nullopt;
        }
        QString initrds = QStringLiteral("all");
        if (!readString(p, QStringLiteral("initrds"), initrds, error))
            return std::nullopt;
        if (initrds != QLatin1String("all") && initrds != QLatin1String("newest")) {
            error = QStringLiteral("request: initrds must be all or newest");
            return std::nullopt;
        }
        plymouth.allKernels = initrds == QLatin1String("all");
        if (!validThemeRef(plymouth.daemonTheme, ThemeTarget::Plymouth, r.themes)) {
            error = QStringLiteral("request: invalid daemonTheme \"%1\"").arg(plymouth.daemonTheme);
            return std::nullopt;
        }
        r.plymouth = plymouth;
    }

    if (!r.grub && !r.plymouth) {
        error = QStringLiteral("request: apply needs a grub or plymouth change");
        return std::nullopt;
    }
    if (o.contains(QStringLiteral("keep"))) {
        const QJsonValue keep = o.value(QStringLiteral("keep"));
        QStringList ids;
        for (const QJsonValue &v : keep.toArray()) {
            if (!v.isString() || !validPayloadThemeId(v.toString())) {
                error = QStringLiteral("request: keep holds an invalid theme id");
                return std::nullopt;
            }
            ids << v.toString();
        }
        if (!keep.isArray() || ids.size() > 512) {
            error = QStringLiteral("request: keep must be a list of at most 512 theme ids");
            return std::nullopt;
        }
        r.keep = ids;
    }
    for (const Request::Theme &t : std::as_const(r.themes)) {
        const bool used = (r.grub && r.grub->theme == t.id) || (r.plymouth && (r.plymouth->theme == t.id || r.plymouth->daemonTheme == t.id));
        if (!used) {
            error = QStringLiteral("request: theme \"%1\" is sent but not used").arg(t.id);
            return std::nullopt;
        }
    }
    return r;
}

QByteArray serializeRequest(const Request &r)
{
    static const QMap<Request::Operation, QString> ops = {
        {Request::Operation::Apply, QStringLiteral("apply")},
        {Request::Operation::Recover, QStringLiteral("recover")},
        {Request::Operation::Uninstall, QStringLiteral("uninstall")},
    };
    QJsonObject o{{QStringLiteral("format"), 1}, {QStringLiteral("operation"), ops.value(r.operation)}};
    if (!r.themes.isEmpty()) {
        QJsonArray themes;
        for (const Request::Theme &t : r.themes)
            themes.append(QJsonObject{{QStringLiteral("id"), t.id}, {QStringLiteral("target"), targetKey(t.target)}});
        o.insert(QStringLiteral("themes"), themes);
    }
    if (r.keep)
        o.insert(QStringLiteral("keep"), QJsonArray::fromStringList(*r.keep));
    if (r.grub) {
        QJsonObject g;
        if (r.grub->original) {
            g.insert(QStringLiteral("original"), true);
        } else {
            if (!r.grub->theme.isEmpty())
                g.insert(QStringLiteral("theme"), r.grub->theme);
            if (!r.grub->gfxmode.isEmpty())
                g.insert(QStringLiteral("gfxmode"), r.grub->gfxmode);
            if (r.grub->timeout)
                g.insert(QStringLiteral("timeout"), *r.grub->timeout);
            if (!r.grub->timeoutStyle.isEmpty())
                g.insert(QStringLiteral("timeoutStyle"), r.grub->timeoutStyle);
        }
        o.insert(QStringLiteral("grub"), g);
    }
    if (r.plymouth) {
        QJsonObject p{{QStringLiteral("alternativeMode"), r.plymouth->alternativeMode}, {QStringLiteral("daemonTheme"), r.plymouth->daemonTheme}};
        if (!r.plymouth->theme.isEmpty())
            p.insert(QStringLiteral("theme"), r.plymouth->theme);
        if (!r.plymouth->allKernels)
            p.insert(QStringLiteral("initrds"), QStringLiteral("newest"));
        o.insert(QStringLiteral("plymouth"), p);
    }
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

std::optional<QMap<QString, QList<PayloadFile>>> themesFromPayload(const Request &request, const QList<PayloadFile> &files, QStringList &errors)
{
    QMap<QString, QList<PayloadFile>> themes;
    QMap<QString, ThemeTarget> targets;
    for (const Request::Theme &t : request.themes)
        targets.insert(t.id, t.target);

    for (const PayloadFile &f : files) {
        const qsizetype slash = f.path.indexOf(QLatin1Char('/'));
        const QString id = f.path.left(slash);
        if (slash <= 0 || !targets.contains(id)) {
            errors << QStringLiteral("payload: \"%1\" belongs to no requested theme").arg(f.path);
            continue;
        }
        themes[id].append({f.path.mid(slash + 1), f.data});
    }
    for (auto it = targets.cbegin(); it != targets.cend(); ++it) {
        if (!themes.contains(it.key()))
            errors << QStringLiteral("payload: no files for theme \"%1\"").arg(it.key());
        else
            errors << validateTheme(it.value(), it.key(), themes.value(it.key()));
    }
    if (!errors.isEmpty())
        return std::nullopt;
    return themes;
}
