#include "genplymouth.h"

#include "gencommon.h"

#include <QBuffer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QLinearGradient>
#include <QPainter>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

const QString themesDir = QStringLiteral("/usr/share/plymouth/themes/");
constexpr double twoStepLoop = 2.0; // THROBBER_DURATION in ply-throbber.c
constexpr int spinnerFrameCount = 60; // two-step shows at most 60 frames per loop (30 fps)
constexpr int scriptRate = 50; // refreshes per second the script asks for
const QStringList modes = {QStringLiteral("boot-up"), QStringLiteral("shutdown"), QStringLiteral("reboot"), QStringLiteral("updates"),
                           QStringLiteral("system-upgrade"), QStringLiteral("firmware-upgrade"), QStringLiteral("system-reset")};

QByteArray png(const QImage &image)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return data;
}

QString decimal(double v)
{
    return QString::number(v, 'g', 6);
}

QString hexColour(const QColor &c)
{
    return QStringLiteral("0x") + c.name(QColor::HexRgb).mid(1);
}

QString scriptColour(const QColor &c)
{
    return QStringLiteral("%1, %2, %3").arg(c.redF(), 0, 'f', 3).arg(c.greenF(), 0, 'f', 3).arg(c.blueF(), 0, 'f', 3);
}

// For key file values and script comments: one line, nothing that could start another.
QString oneLine(QString text)
{
    static const QRegularExpression control(QStringLiteral("[\\x00-\\x1f\\x7f]+"));
    return text.replace(control, QStringLiteral(" ")).trimmed();
}

QRectF pixelRect(const PercentBox &b, QSize screen)
{
    return {screen.width() * b.x / 100, screen.height() * b.y / 100, screen.width() * b.width / 100, screen.height() * b.height / 100};
}

double fitScale(QSizeF image, QSizeF box, bool cover)
{
    const double w = box.width() / image.width(), h = box.height() / image.height();
    return cover ? std::max(w, h) : std::min(w, h);
}

// Vector images are rendered at the size they get in box.
QImage readImage(const QString &file, QSizeF box, bool cover)
{
    QImageReader reader(file);
    const QByteArray format = reader.format();
    if ((format == "svg" || format == "svgz") && !box.isEmpty() && !reader.size().isEmpty())
        reader.setScaledSize((QSizeF(reader.size()) * fitScale(reader.size(), box, cover)).toSize());
    return reader.read();
}

// Fitted into box (or covering it), centred.
void drawImage(QPainter &p, const QImage &image, const QRectF &box, bool cover)
{
    if (image.isNull() || box.isEmpty())
        return;
    const QSizeF size = QSizeF(image.size()) * fitScale(image.size(), box.size(), cover);
    p.drawImage(QRectF(box.center() - QPointF(size.width() / 2, size.height() / 2), size), image);
}

