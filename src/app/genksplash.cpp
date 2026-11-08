#include "genksplash.h"

#include "gencommon.h"

#include <QBuffer>
#include <QPainter>
#include <QRadialGradient>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <algorithm>

namespace {

// A QML (JavaScript) string literal.
QString qmlString(const QString &text)
{
    QString out = QStringLiteral("\"");
    for (const QChar c : text) {
        switch (c.unicode()) {
        case u'\\':
            out += QStringLiteral("\\\\");
            break;
        case u'"':
            out += QStringLiteral("\\\"");
            break;
        case u'\n':
            out += QStringLiteral("\\n");
            break;
        case u'\r':
            out += QStringLiteral("\\r");
            break;
        case u'\t':
            out += QStringLiteral("\\t");
            break;
        case 0x2028:
            out += QStringLiteral("\\u2028");
            break;
        case 0x2029:
            out += QStringLiteral("\\u2029");
            break;
        default:
            out += c;
        }
    }
    return out + QLatin1Char('"');
}

QString fraction(double percent)
{
    return QString::number(percent / 100.0, 'g', 6);
}

QByteArray png(const QImage &image)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return data;
}

// Dark edges, stretched over the screen: a radial gradient plain QtQuick can't draw.
QImage vignette()
{
    QImage image(480, 270, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    QRadialGradient fade(QPointF(240, 135), 275);
    fade.setColorAt(0.45, QColor(0, 0, 0, 0));
    fade.setColorAt(1, QColor(0, 0, 0, 170));
    p.fillRect(image.rect(), fade);
    return image;
}

// Film grain: grey noise, tiled. The same every build (a fixed seed).
QImage grain()
{
    QImage image(256, 256, QImage::Format_ARGB32_Premultiplied);
    quint32 seed = 0x2545f491;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
            const int v = int(seed & 0xff);
            image.setPixel(x, y, qPremultiply(qRgba(v, v, v, 255)));
        }
    }
    return image;
}

// Over the background image or colour, under the elements (SPEC.md §4.7).
QString backgroundEffects(const QJsonObject &background, QList<PayloadFile> &files, QStringList &problems)
{
    QString qml;
    const QString particles = background.value(QStringLiteral("particles")).toString();
    if (particles == QLatin1String("snow")) { // flakes falling and drifting, each at its own pace
        qml += QStringLiteral("        Repeater {\n"
                              "            model: 70\n"
                              "            Rectangle {\n"
                              "                readonly property real size: root.height * (0.002 + Math.random() * 0.004)\n"
                              "                readonly property real startX: Math.random() * root.width\n"
                              "                property real fall: Math.random()\n"
                              "                width: size\n"
                              "                height: size\n"
                              "                radius: size / 2\n"
                              "                color: \"white\"\n"
                              "                opacity: 0.4 + Math.random() * 0.5\n"
                              "                x: startX + Math.sin(fall * 6.283 * 2) * root.width * 0.01\n"
                              "                y: -size + fall * (root.height + size)\n"
                              "                NumberAnimation on fall {\n"
                              "                    from: 0\n"
                              "                    to: 1\n"
                              "                    duration: 9000 + Math.random() * 9000\n"
                              "                    loops: Animation.Infinite\n"
                              "                }\n"
                              "            }\n"
                              "        }\n");
    } else if (particles == QLatin1String("stars")) { // twinkling in place
        qml += QStringLiteral("        Repeater {\n"
                              "            model: 90\n"
                              "            Rectangle {\n"
                              "                readonly property real size: root.height * (0.001 + Math.random() * 0.002)\n"
                              "                readonly property int pace: 1200 + Math.random() * 2400\n"
                              "                x: Math.random() * root.width\n"
                              "                y: Math.random() * root.height\n"
                              "                width: size\n"
                              "                height: size\n"
                              "                radius: size / 2\n"
                              "                color: \"white\"\n"
                              "                SequentialAnimation on opacity {\n"
                              "                    loops: Animation.Infinite\n"
                              "                    NumberAnimation {\n"
                              "                        from: 0.2\n"
                              "                        to: 1\n"
                              "                        duration: pace\n"
                              "                        easing.type: Easing.InOutSine\n"
                              "                    }\n"
                              "                    NumberAnimation {\n"
                              "                        from: 1\n"
                              "                        to: 0.2\n"
                              "                        duration: pace\n"
                              "                        easing.type: Easing.InOutSine\n"
                              "                    }\n"
                              "                }\n"
                              "            }\n"
                              "        }\n");
    } else if (!particles.isEmpty()) {
        problems << QStringLiteral("The background has unknown particles “%1”.").arg(particles);
    }
    if (background.value(QStringLiteral("vignette")).toBool()) {
        files << PayloadFile{QStringLiteral("contents/splash/images/vignette.png"), png(vignette())};
        qml += QStringLiteral("        Image {\n            anchors.fill: parent\n            source: \"images/vignette.png\"\n            smooth: true\n        }\n");
    }
    if (background.value(QStringLiteral("grain")).toBool()) { // jittered, so it looks alive
        files << PayloadFile{QStringLiteral("contents/splash/images/grain.png"), png(grain())};
        qml += QStringLiteral("        Image {\n"
                              "            id: grain\n"
                              "            x: -256\n"
                              "            y: -256\n"
                              "            width: parent.width + 512\n"
                              "            height: parent.height + 512\n"
                              "            source: \"images/grain.png\"\n"
                              "            fillMode: Image.Tile\n"
                              "            opacity: 0.06\n"
                              "            Timer {\n"
                              "                interval: 80\n"
                              "                repeat: true\n"
                              "                running: true\n"
                              "                onTriggered: {\n"
                              "                    grain.x = -Math.random() * 256;\n"
                              "                    grain.y = -Math.random() * 256;\n"
                              "                }\n"
                              "            }\n"
                              "        }\n");
    }
    return qml;
}

