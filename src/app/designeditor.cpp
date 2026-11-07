#include "designeditor.h"

#include "gencommon.h"
#include "genksplash.h"
#include "validate.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHash>
#include <QImageReader>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace {

constexpr qsizetype maxUndo = 200;

struct Resolution {
    int width;
    int height;
};
// The editor's resolution switch (SPEC.md §4.4); all 16:9, largest first.
constexpr Resolution resolutions[] = {{2560, 1440}, {1920, 1080}, {1280, 720}};

QJsonValue jsonValue(const QVariant &v)
{
    if (v.typeId() == QMetaType::QColor) {
        const QColor c = v.value<QColor>();
        return c.name(c.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb);
    }
    if (v.typeId() == QMetaType::Double || v.typeId() == QMetaType::Float)
        return std::round(v.toDouble() * 100) / 100; // 0.01 % is a quarter pixel at 1440p
    return QJsonValue::fromVariant(v);
}

void merge(QJsonObject &object, const QVariantMap &values)
{
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (!it->isValid() || it->isNull())
            object.remove(it.key());
        else
            object.insert(it.key(), jsonValue(*it));
    }
}

QJsonArray elementsOf(const QJsonObject &content)
{
    return content.value(QStringLiteral("elements")).toArray();
}

double number(const QJsonObject &o, const char *key)
{
    return o.value(QLatin1String(key)).toDouble();
}

// What can go wrong without making the theme invalid: parts off screen, text that
// overflows its box or is too small, raster images enlarged until blurry. A Plasma
// splash is laid out on each of the three resolutions; a boot splash is drawn once,
// for the screen it is built for.
QStringList designWarnings(const Design &design, const Library &library)
{
    const QList<Resolution> textResolutions = design.stage == Stage::BootSplash
        ? QList<Resolution>{{builtScreen.width(), builtScreen.height()}}
        : QList<Resolution>(std::begin(resolutions), std::end(resolutions));
    QStringList warnings;
    QStringList errors;
    QMap<QString, Asset> assets;
    for (const Asset &a : library.assets(errors))
        assets.insert(a.hash, a);
    // Only raster images get blurry; SVGs render at the size shown (sourceSize).
    const auto pixels = [&](const QString &hash) -> QSize {
        const auto it = assets.constFind(hash);
        if (it == assets.cend() || it->suffix == QLatin1String("svg") || it->suffix == QLatin1String("svgz"))
            return {};
        return QImageReader(library.assetPath(*it)).size();
    };
    const Resolution largest = resolutions[0];

    const QString background = design.content.value(QStringLiteral("background")).toObject().value(QStringLiteral("image")).toString();
    if (const QSize s = pixels(background); !s.isEmpty()) {
        const double scale = std::max(double(largest.width) / s.width(), double(largest.height) / s.height());
        if (scale > 1.05)
            warnings << DesignEditor::tr("1440p: the background image (%1×%2 px) is enlarged ×%3 and looks blurry.")
                            .arg(s.width()).arg(s.height()).arg(scale, 0, 'f', 1);
    }

    for (const QJsonValue &value : elementsOf(design.content)) {
        const QJsonObject e = value.toObject();
        const QString type = e.value(QStringLiteral("type")).toString();
        const double x = number(e, "x"), y = number(e, "y"), w = number(e, "width"), h = number(e, "height");
        QString what = DesignEditor::tr("The spinner");
        if (type == QLatin1String("text")) {
            QString text = e.value(QStringLiteral("text")).toString().simplified();
            if (text.size() > 24)
                text = text.left(23) + QChar(0x2026);
            what = DesignEditor::tr("The text “%1”").arg(text);
        } else if (type == QLatin1String("image")) {
            what = DesignEditor::tr("The image “%1”").arg(assets.value(e.value(QStringLiteral("asset")).toString()).name);
        } else if (type == QLatin1String("video")) {
            what = DesignEditor::tr("The video “%1”").arg(assets.value(e.value(QStringLiteral("asset")).toString()).name);
        } else if (type == QLatin1String("frames")) {
            what = DesignEditor::tr("The animation");
        } else if (type == QLatin1String("progress")) {
            what = DesignEditor::tr("The progress bar");
        }
        if (x < -0.005 || y < -0.005 || x + w > 100.005 || y + h > 100.005)
            warnings << DesignEditor::tr("%1 reaches past the edge of the screen.").arg(what);

        if (type == QLatin1String("image")) {
            const QSize s = pixels(e.value(QStringLiteral("asset")).toString());
            const double scale = s.isEmpty() ? 0 : std::min(largest.width * w / 100 / s.width(), largest.height * h / 100 / s.height());
            if (scale > 1.05)
                warnings << DesignEditor::tr("1440p: %1 (%2×%3 px) is enlarged ×%4 and looks blurry.").arg(what).arg(s.width()).arg(s.height()).arg(scale, 0, 'f', 1);
        } else if (type == QLatin1String("text") && design.stage != Stage::BootMenu) { // GRUB text is measured with its .pf2 fonts
            QFont font = QGuiApplication::font(); // what QML Text uses without a font
            const auto fontAsset = assets.constFind(e.value(QStringLiteral("font")).toString());
            if (fontAsset != assets.cend()) {
                if (const QString family = registerFont(library.assetPath(*fontAsset)); !family.isEmpty())
                    font.setFamily(family);
            }
            const QString align = e.value(QStringLiteral("align")).toString();
            const int flags = Qt::TextWordWrap
                | (align == QLatin1String("left") ? Qt::AlignLeft : align == QLatin1String("right") ? Qt::AlignRight : Qt::AlignHCenter);
            const double size = e.value(QStringLiteral("size")).toDouble(3);
            for (const Resolution &r : textResolutions) {
                const int px = int(r.height * size / 100); // QML truncates font.pixelSize
                if (px < 1)
                    continue;
                font.setPixelSize(px);
                const double boxWidth = r.width * w / 100, boxHeight = r.height * h / 100;
                const QRectF needed = QFontMetricsF(font).boundingRect(QRectF(0, 0, boxWidth, 1e6), flags, e.value(QStringLiteral("text")).toString());
                const double over = std::max(needed.width() - boxWidth, needed.height() - boxHeight);
                if (over > 1)
                    warnings << DesignEditor::tr("%1p: %2 overflows its box by %3 px.").arg(r.height).arg(what).arg(std::ceil(over));
            }
            const Resolution smallest = textResolutions.last();
            if (const int px = int(smallest.height * size / 100); px < 10)
                warnings << DesignEditor::tr("%1p: %2 is only %3 px high and hard to read.").arg(smallest.height).arg(what).arg(px);
        }
    }
    return warnings;
}

// A box for an image or animation of size: 20 % of the screen width (at most 80 % of
// its height) and the same aspect ratio, centred.
QVariantMap defaultBox(QSize size)
{
    double w = 20, h = 20.0 * 16 / 9 * size.height() / size.width();
    if (h > 80) {
        w *= 80 / h;
        h = 80;
    }
    return {{QStringLiteral("x"), 50 - w / 2}, {QStringLiteral("y"), 50 - h / 2}, {QStringLiteral("width"), w}, {QStringLiteral("height"), h}};
}

