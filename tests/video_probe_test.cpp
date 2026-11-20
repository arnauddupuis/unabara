// Tests for VideoProbe::parseFfprobeJson (include/core/video_probe.h) — the
// pure parser behind the one-ffprobe-call metadata probe. Fixtures are
// synthetic ffprobe JSON embedded in this file (tests/data is untracked and
// absent on CI, so nothing here may depend on it); they mirror real outputs:
// a GoPro-style file (tmcd stream + creation_time), an HLG 10-bit file, a
// plain bt709 mp4 with audio, a rotated phone clip, a VFR clip, drop-frame
// timecode, and malformed input. One optional end-to-end probe of the local
// test video is QSKIP-guarded on ffprobe/file availability.

#include <QtTest>

#include "include/core/ffmpeg_probe.h"
#include "include/core/video_probe.h"

// ffprobe emits numeric fields as JSON strings; the fixtures do the same.
static const char kGoProJson[] = R"JSON({
    "streams": [
        {
            "index": 0,
            "codec_name": "hevc",
            "codec_type": "video",
            "width": 3840,
            "height": 2160,
            "pix_fmt": "yuvj420p",
            "color_range": "pc",
            "color_space": "bt709",
            "color_transfer": "bt709",
            "color_primaries": "bt709",
            "r_frame_rate": "60000/1001",
            "avg_frame_rate": "60000/1001",
            "duration": "120.520400",
            "nb_frames": "7225",
            "disposition": { "attached_pic": 0 },
            "tags": { "creation_time": "2026-07-15T08:30:00.000000Z" }
        },
        {
            "index": 1,
            "codec_name": "aac",
            "codec_type": "audio",
            "tags": { "creation_time": "2026-07-15T08:30:00.000000Z" }
        },
        {
            "index": 3,
            "codec_name": "tmcd",
            "codec_type": "data",
            "tags": { "timecode": "10:39:52:29" }
        }
    ],
    "format": {
        "duration": "120.520400",
        "tags": { "creation_time": "2026-07-15T08:30:00.000000Z" }
    }
})JSON";

static const char kHlg10BitJson[] = R"JSON({
    "streams": [
        {
            "codec_name": "hevc",
            "codec_type": "video",
            "width": 3840,
            "height": 2160,
            "pix_fmt": "yuv420p10le",
            "bits_per_raw_sample": "10",
            "color_range": "tv",
            "color_space": "bt2020nc",
            "color_transfer": "arib-std-b67",
            "color_primaries": "bt2020",
            "r_frame_rate": "25/1",
            "avg_frame_rate": "25/1",
            "nb_frames": "750",
            "disposition": { "attached_pic": 0 }
        }
    ],
    "format": { "duration": "30.000000" }
})JSON";

static const char kPlainMp4Json[] = R"JSON({
    "streams": [
        {
            "codec_name": "h264",
            "codec_type": "video",
            "width": 1920,
            "height": 1080,
            "pix_fmt": "yuv420p",
            "color_range": "tv",
            "color_space": "bt709",
            "color_transfer": "bt709",
            "color_primaries": "bt709",
            "r_frame_rate": "30/1",
            "avg_frame_rate": "30/1",
            "nb_frames": "900",
            "disposition": { "attached_pic": 0 }
        },
        {
            "codec_name": "aac",
            "codec_type": "audio"
        }
    ],
    "format": { "duration": "30.000000" }
})JSON";

static const char kRotatedPhoneJson[] = R"JSON({
    "streams": [
        {
            "codec_name": "h264",
            "codec_type": "video",
            "width": 1920,
            "height": 1080,
            "pix_fmt": "yuv420p",
            "r_frame_rate": "30/1",
            "avg_frame_rate": "30/1",
            "disposition": { "attached_pic": 0 },
            "side_data_list": [
                { "side_data_type": "Display Matrix", "rotation": -90 }
            ]
        }
    ],
    "format": { "duration": "12.000000" }
})JSON";

static const char kVfrJson[] = R"JSON({
    "streams": [
        {
            "codec_name": "h264",
            "codec_type": "video",
            "width": 1280,
            "height": 720,
            "pix_fmt": "yuv420p",
            "r_frame_rate": "30/1",
            "avg_frame_rate": "25/1",
            "disposition": { "attached_pic": 0 }
        }
    ],
    "format": { "duration": "10.000000" }
})JSON";

static const char kDropFrameJson[] = R"JSON({
    "streams": [
        {
            "codec_name": "prores",
            "codec_type": "video",
            "width": 1920,
            "height": 1080,
            "pix_fmt": "yuv422p10le",
            "r_frame_rate": "30000/1001",
            "avg_frame_rate": "30000/1001",
            "disposition": { "attached_pic": 0 },
            "tags": { "timecode": "00:59:30;15" }
        }
    ],
    "format": { "duration": "5.000000" }
})JSON";