class Builder
{
public:
    Builder(const Library &library, QStringList &problems, QMap<QString, QString> *links)
        : m_library(library)
        , m_problems(problems)
        , m_links(links)
    {
        QStringList errors;
        for (const Asset &a : library.assets(errors))
            m_assets.insert(a.hash, a);
        m_problems << errors;
    }

    QList<PayloadFile> files;
    QStringList fontLoaders;

    // Puts the asset (and its licence next to it) into the package; returns its
    // path relative to Splash.qml, or empty after adding a problem.
    QString bundle(const QString &hash, Asset::Kind kind)
    {
        static const QMap<Asset::Kind, std::pair<QString, QString>> kinds = {// what, folder
                                                                             {Asset::Kind::Font, {QStringLiteral("font"), QStringLiteral("fonts")}},
                                                                             {Asset::Kind::Image, {QStringLiteral("image"), QStringLiteral("images")}},
                                                                             {Asset::Kind::Video, {QStringLiteral("video"), QStringLiteral("videos")}}};
        const auto [what, dir] = kinds.value(kind);
        const auto it = m_assets.constFind(hash);
        if (it == m_assets.cend() || it->kind != kind) {
            m_problems << QStringLiteral("The design uses %1 %2 that is no longer in the library.")
                              .arg(kind == Asset::Kind::Image ? QStringLiteral("an") : QStringLiteral("a"), what);
            return {};
        }
        const QString rel = dir + QLatin1Char('/') + QFileInfo(m_library.assetPath(*it)).fileName();
        if (m_bundled.contains(splashDir + rel))
            return rel;
        if (!add(splashDir + rel, *it))
            return {};
        if (!it->licence.isEmpty()) {
            const auto licence = m_assets.constFind(it->licence);
            if (licence == m_assets.cend() || !add(splashDir + dir + QLatin1Char('/') + hash + QStringLiteral(".licence.txt"), *licence)) {
                m_problems << QStringLiteral("The licence of the %1 “%2” is no longer in the library.").arg(what, it->name);
                return {};
            }
        }
        return rel;
    }

    // The FontLoader id for a font asset, declared once.
    QString fontId(const QString &hash)
    {
        if (const auto known = m_fontIds.constFind(hash); known != m_fontIds.cend())
            return *known;
        const QString rel = bundle(hash, Asset::Kind::Font);
        if (rel.isEmpty())
            return {};
        const QString id = QStringLiteral("font%1").arg(m_fontIds.size() + 1);
        m_fontIds.insert(hash, id);
        fontLoaders << QStringLiteral("    FontLoader {\n        id: %1\n        source: %2\n    }\n").arg(id, qmlString(rel));
        return id;
    }

    QString color(const QJsonObject &o, const QString &key, const QString &fallback)
    {
        return qmlString(readColour(o, key, fallback, m_problems));
    }

