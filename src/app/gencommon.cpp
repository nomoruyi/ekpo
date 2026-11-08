#include "gencommon.h"

#include <QFontDatabase>
#include <QPainter>
#include <QHash>
#include <QJsonValue>
#include <QRegularExpression>

#include <algorithm>
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

double gradientAngle(const QJsonObject &background)
{
    const double angle = std::fmod(background.value(QStringLiteral("angle")).toDouble(180), 360.0);
    return angle < 0 ? angle + 360 : angle;
}

QLineF gradientLine(const QSizeF &size, double degrees)
{
    const double r = degrees * M_PI / 180;
    const QPointF direction(std::sin(r), -std::cos(r)); // y points down
    const double half = (std::abs(size.width() * direction.x()) + std::abs(size.height() * direction.y())) / 2;
    const QPointF centre(size.width() / 2, size.height() / 2);
    return QLineF(centre - direction * half, centre + direction * half);
}

namespace {
double luminance(const QColor &c)
{
    const auto channel = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}
double contrast(const QColor &a, const QColor &b) // WCAG's ratio, 1–21
{
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
} // namespace

QMap<QString, QString> paletteFromImage(const QImage &source)
{
    struct Cluster {
        double r = 0, g = 0, b = 0;
        int count = 0;
        QColor colour() const { return QColor::fromRgbF(float(r / count), float(g / count), float(b / count)); }
        void add(double r2, double g2, double b2, int n) { r += r2; g += g2; b += b2; count += n; }
    };
    // Coarse bins (16 levels a channel), then bins of nearly the same colour merged.
    const QImage image = source.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
    QHash<int, Cluster> bins;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb p = image.pixel(x, y);
            if (qAlpha(p) < 128)
                continue;
            bins[(qRed(p) >> 4) << 8 | (qGreen(p) >> 4) << 4 | (qBlue(p) >> 4)].add(qRed(p) / 255.0, qGreen(p) / 255.0, qBlue(p) / 255.0, 1);
        }
    }
    QList<Cluster> sorted = bins.values();
    std::sort(sorted.begin(), sorted.end(), [](const Cluster &a, const Cluster &b) { return a.count > b.count; });
    QList<Cluster> clusters;
    for (const Cluster &bin : std::as_const(sorted)) {
        const QColor c = bin.colour();
        const auto near = std::find_if(clusters.begin(), clusters.end(), [&](const Cluster &k) {
            const QColor d = k.colour();
            return std::hypot(c.red() - d.red(), c.green() - d.green(), c.blue() - d.blue()) < 40;
        });
        if (near == clusters.end())
            clusters << bin;
        else
            near->add(bin.r, bin.g, bin.b, bin.count);
    }
    std::sort(clusters.begin(), clusters.end(), [](const Cluster &a, const Cluster &b) { return a.count > b.count; });
    if (clusters.isEmpty())
        return {{QStringLiteral("background"), QStringLiteral("#000000")}, {QStringLiteral("gradient"), QStringLiteral("#000000")},
                {QStringLiteral("accent"), QStringLiteral("#3daee9")}, {QStringLiteral("accent2"), QStringLiteral("#3daee9")},
                {QStringLiteral("text"), QStringLiteral("#ffffff")}};
    int total = 0;
    for (const Cluster &k : std::as_const(clusters))
        total += k.count;

    const QColor background = clusters[0].colour();
    const QColor gradient = clusters.size() > 1 ? clusters[1].colour() : background.darker(150);
    // Vivid: saturated and bright, and more than a speck.
    QList<QColor> vivid;
    for (const Cluster &k : std::as_const(clusters))
        if (k.count * 100 >= total)
            vivid << k.colour();
    std::sort(vivid.begin(), vivid.end(), [](const QColor &a, const QColor &b) { return a.hsvSaturationF() * a.valueF() > b.hsvSaturationF() * b.valueF(); });
    const QColor accent = vivid.first();
    QColor accent2 = accent.lighter(130);
    for (const QColor &c : std::as_const(vivid)) {
        const int hueGap = std::abs(c.hsvHue() - accent.hsvHue());
        if (c.hsvSaturationF() > 0.25 && std::min(hueGap, 360 - hueGap) > 30) {
            accent2 = c;
            break;
        }
    }
    // Text: a light (or dark) colour of the image if it reads well, else white or black.
    QColor text = contrast(Qt::white, background) >= contrast(Qt::black, background) ? QColor(Qt::white) : QColor(Qt::black);
    for (const Cluster &k : std::as_const(clusters)) {
        if (contrast(k.colour(), background) >= 7 && k.colour().hsvSaturationF() < 0.3) {
            text = k.colour();
            break;
        }
    }
    const auto hex = [](const QColor &c) { return c.name(QColor::HexRgb); };
    return {{QStringLiteral("background"), hex(background)}, {QStringLiteral("gradient"), hex(gradient)}, {QStringLiteral("accent"), hex(accent)},
            {QStringLiteral("accent2"), hex(accent2)}, {QStringLiteral("text"), hex(text)}};
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