// The starters (SPEC.md §4.4), tinted with the accent colour; empty for blank.
QJsonObject starterContent(Stage stage, const QString &starter, QColor accent)
{
    if (!accent.isValid())
        accent = QColor(0x3d, 0xae, 0xe9); // Breeze's default accent
    const double hue = std::max(0.0, double(accent.hslHueF())); // -1 for greys
    const double saturation = accent.hslSaturationF();
    const QString dark = QColor::fromHslF(hue, std::min(0.6, saturation), 0.1).name();
    const QString accentName = accent.name();
    const QString light = QStringLiteral("#f5f5f5");
    const auto element = [](const QString &type, double x, double y, double w, double h, QJsonObject more) {
        more.insert(QStringLiteral("type"), type);
        more.insert(QStringLiteral("x"), x);
        more.insert(QStringLiteral("y"), y);
        more.insert(QStringLiteral("width"), w);
        more.insert(QStringLiteral("height"), h);
        return more;
    };
    const auto text = [&](const QString &words, double size, const QString &colour, double x, double y, double w, double h) {
        return element(QStringLiteral("text"), x, y, w, h,
                       {{QStringLiteral("text"), words}, {QStringLiteral("size"), size}, {QStringLiteral("color"), colour}, {QStringLiteral("align"), QStringLiteral("center")}});
    };
    const auto spinner = [&](const QString &colour, double y) {
        return element(QStringLiteral("spinner"), 48.5, y, 3, 5.33, {{QStringLiteral("color"), colour}});
    };

    if (stage == Stage::PlasmaSplash && starter == QLatin1String("welcome")) {
        return {{QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), dark}}},
                {QStringLiteral("fadeIn"), 300},
                {QStringLiteral("elements"), QJsonArray{text(DesignEditor::tr("Welcome"), 5, light, 25, 38, 50, 10), spinner(accentName, 58)}}};
    }
    if (stage == Stage::PlasmaSplash && starter == QLatin1String("accent")) {
        return {{QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QColor::fromHslF(hue, saturation, 0.35).name()}}},
                {QStringLiteral("elements"), QJsonArray{text(DesignEditor::tr("Hello"), 8, QStringLiteral("#ffffff"), 20, 34, 60, 16), spinner(QStringLiteral("#ffffff"), 62)}}};
    }
    if (stage == Stage::BootSplash && starter == QLatin1String("spinner")) {
        return {{QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), dark}, {QStringLiteral("color2"), QStringLiteral("#000000")}}},
                {QStringLiteral("hideMessages"), true},
                {QStringLiteral("elements"), QJsonArray{spinner(accentName, 60)}}};
    }
    // Boot menus: blank still needs a menu (and a countdown is what people expect).
    const auto menu = [&](double x, double y, double w, double h, const QString &style, const QString &selection) {
        return element(QStringLiteral("menu"), x, y, w, h,
                       {{QStringLiteral("size"), 2.2}, {QStringLiteral("color"), QStringLiteral("#c8c8c8")}, {QStringLiteral("selectedColor"), QStringLiteral("#ffffff")},
                        {QStringLiteral("selection"), QJsonObject{{QStringLiteral("style"), style}, {QStringLiteral("color"), selection}}}});
    };
    const auto countdown = [&](double y) {
        return element(QStringLiteral("countdown"), 30, y, 40, 4,
                       {{QStringLiteral("text"), DesignEditor::tr("Starting in %d s")}, {QStringLiteral("size"), 1.6}, {QStringLiteral("color"), light},
                        {QStringLiteral("align"), QStringLiteral("center")}});
    };
    if (stage == Stage::BootMenu && starter.isEmpty()) {
        return {{QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#000000")}}},
                {QStringLiteral("elements"), QJsonArray{menu(30, 30, 40, 40, QStringLiteral("flat"), QStringLiteral("#404040")), countdown(78)}}};
    }
    if (stage == Stage::BootMenu && starter == QLatin1String("centred")) {
        return {{QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), dark}}},
                {QStringLiteral("elements"),
                 QJsonArray{menu(30, 30, 40, 40, QStringLiteral("rounded"), accentName), countdown(76),
                            element(QStringLiteral("progress"), 40, 81, 20, 0.4, {{QStringLiteral("color"), accentName}, {QStringLiteral("background"), QColor::fromHslF(hue, saturation * 0.5, 0.25).name()}}),
                            text(DesignEditor::tr("Enter: start · e: edit · c: command line"), 1.4, light, 10, 92, 80, 4)}}};
    }
    if (stage == Stage::BootMenu && starter == QLatin1String("side")) {
        return {{QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#000000")}}},
                {QStringLiteral("elements"),
                 QJsonArray{text(DesignEditor::tr("Choose a system"), 3, light, 8, 18, 50, 6), menu(8, 28, 45, 55, QStringLiteral("glow"), accentName),
                            element(QStringLiteral("countdown"), 8, 86, 45, 4,
                                    {{QStringLiteral("text"), DesignEditor::tr("Starting in %d s")}, {QStringLiteral("size"), 1.6}, {QStringLiteral("color"), accentName},
                                     {QStringLiteral("align"), QStringLiteral("left")}})}}};
    }
    if (stage == Stage::BootSplash && starter == QLatin1String("progress")) {
        const QString dim = QColor::fromHslF(hue, saturation * 0.5, 0.25).name();
        return {{QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), dark}, {QStringLiteral("color2"), QStringLiteral("#000000")}}},
                {QStringLiteral("elements"),
                 QJsonArray{text(DesignEditor::tr("Starting…"), 2.5, light, 30, 44, 40, 6),
                            element(QStringLiteral("progress"), 40, 52, 20, 0.4, {{QStringLiteral("color"), accentName}, {QStringLiteral("background"), dim}})}}};
    }
    return {};
}

// "frame2" before "frame10", in any locale: QCollator's numeric mode does nothing in
// the C locale (CI runs in C.UTF-8).
bool naturalLess(const QString &a, const QString &b)
{
    static const QRegularExpression chunk(QStringLiteral("\\d+|\\D+"));
    QRegularExpressionMatchIterator i = chunk.globalMatch(a), j = chunk.globalMatch(b);
    while (i.hasNext() && j.hasNext()) {
        const QString x = i.next().captured(), y = j.next().captured();
        if (x == y)
            continue;
        if (x.front().isDigit() && y.front().isDigit()) {
            const auto digits = [](QString n) {
                while (n.size() > 1 && n.front() == QLatin1Char('0'))
                    n.remove(0, 1);
                return n;
            };
            const QString u = digits(x), v = digits(y);
            if (u.size() != v.size())
                return u.size() < v.size();
            if (u != v)
                return u < v;
        }
        return x < y;
    }
    return !i.hasNext() && j.hasNext();
}

QString localPath(const QUrl &file)
{
    return file.isLocalFile() ? file.toLocalFile() : file.toString();
}

// The start that brings the start, centre or end of [start, start + size] onto the
// nearest target within tolerance; guide becomes that target, or -1.
double snapStart(double start, double size, const QList<double> &targets, double tolerance, double &guide)
{
    guide = -1;
    double best = tolerance;
    double result = start;
    for (double target : targets) {
        for (double offset : {0.0, size / 2, size}) {
            if (const double distance = std::abs(start + offset - target); distance <= best) {
                best = distance;
                result = target - offset;
                guide = target;
            }
        }
    }
    return result;
}

// The size that brings the end of [start, start + size] onto the nearest target.
double snapSize(double start, double size, const QList<double> &targets, double tolerance, double &guide)
{
    guide = -1;
    double best = tolerance;
    double result = size;
    for (double target : targets) {
        if (const double distance = std::abs(start + size - target); target > start && distance <= best) {
            best = distance;
            result = target - start;
            guide = target;
        }
    }
    return result;
}

// Edges and centres of the screen and of every element but index, per axis; none if
// index isn't an element.
std::optional<std::pair<QList<double>, QList<double>>> snapTargets(const QJsonArray &elements, int index)
{
    if (index < 0 || index >= elements.size())
        return std::nullopt;
    QList<double> xs{0, 50, 100}, ys{0, 50, 100};
    for (qsizetype i = 0; i < elements.size(); ++i) {
        const QJsonObject o = elements[i].toObject();
        if (i == index)
            continue;
        xs << number(o, "x") << number(o, "x") + number(o, "width") / 2 << number(o, "x") + number(o, "width");
        ys << number(o, "y") << number(o, "y") + number(o, "height") / 2 << number(o, "y") + number(o, "height");
    }
    return std::pair{xs, ys};
}

} // namespace