static const char kAudioOnlyJson[] = R"JSON({
    "streams": [
        { "codec_name": "mp3", "codec_type": "audio" }
    ],
    "format": { "duration": "180.0" }
})JSON";

class VideoProbeTest : public QObject
{
    Q_OBJECT

private slots:
    void goProStyle();
    void hlg10Bit();
    void plainMp4();
    void rotatedPhoneClip();
    void vfrDetection();
    void degenerateNominalRate_data();
    void degenerateNominalRate();
    void nominalFromAverage_data();
    void nominalFromAverage();
    void dropFrameTimecode();
    void coverArtSkipped();
    void malformedJson_data();
    void malformedJson();
    void timecodeParsing_data();
    void timecodeParsing();
    void endToEndLocalVideo();
};

void VideoProbeTest::goProStyle()
{
    const VideoProbe::Result r =
        VideoProbe::parseFfprobeJson(QByteArray(kGoProJson));
    QVERIFY(r.ok);
    QCOMPARE(r.codec, QStringLiteral("hevc"));
    QCOMPARE(r.width, 3840);
    QCOMPARE(r.height, 2160);
    QCOMPARE(r.rotation, 0);
    // Exact rational, never a double round-trip.
    QCOMPARE(r.fpsNum, 60000);
    QCOMPARE(r.fpsDen, 1001);
    QVERIFY(!r.vfr);
    QCOMPARE(r.nbFrames, Q_INT64_C(7225));
    QCOMPARE(r.durationSeconds, 120.5204);
    // The video STREAM's own duration, kept apart from the format duration.
    QCOMPARE(r.videoStreamDurationSeconds, 120.5204);
    QVERIFY(r.hasAudio);
    QCOMPARE(r.audioCodec, QStringLiteral("aac"));
    // tmcd stream timecode, frames ignored: 10:39:52.
    QCOMPARE(r.timecodeSecondsOfDay, 10 * 3600.0 + 39 * 60.0 + 52.0);
    // creation_time as a UTC instant.
    QVERIFY(r.creationTimeUtc.isValid());
    QCOMPARE(r.creationTimeUtc.timeSpec(), Qt::UTC);
    QCOMPARE(r.creationTimeUtc,
             QDateTime(QDate(2026, 7, 15), QTime(8, 30, 0), QTimeZone::utc()));
}

void VideoProbeTest::hlg10Bit()
{
    const VideoProbe::Result r =
        VideoProbe::parseFfprobeJson(QByteArray(kHlg10BitJson));
    QVERIFY(r.ok);
    QCOMPARE(r.pixFmt, QStringLiteral("yuv420p10le"));
    QCOMPARE(r.bitDepth, 10);
    QCOMPARE(r.colorTransfer, QStringLiteral("arib-std-b67"));
    QCOMPARE(r.colorSpace, QStringLiteral("bt2020nc"));
    QCOMPARE(r.colorPrimaries, QStringLiteral("bt2020"));
    QCOMPARE(r.colorRange, QStringLiteral("tv"));
    QVERIFY(!r.hasAudio);
    QCOMPARE(r.timecodeSecondsOfDay, -1.0);
    QVERIFY(!r.creationTimeUtc.isValid());
}

void VideoProbeTest::plainMp4()
{
    const VideoProbe::Result r =
        VideoProbe::parseFfprobeJson(QByteArray(kPlainMp4Json));
    QVERIFY(r.ok);
    QCOMPARE(r.codec, QStringLiteral("h264"));
    QCOMPARE(r.bitDepth, 8);
    QCOMPARE(r.colorSpace, QStringLiteral("bt709"));
    QCOMPARE(r.fpsNum, 30);
    QCOMPARE(r.fpsDen, 1);
    QCOMPARE(r.fps(), 30.0);
    QVERIFY(!r.vfr);
    QVERIFY(r.hasAudio);
    QCOMPARE(r.audioCodec, QStringLiteral("aac"));
    QCOMPARE(r.durationSeconds, 30.0);
    // No stream-level duration in this file: unknown, not the format's.
    QCOMPARE(r.videoStreamDurationSeconds, -1.0);
}

void VideoProbeTest::rotatedPhoneClip()
{
    const VideoProbe::Result r =
        VideoProbe::parseFfprobeJson(QByteArray(kRotatedPhoneJson));
    QVERIFY(r.ok);
    // -90 normalizes to 270; 90/270 swap the display dimensions.
    QCOMPARE(r.rotation, 270);
    QCOMPARE(r.width, 1080);
    QCOMPARE(r.height, 1920);
}

