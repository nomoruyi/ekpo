#include "media.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <algorithm>
#include <utility>

std::optional<VideoInfo> probeVideo(const QString &file, QString &error)
{
    QProcess ffprobe;
    ffprobe.start(QStringLiteral("ffprobe"), {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                                              QStringLiteral("-show_entries"), QStringLiteral("stream=width,height,avg_frame_rate,duration:format=duration"),
                                              QStringLiteral("-of"), QStringLiteral("json"), file});
    if (!ffprobe.waitForFinished(15000) || ffprobe.exitStatus() != QProcess::NormalExit) {
        error = ffprobe.error() == QProcess::FailedToStart ? MediaJob::tr("ffprobe isn't installed (it comes with ffmpeg).")
                                                           : MediaJob::tr("ffprobe couldn't read %1.").arg(QFileInfo(file).fileName());
        ffprobe.kill();
        ffprobe.waitForFinished();
        return std::nullopt;
    }
    const QJsonObject json = QJsonDocument::fromJson(ffprobe.readAllStandardOutput()).object();
    const QJsonObject stream = json.value(QStringLiteral("streams")).toArray().at(0).toObject();
    VideoInfo info;
    info.width = stream.value(QStringLiteral("width")).toInt();
    info.height = stream.value(QStringLiteral("height")).toInt();
    const QStringList rate = stream.value(QStringLiteral("avg_frame_rate")).toString().split(QLatin1Char('/'));
    if (rate.size() == 2 && rate[1].toDouble() > 0)
        info.fps = rate[0].toDouble() / rate[1].toDouble();
    // ffprobe writes durations as strings; some containers only know the overall one.
    info.seconds = stream.value(QStringLiteral("duration")).toString().toDouble();
    if (info.seconds <= 0)
        info.seconds = json.value(QStringLiteral("format")).toObject().value(QStringLiteral("duration")).toString().toDouble();
    if (info.width <= 0 || info.height <= 0 || info.fps <= 0 || info.seconds <= 0) {
        error = MediaJob::tr("%1 isn't a video.").arg(QFileInfo(file).fileName());
        return std::nullopt;
    }
    return info;
}

MediaJob::MediaJob(const QString &input, const QString &output, bool directory, double start, double maxSeconds, QStringList arguments, QObject *parent)
    : QObject(parent)
    , m_input(input)
    , m_output(output)
    , m_directory(directory)
    , m_start(std::max(0.0, start))
    , m_maxSeconds(maxSeconds)
    , m_arguments(std::move(arguments))
{
    // Seeking before -i is fast and, as the output is encoded anew, exact.
    if (m_start > 0)
        m_arguments = QStringList{QStringLiteral("-ss"), QString::number(m_start)} + m_arguments;
    if (m_maxSeconds > 0)
        m_arguments << QStringLiteral("-t") << QString::number(m_maxSeconds);
}