const QStringList spinnerStyles = {QStringLiteral("dots"),  QStringLiteral("arc"),   QStringLiteral("pulse"), QStringLiteral("bars"),
                                   QStringLiteral("orbit"), QStringLiteral("comet"), QStringLiteral("image")};

double spinnerSeconds(const QString &speed)
{
    if (speed.isEmpty() || speed == QLatin1String("normal"))
        return 1;
    if (speed == QLatin1String("slow"))
        return 2;
    return speed == QLatin1String("fast") ? 2.0 / 3 : 0;
}

// Keep in step with spinnerScript below: the two draw the same.
void paintSpinner(QPainter &p, const QString &style, const QColor &colour, double s, double phase)
{
    const auto tint = [&](double alpha) {
        QColor c = colour;
        c.setAlphaF(colour.alphaF() * std::clamp(alpha, 0.0, 1.0));
        return c;
    };
    const auto dot = [&](double x, double y, double radius, double alpha) {
        p.setPen(Qt::NoPen);
        p.setBrush(tint(alpha));
        p.drawEllipse(QPointF(x, y), radius, radius);
    };
    const auto ring = [&](double radius, double width, double alpha) {
        p.setPen(QPen(tint(alpha), width));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(0, 0), radius, radius);
    };
    const double t = 2 * M_PI * phase;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(s / 2, s / 2);
    if (style == QLatin1String("arc")) {
        const double w = s * 0.1, r = s / 2 - w / 2;
        ring(r, w, 0.2);
        QPen pen(tint(1), w);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        // Qt counts degrees anticlockwise, the screen (and the Canvas) clockwise.
        p.drawArc(QRectF(-r, -r, 2 * r, 2 * r), int(std::lround(-t * 180 / M_PI * 16)), int(std::lround(-0.6 * 180 * 16)));
    } else if (style == QLatin1String("pulse")) {
        for (int k = 0; k < 2; ++k) {
            const double q = std::fmod(phase + k / 2.0, 1.0);
            ring(std::max(0.5, (s / 2 - s * 0.03) * q), s * 0.06, 1 - q);
        }
        dot(0, 0, s * 0.08, 1);
    } else if (style == QLatin1String("bars")) {
        const double w = s * 0.08;
        for (int i = 0; i < 12; ++i) {
            const double a = i * M_PI / 6 + t;
            QPen pen(tint((i + 1) / 12.0), w);
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            p.drawLine(QPointF(s * 0.22 * std::cos(a), s * 0.22 * std::sin(a)), QPointF((s / 2 - w / 2) * std::cos(a), (s / 2 - w / 2) * std::sin(a)));
        }
    } else if (style == QLatin1String("orbit")) {
        const double r = s * 0.38;
        ring(r, s * 0.03, 0.25);
        dot(r * std::cos(t), r * std::sin(t), s * 0.09, 1);
        dot(-r * std::cos(t), -r * std::sin(t), s * 0.05, 0.6);
    } else if (style == QLatin1String("comet")) {
        const double r = s * 0.4;
        for (int i = 15; i >= 0; --i) {
            const double a = t - i * M_PI / 18;
            dot(r * std::cos(a), r * std::sin(a), s * 0.1 * (1 - i / 16.0), 1 - i / 16.0);
        }
    } else { // dots
        const double d = s * 0.18;
        for (int i = 0; i < 8; ++i) {
            const double a = i * M_PI / 4 + t;
            dot((s - d) / 2 * std::cos(a), (s - d) / 2 * std::sin(a), d / 2, (i + 1) / 8.0);
        }
    }
    p.restore();
}