    // x, y, width and height as fractions of the screen.
    QString geometry(const QJsonObject &o, int number)
    {
        const auto box = readBox(o, number, m_problems);
        if (!box)
            return {};
        return QStringLiteral("            x: root.width * %1\n            y: root.height * %2\n"
                              "            width: root.width * %3\n            height: root.height * %4\n")
            .arg(fraction(box->x), fraction(box->y), fraction(box->width), fraction(box->height));
    }

    void problem(const QString &text) { m_problems << text; }
    QStringList &problems() { return m_problems; }

private:
    static inline const QString splashDir = QStringLiteral("contents/splash/");

    bool add(const QString &path, const Asset &asset)
    {
        m_bundled.insert(path);
        if (m_links && QFileInfo::exists(m_library.assetPath(asset))) {
            m_links->insert(path, m_library.assetPath(asset));
            return true;
        }
        QFile f(m_library.assetPath(asset));
        if (!f.open(QIODevice::ReadOnly)) {
            m_problems << QStringLiteral("Can't read %1: %2").arg(f.fileName(), f.errorString());
            return false;
        }
        files << PayloadFile{path, f.readAll()};
        return true;
    }

    const Library &m_library;
    QStringList &m_problems;
    QMap<QString, QString> *m_links;
    QSet<QString> m_bundled;
    QMap<QString, Asset> m_assets;
    QMap<QString, QString> m_fontIds;
};

// An image's animation (gencommon.h), as genplymouth's script plays it.
QString imageAnimation(const QString &animation)
{
    const QString ms = QString::number(std::lround(imageAnimationSeconds(animation) * 1000));
    if (animation == QLatin1String("fade"))
        return QStringLiteral("            OpacityAnimator on opacity {\n                from: 0\n                to: 1\n                duration: %1\n            }\n").arg(ms);
    if (animation == QLatin1String("slide")) // up from 5 % of the screen lower
        return QStringLiteral("            OpacityAnimator on opacity {\n                from: 0\n                to: 1\n                duration: %1\n            }\n"
                              "            transform: Translate {\n"
                              "                NumberAnimation on y {\n"
                              "                    from: root.height * 0.05\n"
                              "                    to: 0\n"
                              "                    duration: %1\n"
                              "                    easing.type: Easing.OutCubic\n"
                              "                }\n"
                              "            }\n")
            .arg(ms);
    if (animation == QLatin1String("breathe") || animation == QLatin1String("pulse")) {
        const bool pulse = animation == QLatin1String("pulse");
        return QStringLiteral("            SequentialAnimation on %1 {\n"
                              "                loops: Animation.Infinite\n"
                              "                NumberAnimation {\n"
                              "                    from: 1\n"
                              "                    to: %2\n"
                              "                    duration: %3\n"
                              "                    easing.type: Easing.InOutSine\n"
                              "                }\n"
                              "                NumberAnimation {\n"
                              "                    from: %2\n"
                              "                    to: 1\n"
                              "                    duration: %3\n"
                              "                    easing.type: Easing.InOutSine\n"
                              "                }\n"
                              "            }\n")
            .arg(pulse ? QStringLiteral("scale") : QStringLiteral("opacity"), pulse ? QStringLiteral("0.92") : QStringLiteral("0.4"),
                 QString::number(std::lround(imageAnimationSeconds(animation) * 500)));
    }
    if (animation == QLatin1String("spin"))
        return QStringLiteral("            RotationAnimator on rotation {\n                from: 0\n                to: 360\n                duration: %1\n"
                              "                loops: Animation.Infinite\n            }\n")
            .arg(ms);
    return {};
}

} // namespace