DesignEditor::DesignEditor(QObject *parent)
    : QObject(parent)
{
    m_draftTimer.setSingleShot(true);
    m_draftTimer.setInterval(500);
    connect(&m_draftTimer, &QTimer::timeout, this, &DesignEditor::flushDraft);
    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(50);
    connect(&m_previewTimer, &QTimer::timeout, this, &DesignEditor::flushPreview);
}

DesignEditor::~DesignEditor()
{
    if (m_draftTimer.isActive())
        flushDraft();
    m_job.reset(); // stops ffmpeg without signals
}

void DesignEditor::setLibrary(Library *library)
{
    close();
    m_library = library;
}

void DesignEditor::setName(const QString &name)
{
    edit([&](State &s) { s.name = name; });
}

QString DesignEditor::open(const QString &id)
{
    if (!m_library)
        return tr("The library isn't available.");
    close();
    QString error;
    const auto saved = m_library->design(id, 0, error); // none if it was never saved
    QStringList errors;
    std::optional<Design> draft;
    for (const Design &d : m_library->drafts(errors)) {
        if (d.id == id)
            draft = d;
    }
    if (!saved && !draft)
        return error;
    const Stage stage = saved ? saved->stage : draft->stage;
    const State savedState = saved ? State{saved->name, saved->content} : State{};
    load(id, stage, saved ? saved->version : 0, draft ? State{draft->name, draft->content} : savedState, savedState, draft.has_value());
    return {};
}