void VideoProbeTest::degenerateNominalRate_data()
{
    QTest::addColumn<QString>("rFrameRate");
    QTest::addColumn<QString>("avgFrameRate");
    QTest::addColumn<int>("expectedNum");
    QTest::addColumn<int>("expectedDen");
    QTest::addColumn<bool>("expectedVfr");

    // Irregular timestamps: ffprobe reports the stream TIMEBASE as
    // r_frame_rate. Laying frames on that grid means a 90000 fps export.
    QTest::newRow("90 kHz timebase, ~30 fps")
        << "90000/1" << "240000/7984" << 30 << 1 << true;
    QTest::newRow("90 kHz timebase, ~29.97 fps")
        << "90000/1" << "2997/100" << 30000 << 1001 << true;
    QTest::newRow("QuickTime 600 timescale, ~59.9 fps")
        << "600/1" << "5990/100" << 60000 << 1001 << true;
    QTest::newRow("millisecond timebase, odd average")
        << "1000/1" << "273/10" << 273 << 10 << true;
    // A real, plausible nominal rate is never touched — VFR or not.
    QTest::newRow("vfr at a real 120 fps nominal")
        << "120/1" << "9960/317" << 120 << 1 << true;
    QTest::newRow("vfr at exactly the 240 fps ceiling")
        << "240/1" << "200/1" << 240 << 1 << true;
    QTest::newRow("constant 240 fps")
        << "240/1" << "240/1" << 240 << 1 << false;
    // No average to fall back on: left as reported (and not flagged VFR).
    QTest::newRow("timebase with unknown average")
        << "90000/1" << "0/0" << 90000 << 1 << false;
}

void VideoProbeTest::degenerateNominalRate()
{
    QFETCH(QString, rFrameRate);
    QFETCH(QString, avgFrameRate);
    QFETCH(int, expectedNum);
    QFETCH(int, expectedDen);
    QFETCH(bool, expectedVfr);

    const QString json = QStringLiteral(R"JSON({
        "streams": [ { "codec_name": "h264", "codec_type": "video",
                       "width": 1920, "height": 1080, "pix_fmt": "yuv420p",
                       "r_frame_rate": "%1", "avg_frame_rate": "%2",
                       "nb_frames": "240", "duration": "7.983560",
                       "disposition": { "attached_pic": 0 } } ],
        "format": { "duration": "8.000000" }
    })JSON").arg(rFrameRate, avgFrameRate);
    const VideoProbe::Result r = VideoProbe::parseFfprobeJson(json.toUtf8());
    QVERIFY(r.ok);
    QCOMPARE(r.fpsNum, expectedNum);
    QCOMPARE(r.fpsDen, expectedDen);
    QCOMPARE(r.vfr, expectedVfr);
}

void VideoProbeTest::nominalFromAverage_data()
{
    QTest::addColumn<double>("avg");
    QTest::addColumn<bool>("ok");
    QTest::addColumn<int>("num");
    QTest::addColumn<int>("den");

    QTest::newRow("30.06 -> 30") << 30.06 << true << 30 << 1;
    QTest::newRow("29.97 -> 30000/1001") << 29.97 << true << 30000 << 1001;
    QTest::newRow("23.98 -> 24000/1001") << 23.98 << true << 24000 << 1001;
    QTest::newRow("24.0 -> 24") << 24.0 << true << 24 << 1;
    QTest::newRow("59.5 -> 60000/1001") << 59.5 << true << 60000 << 1001;
    // Outside 2 % of every standard rate: the average itself, reduced.
    QTest::newRow("27.3 -> 273/10") << 27.3 << true << 273 << 10;
    QTest::newRow("13.793 -> 13793/1000") << 13.793 << true << 13793 << 1000;
    QTest::newRow("15 -> 15/1") << 15.0 << true << 15 << 1;
    QTest::newRow("zero") << 0.0 << false << 7 << 9;
    QTest::newRow("negative") << -1.0 << false << 7 << 9;
}

void VideoProbeTest::nominalFromAverage()
{
    QFETCH(double, avg);
    QFETCH(bool, ok);
    QFETCH(int, num);
    QFETCH(int, den);
    int n = 7;
    int d = 9; // sentinels: untouched on failure
    QCOMPARE(VideoProbe::nominalFromAverage(avg, n, d), ok);
    QCOMPARE(n, num);
    QCOMPARE(d, den);
}

void VideoProbeTest::vfrDetection()
{
    const VideoProbe::Result r =
        VideoProbe::parseFfprobeJson(QByteArray(kVfrJson));
    QVERIFY(r.ok);
    QCOMPARE(r.fpsNum, 30);
    QCOMPARE(r.avgFps, 25.0);
    QVERIFY(r.vfr);
}

