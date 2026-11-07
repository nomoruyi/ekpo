#include "genksplash.h"

#include "gencommon.h"

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
                                  "            fillMode: Image.PreserveAspectCrop\n            mipmap: true\n        }\n")
                       .arg(qmlString(b.bundle(image, Asset::Kind::Image)));
    }

    int number = 0;
    int videos = 0;
    for (const QJsonValue &value : c.value(QStringLiteral("elements")).toArray()) {
        const QJsonObject e = value.toObject();
        const QString type = e.value(QStringLiteral("type")).toString();
        ++number;
        if (type == QLatin1String("image")) {
            const QString box = b.geometry(e, number);
            content += QStringLiteral("        Image {\n") + box
                + QStringLiteral("            source: %1\n            sourceSize.width: width\n            sourceSize.height: height\n"
                                 "            fillMode: Image.PreserveAspectFit\n            mipmap: true\n        }\n")
                      .arg(qmlString(b.bundle(e.value(QStringLiteral("asset")).toString(), Asset::Kind::Image)));
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
            content += QStringLiteral("        Text {\n") + box
                + QStringLiteral("            text: %1\n            textFormat: Text.PlainText\n            color: %2\n")
                      .arg(qmlString(e.value(QStringLiteral("text")).toString()), b.color(e, QStringLiteral("color"), QStringLiteral("#ffffff")));
            if (const QString font = e.value(QStringLiteral("font")).toString(); !font.isEmpty())
                content += QStringLiteral("            font.family: %1.font.family\n").arg(b.fontId(font));
            content += QStringLiteral("            font.pixelSize: root.height * %1\n            horizontalAlignment: Text.%2\n"
                                      "            verticalAlignment: Text.AlignVCenter\n            wrapMode: Text.WordWrap\n        }\n")
                           .arg(fraction(size), aligns.value(align, QStringLiteral("AlignHCenter")));
        } else if (type == QLatin1String("spinner")) {
            const QString box = b.geometry(e, number); // problems in reading order: geometry before colour
            content += QStringLiteral("        Item {\n") + box
                + QStringLiteral("            Item {\n"
                                 "                readonly property real size: Math.min(parent.width, parent.height)\n"
                                 "                anchors.centerIn: parent\n"
                                 "                width: size\n"
                                 "                height: size\n"
                                 "                RotationAnimator on rotation {\n"
                                 "                    from: 0\n"
                                 "                    to: 360\n"
                                 "                    duration: 1200\n"
                                 "                    loops: Animation.Infinite\n"
                                 "                }\n"
                                 "                Repeater {\n"
                                 "                    model: 8\n"
                                 "                    Rectangle {\n"
                                 "                        required property int index\n"
                                 "                        readonly property real angle: index * Math.PI / 4\n"
                                 "                        width: parent.width * 0.18\n"
                                 "                        height: width\n"
                                 "                        radius: width / 2\n"
                                 "                        color: %1\n"
                                 "                        opacity: (index + 1) / 8\n"
                                 "                        x: (parent.width - width) / 2 * (1 + Math.cos(angle))\n"
                                 "                        y: (parent.height - height) / 2 * (1 + Math.sin(angle))\n"
                                 "                    }\n"
                                 "                }\n"
                                 "            }\n"
                                 "        }\n")
                      .arg(b.color(e, QStringLiteral("color"), QStringLiteral("#ffffff")));
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
        } else {
            b.problem(QStringLiteral("Element %1 has an unknown type “%2”.").arg(number).arg(type));
        }
    }

    const int fadeIn = c.value(QStringLiteral("fadeIn")).toInt(0);
    QString qml = QStringLiteral("// Generated by Ekpo from the design %1, version %2.\n"
                                 "// Plain QtQuick%5: it keeps working without Ekpo.\n"
                                 "import QtQuick\n%4\n"
                                 "Rectangle {\n"
                                 "    id: root\n"
                                 "    color: %3\n\n"
                                 "    property int stage // set by ksplashqml\n\n")
                      .arg(qmlString(design.name), QString::number(design.version), // one pass: the name may contain %2
                           b.color(background, QStringLiteral("color"), QStringLiteral("#000000")),
                           videos > 0 ? QStringLiteral("import QtMultimedia\n") : QString(),
                           videos > 0 ? QStringLiteral(" and QtMultimedia") : QString());
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