// A frame as stored when it is a PNG that fits; shrunk or converted otherwise.
QByteArray frameData(const QString &file, QSizeF box)
{
    QImage image = readImage(file, box, false);
    if (image.isNull())
        return {};
    if (image.width() > box.width() + 0.5 || image.height() > box.height() + 0.5)
        return png(image.scaled(box.toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    QFile in(file);
    if (QImageReader(file).format() == "png" && in.open(QIODevice::ReadOnly))
        return in.readAll(); // keeps 8-bit palette frames small (SPEC.md §4.6)
    return png(image);
}

// The Plasma splash's dot spinner (genksplash), two turns per two-step loop.
QList<QByteArray> spinnerFrames(QColor colour, int size)
{
    QList<QByteArray> frames;
    const double dot = size * 0.18;
    for (int f = 0; f < spinnerFrameCount; ++f) {
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        const double turn = 4 * std::numbers::pi * f / spinnerFrameCount;
        for (int i = 0; i < 8; ++i) {
            const double angle = i * std::numbers::pi / 4 + turn;
            QColor c = colour;
            c.setAlphaF(colour.alphaF() * (i + 1) / 8);
            p.setBrush(c);
            p.drawEllipse(QPointF(size / 2.0 + (size - dot) / 2 * std::cos(angle), size / 2.0 + (size - dot) / 2 * std::sin(angle)), dot / 2, dot / 2);
        }
        p.end();
        frames << png(image);
    }
    return frames;
}

// two-step won't show at all without lock.png and entry.png (and bullet.png, which
// the entry loads): show_splash_screen() and view_load() in plugins/splash/two-step.
// The script below uses them too. Sizes as for 1080p, scaled by f.
QList<PayloadFile> dialogImages(double f)
{
    const QColor white(255, 255, 255, 230);
    QImage lock(qRound(24 * f), qRound(30 * f), QImage::Format_ARGB32_Premultiplied);
    lock.fill(Qt::transparent);
    {
        QPainter p(&lock);
        p.setRenderHint(QPainter::Antialiasing);
        const double w = lock.width(), h = lock.height();
        p.setPen(QPen(white, w * 0.16));
        p.drawArc(QRectF(w * 0.2, w * 0.08, w * 0.6, w * 0.7), 0, 180 * 16); // shackle
        p.drawLine(QPointF(w * 0.2, w * 0.43), QPointF(w * 0.2, h * 0.5));
        p.drawLine(QPointF(w * 0.8, w * 0.43), QPointF(w * 0.8, h * 0.5));
        p.setPen(Qt::NoPen);
        p.setBrush(white);
        p.drawRoundedRect(QRectF(0, h * 0.45, w, h * 0.55), w * 0.12, w * 0.12);
    }
    QImage entry(qRound(300 * f), qRound(32 * f), QImage::Format_ARGB32_Premultiplied);
    entry.fill(Qt::transparent);
    {
        QPainter p(&entry);
        p.setRenderHint(QPainter::Antialiasing);
        const double line = std::max(1.0, f);
        p.setPen(QPen(QColor(255, 255, 255, 170), line));
        p.setBrush(QColor(0, 0, 0, 150));
        p.drawRoundedRect(QRectF(entry.rect()).adjusted(line / 2, line / 2, -line / 2, -line / 2), 4 * f, 4 * f);
    }
    QImage bullet(qRound(10 * f), qRound(10 * f), QImage::Format_ARGB32_Premultiplied);
    bullet.fill(Qt::transparent);
    {
        QPainter p(&bullet);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(white);
        p.drawEllipse(QRectF(bullet.rect()));
    }
    return {{QStringLiteral("lock.png"), png(lock)}, {QStringLiteral("entry.png"), png(entry)}, {QStringLiteral("bullet.png"), png(bullet)}};
}

struct Animation {
    QList<QByteArray> frames;
    QPointF centre; // fractions of the screen
    QSize size; // of the largest frame
    double seconds = twoStepLoop;
};

struct Progress {
    QRect rect; // pixels
    QColor colour;
    QColor background;
};

QString scriptFile(const Design &design, const Animation &animation, const std::optional<Progress> &progress, bool hideMessages,
                   QColor top, QColor bottom, QSize screen)
{
    QString s = QStringLiteral("# Generated by Ekpo from the design %1, version %2.\n"
                               "# Plymouth's script module, for an animation that loops in %3 s (two-step's loop is 2 s).\n"
                               "# State lives in global.*; callbacks are local scopes.\n\n"
                               "Window.SetBackgroundTopColor(%4);\n"
                               "Window.SetBackgroundBottomColor(%5);\n\n"
                               "global.background_image = Image(\"background.png\");\n"
                               "global.background_sprite = Sprite(global.background_image.Scale(Window.GetWidth(), Window.GetHeight()));\n"
                               "global.background_sprite.SetPosition(Window.GetX(), Window.GetY(), -100);\n\n")
                    .arg(oneLine(design.name), QString::number(design.version), decimal(animation.seconds), scriptColour(top), scriptColour(bottom));

    // The animation: frame = ticks into the loop × frames / ticks per loop, at 50 Hz.
    for (qsizetype i = 0; i < animation.frames.size(); ++i)
        s += QStringLiteral("global.frames[%1] = Image(\"frame-%2.png\");\n").arg(i).arg(i + 1, 4, 10, QLatin1Char('0'));
    s += QStringLiteral("global.anim_count = %1;\n"
                        "global.anim_ticks = %2;\n"
                        "global.anim_tick = 0;\n"
                        "global.anim_sprite = Sprite(global.frames[0]);\n"
                        "global.anim_sprite.SetPosition(Window.GetX() + Window.GetWidth() * %3 - global.frames[0].GetWidth() / 2,\n"
                        "                               Window.GetY() + Window.GetHeight() * %4 - global.frames[0].GetHeight() / 2, 10);\n\n"
                        "fun refresh_callback ()\n"
                        "{\n"
                        "  global.anim_tick = global.anim_tick + 1;\n"
                        "  if (global.anim_tick >= global.anim_ticks)\n"
                        "    global.anim_tick = 0;\n"
                        "  global.anim_sprite.SetImage(global.frames[Math.Int(global.anim_tick * global.anim_count / global.anim_ticks)]);\n"
                        "}\n\n"
                        "Plymouth.SetRefreshRate(%5);\n"
                        "Plymouth.SetRefreshFunction(refresh_callback);\n\n")
             .arg(QString::number(animation.frames.size()), QString::number(std::max(1, int(std::lround(animation.seconds * scriptRate)))),
                  decimal(animation.centre.x()), decimal(animation.centre.y()), QString::number(scriptRate));

    if (progress) {
        s += QStringLiteral("global.progress_box = Image(\"progress-box.png\");\n"
                            "global.progress_bar = Image(\"progress-bar.png\");\n"
                            "global.progress_box_sprite = Sprite(global.progress_box);\n"
                            "global.progress_box_sprite.SetPosition(Window.GetX() + Window.GetWidth() * %1, Window.GetY() + Window.GetHeight() * %2, 5);\n"
                            "global.progress_bar_sprite = Sprite();\n"
                            "global.progress_bar_sprite.SetPosition(Window.GetX() + Window.GetWidth() * %1, Window.GetY() + Window.GetHeight() * %2, 6);\n\n"
                            "fun boot_progress_callback (duration, progress)\n"
                            "{\n"
                            "  width = Math.Int(global.progress_bar.GetWidth() * progress);\n"
                            "  if (width > 0)\n"
                            "    global.progress_bar_sprite.SetImage(global.progress_bar.Scale(width, global.progress_bar.GetHeight()));\n"
                            "}\n\n"
                            "Plymouth.SetBootProgressFunction(boot_progress_callback);\n\n")
                 .arg(decimal(double(progress->rect.x()) / screen.width()), decimal(double(progress->rect.y()) / screen.height()));
    }

    if (!hideMessages) {
        const double below = std::min(0.95, animation.centre.y() + animation.size.height() / 2.0 / screen.height() + 0.02);
        s += QStringLiteral("global.message_sprite = Sprite();\n"
                            "global.message_sprite.SetZ(20);\n\n"
                            "fun display_message_callback (text)\n"
                            "{\n"
                            "  if (text == \"\")\n"
                            "    global.message_sprite.SetOpacity(0);\n"
                            "  else\n"
                            "  {\n"
                            "    image = Image.Text(text, 1, 1, 1);\n"
                            "    global.message_sprite.SetImage(image);\n"
                            "    global.message_sprite.SetX(Window.GetX() + Window.GetWidth() / 2 - image.GetWidth() / 2);\n"
                            "    global.message_sprite.SetY(Window.GetY() + Window.GetHeight() * %1);\n"
                            "    global.message_sprite.SetOpacity(1);\n"
                            "  }\n"
                            "}\n\n"
                            "fun hide_message_callback (text)\n"
                            "{\n"
                            "  global.message_sprite.SetOpacity(0);\n"
                            "}\n\n"
                            "Plymouth.SetDisplayMessageFunction(display_message_callback);\n"
                            "Plymouth.SetHideMessageFunction(hide_message_callback);\n\n")
                 .arg(decimal(below));
    }

    // Password and question prompts always show (SPEC.md §4.6): lock and entry in the
    // middle of the screen, the prompt above, bullets or the answer inside.
    s += QStringLiteral(
        "global.lock_image = Image(\"lock.png\");\n"
        "global.entry_image = Image(\"entry.png\");\n"
        "global.entry_x = Window.GetX() + Window.GetWidth() / 2 - global.entry_image.GetWidth() / 2;\n"
        "global.entry_y = Window.GetY() + Window.GetHeight() / 2 - global.entry_image.GetHeight() / 2;\n"
        "global.dialog_lock = Sprite(global.lock_image);\n"
        "global.dialog_lock.SetPosition(global.entry_x - global.lock_image.GetWidth() * 1.5,\n"
        "                               global.entry_y + global.entry_image.GetHeight() / 2 - global.lock_image.GetHeight() / 2, 100);\n"
        "global.dialog_entry = Sprite(global.entry_image);\n"
        "global.dialog_entry.SetPosition(global.entry_x, global.entry_y, 100);\n"
        "global.dialog_prompt = Sprite();\n"
        "global.dialog_prompt.SetZ(101);\n"
        "global.dialog_text = Sprite();\n"
        "global.dialog_text.SetZ(101);\n\n"
        "fun set_dialog_opacity (opacity)\n"
        "{\n"
        "  global.dialog_lock.SetOpacity(opacity);\n"
        "  global.dialog_entry.SetOpacity(opacity);\n"
        "  global.dialog_prompt.SetOpacity(opacity);\n"
        "  global.dialog_text.SetOpacity(opacity);\n"
        "}\n\n"
        "fun show_dialog (prompt, text)\n"
        "{\n"
        "  set_dialog_opacity(1);\n"
        "  if (prompt == \"\")\n"
        "    global.dialog_prompt.SetOpacity(0);\n"
        "  else\n"
        "  {\n"
        "    image = Image.Text(prompt, 1, 1, 1);\n"
        "    global.dialog_prompt.SetImage(image);\n"
        "    global.dialog_prompt.SetX(Window.GetX() + Window.GetWidth() / 2 - image.GetWidth() / 2);\n"
        "    global.dialog_prompt.SetY(global.entry_y - image.GetHeight() * 2);\n"
        "  }\n"
        "  if (text == \"\")\n"
        "    global.dialog_text.SetOpacity(0);\n"
        "  else\n"
        "  {\n"
        "    image = Image.Text(text, 1, 1, 1);\n"
        "    global.dialog_text.SetImage(image);\n"
        "    global.dialog_text.SetX(global.entry_x + global.entry_image.GetHeight() / 2);\n"
        "    global.dialog_text.SetY(global.entry_y + global.entry_image.GetHeight() / 2 - image.GetHeight() / 2);\n"
        "  }\n"
        "}\n\n"
        "fun display_normal_callback ()\n"
        "{\n"
        "  set_dialog_opacity(0);\n"
        "}\n\n"
        "fun display_password_callback (prompt, bullets)\n"
        "{\n"
        "  text = \"\";\n"
        "  count = Math.Min(bullets, 40);\n"
        "  while (count > 0)\n"
        "  {\n"
        "    text = text + \"•\";\n"
        "    count = count - 1;\n"
        "  }\n"
        "  show_dialog(prompt, text);\n"
        "}\n\n"
        "fun display_question_callback (prompt, entry)\n"
        "{\n"
        "  show_dialog(prompt, entry);\n"
        "}\n\n"
        "set_dialog_opacity(0);\n"
        "Plymouth.SetDisplayNormalFunction(display_normal_callback);\n"
        "Plymouth.SetDisplayPasswordFunction(display_password_callback);\n"
        "Plymouth.SetDisplayQuestionFunction(display_question_callback);\n");
    return s;
}

} // namespace

QList<PayloadFile> buildPlymouthSplash(const Design &design, const QString &themeId, const Library &library, QSize screen, QStringList &problems,
                                       PlymouthLayout *layout)
{
    QStringList errors;
    QMap<QString, Asset> assets;
    for (const Asset &a : library.assets(errors))
        assets.insert(a.hash, a);
    problems << errors;
    const auto assetFile = [&](const QString &hash, Asset::Kind kind) -> QString {
        const auto it = assets.constFind(hash);
        if (it != assets.cend() && it->kind == kind)
            return library.assetPath(*it);
        problems << QStringLiteral("The design uses %1 that is no longer in the library.")
                        .arg(kind == Asset::Kind::Font ? QStringLiteral("a font") : QStringLiteral("an image"));
        return {};
    };

    const QJsonObject &content = design.content;
    const QJsonObject backgroundJson = content.value(QStringLiteral("background")).toObject();
    QColor top(readColour(backgroundJson, QStringLiteral("color"), QStringLiteral("#000000"), problems));
    QColor bottom = backgroundJson.contains(QStringLiteral("color2"))
        ? QColor(readColour(backgroundJson, QStringLiteral("color2"), QStringLiteral("#000000"), problems))
        : top;
    top.setAlpha(255);
    bottom.setAlpha(255);

    QImage background(screen, QImage::Format_RGB32);
    QPainter painter(&background);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
    if (top == bottom) {
        painter.fillRect(background.rect(), top);
    } else {
        const QLineF line = gradientLine(screen, gradientAngle(backgroundJson));
        QLinearGradient gradient(line.p1(), line.p2());
        gradient.setColorAt(0, top);
        gradient.setColorAt(1, bottom);
        painter.fillRect(background.rect(), gradient);
    }
    if (const QString hash = backgroundJson.value(QStringLiteral("image")).toString(); !hash.isEmpty()) {
        if (const QString file = assetFile(hash, Asset::Kind::Image); !file.isEmpty())
            drawImage(painter, readImage(file, screen, true), background.rect(), true);
    }

    std::optional<Animation> animation;
    std::optional<Progress> progress;
    static const QStringList types = {QStringLiteral("image"), QStringLiteral("text"), QStringLiteral("spinner"), QStringLiteral("frames"),
                                      QStringLiteral("progress")};
    int number = 0;
    for (const QJsonValue &value : content.value(QStringLiteral("elements")).toArray()) {
        const QJsonObject e = value.toObject();
        const QString type = e.value(QStringLiteral("type")).toString();
        ++number;
        if (!types.contains(type)) {
            problems << QStringLiteral("Element %1 has an unknown type “%2”.").arg(number).arg(type);
            continue;
        }
        const auto box = readBox(e, number, problems);
        if (!box)
            continue;
        const QRectF rect = pixelRect(*box, screen);

        if (type == QLatin1String("image")) {
            if (const QString file = assetFile(e.value(QStringLiteral("asset")).toString(), Asset::Kind::Image); !file.isEmpty())
                drawImage(painter, readImage(file, rect.size(), false), rect, false);
        } else if (type == QLatin1String("text")) {
            static const QMap<QString, Qt::Alignment> aligns = {{QStringLiteral("left"), Qt::AlignLeft},
                                                                {QStringLiteral("center"), Qt::AlignHCenter},
                                                                {QStringLiteral("right"), Qt::AlignRight}};
            const QString align = e.value(QStringLiteral("align")).toString(QStringLiteral("center"));
            if (!aligns.contains(align))
                problems << QStringLiteral("Element %1 has an unknown alignment “%2”.").arg(number).arg(align);
            const double size = e.value(QStringLiteral("size")).toDouble(3);
            if (!(size > 0))
                problems << QStringLiteral("Element %1 has no valid text size.").arg(number);
            QFont font = QGuiApplication::font();
            if (const QString hash = e.value(QStringLiteral("font")).toString(); !hash.isEmpty()) {
                if (const QString file = assetFile(hash, Asset::Kind::Font); !file.isEmpty()) {
                    const QString family = registerFont(file);
                    if (family.isEmpty())
                        problems << QStringLiteral("The font “%1” can't be loaded.").arg(assets.value(hash).name);
                    else
                        font.setFamily(family);
                }
            }
            font.setPixelSize(std::max(1, int(screen.height() * size / 100))); // as QML truncates it
            painter.setFont(font);
            painter.setPen(QColor(readColour(e, QStringLiteral("color"), QStringLiteral("#ffffff"), problems)));
            painter.drawText(rect, int(Qt::TextWordWrap | Qt::AlignVCenter | aligns.value(align, Qt::AlignHCenter)), e.value(QStringLiteral("text")).toString());
        } else if (type == QLatin1String("progress")) {
            if (progress) {
                problems << QStringLiteral("Element %1 is a second progress bar; a splash has one.").arg(number);
                continue;
            }
            QRect pixels = rect.toRect();
            pixels.setSize(pixels.size().expandedTo(QSize(1, 1)));
            progress = Progress{pixels, QColor(readColour(e, QStringLiteral("color"), QStringLiteral("#ffffff"), problems)),
                                QColor(readColour(e, QStringLiteral("background"), QStringLiteral("#404040"), problems))};
        } else { // spinner or frames
            if (animation) {
                problems << QStringLiteral("Element %1 is a second animation; a splash has one.").arg(number);
                continue;
            }
            Animation a;
            a.centre = QPointF((box->x + box->width / 2) / 100, (box->y + box->height / 2) / 100);
            if (type == QLatin1String("spinner")) {
                const int size = std::max(1, int(std::min(rect.width(), rect.height())));
                a.frames = spinnerFrames(QColor(readColour(e, QStringLiteral("color"), QStringLiteral("#ffffff"), problems)), size);
                a.size = QSize(size, size);
            } else {
                const QJsonArray list = e.value(QStringLiteral("frames")).toArray();
                if (list.isEmpty())
                    problems << QStringLiteral("Element %1 has no frames.").arg(number);
                for (const QJsonValue &frame : list) {
                    const QString file = assetFile(frame.toString(), Asset::Kind::Image);
                    const QByteArray data = file.isEmpty() ? QByteArray() : frameData(file, rect.size());
                    if (!file.isEmpty() && data.isEmpty())
                        problems << QStringLiteral("A frame of element %1 can't be read.").arg(number);
                    a.size = a.size.expandedTo(QImage::fromData(data).size());
                    a.frames << data;
                }
                a.seconds = e.value(QStringLiteral("seconds")).toDouble(list.size() / 30.0);
                if (!(a.seconds > 0) || !std::isfinite(a.seconds))
                    problems << QStringLiteral("Element %1 has no valid loop length.").arg(number);
            }
            animation = a;
        }
    }
    painter.end();

    const bool hideMessages = content.value(QStringLiteral("hideMessages")).toBool();
    const bool script = animation && std::abs(animation->seconds - twoStepLoop) > 0.01;
    const QString dir = themesDir + themeId;
    QList<PayloadFile> files = dialogImages(screen.height() / 1080.0);
    files << PayloadFile{QStringLiteral("background.png"), png(background)};
    QString theme = QStringLiteral("[Plymouth Theme]\nName=%1\nDescription=Boot splash made with Ekpo\nModuleName=%2\n\n")
                        .arg(oneLine(design.name), script ? QStringLiteral("script") : QStringLiteral("two-step"));

    if (script) {
        for (qsizetype i = 0; i < animation->frames.size(); ++i)
            files << PayloadFile{QStringLiteral("frame-%1.png").arg(i + 1, 4, 10, QLatin1Char('0')), animation->frames[i]};
        if (progress) {
            QImage box(progress->rect.size(), QImage::Format_RGB32);
            box.fill(progress->background);
            QImage bar(progress->rect.size(), QImage::Format_RGB32);
            bar.fill(progress->colour);
            files << PayloadFile{QStringLiteral("progress-box.png"), png(box)} << PayloadFile{QStringLiteral("progress-bar.png"), png(bar)};
        }
        files << PayloadFile{QStringLiteral("splash.script"), scriptFile(design, *animation, progress, hideMessages, top, bottom, screen).toUtf8()};
        theme += QStringLiteral("[script]\nImageDir=%1\nScriptFile=%1/splash.script\n").arg(dir);
    } else {
        if (animation) {
            for (qsizetype i = 0; i < animation->frames.size(); ++i)
                files << PayloadFile{QStringLiteral("throbber-%1.png").arg(i + 1, 4, 10, QLatin1Char('0')), animation->frames[i]};
        }
        theme += QStringLiteral("[two-step]\n"
                                "ImageDir=%1\n"
                                "ScaleBackgroundImage=true\n"
                                "BackgroundStartColor=%2\n"
                                "BackgroundEndColor=%3\n"
                                "Transition=none\n"
                                "TransitionDuration=0.0\n"
                                "DialogHorizontalAlignment=0.5\n"
                                "DialogVerticalAlignment=0.5\n"
                                "MessageBelowAnimation=true\n")
                     .arg(dir, hexColour(top), hexColour(bottom));
        if (animation) // two-step centres the throbber on these fractions of the screen
            theme += QStringLiteral("HorizontalAlignment=%1\nVerticalAlignment=%2\n").arg(decimal(animation->centre.x()), decimal(animation->centre.y()));
        if (progress) { // and places the bar at alignment × (screen − bar)
            const QRect r = progress->rect;
            const auto align = [](int offset, int free) { return free > 0 ? double(offset) / free : 0.5; };
            theme += QStringLiteral("ProgressBarBackgroundColor=%1\nProgressBarForegroundColor=%2\n"
                                    "ProgressBarHorizontalAlignment=%3\nProgressBarVerticalAlignment=%4\n"
                                    "ProgressBarWidth=%5\nProgressBarHeight=%6\n")
                         .arg(hexColour(progress->background), hexColour(progress->colour), decimal(align(r.x(), screen.width() - r.width())),
                              decimal(align(r.y(), screen.height() - r.height())), QString::number(r.width()), QString::number(r.height()));
        }
        for (const QString &mode : modes) {
            theme += QStringLiteral("\n[%1]\nUseAnimation=%2\nUseProgressBar=%3\nUseEndAnimation=false\nSuppressMessages=%4\n")
                         .arg(mode, animation ? QStringLiteral("true") : QStringLiteral("false"), progress ? QStringLiteral("true") : QStringLiteral("false"),
                              hideMessages ? QStringLiteral("true") : QStringLiteral("false"));
        }
    }
    files << PayloadFile{themeId + QStringLiteral(".plymouth"), theme.toUtf8()};
    std::sort(files.begin(), files.end(), [](const PayloadFile &a, const PayloadFile &b) { return a.path < b.path; });

    if (layout) {
        *layout = PlymouthLayout{};
        layout->module = script ? QStringLiteral("script") : QStringLiteral("two-step");
        layout->hideMessages = hideMessages;
        // Plymouth decodes every frame and keeps the background twice (as loaded, as scaled).
        layout->decodedBytes = 2 * qint64(screen.width()) * screen.height() * 4;
        if (animation) {
            for (qsizetype i = 0; i < animation->frames.size(); ++i) {
                layout->frameFiles << QStringLiteral("%1-%2.png").arg(script ? QStringLiteral("frame") : QStringLiteral("throbber")).arg(i + 1, 4, 10, QLatin1Char('0'));
                const QSize size = QImage::fromData(animation->frames[i]).size();
                layout->frameDecodedBytes += qint64(size.width()) * size.height() * 4;
                layout->frameBytes += animation->frames[i].size();
            }
            layout->decodedBytes += layout->frameDecodedBytes;
            layout->frameSize = animation->size;
            layout->seconds = animation->seconds;
            layout->centre = animation->centre;
        }
        if (progress) {
            layout->progress = progress->rect;
            layout->progressColour = progress->colour;
            layout->progressBackground = progress->background;
        }
        for (const PayloadFile &f : files)
            layout->bytes += f.data.size();
    }
    return files;
}