void VideoProbeTest::dropFrameTimecode()
{
    const VideoProbe::Result r =
        VideoProbe::parseFfprobeJson(QByteArray(kDropFrameJson));
    QVERIFY(r.ok);
    // Video-stream tag, drop-frame separator: 00:59:30 (frames ignored).
    QCOMPARE(r.timecodeSecondsOfDay, 59 * 60.0 + 30.0);
    QCOMPARE(r.bitDepth, 10);
}

void VideoProbeTest::coverArtSkipped()
{
    // An attached-picture "video" stream (cover art) must not be mistaken
    // for the real video stream.
    static const char json[] = R"JSON({
        "streams": [
            {
                "codec_name": "mjpeg",
                "codec_type": "video",
                "width": 600,
                "height": 600,
                "disposition": { "attached_pic": 1 }
            },
            {
                "codec_name": "h264",
                "codec_type": "video",
                "width": 1280,
                "height": 720,
                "pix_fmt": "yuv420p",
                "r_frame_rate": "24/1",
                "avg_frame_rate": "24/1",
                "disposition": { "attached_pic": 0 }
            }
        ],
        "format": {}
    })JSON";
    const VideoProbe::Result r = VideoProbe::parseFfprobeJson(QByteArray(json));
    QVERIFY(r.ok);
    QCOMPARE(r.codec, QStringLiteral("h264"));
    QCOMPARE(r.width, 1280);
}

void VideoProbeTest::malformedJson_data()
{
    QTest::addColumn<QByteArray>("json");

    QTest::newRow("empty")         << QByteArray();
    QTest::newRow("whitespace")    << QByteArray("   \n  ");
    QTest::newRow("truncated")     << QByteArray("{\"streams\": [{");
    QTest::newRow("not json")      << QByteArray("ffprobe: command not found");
    QTest::newRow("not an object") << QByteArray("[1, 2, 3]");
    QTest::newRow("no streams")    << QByteArray("{\"format\": {}}");
    QTest::newRow("audio only")    << QByteArray(kAudioOnlyJson);
}

void VideoProbeTest::malformedJson()
{
    QFETCH(QByteArray, json);
    // Must report failure, never crash.
    const VideoProbe::Result r = VideoProbe::parseFfprobeJson(json);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
}

void VideoProbeTest::timecodeParsing_data()
{
    QTest::addColumn<QString>("timecode");
    QTest::addColumn<double>("expected");

    QTest::newRow("midnight")    << "00:00:00:00" << 0.0;
    QTest::newRow("non-drop")    << "10:39:52:29" << 38392.0;
    QTest::newRow("drop-frame")  << "00:59:30;15" << 3570.0;
    // av_timecode formats frames with %02d, which widens to 3 digits past
    // frame 99 — 120/240 fps footage carries "HH:MM:SS:119"-style tags.
    QTest::newRow("3-digit frames 120fps") << "10:39:52:119" << 38392.0;
    QTest::newRow("4-digit frames rejected") << "10:39:52:1190" << -1.0;
    QTest::newRow("padded")      << " 12:00:00:00 " << 43200.0;
    QTest::newRow("garbage")     << "not a timecode" << -1.0;
    QTest::newRow("no frames")   << "10:39:52" << -1.0;
    QTest::newRow("empty")       << "" << -1.0;
}

void VideoProbeTest::timecodeParsing()
{
    QFETCH(QString, timecode);
    QFETCH(double, expected);
    QCOMPARE(VideoProbe::parseTimecodeToSecondsOfDay(timecode), expected);
}

void VideoProbeTest::endToEndLocalVideo()
{
    // Optional: real ffprobe run over the local test clip. Both the binary
    // and the clip are absent on CI (tests/videos is untracked), so skip
    // rather than fail.
    const QString videoPath =
        QDir(QStringLiteral(TEST_DATA_DIR))
            .filePath(QStringLiteral("../videos/Trimix_blue_hole.mp4"));
    if (!QFileInfo::exists(videoPath))
        QSKIP("Local test video not present");
    if (FFmpegProbe::instance()->ffprobePath().isEmpty())
        QSKIP("ffprobe not installed");

    const VideoProbe::Result r = VideoProbe::probe(videoPath);
    QVERIFY2(r.ok, qPrintable(r.error));
    // The v0.4 plan's baseline facts for this clip.
    QCOMPARE(r.codec, QStringLiteral("h264"));
    QCOMPARE(r.width, 1280);
    QCOMPARE(r.height, 720);
    QCOMPARE(r.fpsNum, 30);
    QCOMPARE(r.fpsDen, 1);
    QCOMPARE(r.pixFmt, QStringLiteral("yuv420p"));
    QCOMPARE(r.colorSpace, QStringLiteral("bt709"));
    QCOMPARE(r.bitDepth, 8);
    QVERIFY(r.durationSeconds > 0.0);
}

QTEST_GUILESS_MAIN(VideoProbeTest)
#include "video_probe_test.moc"