QString spinnerScript(const QString &style)
{
    QString draw;
    if (style == QLatin1String("arc"))
        draw = QStringLiteral("const w = s * 0.1, r = s / 2 - w / 2;\n"
                              "ctx.lineWidth = w;\n"
                              "ctx.strokeStyle = tinted(0.2);\n"
                              "ctx.beginPath();\n"
                              "ctx.arc(0, 0, r, 0, 2 * Math.PI);\n"
                              "ctx.stroke();\n"
                              "ctx.lineCap = \"round\";\n"
                              "ctx.strokeStyle = tinted(1);\n"
                              "ctx.beginPath();\n"
                              "ctx.arc(0, 0, r, t, t + 0.6 * Math.PI);\n"
                              "ctx.stroke();\n");
    else if (style == QLatin1String("pulse"))
        draw = QStringLiteral("ctx.lineWidth = s * 0.06;\n"
                              "for (let k = 0; k < 2; ++k) {\n"
                              "    const q = (phase + k / 2) % 1;\n"
                              "    ctx.strokeStyle = tinted(1 - q);\n"
                              "    ctx.beginPath();\n"
                              "    ctx.arc(0, 0, Math.max(0.5, (s / 2 - s * 0.03) * q), 0, 2 * Math.PI);\n"
                              "    ctx.stroke();\n"
                              "}\n"
                              "dot(0, 0, s * 0.08, 1);\n");
    else if (style == QLatin1String("bars"))
        draw = QStringLiteral("const w = s * 0.08;\n"
                              "ctx.lineWidth = w;\n"
                              "ctx.lineCap = \"round\";\n"
                              "for (let i = 0; i < 12; ++i) {\n"
                              "    const a = i * Math.PI / 6 + t;\n"
                              "    ctx.strokeStyle = tinted((i + 1) / 12);\n"
                              "    ctx.beginPath();\n"
                              "    ctx.moveTo(s * 0.22 * Math.cos(a), s * 0.22 * Math.sin(a));\n"
                              "    ctx.lineTo((s / 2 - w / 2) * Math.cos(a), (s / 2 - w / 2) * Math.sin(a));\n"
                              "    ctx.stroke();\n"
                              "}\n");
    else if (style == QLatin1String("orbit"))
        draw = QStringLiteral("const r = s * 0.38;\n"
                              "ctx.lineWidth = s * 0.03;\n"
                              "ctx.strokeStyle = tinted(0.25);\n"
                              "ctx.beginPath();\n"
                              "ctx.arc(0, 0, r, 0, 2 * Math.PI);\n"
                              "ctx.stroke();\n"
                              "dot(r * Math.cos(t), r * Math.sin(t), s * 0.09, 1);\n"
                              "dot(-r * Math.cos(t), -r * Math.sin(t), s * 0.05, 0.6);\n");
    else if (style == QLatin1String("comet"))
        draw = QStringLiteral("const r = s * 0.4;\n"
                              "for (let i = 15; i >= 0; --i) {\n"
                              "    const a = t - i * Math.PI / 18;\n"
                              "    dot(r * Math.cos(a), r * Math.sin(a), s * 0.1 * (1 - i / 16), 1 - i / 16);\n"
                              "}\n");
    else
        draw = QStringLiteral("const d = s * 0.18;\n"
                              "for (let i = 0; i < 8; ++i) {\n"
                              "    const a = i * Math.PI / 4 + t;\n"
                              "    dot((s - d) / 2 * Math.cos(a), (s - d) / 2 * Math.sin(a), d / 2, (i + 1) / 8);\n"
                              "}\n");
    return QStringLiteral("const ctx = getContext(\"2d\");\n"
                          "const s = Math.min(width, height), t = 2 * Math.PI * phase;\n"
                          "const tinted = alpha => Qt.rgba(tint.r, tint.g, tint.b, tint.a * Math.max(0, Math.min(1, alpha)));\n"
                          "const dot = (x, y, radius, alpha) => {\n"
                          "    ctx.fillStyle = tinted(alpha);\n"
                          "    ctx.beginPath();\n"
                          "    ctx.arc(x, y, radius, 0, 2 * Math.PI);\n"
                          "    ctx.fill();\n"
                          "};\n"
                          "ctx.reset();\n"
                          "ctx.translate(width / 2, height / 2);\n")
        + draw;
}

