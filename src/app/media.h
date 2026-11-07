#pragma once

#include <QObject>
#include <QProcess>
#include <QSize>
#include <QString>
#include <QStringList>

#include <optional>

// Video conversion through ffmpeg (ARCHITECTURE.md §4 `media`).

struct VideoInfo {
    int width = 0;
    int height = 0;
    double seconds = 0;
    double fps = 0;
};

// ffprobe's view of the first video stream. Stills (no length) don't count.
std::optional<VideoInfo> probeVideo(const QString &file, QString &error);

// One ffmpeg run with progress and cancel. The output is written under a temporary
// name and renamed when ffmpeg succeeds, so a cancelled or failed job leaves nothing.
class MediaJob : public QObject
{
    Q_OBJECT

public:
    // A silent H.264 MP4 (yuv420p, moov atom first) at most maxHeight high, never
    // enlarged: what a Plasma splash plays through QtMultimedia (SPEC.md §4.7). From
    // start, seconds long (0: to the end), so one recording can give several clips.
    static MediaJob *toMp4(const QString &input, const QString &output, int maxHeight, double start = 0, double seconds = 0,
                           QObject *parent = nullptr);
    // frame-0001.png, … in outputDir (which must not exist): 8-bit palette PNGs with
    // one palette for all frames, fitted into size, at fps, at most seconds long from
    // start. A Plymouth animation (SPEC.md §4.6).
    static MediaJob *toFrames(const QString &input, const QString &outputDir, QSize size, double fps, double seconds, double start = 0,
                              QObject *parent = nullptr);
    ~MediaJob() override; // cancels a running job

    void start(); // finished() follows, also when ffmpeg can't start
    void cancel(); // kills ffmpeg, removes the partial output, emits finished(false, {})
    bool isRunning() const { return m_process.state() != QProcess::NotRunning; }
    double progress() const { return m_progress; }

Q_SIGNALS:
    void progressChanged(double fraction); // 0..1
    void finished(bool ok, const QString &error); // no error after cancel()

private:
    MediaJob(const QString &input, const QString &output, bool directory, double start, double maxSeconds, QStringList arguments, QObject *parent);
    QString partial() const { return m_output + QStringLiteral(".part"); }
    void removePartial() const;
    void done(bool ok, const QString &error);

    QString m_input;
    QString m_output;
    bool m_directory;
    double m_start; // where in the video it starts
    double m_maxSeconds; // 0 = to the end
    double m_seconds = 0; // what ffmpeg will write, for progress
    QStringList m_arguments; // the output path is appended
    QProcess m_process;
    QByteArray m_stdout;
    double m_progress = 0;
    bool m_finished = false;
};