MediaJob *MediaJob::toMp4(const QString &input, const QString &output, int maxHeight, double start, double seconds, QObject *parent)
{
    // Even sizes for yuv420p; no metadata from the source (phone videos carry places).
    const QString scale = QStringLiteral("scale=w=-2:h='trunc(min(ih,%1)/2)*2':flags=lanczos").arg(maxHeight);
    return new MediaJob(input, output, false, start, seconds,
                        {QStringLiteral("-i"), input, QStringLiteral("-map"), QStringLiteral("0:v:0"), QStringLiteral("-an"), QStringLiteral("-sn"),
                         QStringLiteral("-dn"), QStringLiteral("-map_metadata"), QStringLiteral("-1"), QStringLiteral("-map_chapters"), QStringLiteral("-1"),
                         QStringLiteral("-vf"), scale, QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("medium"),
                         QStringLiteral("-crf"), QStringLiteral("20"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-profile:v"),
                         QStringLiteral("high"), QStringLiteral("-movflags"), QStringLiteral("+faststart"), QStringLiteral("-f"), QStringLiteral("mp4")},
                        parent);
}

MediaJob *MediaJob::toFrames(const QString &input, const QString &outputDir, QSize size, double fps, double seconds, double start, QObject *parent)
{
    // One palette over all frames (stats_mode=diff favours what moves), as measured in
    // notes/spikes-2026-11-06.md.
    const QString graph = QStringLiteral("fps=%1,scale=w=%2:h=%3:force_original_aspect_ratio=decrease:flags=lanczos,"
                                         "split[a][b];[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=sierra2_4a")
                              .arg(fps)
                              .arg(size.width())
                              .arg(size.height());
    return new MediaJob(input, outputDir, true, start, seconds,
                        {QStringLiteral("-i"), input, QStringLiteral("-map"), QStringLiteral("0:v:0"), QStringLiteral("-an"), QStringLiteral("-vf"), graph},
                        parent);
}

MediaJob::~MediaJob()
{
    if (isRunning()) {
        m_finished = true;
        m_process.kill();
        m_process.waitForFinished();
        removePartial();
    }
}

void MediaJob::start()
{
    QString error;
    const auto info = probeVideo(m_input, error);
    if (info && m_start >= info->seconds) {
        error = tr("The start (%1 s) lies after the end of the video (%2 s).").arg(m_start, 0, 'f', 1).arg(info->seconds, 0, 'f', 1);
    } else if (info) {
        const double left = info->seconds - m_start;
        m_seconds = m_maxSeconds > 0 ? std::min(m_maxSeconds, left) : left;
        removePartial();
        if (m_directory && !QDir().mkpath(partial()))
            error = tr("Can't create %1.").arg(partial());
    }
    if (!error.isEmpty()) {
        QTimer::singleShot(0, this, [this, error] { done(false, error); });
        return;
    }

    // -progress writes key=value lines; out_time_us is how far the output got.
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_stdout += m_process.readAllStandardOutput();
        for (qsizetype end = m_stdout.indexOf('\n'); end >= 0; end = m_stdout.indexOf('\n')) {
            const QByteArray line = m_stdout.left(end).trimmed();
            m_stdout.remove(0, end + 1);
            bool ok = false;
            const double us = line.startsWith("out_time_us=") ? line.mid(12).toDouble(&ok) : 0;
            if (ok && m_seconds > 0 && us / 1e6 / m_seconds > m_progress) {
                m_progress = std::min(us / 1e6 / m_seconds, 0.99); // 1 only once the output is in place
                Q_EMIT progressChanged(m_progress);
            }
        }
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (m_finished)
            return;
        if (status != QProcess::NormalExit || code != 0) {
            removePartial();
            const QString text = QString::fromUtf8(m_process.readAllStandardError()).trimmed().section(QLatin1Char('\n'), -1);
            return done(false, text.isEmpty() ? tr("ffmpeg failed (exit code %1).").arg(code) : tr("ffmpeg: %1").arg(text));
        }
        bool placed = false;
        if (m_directory) {
            placed = !QDir(partial()).isEmpty() && QDir().rename(partial(), m_output);
        } else {
            QFile::remove(m_output);
            placed = QFile::rename(partial(), m_output);
        }
        if (!placed) {
            removePartial();
            return done(false, tr("ffmpeg wrote nothing to %1.").arg(m_output));
        }
        m_progress = 1;
        Q_EMIT progressChanged(1);
        done(true, {});
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            removePartial();
            done(false, tr("ffmpeg isn't installed."));
        }
    });
    const QString target = m_directory ? partial() + QStringLiteral("/frame-%04d.png") : partial();
    m_process.start(QStringLiteral("ffmpeg"), QStringList{QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-loglevel"),
                                                          QStringLiteral("error"), QStringLiteral("-y")}
                                                  + m_arguments
                                                  + QStringList{QStringLiteral("-progress"), QStringLiteral("pipe:1"), QStringLiteral("-nostats"), target});
}

void MediaJob::cancel()
{
    if (std::exchange(m_finished, true))
        return;
    if (isRunning()) {
        m_process.kill();
        m_process.waitForFinished();
    }
    removePartial();
    Q_EMIT finished(false, {});
}

void MediaJob::removePartial() const
{
    if (m_directory)
        QDir(partial()).removeRecursively();
    else
        QFile::remove(partial());
}

void MediaJob::done(bool ok, const QString &error)
{
    if (!std::exchange(m_finished, true))
        Q_EMIT finished(ok, error);
}
