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

// The spinner as the Plasma splash draws it (gencommon.h), in two-step's 2 s loop:
// one, two or three turns. An image style turns the image, fitted so its corners stay
// inside the square.
QList<QByteArray> spinnerFrames(const QString &style, QColor colour, int size, int turns, const QImage &image)
{
    QList<QByteArray> frames;
    for (int f = 0; f < spinnerFrameCount; ++f) {
        QImage frame(size, size, QImage::Format_ARGB32_Premultiplied);
        frame.fill(Qt::transparent);
        QPainter p(&frame);
        const double phase = std::fmod(double(turns) * f / spinnerFrameCount, 1.0);
        if (style == QLatin1String("image")) {
            const double k = size / std::hypot(image.width(), image.height());
            p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
            p.translate(size / 2.0, size / 2.0);
            p.rotate(360 * phase);
            p.drawImage(QRectF(-image.width() * k / 2, -image.height() * k / 2, image.width() * k, image.height() * k), image);
        } else {
            paintSpinner(p, style, colour, size, phase);
        }
        p.end();
        frames << png(frame);
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

// An image the script module moves (gencommon.h); drawn at its own size.
struct AnimatedImage {
    QByteArray data;
    QPointF centre; // fractions of the screen
    QSize size;
    QString animation;
};

struct Progress {
    QRect rect; // pixels
    QColor colour;
    QColor background;
    QString style; // flat, rounded, glow, segmented
};

constexpr int progressSegments = 10;

// A styled bar's images: the track (progress-box.png) and either the fill's ends and
// stretched middle (progress-left/middle/right.png, glow around them) or one segment.
QList<PayloadFile> progressImages(const Progress &bar, ProgressShape &shape)
{
    const int w = bar.rect.width(), h = bar.rect.height();
    const auto blank = [](int width, int height) {
        QImage image(std::max(1, width), std::max(1, height), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        return image;
    };
    QImage track = blank(w, h);
    QPainter p(&track);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(bar.background);
    if (bar.style == QLatin1String("segmented")) {
        shape.segments = progressSegments;
        shape.gap = std::max(2, h / 2);
        shape.segmentWidth = std::max(1, (w - (progressSegments - 1) * shape.gap) / progressSegments);
        const double radius = std::min(h, shape.segmentWidth) / 4.0;
        for (int i = 0; i < progressSegments; ++i)
            p.drawRoundedRect(QRectF(i * (shape.segmentWidth + shape.gap), 0, shape.segmentWidth, h), radius, radius);
        p.end();
        QImage segment = blank(shape.segmentWidth, h);
        p.begin(&segment);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(bar.colour);
        p.drawRoundedRect(QRectF(segment.rect()), radius, radius);
        p.end();
        return {{QStringLiteral("progress-box.png"), png(track)}, {QStringLiteral("progress-segment.png"), png(segment)}};
    }
    // Round ends; glow: a soft halo of the bar's colour, as wide as the bar is high.
    const int r = h / 2, m = bar.style == QLatin1String("glow") ? std::max(2, h) : 0;
    shape.caps = r;
    shape.margin = m;
    p.drawRoundedRect(QRectF(track.rect()), h / 2.0, h / 2.0);
    p.end();
    QImage fill = blank(2 * r + 1 + 2 * m, h + 2 * m);
    p.begin(&fill);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    for (int k = m; k > 0; --k) { // layers that add up towards the bar
        QColor halo = bar.colour;
        halo.setAlphaF(bar.colour.alphaF() * 0.6 / m);
        p.setBrush(halo);
        p.drawRoundedRect(QRectF(m - k, m - k, 2 * r + 1 + 2 * k, h + 2 * k), r + k, r + k);
    }
    p.setBrush(bar.colour);
    p.drawRoundedRect(QRectF(m, m, 2 * r + 1, h), h / 2.0, h / 2.0);
    p.end();
    return {{QStringLiteral("progress-box.png"), png(track)},
            {QStringLiteral("progress-left.png"), png(fill.copy(0, 0, m + r, fill.height()))},
            {QStringLiteral("progress-middle.png"), png(fill.copy(m + r, 0, 1, fill.height()))},
            {QStringLiteral("progress-right.png"), png(fill.copy(m + r + 1, 0, r + m, fill.height()))}};
}

QString animationScript(const Animation &animation);
QString imagesScript(const QList<AnimatedImage> &images);
QString progressScript(const Progress &progress, const ProgressShape &shape, QSize screen);
QString messageScript(double below);
QString dialogScript();

QString scriptFile(const Design &design, const std::optional<Animation> &animation, const QList<AnimatedImage> &images,
                   const std::optional<Progress> &progress, const ProgressShape &shape, bool hideMessages, QColor top, QColor bottom, QSize screen)
{
    QStringList why;
    if (animation && std::abs(animation->seconds - twoStepLoop) > 0.01)
        why << QStringLiteral("an animation that loops in %1 s (two-step's loop is 2 s)").arg(decimal(animation->seconds));
    if (progress && progress->style != QLatin1String("flat"))
        why << QStringLiteral("a %1 progress bar").arg(progress->style);
    if (!images.isEmpty())
        why << QStringLiteral("moving images");
    QString s = QStringLiteral("# Generated by Ekpo from the design %1, version %2.\n"
                               "# Plymouth's script module, for %3.\n"
                               "# State lives in global.*; callbacks are local scopes.\n\n"
                               "Window.SetBackgroundTopColor(%4);\n"
                               "Window.SetBackgroundBottomColor(%5);\n\n"
                               "global.background_image = Image(\"background.png\");\n"
                               "global.background_sprite = Sprite(global.background_image.Scale(Window.GetWidth(), Window.GetHeight()));\n"
                               "global.background_sprite.SetPosition(Window.GetX(), Window.GetY(), -100);\n\n")
                    .arg(oneLine(design.name), QString::number(design.version), why.join(QLatin1String(" and ")), scriptColour(top), scriptColour(bottom));
    if (animation)
        s += animationScript(*animation);
    if (!images.isEmpty())
        s += imagesScript(images);
    if (animation || !images.isEmpty()) {
        s += QStringLiteral("global.ticks = 0;\n\n"
                            "fun refresh_callback ()\n"
                            "{\n"
                            "  global.ticks = global.ticks + 1;\n");
        if (animation)
            s += QStringLiteral("  animation_tick();\n");
        if (!images.isEmpty())
            s += QStringLiteral("  images_tick(global.ticks / %1);\n").arg(scriptRate);
        s += QStringLiteral("}\n\n"
                            "Plymouth.SetRefreshRate(%1);\n"
                            "Plymouth.SetRefreshFunction(refresh_callback);\n\n")
                 .arg(scriptRate);
    }
    if (progress)
        s += progressScript(*progress, shape, screen);
    if (!hideMessages) {
        const double below = animation ? std::min(0.95, animation->centre.y() + animation->size.height() / 2.0 / screen.height() + 0.02) : 0.75;
        s += messageScript(below);
    }
    return s + dialogScript();
}

// The animation: frame = ticks into the loop × frames / ticks per loop, at 50 Hz.
QString animationScript(const Animation &animation)
{
    QString s;
    for (qsizetype i = 0; i < animation.frames.size(); ++i)
        s += QStringLiteral("global.frames[%1] = Image(\"frame-%2.png\");\n").arg(i).arg(i + 1, 4, 10, QLatin1Char('0'));
    s += QStringLiteral("global.anim_count = %1;\n"
                        "global.anim_ticks = %2;\n"
                        "global.anim_tick = 0;\n"
                        "global.anim_sprite = Sprite(global.frames[0]);\n"
                        "global.anim_sprite.SetPosition(Window.GetX() + Window.GetWidth() * %3 - global.frames[0].GetWidth() / 2,\n"
                        "                               Window.GetY() + Window.GetHeight() * %4 - global.frames[0].GetHeight() / 2, 10);\n\n"
                        "fun animation_tick ()\n"
                        "{\n"
                        "  global.anim_tick = global.anim_tick + 1;\n"
                        "  if (global.anim_tick >= global.anim_ticks)\n"
                        "    global.anim_tick = 0;\n"
                        "  global.anim_sprite.SetImage(global.frames[Math.Int(global.anim_tick * global.anim_count / global.anim_ticks)]);\n"
                        "}\n\n")
             .arg(QString::number(animation.frames.size()), QString::number(std::max(1, int(std::lround(animation.seconds * scriptRate)))),
                  decimal(animation.centre.x()), decimal(animation.centre.y()));
    return s;
}

// Moving images, t seconds after the splash started (gencommon.h): fade and slide in
// once; breathe, pulse and spin loop. Scale and Rotate make a new image each tick.
QString imagesScript(const QList<AnimatedImage> &images)
{
    QString s, tick;
    for (qsizetype i = 0; i < images.size(); ++i) {
        const AnimatedImage &image = images[i];
        const QString n = QString::number(i + 1);
        s += QStringLiteral("global.image%1 = Image(\"image-%1.png\");\n"
                            "global.image%1_x = Window.GetX() + Window.GetWidth() * %2;\n"
                            "global.image%1_y = Window.GetY() + Window.GetHeight() * %3;\n"
                            "global.image%1_sprite = Sprite(global.image%1);\n"
                            "global.image%1_sprite.SetPosition(global.image%1_x - global.image%1.GetWidth() / 2, global.image%1_y - global.image%1.GetHeight() / 2, 2);\n")
                 .arg(n, decimal(image.centre.x()), decimal(image.centre.y()));
        const QString seconds = decimal(imageAnimationSeconds(image.animation));
        if (image.animation == QLatin1String("fade")) {
            s += QStringLiteral("global.image%1_sprite.SetOpacity(0);\n").arg(n);
            tick += QStringLiteral("  global.image%1_sprite.SetOpacity(Math.Min(1, t / %2));\n").arg(n, seconds);
        } else if (image.animation == QLatin1String("breathe")) {
            tick += QStringLiteral("  global.image%1_sprite.SetOpacity(0.7 + 0.3 * Math.Cos(t * 2 * Math.Pi / %2));\n").arg(n, seconds);
        } else if (image.animation == QLatin1String("pulse")) {
            tick += QStringLiteral("  scale = 0.96 + 0.04 * Math.Cos(t * 2 * Math.Pi / %2);\n"
                                   "  image = global.image%1.Scale(global.image%1.GetWidth() * scale, global.image%1.GetHeight() * scale);\n"
                                   "  global.image%1_sprite.SetImage(image);\n"
                                   "  global.image%1_sprite.SetPosition(global.image%1_x - image.GetWidth() / 2, global.image%1_y - image.GetHeight() / 2, 2);\n")
                        .arg(n, seconds);
        } else if (image.animation == QLatin1String("slide")) { // up from 5 % of the screen lower, easing out
            s += QStringLiteral("global.image%1_sprite.SetOpacity(0);\n").arg(n);
            tick += QStringLiteral("  p = Math.Min(1, t / %2);\n"
                                   "  global.image%1_sprite.SetY(global.image%1_y - global.image%1.GetHeight() / 2 + Window.GetHeight() * 0.05 * (1 - p) * (1 - p) * (1 - p));\n"
                                   "  global.image%1_sprite.SetOpacity(p);\n")
                        .arg(n, seconds);
        } else { // spin: the image is square, with room for its corners
            tick += QStringLiteral("  global.image%1_sprite.SetImage(global.image%1.Rotate(t * 2 * Math.Pi / %2));\n").arg(n, seconds);
        }
    }
    return s + QStringLiteral("\nfun images_tick (t)\n{\n") + tick + QStringLiteral("}\n\n");
}

QString progressScript(const Progress &progress, const ProgressShape &shape, QSize screen)
{
    const QString x = decimal(double(progress.rect.x()) / screen.width()), y = decimal(double(progress.rect.y()) / screen.height());
    if (progress.style == QLatin1String("segmented")) {
        QString s = QStringLiteral("global.progress_x = Window.GetX() + Window.GetWidth() * %1;\n"
                                   "global.progress_y = Window.GetY() + Window.GetHeight() * %2;\n"
                                   "global.progress_box_sprite = Sprite(Image(\"progress-box.png\"));\n"
                                   "global.progress_box_sprite.SetPosition(global.progress_x, global.progress_y, 5);\n"
                                   "global.progress_segment = Image(\"progress-segment.png\");\n")
                        .arg(x, y);
        for (int i = 0; i < shape.segments; ++i)
            s += QStringLiteral("global.progress_segments[%1] = Sprite(global.progress_segment);\n"
                                "global.progress_segments[%1].SetPosition(global.progress_x + %2, global.progress_y, 6);\n"
                                "global.progress_segments[%1].SetOpacity(0);\n")
                     .arg(i)
                     .arg(i * (shape.segmentWidth + shape.gap));
        return s
            + QStringLiteral("\nfun boot_progress_callback (duration, progress)\n"
                             "{\n"
                             "  count = Math.Int(%1 * progress);\n"
                             "  i = 0;\n"
                             "  while (i < %1)\n"
                             "  {\n"
                             "    if (i < count)\n"
                             "      global.progress_segments[i].SetOpacity(1);\n"
                             "    else\n"
                             "      global.progress_segments[i].SetOpacity(0);\n"
                             "    i = i + 1;\n"
                             "  }\n"
                             "}\n\n"
                             "Plymouth.SetBootProgressFunction(boot_progress_callback);\n\n")
                  .arg(shape.segments);
    }
    if (progress.style != QLatin1String("flat")) { // rounded, glow: ends, and the middle stretched between them
        return QStringLiteral("global.progress_x = Window.GetX() + Window.GetWidth() * %1;\n"
                              "global.progress_y = Window.GetY() + Window.GetHeight() * %2;\n"
                              "global.progress_box_sprite = Sprite(Image(\"progress-box.png\"));\n"
                              "global.progress_box_sprite.SetPosition(global.progress_x, global.progress_y, 5);\n"
                              "global.progress_middle = Image(\"progress-middle.png\");\n"
                              "global.progress_left_sprite = Sprite(Image(\"progress-left.png\"));\n"
                              "global.progress_left_sprite.SetPosition(global.progress_x - %3, global.progress_y - %3, 6);\n"
                              "global.progress_left_sprite.SetOpacity(0);\n"
                              "global.progress_middle_sprite = Sprite();\n"
                              "global.progress_middle_sprite.SetPosition(global.progress_x + %4, global.progress_y - %3, 6);\n"
                              "global.progress_right_sprite = Sprite(Image(\"progress-right.png\"));\n"
                              "global.progress_right_sprite.SetZ(6);\n"
                              "global.progress_right_sprite.SetOpacity(0);\n\n"
                              "fun boot_progress_callback (duration, progress)\n"
                              "{\n"
                              "  width = Math.Int(%5 * progress);\n"
                              "  if (width > %6)\n"
                              "  {\n"
                              "    global.progress_middle_sprite.SetImage(global.progress_middle.Scale(width - %6, global.progress_middle.GetHeight()));\n"
                              "    global.progress_right_sprite.SetPosition(global.progress_x + width - %4, global.progress_y - %3, 6);\n"
                              "    global.progress_left_sprite.SetOpacity(1);\n"
                              "    global.progress_right_sprite.SetOpacity(1);\n"
                              "  }\n"
                              "}\n\n"
                              "Plymouth.SetBootProgressFunction(boot_progress_callback);\n\n")
            .arg(x, y)
            .arg(shape.margin)
            .arg(shape.caps)
            .arg(progress.rect.width())
            .arg(2 * shape.caps);
    }
    // flat: the whole bar, scaled to the progress
    return QStringLiteral("global.progress_box = Image(\"progress-box.png\");\n"
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
        .arg(x, y);
}

QString messageScript(double below)
{
    return QStringLiteral("global.message_sprite = Sprite();\n"
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
QString dialogScript()
{
    return QStringLiteral(
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
    QList<AnimatedImage> movingImages;
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
            const QString file = assetFile(e.value(QStringLiteral("asset")).toString(), Asset::Kind::Image);
            const QString moving = e.value(QStringLiteral("animation")).toString();
            if (!moving.isEmpty() && !imageAnimations.contains(moving))
                problems << QStringLiteral("Element %1 has an unknown animation “%2”.").arg(number).arg(moving);
            if (file.isEmpty())
                continue;
            if (moving.isEmpty() || !imageAnimations.contains(moving)) {
                drawImage(painter, readImage(file, rect.size(), false), rect, false);
                continue;
            }
            // Its own file, fitted to its box; a spinning one on a square with room for its corners.
            QImage image = readImage(file, rect.size(), false);
            if (image.isNull()) {
                problems << QStringLiteral("Element %1's image can't be read.").arg(number);
                continue;
            }
            const QSizeF fitted = QSizeF(image.size()) * fitScale(image.size(), rect.size(), false);
            if (moving == QLatin1String("spin")) {
                const int side = int(std::ceil(std::hypot(fitted.width(), fitted.height())));
                QImage square(side, side, QImage::Format_ARGB32_Premultiplied);
                square.fill(Qt::transparent);
                QPainter p(&square);
                p.setRenderHint(QPainter::SmoothPixmapTransform);
                p.drawImage(QRectF((side - fitted.width()) / 2, (side - fitted.height()) / 2, fitted.width(), fitted.height()), image);
                p.end();
                image = square;
            } else {
                image = image.scaled(fitted.toSize().expandedTo(QSize(1, 1)), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
            movingImages << AnimatedImage{png(image), QPointF((box->x + box->width / 2) / 100, (box->y + box->height / 2) / 100), image.size(), moving};
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
            const QColor colour(readColour(e, QStringLiteral("color"), QStringLiteral("#ffffff"), problems));
            drawStyledText(painter, rect, int(Qt::TextWordWrap | Qt::AlignVCenter | aligns.value(align, Qt::AlignHCenter)), e.value(QStringLiteral("text")).toString(),
                           colour, readTextEffects(e, problems));
        } else if (type == QLatin1String("progress")) {
            if (progress) {
                problems << QStringLiteral("Element %1 is a second progress bar; a splash has one.").arg(number);
                continue;
            }
            QRect pixels = rect.toRect();
            pixels.setSize(pixels.size().expandedTo(QSize(1, 1)));
            progress = Progress{pixels, QColor(readColour(e, QStringLiteral("color"), QStringLiteral("#ffffff"), problems)),
                                QColor(readColour(e, QStringLiteral("background"), QStringLiteral("#404040"), problems)),
                                e.value(QStringLiteral("style")).toString(QStringLiteral("flat"))};
            static const QStringList styles = {QStringLiteral("flat"), QStringLiteral("rounded"), QStringLiteral("glow"), QStringLiteral("segmented")};
            if (!styles.contains(progress->style)) {
                problems << QStringLiteral("Element %1 has an unknown progress style “%2”.").arg(number).arg(progress->style);
                progress->style = QStringLiteral("flat");
            }
        } else { // spinner or frames
            if (animation) {
                problems << QStringLiteral("Element %1 is a second animation; a splash has one.").arg(number);
                continue;
            }
            Animation a;
            a.centre = QPointF((box->x + box->width / 2) / 100, (box->y + box->height / 2) / 100);
            if (type == QLatin1String("spinner")) {
                const int size = std::max(1, int(std::min(rect.width(), rect.height())));
                const QString style = e.value(QStringLiteral("style")).toString(QStringLiteral("dots"));
                const double seconds = spinnerSeconds(e.value(QStringLiteral("speed")).toString());
                if (!spinnerStyles.contains(style))
                    problems << QStringLiteral("Element %1 has an unknown spinner style “%2”.").arg(number).arg(style);
                if (seconds <= 0)
                    problems << QStringLiteral("Element %1 has an unknown speed “%2”.").arg(number).arg(e.value(QStringLiteral("speed")).toString());
                QImage image;
                if (style == QLatin1String("image")) {
                    if (const QString hash = e.value(QStringLiteral("asset")).toString(); !hash.isEmpty())
                        if (const QString file = assetFile(hash, Asset::Kind::Image); !file.isEmpty())
                            image = readImage(file, QSizeF(size, size), false);
                    if (image.isNull()) {
                        problems << QStringLiteral("Element %1 has no image to turn.").arg(number);
                        continue;
                    }
                }
                const int turns = seconds > 0 ? std::max(1, int(std::lround(twoStepLoop / seconds))) : 2;
                a.frames = spinnerFrames(style, QColor(readColour(e, QStringLiteral("color"), QStringLiteral("#ffffff"), problems)), size, turns, image);
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
    // two-step loops in 2 s and draws a plain bar; anything else needs the script module.
    const bool script = (animation && std::abs(animation->seconds - twoStepLoop) > 0.01) || (progress && progress->style != QLatin1String("flat"))
        || !movingImages.isEmpty();
    const QString dir = themesDir + themeId;
    QList<PayloadFile> files = dialogImages(screen.height() / 1080.0);
    files << PayloadFile{QStringLiteral("background.png"), png(background)};
    QString theme = QStringLiteral("[Plymouth Theme]\nName=%1\nDescription=Boot splash made with Ekpo\nModuleName=%2\n\n")
                        .arg(oneLine(design.name), script ? QStringLiteral("script") : QStringLiteral("two-step"));

    ProgressShape shape;
    if (script) {
        for (qsizetype i = 0; animation && i < animation->frames.size(); ++i)
            files << PayloadFile{QStringLiteral("frame-%1.png").arg(i + 1, 4, 10, QLatin1Char('0')), animation->frames[i]};
        if (progress && progress->style == QLatin1String("flat")) {
            QImage box(progress->rect.size(), QImage::Format_RGB32);
            box.fill(progress->background);
            QImage bar(progress->rect.size(), QImage::Format_RGB32);
            bar.fill(progress->colour);
            files << PayloadFile{QStringLiteral("progress-box.png"), png(box)} << PayloadFile{QStringLiteral("progress-bar.png"), png(bar)};
        } else if (progress) {
            files << progressImages(*progress, shape);
        }
        for (qsizetype i = 0; i < movingImages.size(); ++i)
            files << PayloadFile{QStringLiteral("image-%1.png").arg(i + 1), movingImages[i].data};
        files << PayloadFile{QStringLiteral("splash.script"), scriptFile(design, animation, movingImages, progress, shape, hideMessages, top, bottom, screen).toUtf8()};
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
        for (qsizetype i = 0; i < movingImages.size(); ++i) {
            const AnimatedImage &image = movingImages[i];
            layout->images << PlymouthImage{QStringLiteral("image-%1.png").arg(i + 1), image.centre, image.size, image.animation};
            layout->decodedBytes += qint64(image.size.width()) * image.size.height() * 4;
        }
        if (progress) {
            layout->progress = progress->rect;
            layout->progressColour = progress->colour;
            layout->progressBackground = progress->background;
            layout->progressStyle = progress->style;
            layout->progressShape = shape;
        }
        for (const PayloadFile &f : files)
            layout->bytes += f.data.size();
    }
    return files;
}