void DesignEditor::create(const QString &stage, const QString &starter, const QColor &accent)
{
    const auto s = stageFromKey(stage);
    if (!m_library || !s)
        return;
    close();
    QJsonObject content = starterContent(*s, starter, accent);
    if (content.isEmpty())
        content = {{QStringLiteral("background"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#000000")}}}, {QStringLiteral("elements"), QJsonArray()}};
    const QString name = *s == Stage::BootMenu ? tr("New boot menu") : *s == Stage::BootSplash ? tr("New boot splash") : tr("New splash");
    load({}, *s, 0, State{name, content}, {}, false);
}

QString DesignEditor::duplicate(const QString &id)
{
    if (!m_library)
        return tr("The library isn't available.");
    QString error;
    const auto design = m_library->design(id, 0, error);
    if (!design)
        return error;
    close();
    load({}, design->stage, 0, State{tr("Copy of %1").arg(design->name), design->content}, {}, false);
    return {};
}

void DesignEditor::load(const QString &id, Stage stage, int version, const State &state, const State &saved, bool recovered)
{
    m_open = true;
    m_stage = stage;
    m_id = id;
    m_version = version;
    m_current = state;
    m_saved = saved;
    m_recovered = recovered;
    m_draftWritten = recovered;
    m_undo.clear();
    m_redo.clear();
    m_gesture = m_gestureRecorded = false;
    reloadLists();
    flushPreview();
    Q_EMIT changed();
}

QString DesignEditor::save()
{
    if (!m_open || !m_library)
        return tr("No design is open.");
    if (!dirty())
        return {};
    if (m_current.name.trimmed().isEmpty())
        return tr("The design needs a name.");
    Design design = currentDesign();
    QString error;
    if (!m_library->saveDesign(design, error))
        return error;
    m_draftTimer.stop();
    m_id = design.id;
    m_version = design.version;
    m_saved = m_current;
    m_recovered = m_draftWritten = false;
    reloadLists();
    m_previewTimer.start(); // the version number is part of the package
    Q_EMIT changed();
    Q_EMIT libraryChanged();
    return {};
}

void DesignEditor::close()
{
    if (!m_open)
        return;
    cancelConversion();
    m_sample.reset();
    if (m_draftTimer.isActive())
        flushDraft();
    const bool hadDraft = m_draftWritten;
    m_open = false;
    m_id.clear();
    m_version = 0;
    m_current = m_saved = State{};
    m_recovered = m_draftWritten = false;
    m_undo.clear();
    m_redo.clear();
    m_gesture = m_gestureRecorded = false;
    m_versions.clear();
    m_previewTimer.stop();
    m_previewPackage.clear();
    m_previewUrl.clear();
    m_plymouth.clear();
    m_bootMenu.clear();
    m_problems.clear();
    m_warnings.clear();
    Q_EMIT changed();
    Q_EMIT previewChanged();
    if (hadDraft)
        Q_EMIT libraryChanged();
}

void DesignEditor::discard()
{
    if (!m_open)
        return;
    m_draftTimer.stop();
    if (m_library && !m_id.isEmpty())
        m_library->clearDraft(m_id);
    m_draftWritten = false;
    if (m_version == 0)
        close();
    else
        load(m_id, m_stage, m_version, m_saved, m_saved, false);
    Q_EMIT libraryChanged();
}

void DesignEditor::edit(const std::function<void(State &)> &change)
{
    if (!m_open)
        return;
    State next = m_current;
    change(next);
    if (next == m_current)
        return;
    if (!m_gestureRecorded) {
        m_undo.append(m_current);
        if (m_undo.size() > maxUndo)
            m_undo.removeFirst();
        m_redo.clear();
        m_gestureRecorded = m_gesture;
    }
    m_current = next;
    m_draftTimer.start();
    if (!m_gesture)
        m_previewTimer.start();
    Q_EMIT changed();
}

void DesignEditor::undo()
{
    if (m_undo.isEmpty())
        return;
    m_redo.append(m_current);
    m_current = m_undo.takeLast();
    m_draftTimer.start();
    m_previewTimer.start();
    Q_EMIT changed();
}

void DesignEditor::redo()
{
    if (m_redo.isEmpty())
        return;
    m_undo.append(m_current);
    m_current = m_redo.takeLast();
    m_draftTimer.start();
    m_previewTimer.start();
    Q_EMIT changed();
}

void DesignEditor::beginGesture()
{
    m_gesture = true;
    m_gestureRecorded = false;
}

void DesignEditor::endGesture()
{
    m_gesture = m_gestureRecorded = false;
    m_previewTimer.start();
}

void DesignEditor::setContent(const QVariantMap &values)
{
    edit([&](State &s) { merge(s.content, values); });
}

void DesignEditor::setBackground(const QVariantMap &values)
{
    edit([&](State &s) {
        QJsonObject background = s.content.value(QStringLiteral("background")).toObject();
        merge(background, values);
        s.content.insert(QStringLiteral("background"), background);
    });
}

void DesignEditor::setElement(int index, const QVariantMap &values)
{
    edit([&](State &s) {
        QJsonArray elements = elementsOf(s.content);
        if (index < 0 || index >= elements.size())
            return;
        QJsonObject e = elements[index].toObject();
        merge(e, values);
        elements[index] = e;
        s.content.insert(QStringLiteral("elements"), elements);
    });
}

int DesignEditor::addElement(const QString &type, const QVariantMap &values)
{
    QJsonObject e;
    if (type == QLatin1String("text")) {
        e = {{QStringLiteral("text"), tr("Text")}, {QStringLiteral("size"), 4}, {QStringLiteral("color"), QStringLiteral("#ffffff")},
             {QStringLiteral("align"), QStringLiteral("center")}, {QStringLiteral("x"), 30}, {QStringLiteral("y"), 45},
             {QStringLiteral("width"), 40}, {QStringLiteral("height"), 10}};
    } else if (type == QLatin1String("spinner")) {
        e = {{QStringLiteral("color"), QStringLiteral("#ffffff")}, {QStringLiteral("x"), 48.5}, {QStringLiteral("y"), 70},
             {QStringLiteral("width"), 3}, {QStringLiteral("height"), 5.33}}; // about square at 16:9
    } else if (type == QLatin1String("image")) {
        e = {{QStringLiteral("x"), 40}, {QStringLiteral("y"), 30}, {QStringLiteral("width"), 20}, {QStringLiteral("height"), 20}};
    } else if (type == QLatin1String("video")) {
        e = {{QStringLiteral("fill"), QStringLiteral("crop")}, {QStringLiteral("x"), 0}, {QStringLiteral("y"), 0},
             {QStringLiteral("width"), 100}, {QStringLiteral("height"), 100}};
    } else if (type == QLatin1String("frames")) {
        e = {{QStringLiteral("frames"), QJsonArray()}, {QStringLiteral("seconds"), 2}, {QStringLiteral("x"), 40}, {QStringLiteral("y"), 40},
             {QStringLiteral("width"), 20}, {QStringLiteral("height"), 20}};
    } else if (type == QLatin1String("progress")) {
        e = {{QStringLiteral("color"), QStringLiteral("#ffffff")}, {QStringLiteral("background"), QStringLiteral("#404040")},
             {QStringLiteral("x"), 40}, {QStringLiteral("y"), 85}, {QStringLiteral("width"), 20}, {QStringLiteral("height"), 0.5}};
    } else if (type == QLatin1String("menu")) {
        e = {{QStringLiteral("size"), 2.2}, {QStringLiteral("color"), QStringLiteral("#c8c8c8")}, {QStringLiteral("selectedColor"), QStringLiteral("#ffffff")},
             {QStringLiteral("selection"), QJsonObject{{QStringLiteral("style"), QStringLiteral("rounded")}, {QStringLiteral("color"), QStringLiteral("#40ffffff")}}},
             {QStringLiteral("x"), 30}, {QStringLiteral("y"), 30}, {QStringLiteral("width"), 40}, {QStringLiteral("height"), 40}};
    } else if (type == QLatin1String("countdown")) {
        e = {{QStringLiteral("text"), tr("Starting in %d s")}, {QStringLiteral("size"), 1.6}, {QStringLiteral("color"), QStringLiteral("#ffffff")},
             {QStringLiteral("align"), QStringLiteral("center")}, {QStringLiteral("x"), 30}, {QStringLiteral("y"), 78}, {QStringLiteral("width"), 40},
             {QStringLiteral("height"), 4}};
    } else {
        return -1;
    }
    e.insert(QStringLiteral("type"), type);
    merge(e, values);

    // Images and animations keep their aspect ratio (defaultBox).
    const QString sizeFrom = type == QLatin1String("image") ? e.value(QStringLiteral("asset")).toString()
        : type == QLatin1String("frames")                  ? e.value(QStringLiteral("frames")).toArray().at(0).toString()
                                                           : QString();
    if (!sizeFrom.isEmpty() && !values.contains(QStringLiteral("width")) && m_library) {
        QStringList errors;
        for (const Asset &a : m_library->assets(errors)) {
            if (const QSize s = a.hash == sizeFrom ? QImageReader(m_library->assetPath(a)).size() : QSize(); !s.isEmpty())
                merge(e, defaultBox(s));
        }
    }

    int index = -1;
    edit([&](State &s) {
        QJsonArray elements = elementsOf(s.content);
        index = type == QLatin1String("video") ? 0 : int(elements.size());
        elements.insert(index, e);
        s.content.insert(QStringLiteral("elements"), elements);
    });
    return index;
}

void DesignEditor::removeElement(int index)
{
    edit([&](State &s) {
        QJsonArray elements = elementsOf(s.content);
        if (index >= 0 && index < elements.size())
            elements.removeAt(index);
        s.content.insert(QStringLiteral("elements"), elements);
    });
}

void DesignEditor::moveElement(int from, int to)
{
    edit([&](State &s) {
        QJsonArray elements = elementsOf(s.content);
        if (from < 0 || to < 0 || from >= elements.size() || to >= elements.size())
            return;
        const QJsonValue moved = elements.takeAt(from);
        elements.insert(to, moved);
        s.content.insert(QStringLiteral("elements"), elements);
    });
}

void DesignEditor::restoreVersion(int version)
{
    QString error;
    const auto design = m_library && !m_id.isEmpty() ? m_library->design(m_id, version, error) : std::nullopt;
    if (design) {
        edit([&](State &s) {
            s.name = design->name;
            s.content = design->content;
        });
    }
}

QVariantMap DesignEditor::addImage(const QUrl &file)
{
    if (!m_library)
        return {{QStringLiteral("error"), tr("The library isn't available.")}};
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!QImageReader(path).canRead())
        return {{QStringLiteral("error"), tr("%1 isn't an image Ekpo can read.").arg(QFileInfo(path).fileName())}};
    QString error;
    const auto asset = m_library->addAsset(path, Asset::Kind::Image, QFileInfo(path).completeBaseName(), error);
    if (!asset)
        return {{QStringLiteral("error"), error}};
    reloadLists();
    Q_EMIT changed();
    return {{QStringLiteral("hash"), asset->hash}};
}

QVariantMap DesignEditor::addSystemFont(const QString &family)
{
    if (!m_library)
        return {{QStringLiteral("error"), tr("The library isn't available.")}};
    static const QRegularExpression foundry(QStringLiteral("\\s*\\[[^\\]]*\\]$")); // Qt's "Family [Foundry]" for duplicates
    const QString name = QString(family).remove(foundry);
    QProcess fcMatch;
    fcMatch.start(QStringLiteral("fc-match"), {QStringLiteral("--format=%{family}\n%{file}"), name});
    const bool ran = fcMatch.waitForFinished(5000) && fcMatch.exitStatus() == QProcess::NormalExit && fcMatch.exitCode() == 0;
    const QStringList lines = QString::fromUtf8(fcMatch.readAllStandardOutput()).split(QLatin1Char('\n'));
    // fc-match falls back to another family when it doesn't know this one.
    if (!ran || !lines.value(0).split(QLatin1Char(',')).contains(name, Qt::CaseInsensitive) || lines.value(1).isEmpty())
        return {{QStringLiteral("error"), tr("Can't find the font file of %1.").arg(name)}};
    QString error;
    const auto asset = m_library->addFont(lines.value(1), name, error);
    if (!asset)
        return {{QStringLiteral("error"), error}};
    reloadLists();
    Q_EMIT changed();
    return {{QStringLiteral("hash"), asset->hash}};
}

QString DesignEditor::startJob(MediaJob *job, const QString &name, const std::function<QString()> &done)
{
    m_job.reset(job);
    m_jobName = name;
    m_jobDone = done;
    connect(job, &MediaJob::progressChanged, this, &DesignEditor::conversionChanged);
    connect(job, &MediaJob::finished, this, &DesignEditor::finishConversion);
    job->start();
    Q_EMIT conversionChanged();
    return {};
}

// "menu (1:23.0–1:25.0)": which part of the recording a clip is.
static QString clipName(const QString &path, double start, double seconds)
{
    const QString base = QFileInfo(path).completeBaseName();
    if (start <= 0 && seconds <= 0)
        return base;
    const auto time = [](double s) {
        const int tenths = qRound(s * 10);
        return QStringLiteral("%1:%2.%3").arg(tenths / 600).arg(tenths / 10 % 60, 2, 10, QLatin1Char('0')).arg(tenths % 10);
    };
    return seconds > 0 ? QStringLiteral("%1 (%2–%3)").arg(base, time(start), time(start + seconds)) : QStringLiteral("%1 (from %2)").arg(base, time(start));
}

QString DesignEditor::addVideo(const QUrl &file, int replaceIndex, double start, double seconds)
{
    if (!m_open || !m_library)
        return tr("No design is open.");
    if (m_job)
        return tr("Wait until “%1” is converted.").arg(m_jobName);
    const QString path = localPath(file);
    const QString name = clipName(path, start, seconds);
    const QString output = m_mediaDir.filePath(QStringLiteral("video.mp4"));
    return startJob(MediaJob::toMp4(path, output, resolutions[0].height, start, seconds), name, [this, path, name, output, replaceIndex] {
        QString error;
        const auto asset = m_library->addAsset(output, Asset::Kind::Video, name, error, {}, path);
        QFile::remove(output);
        if (!asset)
            return error;
        reloadLists();
        if (replaceIndex < 0)
            addElement(QStringLiteral("video"), {{QStringLiteral("asset"), asset->hash}});
        else if (element(replaceIndex).value(QStringLiteral("type")) == QLatin1String("video")) // still the one it was for
            setElement(replaceIndex, {{QStringLiteral("asset"), asset->hash}});
        return QString();
    });
}

void DesignEditor::cancelConversion()
{
    if (m_job)
        m_job->cancel(); // finishes the conversion through finished()
}

void DesignEditor::finishConversion(bool ok, const QString &error)
{
    m_job.release()->deleteLater(); // it is emitting this
    const std::function<QString()> done = std::exchange(m_jobDone, {});
    const QString message = ok && m_library ? done() : error;
    Q_EMIT changed();
    Q_EMIT conversionChanged();
    Q_EMIT conversionFinished(message);
}

QString DesignEditor::putFrames(const QStringList &files, const QString &name, const QString &source, double seconds, int replaceIndex)
{
    QVariantList hashes;
    for (qsizetype i = 0; i < files.size(); ++i) {
        QString error;
        const auto asset = m_library->addAsset(files[i], Asset::Kind::Image, QStringLiteral("%1 %2").arg(name).arg(i + 1, 4, 10, QLatin1Char('0')), error, {},
                                               source.isEmpty() ? files[i] : source);
        if (!asset)
            return error;
        hashes << asset->hash;
    }
    reloadLists();
    if (replaceIndex >= 0 && element(replaceIndex).value(QStringLiteral("type")) == QLatin1String("frames"))
        setElement(replaceIndex, {{QStringLiteral("frames"), hashes}, {QStringLiteral("seconds"), seconds}});
    else if (replaceIndex < 0)
        addElement(QStringLiteral("frames"), {{QStringLiteral("frames"), hashes}, {QStringLiteral("seconds"), seconds}});
    return {};
}

QString DesignEditor::addImageFrames(const QList<QUrl> &files, int replaceIndex)
{
    if (!m_open || !m_library)
        return tr("No design is open.");
    QStringList paths;
    for (const QUrl &file : files) {
        paths << localPath(file);
        if (!QImageReader(paths.last()).canRead())
            return tr("%1 isn't an image Ekpo can read.").arg(QFileInfo(paths.last()).fileName());
    }
    if (paths.isEmpty())
        return tr("No images were chosen.");
    std::sort(paths.begin(), paths.end(), naturalLess);
    // Up to 60 frames play in two-step's 2 s loop; more play at 30 fps (script module).
    return putFrames(paths, QFileInfo(paths.first()).dir().dirName(), {}, paths.size() <= 60 ? 2.0 : paths.size() / 30.0, replaceIndex);
}

QVariantMap DesignEditor::videoPlan(const QUrl &file, int replaceIndex) const
{
    QString error;
    const auto info = probeVideo(localPath(file), error);
    if (!info)
        return {{QStringLiteral("error"), error}};
    const bool replacing = replaceIndex >= 0 && element(replaceIndex).value(QStringLiteral("type")) == QLatin1String("frames");
    const QVariantMap box = replacing ? element(replaceIndex).toVariantMap() : defaultBox(QSize(info->width, info->height));
    const QSizeF pixels(builtScreen.width() * box.value(QStringLiteral("width")).toDouble() / 100,
                        builtScreen.height() * box.value(QStringLiteral("height")).toDouble() / 100);
    const double scale = std::min(pixels.width() / info->width, pixels.height() / info->height);
    // What the rest of the theme leaves: the last preview without its animation.
    const qint64 memory = m_layout.decodedBytes ? m_layout.decodedBytes - m_layout.frameDecodedBytes : 2 * qint64(builtScreen.width()) * builtScreen.height() * 4;
    return {{QStringLiteral("width"), info->width},
            {QStringLiteral("height"), info->height},
            {QStringLiteral("fps"), info->fps},
            {QStringLiteral("seconds"), info->seconds},
            {QStringLiteral("frameWidth"), std::max(1, int(info->width * scale))},
            {QStringLiteral("frameHeight"), std::max(1, int(info->height * scale))},
            {QStringLiteral("memoryLeft"), double(plymouthMemoryBudget - memory)},
            {QStringLiteral("bytesLeft"), double(plymouthThemeCap - (m_layout.bytes - m_layout.frameBytes))}};
}

void DesignEditor::measureFrames(const QUrl &file, int width, int height, double fps, double start)
{
    m_sample.reset(); // stops a measurement still running
    const QString dir = m_mediaDir.filePath(QStringLiteral("sample"));
    QDir(dir).removeRecursively();
    m_sample.reset(MediaJob::toFrames(localPath(file), dir, QSize(width, height), fps, 1.0, start));
    connect(m_sample.get(), &MediaJob::finished, this, [this, dir](bool ok, const QString &) {
        m_sample.release()->deleteLater();
        qint64 bytes = 0;
        const QFileInfoList frames = QDir(dir).entryInfoList({QStringLiteral("frame-*.png")}, QDir::Files);
        for (const QFileInfo &f : frames)
            bytes += f.size();
        QDir(dir).removeRecursively();
        Q_EMIT framesMeasured(ok && !frames.isEmpty() ? double(bytes) / frames.size() : -1);
    });
    m_sample->start();
}

QString DesignEditor::addVideoFrames(const QUrl &file, int width, int height, double fps, double seconds, int replaceIndex, double start)
{
    if (!m_open || !m_library)
        return tr("No design is open.");
    if (m_job)
        return tr("Wait until “%1” is converted.").arg(m_jobName);
    const QString path = localPath(file);
    const QString name = clipName(path, start, seconds);
    const QString dir = m_mediaDir.filePath(QStringLiteral("frames"));
    QDir(dir).removeRecursively();
    return startJob(MediaJob::toFrames(path, dir, QSize(width, height), fps, seconds, start), name, [this, path, name, dir, fps, replaceIndex] {
        QStringList frames;
        for (const QFileInfo &f : QDir(dir).entryInfoList({QStringLiteral("frame-*.png")}, QDir::Files, QDir::Name))
            frames << f.filePath();
        const QString error = putFrames(frames, name, path, frames.size() / fps, replaceIndex);
        QDir(dir).removeRecursively();
        return error;
    });
}

QString DesignEditor::saveShared(SharedElement &element)
{
    QString error;
    if (!m_library || !m_library->saveSharedElement(element, error))
        return error.isEmpty() ? tr("The library isn't available.") : error;
    reloadLists();
    m_previewTimer.start();
    Q_EMIT changed();
    Q_EMIT libraryChanged(); // other designs change too, and what has an update available
    return {};
}

QString DesignEditor::shareLogo(int index, const QString &name)
{
    const QJsonObject e = element(index);
    const QString asset = e.value(QStringLiteral("asset")).toString();
    if (e.value(QStringLiteral("type")) != QLatin1String("image") || asset.isEmpty())
        return tr("Only an image can be shared as a logo.");
    SharedElement logo{{}, SharedElement::Kind::Logo, name.trimmed().isEmpty() ? m_assets.value(asset).toMap().value(QStringLiteral("name")).toString() : name.trimmed(),
                       asset, {}};
    if (const QString error = saveShared(logo); !error.isEmpty())
        return error;
    setElement(index, {{QStringLiteral("shared"), logo.id}});
    return {};
}

int DesignEditor::addSharedLogo(const QString &id)
{
    const QVariantMap logo = m_shared.value(id).toMap();
    if (logo.value(QStringLiteral("kind")) != QLatin1String("logo"))
        return -1;
    return addElement(QStringLiteral("image"), {{QStringLiteral("asset"), logo.value(QStringLiteral("asset"))}, {QStringLiteral("shared"), id}});
}

QString DesignEditor::setSharedLogo(const QString &id, const QString &asset)
{
    QStringList errors;
    for (SharedElement e : m_library ? m_library->sharedElements(errors) : QList<SharedElement>()) {
        if (e.id == id && e.kind == SharedElement::Kind::Logo) {
            e.asset = asset;
            return saveShared(e);
        }
    }
    return tr("The shared logo is no longer in the library.");
}

void DesignEditor::unlink(int index)
{
    const QString id = element(index).value(QStringLiteral("shared")).toString();
    if (id.isEmpty())
        return;
    QVariantMap values{{QStringLiteral("shared"), QVariant()}};
    if (const QString asset = m_shared.value(id).toMap().value(QStringLiteral("asset")).toString(); !asset.isEmpty())
        values.insert(QStringLiteral("asset"), asset); // what it shows now
    setElement(index, values);
}

QString DesignEditor::newPalette(const QString &name)
{
    if (!m_open || !m_library)
        return tr("No design is open.");
    // The design's colours as they look now: background and its gradient's second
    // colour, the first spinner or progress bar (accent) and the next one in another
    // colour (accent2), and the first text.
    QStringList problems;
    const Design now = m_library->resolved(currentDesign(), problems);
    const QJsonArray elements = elementsOf(now.content);
    const auto first = [&](const QStringList &types, const QString &fallback) {
        for (const QJsonValue &v : elements) {
            if (types.contains(v.toObject().value(QStringLiteral("type")).toString()))
                return v.toObject().value(QStringLiteral("color")).toString(fallback).left(7).toLower();
        }
        return fallback;
    };
    const QJsonObject backgroundNow = now.content.value(QStringLiteral("background")).toObject();
    const QString backgroundColour = backgroundNow.value(QStringLiteral("color")).toString(QStringLiteral("#000000")).left(7).toLower();
    const QString accent = first({QStringLiteral("spinner"), QStringLiteral("progress")}, QStringLiteral("#3daee9"));
    QString accent2 = accent;
    for (const QJsonValue &v : elements) {
        const QString type = v.toObject().value(QStringLiteral("type")).toString();
        const QString colour = v.toObject().value(QStringLiteral("color")).toString().left(7).toLower();
        if ((type == QLatin1String("spinner") || type == QLatin1String("progress")) && !colour.isEmpty() && colour != accent) {
            accent2 = colour;
            break;
        }
    }
    SharedElement palette{{}, SharedElement::Kind::Palette, name.trimmed().isEmpty() ? tr("Palette of %1").arg(m_current.name) : name.trimmed(), {}, {}};
    palette.colours = {{QStringLiteral("background"), backgroundColour},
                       {QStringLiteral("gradient"), backgroundNow.value(QStringLiteral("color2")).toString(backgroundColour).left(7).toLower()},
                       {QStringLiteral("accent"), accent},
                       {QStringLiteral("accent2"), accent2},
                       {QStringLiteral("text"), first({QStringLiteral("text")}, QStringLiteral("#ffffff"))}};
    // The same colours again (e.g. a second click) link the palette made before instead
    // of a copy (issue #2); other colours get a name of their own.
    QStringList errors;
    const QList<SharedElement> existing = m_library->sharedElements(errors);
    const auto same = std::find_if(existing.cbegin(), existing.cend(), [&](const SharedElement &e) {
        return e.kind == SharedElement::Kind::Palette && e.colours == palette.colours;
    });
    if (same != existing.cend()) {
        palette = *same;
    } else {
        const QString base = palette.name;
        const auto taken = [&] { return std::any_of(existing.cbegin(), existing.cend(), [&](const SharedElement &e) { return e.name == palette.name; }); };
        for (int n = 2; taken(); ++n)
            palette.name = QStringLiteral("%1 %2").arg(base).arg(n);
        if (const QString error = saveShared(palette); !error.isEmpty())
            return error;
    }
    // Those colours now come from the palette.
    edit([&](State &s) {
        s.content = now.content;
        s.content.insert(QStringLiteral("palette"), palette.id);
        QJsonObject background = s.content.value(QStringLiteral("background")).toObject();
        if (background.value(QStringLiteral("color")).toString().left(7).toLower() == palette.colours.value(QStringLiteral("background")))
            background.insert(QStringLiteral("color"), QStringLiteral("@background"));
        if (background.contains(QStringLiteral("color2"))
            && background.value(QStringLiteral("color2")).toString().left(7).toLower() == palette.colours.value(QStringLiteral("gradient")))
            background.insert(QStringLiteral("color2"), QStringLiteral("@gradient"));
        s.content.insert(QStringLiteral("background"), background);
        QJsonArray updated;
        for (const QJsonValue &v : elementsOf(s.content)) {
            QJsonObject e = v.toObject();
            const QString colour = e.value(QStringLiteral("color")).toString().toLower();
            const QString type = e.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("text") && colour == palette.colours.value(QStringLiteral("text")))
                e.insert(QStringLiteral("color"), QStringLiteral("@text"));
            else if ((type == QLatin1String("spinner") || type == QLatin1String("progress")) && colour == palette.colours.value(QStringLiteral("accent")))
                e.insert(QStringLiteral("color"), QStringLiteral("@accent"));
            else if ((type == QLatin1String("spinner") || type == QLatin1String("progress")) && colour == palette.colours.value(QStringLiteral("accent2")))
                e.insert(QStringLiteral("color"), QStringLiteral("@accent2"));
            updated.append(e);
        }
        s.content.insert(QStringLiteral("elements"), updated);
    });
    return {};
}

void DesignEditor::linkPalette(const QString &id)
{
    if (!m_open || !m_library)
        return;
    if (!id.isEmpty()) {
        setContent({{QStringLiteral("palette"), id}});
        return;
    }
    // Unlinked: the palette's colours become the design's own.
    QStringList problems;
    Design own = m_library->resolved(currentDesign(), problems);
    own.content.remove(QStringLiteral("palette"));
    edit([&](State &s) {
        // Shared logos stay linked: resolved() only filled in their current asset.
        s.content = own.content;
    });
}

QString DesignEditor::deletePalette(const QString &id)
{
    if (!m_library)
        return tr("The library isn't available.");
    // Every saved version and draft that links it would lose its colours; this
    // design's unsaved changes are unlinked below.
    QStringList users;
    QStringList errors;
    for (const Library::Use &use : m_library->uses(errors).value(id)) {
        if (use.design == m_id && use.version > 0)
            users << tr("version %1 of this design").arg(use.version);
        else if (use.design != m_id || use.version > 0)
            users << use.label;
    }
    if (!users.isEmpty())
        return tr("The palette is still used by %1. Switch those to another palette or None first.").arg(users.join(QStringLiteral(", ")));
    if (m_current.content.value(QStringLiteral("palette")).toString() == id)
        linkPalette({});
    QString error;
    if (!m_library->removeSharedElement(id, error))
        return error;
    reloadLists();
    Q_EMIT changed();
    Q_EMIT libraryChanged();
    return {};
}

QString DesignEditor::setPaletteColour(const QString &key, const QColor &colour)
{
    const QString id = m_current.content.value(QStringLiteral("palette")).toString();
    QStringList errors;
    for (SharedElement e : m_library ? m_library->sharedElements(errors) : QList<SharedElement>()) {
        if (e.id == id && e.kind == SharedElement::Kind::Palette && paletteKeys.contains(key)) {
            e.colours.insert(key, colour.name(QColor::HexRgb));
            return saveShared(e);
        }
    }
    return tr("The design has no palette.");
}

QJsonObject DesignEditor::element(int index) const
{
    return elementsOf(m_current.content).at(index).toObject();
}

QVariantMap DesignEditor::snapMove(int index, double x, double y, double toleranceX, double toleranceY) const
{
    double guideX = -1, guideY = -1;
    if (const auto t = snapTargets(elementsOf(m_current.content), index)) {
        const QJsonObject e = element(index);
        x = snapStart(x, number(e, "width"), t->first, toleranceX, guideX);
        y = snapStart(y, number(e, "height"), t->second, toleranceY, guideY);
    }
    return {{QStringLiteral("x"), x}, {QStringLiteral("y"), y}, {QStringLiteral("guideX"), guideX}, {QStringLiteral("guideY"), guideY}};
}

QVariantMap DesignEditor::snapResize(int index, double width, double height, double toleranceX, double toleranceY) const
{
    double guideX = -1, guideY = -1;
    if (const auto t = snapTargets(elementsOf(m_current.content), index)) {
        const QJsonObject e = element(index);
        width = std::max(snapSize(number(e, "x"), width, t->first, toleranceX, guideX), 0.5);
        height = std::max(snapSize(number(e, "y"), height, t->second, toleranceY, guideY), 0.5);
    }
    return {{QStringLiteral("width"), width}, {QStringLiteral("height"), height}, {QStringLiteral("guideX"), guideX}, {QStringLiteral("guideY"), guideY}};
}

QString DesignEditor::runRealSplash() const
{
    if (m_previewPackage.isEmpty())
        return tr("There is no preview to show yet.");
    if (!QProcess::startDetached(QStringLiteral("ksplashqml"), {QStringLiteral("--test"), QStringLiteral("--window"), m_previewPackage}))
        return tr("Can't start ksplashqml (part of plasma-workspace).");
    return {};
}

void DesignEditor::flushPreview()
{
    m_previewTimer.stop();
    if (!m_open || !m_library)
        return;
    QStringList problems;
    const Design design = m_library->resolved(currentDesign(), problems); // with its shared logos and palette
    if (m_stage == Stage::BootMenu)
        return flushBootMenuPreview(design, problems);
    // Plasma splash assets are linked, not copied: a video would otherwise be rewritten
    // on every edit. A boot splash is all generated files.
    QMap<QString, QString> links;
    PlymouthLayout layout;
    const bool boot = m_stage == Stage::BootSplash;
    const QList<PayloadFile> files = boot ? buildPlymouthSplash(design, QStringLiteral("ekpo-preview"), *m_library, builtScreen, problems, &layout)
                                          : buildPlasmaSplash(design, QStringLiteral("ekpo-preview"), *m_library, problems, &links);
    QList<PayloadFile> hashed = files;
    for (auto it = links.cbegin(); it != links.cend(); ++it)
        hashed << PayloadFile{it.key(), it->toUtf8()}; // asset names are content hashes
    const QString hash = Library::buildHash(hashed);
    const QString dir = m_previewDir.filePath(hash);
    if (!m_previewDir.isValid()) {
        problems << tr("Can't create a folder for the preview: %1").arg(m_previewDir.errorString());
    } else if (!QFileInfo::exists(dir)) {
        bool written = true;
        for (const PayloadFile &f : files) {
            QSaveFile out(dir + QLatin1Char('/') + f.path);
            if (!QDir().mkpath(QFileInfo(out.fileName()).path()) || !out.open(QIODevice::WriteOnly) || out.write(f.data) != f.data.size() || !out.commit()) {
                problems << tr("Can't write the preview: %1").arg(out.errorString());
                written = false;
                break;
            }
        }
        for (auto it = links.cbegin(); written && it != links.cend(); ++it) {
            const QString link = dir + QLatin1Char('/') + it.key();
            if (!QDir().mkpath(QFileInfo(link).path()) || !QFile::link(*it, link)) {
                problems << tr("Can't link %1 into the preview.").arg(*it);
                written = false;
            }
        }
        if (!written)
            QDir(dir).removeRecursively();
    }
    keepPreviews(hash);
    m_previewPackage = dir;
    m_problems = problems;
    m_warnings = designWarnings(design, *m_library);
    m_previewUrl.clear();
    m_plymouth.clear();
    m_bootMenu.clear();
    if (!boot) {
        m_previewUrl = QUrl::fromLocalFile(dir + QStringLiteral("/contents/splash/Splash.qml"));
    } else {
        m_layout = layout;
        QVariantList frames;
        for (const QString &f : layout.frameFiles)
            frames << QUrl::fromLocalFile(dir + QLatin1Char('/') + f);
        m_plymouth = {{QStringLiteral("background"), QUrl::fromLocalFile(dir + QStringLiteral("/background.png"))},
                      {QStringLiteral("frames"), frames},
                      {QStringLiteral("seconds"), layout.seconds},
                      {QStringLiteral("centreX"), layout.centre.x()},
                      {QStringLiteral("centreY"), layout.centre.y()},
                      {QStringLiteral("module"), layout.module},
                      {QStringLiteral("hideMessages"), layout.hideMessages},
                      {QStringLiteral("bytes"), double(layout.bytes)},
                      {QStringLiteral("capBytes"), double(plymouthThemeCap)},
                      {QStringLiteral("memory"), double(layout.decodedBytes)},
                      {QStringLiteral("budgetBytes"), double(plymouthMemoryBudget)},
                      {QStringLiteral("builtWidth"), builtScreen.width()},
                      {QStringLiteral("builtHeight"), builtScreen.height()}};
        if (layout.progress) {
            const QRect r = *layout.progress;
            m_plymouth.insert(QStringLiteral("progress"), QVariantMap{{QStringLiteral("x"), r.x()}, {QStringLiteral("y"), r.y()},
                                                                        {QStringLiteral("width"), r.width()}, {QStringLiteral("height"), r.height()},
                                                                        {QStringLiteral("color"), layout.progressColour},
                                                                        {QStringLiteral("background"), layout.progressBackground}});
        }
        const auto megabytes = [](double bytes) { return QString::number(bytes / (1 << 20), 'f', 1); };
        if (layout.bytes > plymouthThemeCap)
            m_problems << tr("The theme takes %1 MB; a boot splash may take at most %2 MB, since it is copied into every initrd.")
                              .arg(megabytes(layout.bytes), megabytes(plymouthThemeCap));
        else if (layout.bytes > plymouthThemeCap * 8 / 10)
            m_warnings << tr("The theme takes %1 of the %2 MB a boot splash may take.").arg(megabytes(layout.bytes), megabytes(plymouthThemeCap));
        if (layout.decodedBytes > plymouthMemoryBudget)
            m_warnings << tr("Plymouth holds %1 MB in memory before the splash shows (about %2 MB is the budget): a shorter, smaller or slower animation starts sooner.")
                              .arg(megabytes(layout.decodedBytes), megabytes(plymouthMemoryBudget));
    }
    Q_EMIT previewChanged();
}

void DesignEditor::keepPreviews(const QString &current)
{
    // Keeps this build and the one on screen, which the preview may still show.
    if (!m_previewDir.isValid())
        return;
    for (const QString &old : QDir(m_previewDir.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (old != current && m_previewDir.filePath(old) != m_previewPackage)
            QDir(m_previewDir.filePath(old)).removeRecursively();
    }
}

void DesignEditor::setMenuEntries(const QStringList &entries)
{
    if (entries == m_menuEntries)
        return;
    m_menuEntries = entries;
    if (m_open && m_stage == Stage::BootMenu)
        m_previewTimer.start();
}

QStringList DesignEditor::menuEntries() const
{
    return m_menuEntries.isEmpty() ? QStringList{QStringLiteral("Kubuntu"), QStringLiteral("Advanced options for Kubuntu"),
                                                 QStringLiteral("Kubuntu, with Linux 7.0.0-42-generic (recovery mode)"), QStringLiteral("UEFI Firmware Settings")}
                                   : m_menuEntries;
}

void DesignEditor::setPreviewScreen(int width, int height)
{
    const QSize size(width, height);
    if (size.isEmpty() || size == m_previewScreen)
        return;
    m_previewScreen = size;
    if (m_open && m_stage == Stage::BootMenu)
        m_previewTimer.start();
}

void DesignEditor::useBuiltinIcons()
{
    QVariantMap icons = m_current.content.value(QStringLiteral("icons")).toObject().toVariantMap();
    for (const QString &c : iconClasses())
        icons.insert(c, QStringLiteral("builtin:") + builtinIconFor(c));
    setContent({{QStringLiteral("icons"), icons}});
}

QString DesignEditor::useImportIcons(const QString &importId)
{
    if (!m_open || !m_library)
        return tr("No design is open.");
    const QDir dir(m_library->importFilesPath(importId) + QStringLiteral("/icons"));
    QVariantMap icons = m_current.content.value(QStringLiteral("icons")).toObject().toVariantMap();
    int found = 0;
    for (const QString &c : iconClasses()) {
        const QString file = dir.filePath(c + QStringLiteral(".png"));
        if (!QFileInfo::exists(file))
            continue;
        QString error;
        const auto asset = m_library->addAsset(file, Asset::Kind::Image, QStringLiteral("%1 icon").arg(c), error);
        if (!asset)
            return error;
        icons.insert(c, asset->hash);
        ++found;
    }
    if (found == 0)
        return tr("That theme has no icons for %1.").arg(iconClasses().join(QStringLiteral(", ")));
    reloadLists();
    setContent({{QStringLiteral("icons"), icons}});
    return {};
}

void DesignEditor::flushBootMenuPreview(const Design &design, QStringList problems)
{
    // Built for the editor's resolution and drawn as GRUB will draw it (SPEC.md §4.5).
    const QStringList entries = menuEntries();
    GrubLayout layout;
    const QList<PayloadFile> files = buildBootMenu(design, *m_library, m_previewScreen, entries, problems, &layout);
    QList<PayloadFile> hashed = files;
    hashed << PayloadFile{QStringLiteral("entries"), entries.join(QLatin1Char('\n')).toUtf8()}
           << PayloadFile{QStringLiteral("screen"), QStringLiteral("%1x%2").arg(m_previewScreen.width()).arg(m_previewScreen.height()).toUtf8()};
    const QString hash = Library::buildHash(hashed);
    const QString dir = m_previewDir.filePath(hash);
    const QString image = dir + QStringLiteral("/preview.png");
    if (m_previewDir.isValid() && !QFileInfo::exists(image)) {
        // Uncompressed: written on every change, read once.
        if (!QDir().mkpath(dir) || !renderBootMenu(files, m_previewScreen, entries).save(image, "PNG", 100))
            problems << tr("Can't write the preview.");
    }
    keepPreviews(hash);
    m_previewPackage = dir;
    m_previewUrl.clear();
    m_plymouth.clear();
    m_problems = problems;
    m_warnings = designWarnings(design, *m_library);
    m_bootMenu = {{QStringLiteral("preview"), QUrl::fromLocalFile(image)}, {QStringLiteral("fits"), layout.fits}};

    // Each resolution: does the longest entry fit, did the menu have to widen, is a text cut off?
    double designed = 0;
    for (const QJsonValue &v : elementsOf(design.content)) {
        if (v.toObject().value(QStringLiteral("type")) == QLatin1String("menu"))
            designed = v.toObject().value(QStringLiteral("width")).toDouble();
    }
    QStringList widened;
    QString longest;
    for (const Resolution &r : resolutions) {
        const QSize size(r.width, r.height);
        GrubLayout l = layout;
        QStringList ignored; // the same as above
        if (size != m_previewScreen)
            buildBootMenu(design, *m_library, size, entries, ignored, &l, false);
        const QString at = QStringLiteral("%1p").arg(r.height);
        if (!l.fits)
            m_warnings << tr("%1: \u201c%2\u201d doesn't fit even across the whole screen, so GRUB cuts it off. A smaller menu font fixes it.").arg(at, l.longestEntry);
        else if (l.menu.width() > std::ceil(r.width * designed / 100) + 1)
            widened << QStringLiteral("%1 % at %2").arg(std::lround(100.0 * l.menu.width() / r.width)).arg(at);
        longest = l.longestEntry;
        for (const QString &text : std::as_const(l.clippedTexts))
            m_warnings << tr("%1: GRUB cuts off \u201c%2\u201d; a text is one line, clipped to its box.").arg(at, text);
    }
    // Not a fault: the menu is wider than drawn, so the longest entry fits (SPEC.md §4.5).
    if (!widened.isEmpty())
        m_warnings << tr("The menu widens to %1 so \u201c%2\u201d fits.").arg(widened.join(QStringLiteral(", ")), longest);
    Q_EMIT previewChanged();
}

void DesignEditor::flushDraft()
{
    m_draftTimer.stop();
    if (!m_open || !m_library)
        return;
    if (!dirty()) { // undone back to the saved version
        if (std::exchange(m_draftWritten, false)) {
            m_library->clearDraft(m_id);
            Q_EMIT libraryChanged();
        }
        return;
    }
    Design design = currentDesign();
    QString error;
    if (!m_library->saveDraft(design, error)) {
        m_warnings.prepend(tr("Can't keep a copy of the unsaved changes: %1").arg(error));
        Q_EMIT previewChanged();
        return;
    }
    const bool first = !std::exchange(m_draftWritten, true);
    m_id = design.id; // a new design gets its id with the first draft
    if (first) {
        Q_EMIT changed();
        Q_EMIT libraryChanged();
    }
}

Design DesignEditor::currentDesign() const
{
    Design design;
    design.id = m_id;
    design.stage = m_stage;
    design.name = m_current.name;
    design.version = m_version;
    design.content = m_current.content;
    return design;
}

void DesignEditor::reloadLists()
{
    m_assets.clear();
    m_versions.clear();
    m_shared.clear();
    if (!m_library)
        return;
    QStringList errors;
    // Shared elements, and how many saved designs link each.
    QMap<QString, int> uses;
    for (const Design &d : m_library->designs(errors)) {
        QSet<QString> linked;
        if (const QString palette = d.content.value(QStringLiteral("palette")).toString(); !palette.isEmpty())
            linked.insert(palette);
        for (const QJsonValue &v : elementsOf(d.content)) {
            if (const QString logo = v.toObject().value(QStringLiteral("shared")).toString(); !logo.isEmpty())
                linked.insert(logo);
        }
        for (const QString &id : std::as_const(linked))
            ++uses[id];
    }
    for (const SharedElement &e : m_library->sharedElements(errors)) {
        QVariantMap colours;
        for (auto it = e.colours.cbegin(); it != e.colours.cend(); ++it)
            colours.insert(it.key(), *it);
        m_shared.insert(e.id, QVariantMap{{QStringLiteral("kind"), e.kind == SharedElement::Kind::Logo ? QStringLiteral("logo") : QStringLiteral("palette")},
                                          {QStringLiteral("name"), e.name},
                                          {QStringLiteral("asset"), e.asset},
                                          {QStringLiteral("colours"), colours},
                                          {QStringLiteral("uses"), uses.value(e.id)}});
    }
    for (const Asset &a : m_library->assets(errors))
        m_assets.insert(a.hash, QVariantMap{{QStringLiteral("name"), a.name}, {QStringLiteral("url"), QUrl::fromLocalFile(m_library->assetPath(a))}});
    if (m_id.isEmpty())
        return;
    for (int n : m_library->designVersions(m_id)) {
        QString error;
        if (const auto d = m_library->design(m_id, n, error))
            m_versions.prepend(QVariantMap{{QStringLiteral("version"), n}, {QStringLiteral("savedAt"), d->savedAt.toLocalTime()}});
    }
}