QList<PayloadFile> buildPlasmaSplash(const Design &design, const QString &packageId, const Library &library, QStringList &problems,
                                     QMap<QString, QString> *links)
{
    Builder b(library, problems, links);
    const QJsonObject &c = design.content;
    const QJsonObject background = c.value(QStringLiteral("background")).toObject();

    QString content;
    if (const QString image = background.value(QStringLiteral("image")).toString(); !image.isEmpty()) {
        content += QStringLiteral("        Image {\n            anchors.fill: parent\n            source: %1\n"
                                  "            sourceSize.width: width\n            sourceSize.height: height\n"
                                  "            fillMode: Image.PreserveAspectCrop\n            mipmap: true\n")
                       .arg(qmlString(b.bundle(image, Asset::Kind::Image)));
        if (background.value(QStringLiteral("motion")).toBool()) // a slow pan and zoom, back and forth
            content += QStringLiteral("            transform: Translate {\n"
                                      "                SequentialAnimation on x {\n"
                                      "                    loops: Animation.Infinite\n"
                                      "                    NumberAnimation {\n"
                                      "                        from: -root.width * 0.02\n"
                                      "                        to: root.width * 0.02\n"
                                      "                        duration: 20000\n"
                                      "                        easing.type: Easing.InOutSine\n"
                                      "                    }\n"
                                      "                    NumberAnimation {\n"
                                      "                        from: root.width * 0.02\n"
                                      "                        to: -root.width * 0.02\n"
                                      "                        duration: 20000\n"
                                      "                        easing.type: Easing.InOutSine\n"
                                      "                    }\n"
                                      "                }\n"
                                      "            }\n"
                                      "            SequentialAnimation on scale {\n"
                                      "                loops: Animation.Infinite\n"
                                      "                NumberAnimation {\n"
                                      "                    from: 1.06\n"
                                      "                    to: 1.14\n"
                                      "                    duration: 20000\n"
                                      "                    easing.type: Easing.InOutSine\n"
                                      "                }\n"
                                      "                NumberAnimation {\n"
                                      "                    from: 1.14\n"
                                      "                    to: 1.06\n"
                                      "                    duration: 20000\n"
                                      "                    easing.type: Easing.InOutSine\n"
                                      "                }\n"
                                      "            }\n");
        content += QStringLiteral("        }\n");
    }
    content += backgroundEffects(background, b.files, problems);

    int number = 0;
    int videos = 0;
    int animations = 0;
    for (const QJsonValue &value : c.value(QStringLiteral("elements")).toArray()) {
        const QJsonObject e = value.toObject();
        const QString type = e.value(QStringLiteral("type")).toString();
        ++number;
        if (type == QLatin1String("image")) {
            const QString box = b.geometry(e, number);
            const QString moving = e.value(QStringLiteral("animation")).toString();
            if (!moving.isEmpty() && !imageAnimations.contains(moving))
                b.problem(QStringLiteral("Element %1 has an unknown animation “%2”.").arg(number).arg(moving));
            content += QStringLiteral("        Image {\n") + box
                + QStringLiteral("            source: %1\n            sourceSize.width: width\n            sourceSize.height: height\n"
                                 "            fillMode: Image.PreserveAspectFit\n            mipmap: true\n")
                      .arg(qmlString(b.bundle(e.value(QStringLiteral("asset")).toString(), Asset::Kind::Image)))
                + imageAnimation(moving) + QStringLiteral("        }\n");
        } else if (type == QLatin1String("text")) {
            static const QMap<QString, QString> aligns = {{QStringLiteral("left"), QStringLiteral("AlignLeft")},
                                                          {QStringLiteral("center"), QStringLiteral("AlignHCenter")},
                                                          {QStringLiteral("right"), QStringLiteral("AlignRight")}};
            const QString align = e.value(QStringLiteral("align")).toString(QStringLiteral("center"));
            if (!aligns.contains(align))
                b.problem(QStringLiteral("Element %1 has an unknown alignment “%2”.").arg(number).arg(align));
            const double size = e.value(QStringLiteral("size")).toDouble(3);
            if (!(size > 0))
                b.problem(QStringLiteral("Element %1 has no valid text size.").arg(number));
            const QString box = b.geometry(e, number);
            const QString head = box + QStringLiteral("            text: %1\n            textFormat: Text.PlainText\n").arg(qmlString(e.value(QStringLiteral("text")).toString()));
            const QString colour = b.color(e, QStringLiteral("color"), QStringLiteral("#ffffff"));
            QString tail;
            if (const QString font = e.value(QStringLiteral("font")).toString(); !font.isEmpty())
                tail += QStringLiteral("            font.family: %1.font.family\n").arg(b.fontId(font));
            tail += QStringLiteral("            font.pixelSize: root.height * %1\n            horizontalAlignment: Text.%2\n"
                                   "            verticalAlignment: Text.AlignVCenter\n            wrapMode: Text.WordWrap\n")
                        .arg(fraction(size), aligns.value(align, QStringLiteral("AlignHCenter")));
            // Effects as genplymouth draws them (gencommon.h), in plain QtQuick: copies
            // underneath. A glow is three rings of 8 faint copies (a blur needs
            // QtQuick.Effects), a shadow one copy, an outline 8 copies around.
            const TextEffects effects = readTextEffects(e, b.problems());
            const auto copy = [&](const QString &id, const QString &colour, const QString &more) {
                return QStringLiteral("        Text {\n            id: %1\n").arg(id) + head + QStringLiteral("            color: %1\n").arg(colour) + tail + more
                    + QStringLiteral("        }\n");
            };
            const QString n = QString::number(number);
            const auto repeated = [](int count, QString text) {
                return QStringLiteral("        Repeater {\n            model: %1\n").arg(count)
                    + text.replace(QLatin1String("\n        "), QLatin1String("\n            ")).prepend(QLatin1String("    ")) + QStringLiteral("        }\n");
            };
            if (effects.glow.isValid())
                content += repeated(24, copy(QStringLiteral("glow") + n, qmlString(effects.glow.name(QColor::HexArgb)),
                                             QStringLiteral("            required property int index\n"
                                                            "            readonly property real reach: font.pixelSize * 0.3 * (Math.floor(index / 8) + 1) / 3\n"
                                                            "            readonly property real angle: (index % 8 + Math.floor(index / 8) / 2) * Math.PI / 4\n"
                                                            "            opacity: 0.14\n"
                                                            "            transform: Translate {\n"
                                                            "                x: glow%1.reach * Math.cos(glow%1.angle)\n"
                                                            "                y: glow%1.reach * Math.sin(glow%1.angle)\n"
                                                            "            }\n")
                                                 .arg(n)));
            if (effects.shadow.isValid())
                content += copy(QStringLiteral("shadow") + n, qmlString(effects.shadow.name(QColor::HexArgb)),
                                QStringLiteral("            transform: Translate {\n"
                                               "                x: Math.max(1, shadow%1.font.pixelSize * 0.06)\n"
                                               "                y: x\n"
                                               "            }\n")
                                    .arg(n));
            if (effects.outline.isValid())
                content += repeated(8, copy(QStringLiteral("outline") + n, qmlString(effects.outline.name(QColor::HexArgb)),
                                            QStringLiteral("            required property int index\n"
                                                           "            transform: Translate {\n"
                                                           "                x: Math.max(1, outline%1.font.pixelSize * 0.05) * Math.cos(outline%1.index * Math.PI / 4)\n"
                                                           "                y: Math.max(1, outline%1.font.pixelSize * 0.05) * Math.sin(outline%1.index * Math.PI / 4)\n"
                                                           "            }\n")
                                                .arg(n)));
            content += QStringLiteral("        Text {\n") + head + QStringLiteral("            color: %1\n").arg(colour) + tail + QStringLiteral("        }\n");
        } else if (type == QLatin1String("spinner")) {
            const QString box = b.geometry(e, number); // problems in reading order: geometry before colour
            const QString style = e.value(QStringLiteral("style")).toString(QStringLiteral("dots"));
            const double seconds = spinnerSeconds(e.value(QStringLiteral("speed")).toString());
            if (!spinnerStyles.contains(style))
                b.problem(QStringLiteral("Element %1 has an unknown spinner style “%2”.").arg(number).arg(style));
            if (seconds <= 0)
                b.problem(QStringLiteral("Element %1 has an unknown speed “%2”.").arg(number).arg(e.value(QStringLiteral("speed")).toString()));
            const QString duration = QString::number(std::lround((seconds > 0 ? seconds : 1) * 1000));
            content += QStringLiteral("        Item {\n") + box;
            if (style == QLatin1String("image")) { // turns, fitted so its corners stay inside the square
                const QString hash = e.value(QStringLiteral("asset")).toString();
                if (hash.isEmpty())
                    b.problem(QStringLiteral("Element %1 has no image to turn.").arg(number));
                content += QStringLiteral("            Image {\n"
                                          "                readonly property real size: Math.min(parent.width, parent.height)\n"
                                          "                readonly property real fit: size / Math.max(1, Math.hypot(implicitWidth, implicitHeight))\n"
                                          "                anchors.centerIn: parent\n"
                                          "                width: implicitWidth * fit\n"
                                          "                height: implicitHeight * fit\n"
                                          "                source: %1\n"
                                          "                mipmap: true\n"
                                          "                RotationAnimator on rotation {\n"
                                          "                    from: 0\n"
                                          "                    to: 360\n"
                                          "                    duration: %2\n"
                                          "                    loops: Animation.Infinite\n"
                                          "                }\n"
                                          "            }\n"
                                          "        }\n")
                               .arg(qmlString(hash.isEmpty() ? QString() : b.bundle(hash, Asset::Kind::Image)), duration);
            } else { // drawn as genplymouth draws it (gencommon.h)
                const QString script = spinnerScript(style).split(QLatin1Char('\n'), Qt::SkipEmptyParts).join(QLatin1String("\n                    "));
                content += QStringLiteral("            Canvas {\n"
                                          "                readonly property color tint: %1\n"
                                          "                property real phase: 0\n"
                                          "                anchors.centerIn: parent\n"
                                          "                width: Math.min(parent.width, parent.height)\n"
                                          "                height: width\n"
                                          "                onPhaseChanged: requestPaint()\n"
                                          "                NumberAnimation on phase {\n"
                                          "                    from: 0\n"
                                          "                    to: 1\n"
                                          "                    duration: %2\n"
                                          "                    loops: Animation.Infinite\n"
                                          "                }\n"
                                          "                onPaint: {\n"
                                          "                    %3\n"
                                          "                }\n"
                                          "            }\n"
                                          "        }\n")
                               .arg(b.color(e, QStringLiteral("color"), QStringLiteral("#ffffff")), duration, script);
            }
        } else if (type == QLatin1String("video")) {
            // QtMultimedia plays it on the GPU; silent, looping (SPEC.md §4.7).
            static const QMap<QString, QString> fills = {{QStringLiteral("crop"), QStringLiteral("PreserveAspectCrop")},
                                                         {QStringLiteral("fit"), QStringLiteral("PreserveAspectFit")}};
            const QString fill = e.value(QStringLiteral("fill")).toString(QStringLiteral("crop"));
            if (!fills.contains(fill))
                b.problem(QStringLiteral("Element %1 has an unknown fill “%2”.").arg(number).arg(fill));
            const QString box = b.geometry(e, number);
            const QString id = QStringLiteral("video%1").arg(++videos);
            content += QStringLiteral("        VideoOutput {\n            id: %1\n").arg(id) + box
                + QStringLiteral("            fillMode: VideoOutput.%1\n        }\n").arg(fills.value(fill, QStringLiteral("PreserveAspectCrop")))
                + QStringLiteral("        MediaPlayer {\n            source: Qt.resolvedUrl(%1)\n            videoOutput: %2\n"
                                 "            loops: MediaPlayer.Infinite\n            Component.onCompleted: play()\n        }\n")
                      .arg(qmlString(b.bundle(e.value(QStringLiteral("asset")).toString(), Asset::Kind::Video)), id);
        } else if (type == QLatin1String("progress")) {
            // ksplashqml counts Plasma's start from stage 1 and quits at 6 (splashapp.cpp).
            const QString box = b.geometry(e, number);
            const QString background = b.color(e, QStringLiteral("background"), QStringLiteral("#404040"));
            const QString colour = b.color(e, QStringLiteral("color"), QStringLiteral("#ffffff"));
            const QString style = e.value(QStringLiteral("style")).toString(QStringLiteral("flat"));
            static const QStringList styles = {QStringLiteral("flat"), QStringLiteral("rounded"), QStringLiteral("glow"), QStringLiteral("segmented")};
            if (!styles.contains(style))
                b.problem(QStringLiteral("Element %1 has an unknown progress style “%2”.").arg(number).arg(style));
            if (style == QLatin1String("segmented")) { // one segment per stage
                content += QStringLiteral("        Row {\n") + box
                    + QStringLiteral("            spacing: height * 0.5\n"
                                     "            Repeater {\n"
                                     "                model: 6\n"
                                     "                Rectangle {\n"
                                     "                    required property int index\n"
                                     "                    width: (parent.width - parent.spacing * 5) / 6\n"
                                     "                    height: parent.height\n"
                                     "                    radius: Math.min(width, height) / 4\n"
                                     "                    color: index < Math.min(root.stage, 6) ? %2 : %1\n"
                                     "                    Behavior on color {\n"
                                     "                        ColorAnimation {\n"
                                     "                            duration: 300\n"
                                     "                        }\n"
                                     "                    }\n"
                                     "                }\n"
                                     "            }\n"
                                     "        }\n")
                          .arg(background, colour);
                continue;
            }
            const QString radius = style == QLatin1String("flat") ? QStringLiteral("0") : QStringLiteral("height / 2");
            content += QStringLiteral("        Rectangle {\n") + box
                + QStringLiteral("            color: %1\n"
                                 "            radius: %2\n"
                                 "            Item {\n"
                                 "                width: parent.width * Math.min(root.stage, 6) / 6\n"
                                 "                height: parent.height\n"
                                 "                Behavior on width {\n"
                                 "                    NumberAnimation {\n"
                                 "                        duration: 400\n"
                                 "                    }\n"
                                 "                }\n")
                      .arg(background, radius);
            if (style == QLatin1String("glow")) // a soft halo: layers that add up towards the bar
                content += QStringLiteral("                Repeater {\n"
                                          "                    model: 4\n"
                                          "                    Rectangle {\n"
                                          "                        required property int index\n"
                                          "                        readonly property real spread: parent.height * (index + 1) / 4\n"
                                          "                        x: -spread\n"
                                          "                        y: -spread\n"
                                          "                        width: parent.width + 2 * spread\n"
                                          "                        height: parent.height + 2 * spread\n"
                                          "                        radius: height / 2\n"
                                          "                        color: %1\n"
                                          "                        opacity: 0.15\n"
                                          "                    }\n"
                                          "                }\n")
                               .arg(colour);
            content += QStringLiteral("                Rectangle {\n"
                                      "                    anchors.fill: parent\n"
                                      "                    radius: %2\n"
                                      "                    color: %1\n"
                                      "                }\n")
                           .arg(colour, radius);
            if (e.value(QStringLiteral("shimmer")).toBool()) // a light sweeping along the filled part
                content += QStringLiteral("                Item {\n"
                                          "                    anchors.fill: parent\n"
                                          "                    clip: true\n"
                                          "                    Rectangle {\n"
                                          "                        property real phase: 0\n"
                                          "                        width: Math.max(parent.height * 4, parent.width * 0.25)\n"
                                          "                        height: parent.height\n"
                                          "                        x: -width + (parent.width + width) * phase\n"
                                          "                        gradient: Gradient {\n"
                                          "                            orientation: Gradient.Horizontal\n"
                                          "                            GradientStop {\n"
                                          "                                position: 0\n"
                                          "                                color: \"#00ffffff\"\n"
                                          "                            }\n"
                                          "                            GradientStop {\n"
                                          "                                position: 0.5\n"
                                          "                                color: \"#66ffffff\"\n"
                                          "                            }\n"
                                          "                            GradientStop {\n"
                                          "                                position: 1\n"
                                          "                                color: \"#00ffffff\"\n"
                                          "                            }\n"
                                          "                        }\n"
                                          "                        NumberAnimation on phase {\n"
                                          "                            from: 0\n"
                                          "                            to: 1\n"
                                          "                            duration: 1600\n"
                                          "                            loops: Animation.Infinite\n"
                                          "                        }\n"
                                          "                    }\n"
                                          "                }\n");
            content += QStringLiteral("            }\n        }\n");
        } else if (type == QLatin1String("frames")) {
            // Frame by frame, looping in `seconds` (e.g. the boot splash's animation).
            const QString box = b.geometry(e, number);
            QStringList sources;
            for (const QJsonValue &frame : e.value(QStringLiteral("frames")).toArray())
                sources << qmlString(b.bundle(frame.toString(), Asset::Kind::Image));
            if (sources.isEmpty())
                b.problem(QStringLiteral("Element %1 has no frames.").arg(number));
            const double seconds = e.value(QStringLiteral("seconds")).toDouble(sources.size() / 30.0);
            if (!(seconds > 0))
                b.problem(QStringLiteral("Element %1 has no valid loop length.").arg(number));
            const QString id = QStringLiteral("frames%1").arg(++animations);
            content += QStringLiteral("        Image {\n            id: %1\n").arg(id) + box
                + QStringLiteral("            readonly property var frames: [%1]\n"
                                 "            property int frame: 0\n"
                                 "            source: frames[frame]\n"
                                 "            sourceSize.width: width\n"
                                 "            sourceSize.height: height\n"
                                 "            fillMode: Image.PreserveAspectFit\n"
                                 "            mipmap: true\n"
                                 "            Timer {\n"
                                 "                interval: %2\n"
                                 "                repeat: true\n"
                                 "                running: %3.frames.length > 1\n"
                                 "                onTriggered: %3.frame = (%3.frame + 1) % %3.frames.length\n"
                                 "            }\n"
                                 "        }\n")
                      .arg(sources.join(QStringLiteral(", ")), QString::number(std::max(16, int(std::lround(seconds * 1000 / std::max<qsizetype>(1, sources.size()))))), id);
        } else {
            b.problem(QStringLiteral("Element %1 has an unknown type “%2”.").arg(number).arg(type));
        }
    }

    const int fadeIn = c.value(QStringLiteral("fadeIn")).toInt(0);
    // A gradient from color to color2 at its angle, as in the other stages. Plain
    // QtQuick only does vertical and horizontal ones: other angles turn a rectangle
    // with a vertical gradient, sized as CSS sizes the gradient line.
    QString gradient;
    if (background.contains(QStringLiteral("color2"))) {
        const QString stops = QStringLiteral("GradientStop {\n            position: 0\n            color: %1\n        }\n"
                                             "        GradientStop {\n            position: 1\n            color: %2\n        }\n")
                                  .arg(b.color(background, QStringLiteral("color"), QStringLiteral("#000000")),
                                       b.color(background, QStringLiteral("color2"), QStringLiteral("#000000")));
        if (const double angle = gradientAngle(background); angle == 180) {
            gradient = QStringLiteral("    gradient: Gradient {\n        %1    }\n").arg(stops);
        } else {
            gradient = QStringLiteral("    clip: true\n\n"
                                      "    // The gradient at %1°.\n"
                                      "    Rectangle {\n"
                                      "        readonly property real angle: %1 * Math.PI / 180\n"
                                      "        anchors.centerIn: parent\n"
                                      "        width: Math.abs(parent.width * Math.cos(angle)) + Math.abs(parent.height * Math.sin(angle))\n"
                                      "        height: Math.abs(parent.width * Math.sin(angle)) + Math.abs(parent.height * Math.cos(angle))\n"
                                      "        rotation: %2\n"
                                      "        gradient: Gradient {\n        %3    }\n"
                                      "    }\n")
                           .arg(QString::number(angle), QString::number(angle - 180), QString(stops).replace(QLatin1String("\n        "), QLatin1String("\n            ")));
        }
    }
    QStringList extraModules;
    if (videos > 0)
        extraModules << QStringLiteral("QtMultimedia");
    QString qml = QStringLiteral("// Generated by Ekpo from the design %1, version %2.\n"
                                 "// Plain QtQuick%5: it keeps working without Ekpo.\n"
                                 "import QtQuick\n%4\n"
                                 "Rectangle {\n"
                                 "    id: root\n"
                                 "    color: %3\n%6\n"
                                 "    property int stage // set by ksplashqml\n\n")
                      .arg(qmlString(design.name), QString::number(design.version), // one pass: the name may contain %2
                           b.color(background, QStringLiteral("color"), QStringLiteral("#000000")),
                           extraModules.isEmpty() ? QString() : QStringLiteral("import %1\n").arg(extraModules.join(QLatin1String("\nimport "))),
                           extraModules.isEmpty() ? QString() : QStringLiteral(" and %1").arg(extraModules.join(QLatin1String(" and "))), gradient);
    qml += b.fontLoaders.join(QString());
    qml += QStringLiteral("    Item {\n        anchors.fill: parent\n");
    if (fadeIn > 0)
        qml += QStringLiteral("        OpacityAnimator on opacity {\n            from: 0\n            to: 1\n            duration: %1\n        }\n").arg(fadeIn);
    qml += content + QStringLiteral("    }\n}\n");

    const QJsonObject metadata{
        {QStringLiteral("KPackageStructure"), QStringLiteral("Plasma/LookAndFeel")},
        {QStringLiteral("KPlugin"),
         QJsonObject{
             {QStringLiteral("Authors"), QJsonArray{QJsonObject{{QStringLiteral("Name"), c.value(QStringLiteral("author")).toString(QStringLiteral("nomoruyi"))}}}},
             {QStringLiteral("Description"), QStringLiteral("Plasma splash made with Ekpo")},
             {QStringLiteral("Id"), packageId},
             {QStringLiteral("License"), c.value(QStringLiteral("licence")).toString(QStringLiteral("MIT"))},
             {QStringLiteral("Name"), design.name},
             {QStringLiteral("Version"), QString::number(design.version)},
         }},
        {QStringLiteral("X-Plasma-APIVersion"), QStringLiteral("2")},
    };

    QList<PayloadFile> files = b.files;
    files << PayloadFile{QStringLiteral("metadata.json"), QJsonDocument(metadata).toJson()}
          << PayloadFile{QStringLiteral("contents/splash/Splash.qml"), qml.toUtf8()};
    std::sort(files.begin(), files.end(), [](const PayloadFile &a, const PayloadFile &b) { return a.path < b.path; });
    return files;
}
