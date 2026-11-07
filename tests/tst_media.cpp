#include "media.h"
#include "testmain.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtEndian>

#include <algorithm>
#include <memory>

class TestMedia : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void probes();
    void makesSilentMp4();
    void neverEnlarges();
    void makesPaletteFrames();
    void cancels();
    void reportsNonVideo();

private:
    // A test video made by ffmpeg itself: odd height, 4:4:4 colour and a sound track,
    // all of which the conversions must deal with.
    QString makeVideo(const QString &name, const QString &size, int seconds) const
    {
        const QString path = m_tmp.filePath(name);
        QProcess ffmpeg;
        ffmpeg.start(QStringLiteral("ffmpeg"), {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-nostdin"),
                                                QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc=size=%1:rate=25").arg(size),
                                                QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=frequency=440"),
                                                QStringLiteral("-t"), QString::number(seconds), QStringLiteral("-c:v"), QStringLiteral("libx264"),
                                                QStringLiteral("-preset"), QStringLiteral("ultrafast"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv444p"),
                                                QStringLiteral("-c:a"), QStringLiteral("aac"), path});
        return ffmpeg.waitForFinished(60000) && ffmpeg.exitCode() == 0 ? path : QString();
    }
    static bool run(MediaJob &job, QString &error)
    {
        QSignalSpy finished(&job, &MediaJob::finished);
        job.start();
        if (!finished.wait(120000))
            return false;
        error = finished.first().at(1).toString();
        return finished.first().at(0).toBool();
    }
    static QString streams(const QString &file)
    {
        QProcess ffprobe;
        ffprobe.start(QStringLiteral("ffprobe"), {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"),
                                                  QStringLiteral("stream=codec_type,codec_name,width,height,pix_fmt"), QStringLiteral("-of"),
                                                  QStringLiteral("compact=p=0:nk=1"), file});
        ffprobe.waitForFinished();
        return QString::fromUtf8(ffprobe.readAllStandardOutput()).trimmed();
    }

    QTemporaryDir m_tmp;
    QString m_video;
};

void TestMedia::initTestCase()
{
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty() || QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty())
        QSKIP("ffmpeg isn't installed");
    m_video = makeVideo(QStringLiteral("source.mkv"), QStringLiteral("320x181"), 2);
    QVERIFY(!m_video.isEmpty());
}

void TestMedia::probes()
{
    QString error;
    const auto info = probeVideo(m_video, error);
    QVERIFY2(info, qPrintable(error));
    QCOMPARE(info->width, 320);
    QCOMPARE(info->height, 181);
    QCOMPARE(info->fps, 25.0);
    QVERIFY(qAbs(info->seconds - 2.0) < 0.1);
}

void TestMedia::makesSilentMp4()
{
    const QString output = m_tmp.filePath(QStringLiteral("small.mp4"));
    std::unique_ptr<MediaJob> job(MediaJob::toMp4(m_video, output, 100));
    QList<double> progress;
    connect(job.get(), &MediaJob::progressChanged, this, [&](double p) { progress << p; });
    QString error;
    QVERIFY2(run(*job, error), qPrintable(error));
    // Video only, 100 high, even width, yuv420p.
    QCOMPARE(streams(output), QStringLiteral("h264|video|176|100|yuv420p"));
    QVERIFY(!QFile::exists(output + QStringLiteral(".part")));
    QVERIFY(std::is_sorted(progress.cbegin(), progress.cend()));
    QCOMPARE(progress.last(), 1.0);
    QCOMPARE(job->progress(), 1.0);
}

void TestMedia::neverEnlarges()
{
    const QString output = m_tmp.filePath(QStringLiteral("full.mp4"));
    std::unique_ptr<MediaJob> job(MediaJob::toMp4(m_video, output, 1440));
    QString error;
    QVERIFY2(run(*job, error), qPrintable(error));
    QCOMPARE(streams(output), QStringLiteral("h264|video|318|180|yuv420p")); // 181 rounded down to even, aspect kept
}

void TestMedia::makesPaletteFrames()
{
    const QString dir = m_tmp.filePath(QStringLiteral("frames"));
    std::unique_ptr<MediaJob> job(MediaJob::toFrames(m_video, dir, QSize(64, 64), 10, 1));
    QString error;
    QVERIFY2(run(*job, error), qPrintable(error));
    const QStringList frames = QDir(dir).entryList(QDir::Files, QDir::Name);
    QCOMPARE(frames.size(), 10);
    QCOMPARE(frames.first(), QStringLiteral("frame-0001.png"));
    QCOMPARE(frames.last(), QStringLiteral("frame-0010.png"));
    QFile png(dir + QStringLiteral("/frame-0001.png"));
    QVERIFY(png.open(QIODevice::ReadOnly));
    const QByteArray header = png.read(26);
    // IHDR: 64 wide (aspect kept, so 36 high), colour type 3 = palette.
    QCOMPARE(qFromBigEndian<quint32>(header.constData() + 16), 64u);
    QCOMPARE(qFromBigEndian<quint32>(header.constData() + 20), 36u);
    QCOMPARE(int(header.at(25)), 3);
    QVERIFY(!QFileInfo::exists(dir + QStringLiteral(".part")));
}

void TestMedia::cancels()
{
    const QString longVideo = makeVideo(QStringLiteral("long.mkv"), QStringLiteral("1280x720"), 20);
    QVERIFY(!longVideo.isEmpty());
    const QString output = m_tmp.filePath(QStringLiteral("cancelled.mp4"));
    std::unique_ptr<MediaJob> job(MediaJob::toMp4(longVideo, output, 1440));
    QSignalSpy progress(job.get(), &MediaJob::progressChanged);
    QSignalSpy finished(job.get(), &MediaJob::finished);
    job->start();
    QVERIFY(progress.wait(60000));
    QVERIFY(job->isRunning());
    job->cancel();
    QVERIFY(!job->isRunning());
    QCOMPARE(finished.size(), 1);
    QCOMPARE(finished.first().at(0).toBool(), false);
    QCOMPARE(finished.first().at(1).toString(), QString()); // a cancel isn't an error
    QVERIFY(!QFile::exists(output));
    QVERIFY(!QFile::exists(output + QStringLiteral(".part")));
}

void TestMedia::reportsNonVideo()
{
    std::unique_ptr<MediaJob> job(MediaJob::toMp4(QStringLiteral(FIXTURES_DIR "/assets/logo.png"), m_tmp.filePath(QStringLiteral("x.mp4")), 1440));
    QString error;
    QVERIFY(!run(*job, error));
    QCOMPARE(error, QStringLiteral("logo.png isn't a video."));
}

EKPO_TEST_MAIN(TestMedia)
#include "tst_media.moc"