TextEffects readTextEffects(const QJsonObject &e, QStringList &problems)
{
    TextEffects effects;
    const auto read = [&](const char *key) {
        const QString name = QLatin1String(key);
        return e.contains(name) ? QColor(readColour(e, name, QStringLiteral("#000000"), problems)) : QColor();
    };
    effects.shadow = read("shadow");
    effects.outline = read("outline");
    effects.glow = read("glow");
    return effects;
}

namespace {

// Three box blurs, close to a Gaussian, on a premultiplied image.
void blur(QImage &image, int radius)
{
    if (radius < 1)
        return;
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = image.width(), h = image.height();
    std::vector<int> sums(4);
    std::vector<QRgb> line(std::max(w, h));
    const auto pass = [&](bool horizontal) {
        const int length = horizontal ? w : h, count = horizontal ? h : w;
        for (int k = 0; k < count; ++k) {
            const auto at = [&](int i) -> QRgb & {
                return horizontal ? reinterpret_cast<QRgb *>(image.scanLine(k))[i] : reinterpret_cast<QRgb *>(image.scanLine(i))[k];
            };
            for (int i = 0; i < length; ++i)
                line[i] = at(i);
            std::fill(sums.begin(), sums.end(), 0);
            const auto add = [&](int i, int sign) {
                if (i < 0 || i >= length)
                    return;
                const QRgb v = line[i];
                sums[0] += sign * qAlpha(v), sums[1] += sign * qRed(v), sums[2] += sign * qGreen(v), sums[3] += sign * qBlue(v);
            };
            for (int i = -radius; i < radius; ++i)
                add(i, 1);
            const int span = 2 * radius + 1;
            for (int i = 0; i < length; ++i) {
                add(i + radius, 1);
                at(i) = qRgba(sums[1] / span, sums[2] / span, sums[3] / span, sums[0] / span);
                add(i - radius, -1);
            }
        }
    };
    for (int n = 0; n < 3; ++n) {
        pass(true);
        pass(false);
    }
}

} // namespace

void drawStyledText(QPainter &p, const QRectF &rect, int flags, const QString &text, const QColor &colour, const TextEffects &effects)
{
    const double px = p.font().pixelSize() > 0 ? p.font().pixelSize() : p.font().pointSizeF() * 96 / 72;
    const double shadow = std::max(1.0, px * 0.06), outline = std::max(1.0, px * 0.05);
    const int glow = std::max(1, int(std::lround(px * 0.3)));
    const int reach = 3 * glow + int(std::ceil(shadow + outline)) + 2;
    const QRect area = rect.toAlignedRect().adjusted(-reach, -reach, reach, reach);
    const auto layer = [&](const QColor &c, const QList<QPointF> &offsets) {
        QImage image(area.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter q(&image);
        q.setRenderHints(p.renderHints() | QPainter::TextAntialiasing);
        q.setFont(p.font());
        q.setPen(c);
        for (const QPointF &offset : offsets) {
            q.save();
            q.translate(QPointF(-area.topLeft()) + offset);
            q.drawText(rect, flags, text);
            q.restore();
        }
        return image;
    };
    if (effects.glow.isValid()) {
        QImage image = layer(effects.glow, {{0, 0}});
        blur(image, glow);
        p.drawImage(area.topLeft(), image);
        p.drawImage(area.topLeft(), image); // twice: a blur thins it out
    }
    if (effects.shadow.isValid())
        p.drawImage(area.topLeft(), layer(effects.shadow, {{shadow, shadow}}));
    if (effects.outline.isValid()) {
        QList<QPointF> around;
        for (int i = 0; i < 8; ++i)
            around << QPointF(outline * std::cos(i * M_PI / 4), outline * std::sin(i * M_PI / 4));
        p.drawImage(area.topLeft(), layer(effects.outline, around));
    }
    p.save();
    p.setPen(colour);
    p.drawText(rect, flags, text);
    p.restore();
}

const QStringList imageAnimations = {QStringLiteral("fade"), QStringLiteral("breathe"), QStringLiteral("pulse"), QStringLiteral("slide"), QStringLiteral("spin")};

double imageAnimationSeconds(const QString &animation)
{
    static const QHash<QString, double> seconds = {{QStringLiteral("fade"), 1.0}, {QStringLiteral("breathe"), 3.0}, {QStringLiteral("pulse"), 1.5},
                                                   {QStringLiteral("slide"), 0.8}, {QStringLiteral("spin"), 4.0}};
    return seconds.value(animation, 0);
}
