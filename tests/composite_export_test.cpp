// Tests for the composited export (PR B3).
//
// Part 1 — pure string/geometry units (always run):
//   - FFmpegArgs::decideAudio / inColorMatrixFor: every branch of the
//     audio-copy and colour-matrix decision tables.
//   - FFmpegArgs::buildCompositeFilterGraph: exact graph strings for every
//     branch (colour snippet present/absent, forced in_color_matrix
//     present/absent, prores vs h264 output format).
//   - FFmpegArgs::compositeArgs: full command shape (input seek window
//     before -i, raw pipe input block, maps, codec tail per codec, audio
//     copy/transcode/none, bt709/tv tags, -shortest, -frames:v, output
//     path last) and the quarter-frame seek backoff.
//   - FrameCompositor::placementRect/fitRect: exact parity cases against
//     VideoOverlayItem.qml's Math.round placement and
//     Image.PreserveAspectFit centring (the documented QML↔C++
//     duplication), plus composeFrame pixel checks and the
//     CompositorFrameSource begin/end fan-out.
//
// Part 2 — end-to-end (QSKIP without FFmpeg), the plan's verification
// strategy: synthesize a source whose every frame burns its own frame
// number into the pixels (gray = N*7 mod 256 — adjacent frames differ by
// 7, so an off-by-one is unmissable), with an AAC sine track; export the
// whole clip and a sub-range through the REAL VideoExporter with a stub
// overlay generator that encodes the dive-time-derived frame index the
// same way; decode the result and assert overlay frame i sits exactly on
// source frame i, frame counts 150/75 at 30000/1001, bt709/tv tags,
// yuv420p, and the copied AAC track.

#include <QtTest>
#include <QPainter>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <cmath>

#include "include/core/color_grade.h"
#include "include/core/color_grade_export.h"
#include "include/core/dive_data.h"
#include "include/core/ffmpeg_probe.h"
#include "include/core/video_clip.h"
#include "include/core/video_probe.h"
#include "include/export/ffmpeg_args.h"
#include "include/export/frame_compositor.h"
#include "include/export/video_export.h"
#include "include/generators/i_frame_generator.h"

namespace {

constexpr int kSrcW = 64;
constexpr int kSrcH = 48;
constexpr int kFpsNum = 30000;
constexpr int kFpsDen = 1001;
constexpr int kSrcFrames = 150;

// Stub overlay: a full-canvas transparent plane with an opaque gray bar in
// the top rows whose gray value encodes the frame index derived from DIVE
// TIME — the same N*7 mod 256 coding the synthesized source burns in. With
// syncOffset 0, output frame k must therefore show source gray == overlay
// gray == ((startFrame+k)*7) mod 256; any off-by-one in either stream (seek
// window, fps grid, pipe timing) breaks the equality by ±7.
class FrameCodeGenerator : public QObject, public IFrameGenerator
{
    Q_OBJECT
public:
    QImage generate(DiveData *dive, double timePoint) override
    {
        Q_UNUSED(dive);
        const int frameIndex =
            qRound(timePoint * kFpsNum / static_cast<double>(kFpsDen));
        const int v = (frameIndex * 7) % 256;
        QImage img(kSrcW, kSrcH, QImage::Format_RGBA8888_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.fillRect(QRect(0, 0, kSrcW, 8), QColor(v, v, v, 255));
        p.end();
        ++generatedFrames;
        return img;
    }
    void beginExport() override { ++beginCalls; }
    void endExport() override { ++endCalls; }

    int generatedFrames = 0;
    int beginCalls = 0;
    int endCalls = 0;
};

// Opaque red frame that records every dive time it renders — the overlay-
// only pre-dive test asserts it is never consulted for t < 0.
class RecordingRedGenerator : public QObject, public IFrameGenerator
{
public:
    QImage generate(DiveData *, double t) override
    {
        times.append(t);
        QImage img(kSrcW, kSrcH, QImage::Format_ARGB32);
        img.fill(Qt::red);
        return img;
    }
    QList<double> times;
};

// Simple fixed-color layer for the compositor pixel tests.
class SolidGenerator : public IFrameGenerator
{
public:
    explicit SolidGenerator(const QSize &size, const QColor &color)
        : m_size(size), m_color(color) {}
    QImage generate(DiveData *, double) override
    {
        QImage img(m_size, QImage::Format_RGBA8888_Premultiplied);
        img.fill(m_color);
        ++generated;
        return img;
    }
    void beginExport() override { ++beginCalls; }
    void endExport() override { ++endCalls; }

    QSize m_size;
    QColor m_color;
    int generated = 0;
    int beginCalls = 0;
    int endCalls = 0;
};

// Circular distance mod 256 — the burned-in values wrap, and YUV round-
// tripping can land a near-0 value just below 256.
int grayDiff(int a, int b)
{
    const int d = qAbs(a - b) % 256;
    return qMin(d, 256 - d);
}

// ── PR C5 fixtures ─────────────────────────────────────────────────────

// The four flat quadrant colours of the colour-parity source (top-left,
// top-right, bottom-left, bottom-right) and their sample centres —
// mid-quadrant, away from chroma-subsampling edges and below the overlay
// marker bar (rows 0-7).
const QColor kQuadColors[4] = {QColor(180, 60, 40), QColor(40, 120, 180),
                               QColor(120, 180, 60), QColor(200, 200, 200)};
const QPoint kQuadCenters[4] = {QPoint(16, 16), QPoint(48, 16),
                                QPoint(16, 36), QPoint(48, 36)};
constexpr int kColorFps = 30;
constexpr int kColorFrames = 90; // 3 s — room for the depth ramp

// The overlay marker colour. Chosen so the static grade below moves it by
// dozens of LSB if it were (wrongly) graded — the "overlays stay
// ungraded" assertion must be able to fail loudly.
const QColor kMarkerColor(230, 20, 230);

// Full-canvas transparent plane with an opaque marker bar in rows 0-7:
// the overlay sits BETWEEN the colour snippet and the encoder, so these
// pixels must come through ungraded while the footage shifts.
class MarkerBarGenerator : public QObject, public IFrameGenerator
{
    Q_OBJECT
public:
    QImage generate(DiveData *dive, double timePoint) override
    {
        Q_UNUSED(dive);
        Q_UNUSED(timePoint);
        QImage img(kSrcW, kSrcH, QImage::Format_RGBA8888_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.fillRect(QRect(0, 0, kSrcW, 8), kMarkerColor);
        p.end();
        return img;
    }
    void beginExport() override {}
    void endExport() override {}
};

// GradeOps ground truth → expected 8-bit output for one source colour.
void applyExpected(const Unabara::GradeOps &ops, const QColor &in,
                   int *r, int *g, int *b)
{
    double dr = in.red() / 255.0;
    double dg = in.green() / 255.0;
    double db = in.blue() / 255.0;
    ops.apply(dr, dg, db);
    *r = qRound(dr * 255.0);
    *g = qRound(dg * 255.0);
    *b = qRound(db * 255.0);
}

// Capture of VideoExporter's one-per-run "FFmpeg command:" qInfo line, so
// the tests can assert what actually reached the command line (the
// no-grade byte-stability and override-wins checks).
QStringList g_capturedCommands;          // NOLINT
QtMessageHandler g_prevHandler = nullptr; // NOLINT

void commandCaptureHandler(QtMsgType type, const QMessageLogContext &ctx,
                           const QString &msg)
{
    if (msg.startsWith(QLatin1String("FFmpeg command:")))
        g_capturedCommands << msg;
    if (g_prevHandler)
        g_prevHandler(type, ctx, msg);
}

struct CommandCapture {
    CommandCapture()
    {
        g_capturedCommands.clear();
        g_prevHandler = qInstallMessageHandler(commandCaptureHandler);
    }
    ~CommandCapture()
    {
        qInstallMessageHandler(g_prevHandler);
        g_prevHandler = nullptr;
    }
};

} // namespace

class CompositeExportTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // Part 1 — always run
    void audioDecision_data();
    void audioDecision();
    void inColorMatrix_data();
    void inColorMatrix();
    void graphString_plain();
    void graphString_allOptions();
    void compositeArgsShape_data();
    void compositeArgsShape();
    void seekBackoff();
    void placementRectParity();
    void fitRectParity();
    void composeFramePixels();
    void composeFramePreDiveIsTransparent();
    void beginEndFanOut();

    // Part 2 — end-to-end (QSKIP without FFmpeg)
    void compositeEndToEnd_data();
    void compositeEndToEnd();
    void compositeFfmpegFailureCleansUp();
    void overlayOnlyPreDiveFramesAreBlank();

    // Part 3 — colour-grade export integration (PR C5), QSKIP without
    // FFmpeg: full-pipeline parity against GradeOps::apply(), the
    // overlays-stay-ungraded pin, the depth-linked time variation, the
    // dialog texts and the override hook / one-shot semantics.
    void compositeGradeTexts();
    void gradedCompositeStaticParity();
    void gradedCompositeDepthLinked();
    void gradeOverrideHookWins();

private:
    bool synthesizeSource();
    bool synthesizeColorSource();
    QMap<QString, QString> probeStream(const QString &path, const QString &selector,
                                       const QString &entries);
    QByteArray decodeRgb24(const QString &path);

    QTemporaryDir m_workDir;
    QString m_sourcePath;
    QString m_colorSourcePath;
    bool m_sourceReady = false;
    bool m_colorSourceReady = false;
};

void CompositeExportTest::initTestCase()
{
    QVERIFY(m_workDir.isValid());
    m_sourcePath = m_workDir.filePath(QStringLiteral("frame_coded_source.nut"));
    m_colorSourcePath = m_workDir.filePath(QStringLiteral("quadrant_source.nut"));
}

// ---------------------------------------------------------------------
// Part 1
// ---------------------------------------------------------------------

void CompositeExportTest::audioDecision_data()
{
    QTest::addColumn<QString>("videoCodec");
    QTest::addColumn<bool>("hasAudio");
    QTest::addColumn<QString>("audioCodec");
    QTest::addColumn<int>("expected"); // 0 None, 1 Copy, 2 Transcode

    QTest::newRow("no audio") << "h264" << false << "" << 0;
    QTest::newRow("mp4 aac copy") << "h264" << true << "aac" << 1;
    QTest::newRow("mp4 mp3 copy") << "hevc" << true << "mp3" << 1;
    QTest::newRow("mp4 ac3 copy") << "h264" << true << "ac3" << 1;
    QTest::newRow("mp4 eac3 copy") << "h264" << true << "eac3" << 1;
    QTest::newRow("mp4 pcm transcode") << "h264" << true << "pcm_s16le" << 2;
    QTest::newRow("mp4 opus transcode") << "h264" << true << "opus" << 2;
    QTest::newRow("mov pcm copy") << "prores" << true << "pcm_s16le" << 1;
    QTest::newRow("mov alac copy") << "prores" << true << "alac" << 1;
    QTest::newRow("mov aac copy") << "prores" << true << "aac" << 1;
    QTest::newRow("mov opus transcode") << "prores" << true << "opus" << 2;
}

void CompositeExportTest::audioDecision()
{
    QFETCH(QString, videoCodec);
    QFETCH(bool, hasAudio);
    QFETCH(QString, audioCodec);
    QFETCH(int, expected);
    QCOMPARE(static_cast<int>(
                 FFmpegArgs::decideAudio(videoCodec, hasAudio, audioCodec)),
             expected);
}

void CompositeExportTest::inColorMatrix_data()
{
    QTest::addColumn<QString>("colorSpace");
    QTest::addColumn<int>("height");
    QTest::addColumn<QString>("expected");

    // Tagged → decode as tagged, no forced matrix (even non-bt709 tags:
    // swscale reads them from the frames).
    QTest::newRow("tagged bt709") << "bt709" << 1080 << "";
    QTest::newRow("tagged bt2020") << "bt2020nc" << 2160 << "";
    QTest::newRow("tagged bt601 SD") << "smpte170m" << 480 << "";
    // Untagged → resolution convention.
    QTest::newRow("untagged HD") << "" << 1080 << "bt709";
    QTest::newRow("untagged 720 boundary") << "" << 720 << "bt709";
    QTest::newRow("unknown tag HD") << "unknown" << 720 << "bt709";
    QTest::newRow("untagged SD") << "" << 480 << "bt601";
}

void CompositeExportTest::inColorMatrix()
{
    QFETCH(QString, colorSpace);
    QFETCH(int, height);
    QFETCH(QString, expected);
    QCOMPARE(FFmpegArgs::inColorMatrixFor(colorSpace, height), expected);
}

void CompositeExportTest::graphString_plain()
{
    // Tagged source, no colour snippet, h264: the overlay wires straight to
    // [src] — no null passthrough stage, no forced matrix.
    FFmpegArgs::CompositePlan plan;
    plan.fps = {30, 1};
    plan.codec = QStringLiteral("h264");
    QCOMPARE(FFmpegArgs::buildCompositeFilterGraph(plan),
             QStringLiteral(
                 "[0:v]fps=30:round=near,format=rgb24[src];"
                 "[src][1:v]overlay=alpha=premultiplied:format=rgb:"
                 "eof_action=endall[comp];"
                 "[comp]scale=out_color_matrix=bt709:out_range=tv,"
                 "setparams=color_primaries=bt709:color_trc=bt709,"
                 "format=yuv420p[out]"));
}

void CompositeExportTest::graphString_allOptions()
{
    // Untagged SD source (forced bt601), colour snippet inserted verbatim
    // between format=rgb24 and overlay, prores output format.
    FFmpegArgs::CompositePlan plan;
    plan.fps = {30000, 1001};
    plan.codec = QStringLiteral("prores");
    plan.inColorMatrix = QStringLiteral("bt601");
    plan.colorFilterSnippet = QStringLiteral("lut3d=file='grade.cube'");
    QCOMPARE(FFmpegArgs::buildCompositeFilterGraph(plan),
             QStringLiteral(
                 "[0:v]fps=30000/1001:round=near,scale=in_color_matrix=bt601,"
                 "format=rgb24[src];"
                 "[src]lut3d=file='grade.cube'[graded];"
                 "[graded][1:v]overlay=alpha=premultiplied:format=rgb:"
                 "eof_action=endall[comp];"
                 "[comp]scale=out_color_matrix=bt709:out_range=tv,"
                 "setparams=color_primaries=bt709:color_trc=bt709,"
                 "format=yuv422p10le[out]"));
}

void CompositeExportTest::compositeArgsShape_data()
{
    QTest::addColumn<QString>("codec");
    QTest::addColumn<QString>("quality");
    QTest::addColumn<bool>("hasAudio");
    QTest::addColumn<QString>("audioCodec");
    QTest::addColumn<QString>("encoder");

    QTest::newRow("h264 best aac-copy") << "h264" << "best" << true << "aac" << "libx264";
    QTest::newRow("hevc std no-audio") << "hevc" << "standard" << false << "" << "libx265";
    QTest::newRow("prores high pcm-copy") << "prores" << "high" << true << "pcm_s16le" << "prores_ks";
    QTest::newRow("h264 small opus-transcode") << "h264" << "small" << true << "opus" << "libx264";
}

void CompositeExportTest::compositeArgsShape()
{
    QFETCH(QString, codec);
    QFETCH(QString, quality);
    QFETCH(bool, hasAudio);
    QFETCH(QString, audioCodec);
    QFETCH(QString, encoder);

    FFmpegArgs::CompositePlan plan;
    plan.clipPath = QStringLiteral("/videos/dive.mp4");
    plan.inPoint = 30 * 1001.0 / 30000.0;
    plan.outPoint = 105 * 1001.0 / 30000.0;
    plan.totalFrames = 75;
    plan.fps = {30000, 1001};
    plan.width = 1280;
    plan.height = 720;
    plan.codec = codec;
    plan.quality = quality;
    plan.hasAudio = hasAudio;
    plan.audioCodec = audioCodec;
    plan.outputPath = QStringLiteral("/out/dive_composited.mp4");

    const QStringList args = FFmpegArgs::compositeArgs(plan);

    // Global flags + seek window all BEFORE the clip input.
    const int clipIdx = args.indexOf(plan.clipPath);
    QVERIFY(clipIdx > 0);
    QCOMPARE(args.at(clipIdx - 1), QStringLiteral("-i"));
    for (const char *flag : {"-y", "-hide_banner", "-progress", "-nostats",
                             "-accurate_seek", "-ss", "-to"}) {
        const int idx = args.indexOf(QString::fromLatin1(flag));
        QVERIFY2(idx >= 0 && idx < clipIdx, flag);
    }

    // Raw pipe input block after the clip input.
    const int pipeIdx = args.indexOf(QStringLiteral("pipe:0"));
    QVERIFY(pipeIdx > clipIdx);
    const int sIdx = args.indexOf(QStringLiteral("-s"), clipIdx);
    QVERIFY(sIdx > clipIdx && sIdx < pipeIdx);
    QCOMPARE(args.at(sIdx + 1), QStringLiteral("1280x720"));
    const int frIdx = args.indexOf(QStringLiteral("-framerate"), clipIdx);
    QVERIFY(frIdx > clipIdx && frIdx < pipeIdx);
    QCOMPARE(args.at(frIdx + 1), QStringLiteral("30000/1001"));

    // Filter graph + maps.
    const int fcIdx = args.indexOf(QStringLiteral("-filter_complex"));
    QVERIFY(fcIdx > pipeIdx);
    QCOMPARE(args.at(fcIdx + 1), FFmpegArgs::buildCompositeFilterGraph(plan));
    const int mapOutIdx = args.indexOf(QStringLiteral("[out]"));
    QVERIFY(mapOutIdx > fcIdx);
    QCOMPARE(args.at(mapOutIdx - 1), QStringLiteral("-map"));
    const int mapAudioIdx = args.indexOf(QStringLiteral("0:a?"));
    QVERIFY(mapAudioIdx > mapOutIdx);
    QCOMPARE(args.at(mapAudioIdx - 1), QStringLiteral("-map"));

    // Codec tail.
    const int cIdx = args.indexOf(QStringLiteral("-c:v"));
    QVERIFY(cIdx > mapAudioIdx);
    QCOMPARE(args.at(cIdx + 1), encoder);
    if (codec == QLatin1String("prores")) {
        const int pIdx = args.indexOf(QStringLiteral("-profile:v"));
        QVERIFY(pIdx > cIdx);
        // "high" → ProRes 422 HQ (profile 3).
        QCOMPARE(args.at(pIdx + 1), QStringLiteral("3"));
        QVERIFY(!args.contains(QStringLiteral("-crf")));
    } else {
        const int crfIdx = args.indexOf(QStringLiteral("-crf"));
        QVERIFY(crfIdx > cIdx);
        QCOMPARE(args.at(crfIdx + 1),
                 QString::number(FFmpegArgs::qualityCrf(quality)));
    }
    if (codec == QLatin1String("hevc")) {
        const int tagIdx = args.indexOf(QStringLiteral("-tag:v"));
        QVERIFY(tagIdx > cIdx);
        QCOMPARE(args.at(tagIdx + 1), QStringLiteral("hvc1"));
    }

    // Audio decision on the command line.
    const int caIdx = args.indexOf(QStringLiteral("-c:a"));
    switch (FFmpegArgs::decideAudio(codec, hasAudio, audioCodec)) {
    case FFmpegArgs::AudioDecision::Copy:
        QVERIFY(caIdx > 0);
        QCOMPARE(args.at(caIdx + 1), QStringLiteral("copy"));
        break;
    case FFmpegArgs::AudioDecision::Transcode:
        QVERIFY(caIdx > 0);
        QCOMPARE(args.at(caIdx + 1), QStringLiteral("aac"));
        QVERIFY(args.contains(QStringLiteral("-b:a")));
        break;
    case FFmpegArgs::AudioDecision::None:
        QCOMPARE(caIdx, -1);
        break;
    }

    // Output colour tags, -shortest, exact frame cap, output path last.
    const int csIdx = args.indexOf(QStringLiteral("-colorspace"));
    QVERIFY(csIdx > 0);
    QCOMPARE(args.at(csIdx + 1), QStringLiteral("bt709"));
    QCOMPARE(args.at(args.indexOf(QStringLiteral("-color_range")) + 1),
             QStringLiteral("tv"));
    QVERIFY(args.contains(QStringLiteral("-color_primaries")));
    QVERIFY(args.contains(QStringLiteral("-color_trc")));
    // NO -shortest: it truncated the delayed encoder tail whenever the
    // source audio ran marginally shorter than the video (694/697 frames
    // on a real clip) — the -to window and -frames:v already bound both
    // streams.
    QVERIFY(!args.contains(QStringLiteral("-shortest")));
    const int framesIdx = args.indexOf(QStringLiteral("-frames:v"));
    QVERIFY(framesIdx > 0);
    QCOMPARE(args.at(framesIdx + 1), QStringLiteral("75"));
    QCOMPARE(args.last(), plan.outputPath);
}

void CompositeExportTest::seekBackoff()
{
    // The -ss/-to spellings back off a quarter frame from the snapped
    // boundaries (decimal rounding must never flip a boundary frame), and
    // frame 0 clamps at 0.
    FFmpegArgs::CompositePlan plan;
    plan.clipPath = QStringLiteral("clip.mp4");
    plan.fps = {30000, 1001};
    plan.inPoint = 30 * 1001.0 / 30000.0;
    plan.outPoint = 105 * 1001.0 / 30000.0;
    plan.totalFrames = 75;
    plan.outputPath = QStringLiteral("out.mp4");

    const double frameDur = 1001.0 / 30000.0;
    QStringList args = FFmpegArgs::compositeArgs(plan);
    QCOMPARE(args.at(args.indexOf(QStringLiteral("-ss")) + 1),
             QString::number(plan.inPoint - 0.25 * frameDur, 'f', 6));
    QCOMPARE(args.at(args.indexOf(QStringLiteral("-to")) + 1),
             QString::number(plan.outPoint + 0.25 * frameDur, 'f', 6));

    plan.inPoint = 0.0;
    plan.outPoint = 150 * frameDur;
    args = FFmpegArgs::compositeArgs(plan);
    QCOMPARE(args.at(args.indexOf(QStringLiteral("-ss")) + 1),
             QStringLiteral("0.000000"));

    // A source whose video stream does not start at 0 (e.g. AAC-priming
    // muxer shift) gets the offset folded into the seek window, so the
    // fps grid still lands on real frames.
    plan.sourceStartOffset = 0.023233;
    args = FFmpegArgs::compositeArgs(plan);
    QCOMPARE(args.at(args.indexOf(QStringLiteral("-ss")) + 1),
             QString::number(0.023233 - 0.25 * frameDur, 'f', 6));
    QCOMPARE(args.at(args.indexOf(QStringLiteral("-to")) + 1),
             QString::number(150 * frameDur + 0.023233 + 0.25 * frameDur, 'f', 6));
}

void CompositeExportTest::placementRectParity()
{
    // KEEP IN SYNC with VideoOverlayItem.qml: each component Math.round()ed
    // independently. 0.02*1920 = 38.4 → 38; 0.55*1080 = 594; 0.30*1920 =
    // 576; 0.40*1080 = 432 (the default dive-computer placement).
    QCOMPARE(FrameCompositor::placementRect(QRectF(0.02, 0.55, 0.30, 0.40),
                                            QSize(1920, 1080)),
             QRect(38, 594, 576, 432));
    // Half-pixel: JS Math.round(959.5) == 960 (half up) == qRound here.
    QCOMPARE(FrameCompositor::placementRect(QRectF(0.5, 0.0, 0.25, 1.0),
                                            QSize(1919, 1080)),
             QRect(960, 0, 480, 1080));
}

void CompositeExportTest::fitRectParity()
{
    // Image.PreserveAspectFit: uniform min-ratio scale, centred both axes.
    // Exact fit: 800x600 in 576x432 → scale 0.72 both → fills the box.
    QCOMPARE(FrameCompositor::fitRect(QSize(800, 600), QRect(38, 594, 576, 432)),
             QRect(38, 594, 576, 432));
    // Square image in a wide box: height-limited, centred horizontally
    // ((576-432)/2 = 72).
    QCOMPARE(FrameCompositor::fitRect(QSize(100, 100), QRect(0, 0, 576, 432)),
             QRect(72, 0, 432, 432));
    // Half-pixel centring: (50-25) scaled 1.0 into a 50x50 box → y offset
    // (50-25)/2 = 12.5 → rounds to 13 (Math.round half-up parity).
    QCOMPARE(FrameCompositor::fitRect(QSize(50, 25), QRect(0, 0, 50, 50)),
             QRect(0, 13, 50, 25));
    // Degenerate inputs.
    QVERIFY(FrameCompositor::fitRect(QSize(), QRect(0, 0, 10, 10)).isEmpty());
    QVERIFY(FrameCompositor::fitRect(QSize(10, 10), QRect()).isEmpty());
}

void CompositeExportTest::composeFramePixels()
{
    SolidGenerator red(QSize(50, 25), QColor(255, 0, 0, 255));
    SolidGenerator blue(QSize(10, 10), QColor(0, 0, 255, 255));

    FrameCompositor compositor(QSize(100, 50));
    OverlayPlacement left{true, QRectF(0.0, 0.0, 0.5, 1.0)};   // box 50x50
    OverlayPlacement hidden{false, QRectF(0.5, 0.0, 0.5, 1.0)};
    compositor.addLayer(&red, left);
    compositor.addLayer(&blue, hidden);

    const QImage frame = compositor.composeFrame(nullptr, 0.0);
    QCOMPARE(frame.format(), QImage::Format_RGBA8888_Premultiplied);
    QCOMPARE(frame.size(), QSize(100, 50));

    // Red layer: 50x25 fitted into (0,0,50,50) → (0,13,50,25).
    QCOMPARE(frame.pixelColor(10, 20), QColor(255, 0, 0, 255));
    QCOMPARE(frame.pixelColor(10, 5).alpha(), 0);   // above the fitted rect
    QCOMPARE(frame.pixelColor(10, 45).alpha(), 0);  // below it
    // Hidden layer contributed nothing.
    QCOMPARE(frame.pixelColor(75, 25).alpha(), 0);
    QCOMPARE(blue.generated, 0);
    QCOMPARE(red.generated, 1);
}

void CompositeExportTest::composeFramePreDiveIsTransparent()
{
    // Preview-of-export parity: VideoOverlayItem.qml hides both overlays
    // while diveTime < 0 (pre-dive footage of a negative-syncOffset clip).
    // The compositor must not ask a generator for a negative time (it would
    // clamp to sample 0 and burn a frozen overlay) — yet must still return
    // a real transparent canvas, never a null image (null = "repeat the
    // previous frame" in streamFrames, and a fatal error for frame 0).
    SolidGenerator red(QSize(50, 25), QColor(255, 0, 0, 255));
    FrameCompositor compositor(QSize(100, 50));
    compositor.addLayer(&red, OverlayPlacement{true, QRectF(0.0, 0.0, 0.5, 1.0)});

    for (double t : {-1.0, -0.001}) {
        const QImage frame = compositor.composeFrame(nullptr, t);
        QVERIFY2(!frame.isNull(), qPrintable(QString::number(t)));
        QCOMPARE(frame.size(), QSize(100, 50));
        QCOMPARE(frame.format(), QImage::Format_RGBA8888_Premultiplied);
        for (int y = 0; y < frame.height(); ++y) {
            const uchar *row = frame.constScanLine(y);
            for (int x = 0; x < frame.width(); ++x) {
                if (row[x * 4 + 3] != 0)
                    QFAIL(qPrintable(QStringLiteral("t=%1: opaque pixel at %2,%3")
                                         .arg(t).arg(x).arg(y)));
            }
        }
    }
    QCOMPARE(red.generated, 0);

    // Over-gating guard: t == 0 is IN the dive (the preview's gate is >= 0).
    const QImage atZero = compositor.composeFrame(nullptr, 0.0);
    QCOMPARE(red.generated, 1);
    QCOMPARE(atZero.pixelColor(10, 20), QColor(255, 0, 0, 255));
    compositor.composeFrame(nullptr, 1.5);
    QCOMPARE(red.generated, 2);
}

void CompositeExportTest::beginEndFanOut()
{
    // One begin/end per DISTINCT generator, however many layers it backs.
    SolidGenerator gen(QSize(10, 10), Qt::white);
    FrameCompositor compositor(QSize(100, 50));
    compositor.addLayer(&gen, OverlayPlacement{true, QRectF(0, 0, 0.5, 0.5)});
    compositor.addLayer(&gen, OverlayPlacement{true, QRectF(0.5, 0.5, 0.5, 0.5)});

    CompositorFrameSource source(&compositor);
    source.beginExport();
    source.endExport();
    QCOMPARE(gen.beginCalls, 1);
    QCOMPARE(gen.endCalls, 1);
}

// ---------------------------------------------------------------------
// Part 2 — end-to-end
// ---------------------------------------------------------------------

bool CompositeExportTest::synthesizeSource()
{
    if (m_sourceReady)
        return true;

    const QString ffmpeg = FFmpegProbe::instance()->ffmpegPath();
    if (ffmpeg.isEmpty())
        return false;

    // 150 frames at 30000/1001, every frame a flat gray N*7 mod 256
    // (adjacent frames differ by 7), lossless ffv1 in NUT (exact rational
    // timestamps — mkv would quantize to milliseconds) + an AAC sine track
    // so the mp4 audio COPY path is exercised, not just asserted. The
    // audio is made deliberately SHORTER than the video (4.9 s vs 5.005 s,
    // like real camera files whose audio ends early): a -shortest in the
    // composite command would then truncate the encoder tail and fail the
    // exact-frame-count assertion — the bug class this fixture pins.
    QProcess proc;
    proc.start(ffmpeg,
               {QStringLiteral("-y"), QStringLiteral("-hide_banner"),
                QStringLiteral("-f"), QStringLiteral("lavfi"),
                QStringLiteral("-i"),
                QStringLiteral("color=c=black:s=%1x%2:r=%3/%4,"
                               "geq=r='mod(N*7,256)':g='mod(N*7,256)':b='mod(N*7,256)',"
                               "format=gbrp")
                    .arg(kSrcW).arg(kSrcH).arg(kFpsNum).arg(kFpsDen),
                QStringLiteral("-f"), QStringLiteral("lavfi"),
                QStringLiteral("-i"),
                QStringLiteral("sine=frequency=440:sample_rate=44100:duration=4.9"),
                QStringLiteral("-frames:v"), QString::number(kSrcFrames),
                QStringLiteral("-c:v"), QStringLiteral("ffv1"),
                QStringLiteral("-c:a"), QStringLiteral("aac"),
                QStringLiteral("-b:a"), QStringLiteral("128k"),
                m_sourcePath});
    if (!proc.waitForFinished(60000) || proc.exitCode() != 0) {
        qWarning().noquote() << "source synthesis failed:"
                             << QString::fromUtf8(proc.readAllStandardError());
        return false;
    }
    m_sourceReady = QFile::exists(m_sourcePath);
    return m_sourceReady;
}

QMap<QString, QString> CompositeExportTest::probeStream(
    const QString &path, const QString &selector, const QString &entries)
{
    QMap<QString, QString> result;
    const QString ffprobe = FFmpegProbe::instance()->ffprobePath();
    if (ffprobe.isEmpty())
        return result;

    QProcess proc;
    proc.start(ffprobe,
               {QStringLiteral("-v"), QStringLiteral("error"),
                QStringLiteral("-count_frames"),
                QStringLiteral("-select_streams"), selector,
                QStringLiteral("-show_entries"), entries,
                QStringLiteral("-of"), QStringLiteral("default=nw=1"),
                path});
    if (!proc.waitForFinished(30000))
        return result;
    const QStringList lines =
        QString::fromUtf8(proc.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int eq = line.indexOf('=');
        if (eq > 0)
            result.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
    }
    return result;
}

QByteArray CompositeExportTest::decodeRgb24(const QString &path)
{
    const QString ffmpeg = FFmpegProbe::instance()->ffmpegPath();
    QProcess proc;
    proc.start(ffmpeg,
               {QStringLiteral("-v"), QStringLiteral("error"),
                QStringLiteral("-i"), path,
                QStringLiteral("-f"), QStringLiteral("rawvideo"),
                QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
                QStringLiteral("pipe:1")});
    QByteArray out;
    while (proc.waitForReadyRead(30000))
        out += proc.readAllStandardOutput();
    proc.waitForFinished(30000);
    out += proc.readAllStandardOutput();
    return out;
}

void CompositeExportTest::compositeEndToEnd_data()
{
    QTest::addColumn<int>("startFrame");
    QTest::addColumn<int>("endFrame");

    // Whole clip and a mid-clip sub-range: the sub-range proves the
    // -ss seek window lands exactly on the requested frame.
    QTest::newRow("whole clip 150") << 0 << kSrcFrames;
    QTest::newRow("sub-range 30..105") << 30 << 105;
}

void CompositeExportTest::compositeEndToEnd()
{
    QFETCH(int, startFrame);
    QFETCH(int, endFrame);

    if (!FFmpegProbe::instance()->isAvailable())
        QSKIP("FFmpeg not found — composite end-to-end test skipped");
    if (FFmpegProbe::instance()->ffprobePath().isEmpty())
        QSKIP("ffprobe not found — composite end-to-end test skipped");
    if (!FFmpegProbe::instance()->hasEncoder(QStringLiteral("libx264")))
        QSKIP("FFmpeg lacks libx264 — composite end-to-end test skipped");
    QVERIFY2(synthesizeSource(), "failed to synthesize the frame-coded source");

    // A real probed clip (this also exercises VideoProbe on the synthetic
    // file): 64x48, 30000/1001, AAC audio.
    VideoClip clip(m_sourcePath);
    clip.applyProbeResult(VideoProbe::probe(m_sourcePath));
    QCOMPARE(clip.probeState(), VideoClip::Probed);
    QCOMPARE(clip.width(), kSrcW);
    QCOMPARE(clip.height(), kSrcH);
    QCOMPARE(clip.fpsNum(), kFpsNum);
    QCOMPARE(clip.fpsDen(), kFpsDen);
    QVERIFY(clip.hasAudio());
    QCOMPARE(clip.audioCodec(), QStringLiteral("aac"));
    clip.setSyncOffset(0.0); // dive time == clip time: the stub's coding relies on it

    // Full-frame dive-computer placement (identity fit: the stub renders at
    // clip size, so no scaling blurs the coded bar); profile hidden.
    VideoOverlayLayout layout;
    layout.diveComputer = OverlayPlacement{true, QRectF(0, 0, 1, 1)};
    layout.diveProfile.visible = false;
    clip.setOverlayLayout(layout);

    DiveData dive;
    dive.setDiveName(QStringLiteral("CompositeTest"));

    FrameCodeGenerator stub;
    QTemporaryDir outDir;
    QVERIFY(outDir.isValid());

    VideoExporter exporter;
    exporter.setBaseDirectory(outDir.path());
    exporter.setVideoCodec(QStringLiteral("h264"));
    exporter.setVideoQuality(QStringLiteral("best")); // CRF 18: tiny flat frames, near-lossless

    const int expectedFrames = endFrame - startFrame;
    const double in = startFrame * static_cast<double>(kFpsDen) / kFpsNum;
    const double out = endFrame * static_cast<double>(kFpsDen) / kFpsNum;

    QSignalSpy finishedSpy(&exporter, &VideoExporter::exportFinished);
    QSignalSpy errorSpy(&exporter, &VideoExporter::exportError);
    QSignalSpy cancelledSpy(&exporter, &VideoExporter::exportCancelled);

    // No-grade byte-stability (PR C5): a clip without a grade must produce
    // the identical pre-C5 graph — no colour filter may appear in the
    // assembled command.
    CommandCapture capture;
    QVERIFY(exporter.exportComposite(&dive, &clip, &stub, nullptr, in, out));
    QCOMPARE(g_capturedCommands.size(), 1);
    QVERIFY2(!g_capturedCommands.first().contains(QLatin1String("lut3d"))
                 && !g_capturedCommands.first().contains(QLatin1String("sendcmd")),
             qPrintable(g_capturedCommands.first()));

    QTRY_VERIFY_WITH_TIMEOUT(
        finishedSpy.count() > 0 || errorSpy.count() > 0 || cancelledSpy.count() > 0,
        60000);
    QVERIFY2(errorSpy.isEmpty(),
             qPrintable(errorSpy.isEmpty() ? QString()
                                           : errorSpy.first().first().toString()));
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(!exporter.isBusy());
    QCOMPARE(stub.beginCalls, 1);
    QCOMPARE(stub.endCalls, 1);
    QCOMPARE(stub.generatedFrames, expectedFrames);

    const QString outputPath = finishedSpy.takeFirst().at(1).toString();
    QVERIFY2(QFile::exists(outputPath), qPrintable(outputPath));
    QVERIFY(outputPath.endsWith(QStringLiteral(".mp4")));

    // Container facts: exact frame count, clip-native rate, yuv420p,
    // bt709/tv tags on the video stream; the AAC track made it across
    // (decideAudio: copy — the unit rows pin the decision itself).
    const QMap<QString, QString> v = probeStream(
        outputPath, QStringLiteral("v:0"),
        QStringLiteral("stream=nb_read_frames,r_frame_rate,pix_fmt,width,height,"
                       "color_space,color_transfer,color_primaries,color_range"));
    QVERIFY2(!v.isEmpty(), "ffprobe produced no video stream info");
    QCOMPARE(v.value(QStringLiteral("nb_read_frames")).toInt(), expectedFrames);
    QCOMPARE(v.value(QStringLiteral("r_frame_rate")), QStringLiteral("30000/1001"));
    QCOMPARE(v.value(QStringLiteral("pix_fmt")), QStringLiteral("yuv420p"));
    QCOMPARE(v.value(QStringLiteral("width")).toInt(), kSrcW);
    QCOMPARE(v.value(QStringLiteral("height")).toInt(), kSrcH);
    QCOMPARE(v.value(QStringLiteral("color_space")), QStringLiteral("bt709"));
    QCOMPARE(v.value(QStringLiteral("color_transfer")), QStringLiteral("bt709"));
    QCOMPARE(v.value(QStringLiteral("color_primaries")), QStringLiteral("bt709"));
    QCOMPARE(v.value(QStringLiteral("color_range")), QStringLiteral("tv"));

    const QMap<QString, QString> a = probeStream(
        outputPath, QStringLiteral("a:0"), QStringLiteral("stream=codec_name"));
    QCOMPARE(a.value(QStringLiteral("codec_name")), QStringLiteral("aac"));

    // Frame alignment: decode the whole output to rgb24 and check first /
    // middle / last frames. Source region (below the bar) must carry
    // (startFrame+k)*7 mod 256 — proving the seek window started on the
    // exact requested frame — and the overlay bar must carry the SAME
    // value — proving overlay frame k landed on source frame k. The ±3
    // tolerance absorbs the YUV round trip; an off-by-one is a ±7 step.
    const QByteArray rgb = decodeRgb24(outputPath);
    const int frameBytes = kSrcW * kSrcH * 3;
    QCOMPARE(rgb.size(), qint64(frameBytes) * expectedFrames);

    const auto grayAt = [&](int frame, int x, int y) {
        const uchar *p = reinterpret_cast<const uchar *>(rgb.constData())
                         + qint64(frame) * frameBytes + (y * kSrcW + x) * 3;
        return static_cast<int>(p[0]);
    };

    for (int k : {0, expectedFrames / 2, expectedFrames - 1}) {
        const int expected = ((startFrame + k) * 7) % 256;
        const int source = grayAt(k, kSrcW / 2, 30); // below the overlay bar
        const int overlay = grayAt(k, kSrcW / 2, 4); // inside the bar
        QVERIFY2(grayDiff(source, expected) <= 3,
                 qPrintable(QStringLiteral("frame %1: source gray %2, expected %3")
                                .arg(k).arg(source).arg(expected)));
        QVERIFY2(grayDiff(overlay, expected) <= 3,
                 qPrintable(QStringLiteral("frame %1: overlay gray %2, expected %3")
                                .arg(k).arg(overlay).arg(expected)));
    }
}

void CompositeExportTest::compositeFfmpegFailureCleansUp()
{
    // FFmpeg failing mid-run (nonzero exit, or the pipe-error kill when the
    // render loop notices first — timing decides, both must behave the
    // same): the partial output file is removed and the user-facing error
    // carries FFmpeg's own last stderr lines, not just a code. The failure
    // is forced deterministically through the colour-chain hook with a
    // crop larger than the 64x48 source: it parses fine, then fails at
    // filter configuration — AFTER the muxer has created the (0-byte)
    // output file, which is exactly what the cleanup must remove.
    if (!FFmpegProbe::instance()->isAvailable())
        QSKIP("FFmpeg not found — composite failure test skipped");
    if (FFmpegProbe::instance()->ffprobePath().isEmpty())
        QSKIP("ffprobe not found — composite failure test skipped");
    if (!FFmpegProbe::instance()->hasEncoder(QStringLiteral("libx264")))
        QSKIP("FFmpeg lacks libx264 — composite failure test skipped");
    QVERIFY2(synthesizeSource(), "failed to synthesize the frame-coded source");

    VideoClip clip(m_sourcePath);
    clip.applyProbeResult(VideoProbe::probe(m_sourcePath));
    QCOMPARE(clip.probeState(), VideoClip::Probed);
    clip.setSyncOffset(0.0);
    VideoOverlayLayout layout;
    layout.diveComputer = OverlayPlacement{true, QRectF(0, 0, 1, 1)};
    layout.diveProfile.visible = false;
    clip.setOverlayLayout(layout);

    DiveData dive;
    dive.setDiveName(QStringLiteral("CompositeFailTest"));
    FrameCodeGenerator stub;
    QTemporaryDir outDir;
    QVERIFY(outDir.isValid());

    VideoExporter exporter;
    exporter.setBaseDirectory(outDir.path());
    exporter.setVideoCodec(QStringLiteral("h264"));
    exporter.setVideoQuality(QStringLiteral("standard"));

    QSignalSpy finishedSpy(&exporter, &VideoExporter::exportFinished);
    QSignalSpy errorSpy(&exporter, &VideoExporter::exportError);
    QSignalSpy cancelledSpy(&exporter, &VideoExporter::exportCancelled);

    exporter.setColorFilterChain(QStringLiteral("crop=w=100000:h=100000"));
    // Return value deliberately unchecked: true (all frames delivered
    // before FFmpeg died) or false (pipe failed mid-stream) are both legal.
    exporter.exportComposite(&dive, &clip, &stub, nullptr, 0.0,
                             kSrcFrames * static_cast<double>(kFpsDen) / kFpsNum);

    QTRY_VERIFY_WITH_TIMEOUT(
        finishedSpy.count() > 0 || errorSpy.count() > 0 || cancelledSpy.count() > 0,
        60000);
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(finishedSpy.isEmpty());
    QVERIFY(cancelledSpy.isEmpty());
    QVERIFY(!exporter.isBusy());

    const QString message = errorSpy.first().first().toString();
    // Generic text, blank line, then FFmpeg's last (at most 3) non-empty
    // stderr lines — an excerpt, never the whole 4000-char rolling tail.
    // The exact FFmpeg wording is version-dependent, so only the shape is
    // pinned (with -loglevel warning FFmpeg 7 prints no "Conversion
    // failed!"; its last lines are the encoder/muxer cascade).
    const int split = message.indexOf(QLatin1String("\n\n"));
    QVERIFY2(split > 0, qPrintable(message));
    const QString head = message.left(split);
    QVERIFY2(head.startsWith(QLatin1String("FFmpeg exited with error code"))
                 || head == QLatin1String("Failed to write frames to FFmpeg"),
             qPrintable(message));
    const QStringList excerpt =
        message.mid(split + 2).split(QLatin1Char('\n'));
    QVERIFY2(!excerpt.isEmpty() && excerpt.size() <= 3, qPrintable(message));
    for (const QString &line : excerpt)
        QVERIFY2(!line.trimmed().isEmpty(), qPrintable(message));

    // The run's (empty/truncated) output file is gone; nothing else was
    // ever in this fresh directory.
    const QStringList left = QDir(outDir.path()).entryList(QDir::Files | QDir::NoDotAndDotDot);
    QVERIFY2(left.isEmpty(), qPrintable(left.join(QLatin1String(", "))));
}

void CompositeExportTest::overlayOnlyPreDiveFramesAreBlank()
{
    // Plain overlay-only export (exportVideo) over a range that starts
    // before the dive — the "imported video range" of a clip starting
    // pre-dive: [-1, 1) at 10 fps = 20 frames, the first 10 pre-dive. The
    // generator must never be asked for t < 0, the container must hold the
    // FULL 20 frames (no clamp, no skip), and the pre-dive frames must be
    // blank. h264 drops alpha (rgba -> yuv420p), so a transparent frame
    // decodes black while the opaque red overlay decodes red.
    if (!FFmpegProbe::instance()->isAvailable())
        QSKIP("FFmpeg not found — overlay-only pre-dive test skipped");
    if (FFmpegProbe::instance()->ffprobePath().isEmpty())
        QSKIP("ffprobe not found — overlay-only pre-dive test skipped");
    if (!FFmpegProbe::instance()->hasEncoder(QStringLiteral("libx264")))
        QSKIP("FFmpeg lacks libx264 — overlay-only pre-dive test skipped");

    DiveData dive;
    dive.setDiveName(QStringLiteral("PreDiveOverlayTest"));
    RecordingRedGenerator gen;
    QTemporaryDir outDir;
    QVERIFY(outDir.isValid());

    VideoExporter exporter;
    exporter.setBaseDirectory(outDir.path());
    exporter.setVideoCodec(QStringLiteral("h264"));
    exporter.setVideoQuality(QStringLiteral("standard"));
    exporter.setFrameRate(10.0);

    QSignalSpy finishedSpy(&exporter, &VideoExporter::exportFinished);
    QSignalSpy errorSpy(&exporter, &VideoExporter::exportError);
    QVERIFY(exporter.exportVideo(&dive, &gen, -1.0, 1.0));
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0 || errorSpy.count() > 0, 60000);
    QVERIFY2(errorSpy.isEmpty(),
             qPrintable(errorSpy.isEmpty() ? QString()
                                           : errorSpy.first().first().toString()));
    QVERIFY(finishedSpy.first().at(0).toBool());
    const QString outPath = finishedSpy.first().at(1).toString();

    for (double t : std::as_const(gen.times))
        QVERIFY2(t >= 0.0, qPrintable(QString::number(t)));

    const auto v = probeStream(outPath, QStringLiteral("v:0"),
                               QStringLiteral("stream=nb_read_frames"));
    QCOMPARE(v.value(QStringLiteral("nb_read_frames")), QStringLiteral("20"));

    const QByteArray rgb = decodeRgb24(outPath);
    const int frameBytes = kSrcW * kSrcH * 3;
    QCOMPARE(rgb.size(), 20 * frameBytes);
    for (int k = 0; k < 20; ++k) {
        // Centre pixel (away from chroma-subsampling edges).
        const int off = k * frameBytes + ((kSrcH / 2) * kSrcW + kSrcW / 2) * 3;
        const int r = static_cast<unsigned char>(rgb.at(off));
        const int g = static_cast<unsigned char>(rgb.at(off + 1));
        if (k < 10) {
            QVERIFY2(r < 24 && g < 24,
                     qPrintable(QStringLiteral("pre-dive frame %1 not blank: r=%2 g=%3")
                                    .arg(k).arg(r).arg(g)));
        } else {
            QVERIFY2(r > 200 && g < 60,
                     qPrintable(QStringLiteral("dive frame %1 not red: r=%2 g=%3")
                                    .arg(k).arg(r).arg(g)));
        }
    }
}

// ---------------------------------------------------------------------
// Part 3 — colour-grade export integration (PR C5)
// ---------------------------------------------------------------------

// Skip guards shared by the graded end-to-end tests. A macro because QSKIP
// must return from the TEST SLOT, not from a helper.
#define SKIP_WITHOUT_GRADE_E2E_PREREQS()                                        \
    do {                                                                        \
        if (!FFmpegProbe::instance()->isAvailable())                            \
            QSKIP("FFmpeg not found — graded composite test skipped");          \
        if (FFmpegProbe::instance()->ffprobePath().isEmpty())                   \
            QSKIP("ffprobe not found — graded composite test skipped");         \
        if (!FFmpegProbe::instance()->hasEncoder(QStringLiteral("libx264")))    \
            QSKIP("FFmpeg lacks libx264 — graded composite test skipped");      \
        if (!FFmpegProbe::instance()->hasEncoder(QStringLiteral("prores_ks")))  \
            QSKIP("FFmpeg lacks prores_ks — graded composite test skipped");    \
        if (!FFmpegProbe::instance()->hasFilter(QStringLiteral("lut3d"))        \
            || !FFmpegProbe::instance()->hasFilter(QStringLiteral("sendcmd")))  \
            QSKIP("FFmpeg lacks lut3d/sendcmd — graded composite test skipped");\
    } while (false)

bool CompositeExportTest::synthesizeColorSource()
{
    if (m_colorSourceReady)
        return true;

    const QString ffmpeg = FFmpegProbe::instance()->ffmpegPath();
    if (ffmpeg.isEmpty())
        return false;

    // Four flat colour quadrants, identical on every frame, looped from a
    // PNG into lossless ffv1 RGB (gbrp): the decoded source bytes ARE the
    // PNG's bytes, so GradeOps::apply() on kQuadColors is an exact
    // reference for the whole pipeline (no YUV loss on the way IN).
    QImage pattern(kSrcW, kSrcH, QImage::Format_RGB888);
    QPainter p(&pattern);
    p.fillRect(QRect(0, 0, kSrcW / 2, kSrcH / 2), kQuadColors[0]);
    p.fillRect(QRect(kSrcW / 2, 0, kSrcW / 2, kSrcH / 2), kQuadColors[1]);
    p.fillRect(QRect(0, kSrcH / 2, kSrcW / 2, kSrcH / 2), kQuadColors[2]);
    p.fillRect(QRect(kSrcW / 2, kSrcH / 2, kSrcW / 2, kSrcH / 2), kQuadColors[3]);
    p.end();
    const QString pngPath = m_workDir.filePath(QStringLiteral("quadrants.png"));
    if (!pattern.save(pngPath))
        return false;

    QProcess proc;
    proc.start(ffmpeg,
               {QStringLiteral("-y"), QStringLiteral("-hide_banner"),
                QStringLiteral("-loop"), QStringLiteral("1"),
                QStringLiteral("-framerate"), QString::number(kColorFps),
                QStringLiteral("-i"), pngPath,
                QStringLiteral("-frames:v"), QString::number(kColorFrames),
                QStringLiteral("-c:v"), QStringLiteral("ffv1"),
                QStringLiteral("-pix_fmt"), QStringLiteral("gbrp"),
                m_colorSourcePath});
    if (!proc.waitForFinished(60000) || proc.exitCode() != 0) {
        qWarning().noquote() << "colour source synthesis failed:"
                             << QString::fromUtf8(proc.readAllStandardError());
        return false;
    }
    m_colorSourceReady = QFile::exists(m_colorSourcePath);
    return m_colorSourceReady;
}

void CompositeExportTest::compositeGradeTexts()
{
    // Pure logic, always run (no FFmpeg): the dialog rows must say exactly
    // what the export will do.
    VideoExporter exporter;

    // Null clip / unprobed values → empty everywhere.
    QCOMPARE(exporter.compositeColorTagText(QString(), 0), QString());
    QCOMPARE(exporter.compositeGradeText(nullptr, nullptr), QString());

    // Tagged clip → no colour-tag advisory (the "verify absence" case).
    QCOMPARE(exporter.compositeColorTagText(QStringLiteral("bt709"), 1080),
             QString());

    // Untagged HD → BT.709 wording; untagged SD → BT.601 — the same
    // decision inColorMatrixFor takes for the export itself.
    QVERIFY(exporter.compositeColorTagText(QString(), 1080)
                .contains(QStringLiteral("BT.709")));
    QVERIFY(exporter.compositeColorTagText(QStringLiteral("unknown"), 720)
                .contains(QStringLiteral("BT.709")));
    QVERIFY(exporter.compositeColorTagText(QString(), 480)
                .contains(QStringLiteral("BT.601")));

    VideoProbe::Result probed;
    probed.ok = true;
    probed.width = 1920;
    probed.height = 1080;
    probed.fpsNum = 30;
    probed.fpsDen = 1;
    probed.durationSeconds = 10.0;
    probed.colorSpace = QStringLiteral("bt709");
    VideoClip tagged(QStringLiteral("/videos/tagged.mp4"));
    tagged.applyProbeResult(probed);

    // Grade row: no grade / disabled grade → empty (row hidden).
    QCOMPARE(exporter.compositeGradeText(&tagged, nullptr), QString());
    Unabara::ColorGrade grade;
    grade.enabled = false;
    grade.anchors << Unabara::GradeAnchor{};
    tagged.setColorGrade(grade.toJson());
    QCOMPARE(exporter.compositeGradeText(&tagged, nullptr), QString());

    // Enabled static grade → "(static)".
    grade.enabled = true;
    tagged.setColorGrade(grade.toJson());
    QVERIFY(exporter.compositeGradeText(&tagged, nullptr)
                .contains(QStringLiteral("(static)")));

    // Depth-linked grade with a real ramp: "(depth-linked)" only when a
    // dive is present — without one plan() clamps to the shallow anchor
    // (static route), and the row must agree (usesDepthPath is shared).
    DiveData dive;
    dive.addDataPoint(DiveDataPoint(0.0, 0.0));
    dive.addDataPoint(DiveDataPoint(10.0, 30.0));
    grade.depthLinked = true;
    Unabara::GradeAnchor deep;
    deep.depth = 30.0;
    deep.params.redRecovery = 1.0;
    grade.anchors << deep;
    tagged.setColorGrade(grade.toJson());
    QVERIFY(exporter.compositeGradeText(&tagged, nullptr)
                .contains(QStringLiteral("(static)")));
    QVERIFY(exporter.compositeGradeText(&tagged, &dive)
                .contains(QStringLiteral("(depth-linked)")));

    // Missing-LUT advisory rides along on the grade row.
    grade.lutPath = m_workDir.filePath(QStringLiteral("gone.cube"));
    tagged.setColorGrade(grade.toJson());
    QVERIFY(exporter.compositeGradeText(&tagged, &dive)
                .contains(QStringLiteral("LUT file missing")));
}

void CompositeExportTest::gradedCompositeStaticParity()
{
    SKIP_WITHOUT_GRADE_E2E_PREREQS();
    QVERIFY2(synthesizeColorSource(), "failed to synthesize the quadrant source");

    VideoClip clip(m_colorSourcePath);
    clip.applyProbeResult(VideoProbe::probe(m_colorSourcePath));
    QCOMPARE(clip.probeState(), VideoClip::Probed);
    clip.setSyncOffset(0.0);

    VideoOverlayLayout layout;
    layout.diveComputer = OverlayPlacement{true, QRectF(0, 0, 1, 1)};
    layout.diveProfile.visible = false;
    clip.setOverlayLayout(layout);

    // A strong static grade stored ON THE CLIP — exportComposite must arm
    // it by itself (the C5 design decision under test).
    Unabara::ColorGrade grade;
    grade.enabled = true;
    Unabara::GradeAnchor anchor;
    anchor.params.redRecovery = 0.8;
    anchor.params.waterTint = -0.4;
    anchor.params.clarity = 0.3;
    grade.anchors << anchor;
    clip.setColorGrade(grade.toJson());
    const Unabara::GradeOps ops = grade.resolveStatic();

    // The marker colour must move by a lot IF it were graded, so the
    // "overlay stays ungraded" assertion below cannot pass vacuously.
    {
        int gr, gg, gb;
        applyExpected(ops, kMarkerColor, &gr, &gg, &gb);
        QVERIFY(qAbs(gr - kMarkerColor.red()) > 8
                || qAbs(gg - kMarkerColor.green()) > 8
                || qAbs(gb - kMarkerColor.blue()) > 8);
    }

    DiveData dive;
    MarkerBarGenerator marker;
    QTemporaryDir outDir;
    QVERIFY(outDir.isValid());

    VideoExporter exporter;
    exporter.setBaseDirectory(outDir.path());
    // ProRes 422 HQ: 10-bit YUV keeps the codec's contribution to the
    // parity budget ~1 LSB — h264's 8-bit U quantization alone costs ~2 in
    // blue (gain 1.86) before the encoder even starts.
    exporter.setVideoCodec(QStringLiteral("prores"));
    exporter.setVideoQuality(QStringLiteral("best"));

    const int frames = 30; // 1 s is plenty for a static grade
    const double out = frames / static_cast<double>(kColorFps);

    QSignalSpy finishedSpy(&exporter, &VideoExporter::exportFinished);
    QSignalSpy errorSpy(&exporter, &VideoExporter::exportError);

    CommandCapture capture;
    QVERIFY(exporter.exportComposite(&dive, &clip, &marker, nullptr, 0.0, out));
    // The clip's grade reached the command line as the static lut3d route.
    QCOMPARE(g_capturedCommands.size(), 1);
    QVERIFY2(g_capturedCommands.first().contains(QLatin1String("lut3d")),
             qPrintable(g_capturedCommands.first()));

    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0 || errorSpy.count() > 0, 60000);
    QVERIFY2(errorSpy.isEmpty(),
             qPrintable(errorSpy.isEmpty() ? QString()
                                           : errorSpy.first().first().toString()));
    const QString outputPath = finishedSpy.takeFirst().at(1).toString();

    const QByteArray rgb = decodeRgb24(outputPath);
    const int frameBytes = kSrcW * kSrcH * 3;
    QCOMPARE(rgb.size(), qint64(frameBytes) * frames);
    const auto pixel = [&](int frame, int x, int y, int c) {
        return static_cast<int>(static_cast<uchar>(
            rgb.at(qint64(frame) * frameBytes + (y * kSrcW + x) * 3 + c)));
    };

    // Full-pipeline parity bound, derived: lut3d parity ≤1 LSB (C1 proven)
    // + the inherent RGB→YUV→RGB round trip — ≤1 LSB at ProRes's 10 bits
    // (the plan's "≈1-2 LSB, document don't chase" figure is for 8-bit
    // YUV) — + ProRes 422 HQ quantization on flat regions ≈1 LSB → 3, with
    // 1 LSB margin → 4 on flat-patch centres. (The C1 parity test bounded
    // the FILTER in isolation; this bounds the whole composited pipeline.)
    const int kTol = 4;
    for (int frame : {0, frames - 1}) {
        for (int q = 0; q < 4; ++q) {
            int er, eg, eb;
            applyExpected(ops, kQuadColors[q], &er, &eg, &eb);
            const QPoint pt = kQuadCenters[q];
            const int dr = qAbs(pixel(frame, pt.x(), pt.y(), 0) - er);
            const int dg = qAbs(pixel(frame, pt.x(), pt.y(), 1) - eg);
            const int db = qAbs(pixel(frame, pt.x(), pt.y(), 2) - eb);
            QVERIFY2(dr <= kTol && dg <= kTol && db <= kTol,
                     qPrintable(QStringLiteral(
                                    "frame %1 quadrant %2: got (%3,%4,%5), expected "
                                    "(%6,%7,%8)")
                                    .arg(frame).arg(q)
                                    .arg(pixel(frame, pt.x(), pt.y(), 0))
                                    .arg(pixel(frame, pt.x(), pt.y(), 1))
                                    .arg(pixel(frame, pt.x(), pt.y(), 2))
                                    .arg(er).arg(eg).arg(eb)));
        }

        // THE key regression this test pins: the snippet sits between
        // format=rgb24 and overlay, so the overlay marker must come
        // through UNGRADED (only the YUV round trip applies) while the
        // footage around it shifted.
        const int mr = pixel(frame, kSrcW / 2, 4, 0);
        const int mg = pixel(frame, kSrcW / 2, 4, 1);
        const int mb = pixel(frame, kSrcW / 2, 4, 2);
        QVERIFY2(qAbs(mr - kMarkerColor.red()) <= 3
                     && qAbs(mg - kMarkerColor.green()) <= 3
                     && qAbs(mb - kMarkerColor.blue()) <= 3,
                 qPrintable(QStringLiteral(
                                "frame %1: overlay marker (%2,%3,%4) was graded — "
                                "expected ungraded (%5,%6,%7)")
                                .arg(frame).arg(mr).arg(mg).arg(mb)
                                .arg(kMarkerColor.red()).arg(kMarkerColor.green())
                                .arg(kMarkerColor.blue())));
    }
}

void CompositeExportTest::gradedCompositeDepthLinked()
{
    SKIP_WITHOUT_GRADE_E2E_PREREQS();
    QVERIFY2(synthesizeColorSource(), "failed to synthesize the quadrant source");

    VideoClip clip(m_colorSourcePath);
    clip.applyProbeResult(VideoProbe::probe(m_colorSourcePath));
    QCOMPARE(clip.probeState(), VideoClip::Probed);
    clip.setSyncOffset(0.0);

    VideoOverlayLayout layout;
    layout.diveComputer = OverlayPlacement{true, QRectF(0, 0, 1, 1)};
    layout.diveProfile.visible = false;
    clip.setOverlayLayout(layout);

    // Depth ramps 0→30 m across the 3 s clip; the grade ramps identity →
    // strong red recovery with it.
    const double clipSeconds = kColorFrames / static_cast<double>(kColorFps);
    DiveData dive;
    dive.addDataPoint(DiveDataPoint(0.0, 0.0));
    dive.addDataPoint(DiveDataPoint(clipSeconds, 30.0));

    Unabara::ColorGrade grade;
    grade.enabled = true;
    grade.depthLinked = true;
    Unabara::GradeAnchor shallow;
    shallow.depth = 0.0; // identity params
    Unabara::GradeAnchor deep;
    deep.depth = 30.0;
    deep.params.redRecovery = 1.0;
    deep.params.waterTint = -0.5;
    deep.params.clarity = 0.4;
    grade.anchors << shallow << deep;
    clip.setColorGrade(grade.toJson());

    MarkerBarGenerator marker;
    QTemporaryDir outDir;
    QVERIFY(outDir.isValid());

    VideoExporter exporter;
    exporter.setBaseDirectory(outDir.path());
    // ProRes for the same parity-budget reason as the static test.
    exporter.setVideoCodec(QStringLiteral("prores"));
    exporter.setVideoQuality(QStringLiteral("best"));

    QSignalSpy finishedSpy(&exporter, &VideoExporter::exportFinished);
    QSignalSpy errorSpy(&exporter, &VideoExporter::exportError);

    CommandCapture capture;
    QVERIFY(exporter.exportComposite(&dive, &clip, &marker, nullptr, 0.0, clipSeconds));
    QCOMPARE(g_capturedCommands.size(), 1);
    QVERIFY2(g_capturedCommands.first().contains(QLatin1String("sendcmd")),
             qPrintable(g_capturedCommands.first()));

    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0 || errorSpy.count() > 0, 60000);
    QVERIFY2(errorSpy.isEmpty(),
             qPrintable(errorSpy.isEmpty() ? QString()
                                           : errorSpy.first().first().toString()));
    const QString outputPath = finishedSpy.takeFirst().at(1).toString();

    const QByteArray rgb = decodeRgb24(outputPath);
    const int frameBytes = kSrcW * kSrcH * 3;
    // The exporter snaps/clamps the range to the clip's probed duration
    // and frame count (the looped-PNG nut probes at 89 frames, not the 90
    // requested), so take the frame count from the decoded output rather
    // than assuming — the ramp math below is per-frame anyway.
    QVERIFY(rgb.size() > 0 && rgb.size() % frameBytes == 0);
    const int frames = static_cast<int>(rgb.size() / frameBytes);
    QVERIFY2(frames >= 80, qPrintable(QStringLiteral("only %1 frames").arg(frames)));
    const auto pixel = [&](int frame, int x, int y, int c) {
        return static_cast<int>(static_cast<uchar>(
            rgb.at(qint64(frame) * frameBytes + (y * kSrcW + x) * 3 + c)));
    };

    // Expected ops for frame k: the sendcmd file re-commands at the C1
    // 0.5 s cadence, so the ops in force at pts t are those planned at the
    // last cadence point ≤ t (per-frame exactness is the C1 sendcmd parity
    // test's job; HERE the claim is that the time variation reached the
    // whole pipeline).
    const auto opsForFrame = [&](int frame) {
        const double pts = frame / static_cast<double>(kColorFps);
        const double cmdTime = std::floor(pts / 0.5) * 0.5;
        return grade.resolveAtDepth(dive.dataAtTime(cmdTime).depth);
    };

    // Bound: sendcmd-path parity ≤2 LSB (C1 proven) + 10-bit YUV round
    // trip ≤1 + ProRes flat-frame quantization ≈1 → 4, margin 1 → 5 LSB.
    const int kTol = 5;
    const int first = 0;
    const int last = frames - 1;
    // Quadrant 1 (40,120,180) is the underwater-blue fixture: Red Recovery
    // borrows green into the depleted red channel, so the deep anchor
    // RAISES its red hard. (On the red-heavy quadrant 0 the same borrow
    // would lower red — the direction assertion needs this one.)
    int expFirst[3], expLast[3];
    {
        const Unabara::GradeOps a = opsForFrame(first);
        const Unabara::GradeOps b = opsForFrame(last);
        applyExpected(a, kQuadColors[1], &expFirst[0], &expFirst[1], &expFirst[2]);
        applyExpected(b, kQuadColors[1], &expLast[0], &expLast[1], &expLast[2]);
    }
    const QPoint pt = kQuadCenters[1];
    for (int c = 0; c < 3; ++c) {
        QVERIFY2(qAbs(pixel(first, pt.x(), pt.y(), c) - expFirst[c]) <= kTol,
                 qPrintable(QStringLiteral("first frame ch%1: got %2, expected %3")
                                .arg(c).arg(pixel(first, pt.x(), pt.y(), c))
                                .arg(expFirst[c])));
        QVERIFY2(qAbs(pixel(last, pt.x(), pt.y(), c) - expLast[c]) <= kTol,
                 qPrintable(QStringLiteral("last frame ch%1: got %2, expected %3")
                                .arg(c).arg(pixel(last, pt.x(), pt.y(), c))
                                .arg(expLast[c])));
    }

    // Direction of travel: the deep anchor recovers red, so the red
    // channel must have RISEN from first to last frame by at least half
    // the predicted delta — a static (stuck) grade fails this hard.
    const int predictedDelta = expLast[0] - expFirst[0];
    QVERIFY2(predictedDelta > 10, "fixture too weak: predicted red delta <= 10");
    const int actualDelta =
        pixel(last, pt.x(), pt.y(), 0) - pixel(first, pt.x(), pt.y(), 0);
    QVERIFY2(actualDelta >= predictedDelta / 2,
             qPrintable(QStringLiteral("red delta %1, predicted %2 — the depth "
                                       "link did not reach the pipeline")
                            .arg(actualDelta).arg(predictedDelta)));
}

void CompositeExportTest::gradeOverrideHookWins()
{
    SKIP_WITHOUT_GRADE_E2E_PREREQS();
    QVERIFY2(synthesizeColorSource(), "failed to synthesize the quadrant source");

    VideoClip clip(m_colorSourcePath);
    clip.applyProbeResult(VideoProbe::probe(m_colorSourcePath));
    QCOMPARE(clip.probeState(), VideoClip::Probed);
    clip.setSyncOffset(0.0);
    VideoOverlayLayout layout;
    layout.diveComputer = OverlayPlacement{true, QRectF(0, 0, 1, 1)};
    layout.diveProfile.visible = false;
    clip.setOverlayLayout(layout);

    // The clip carries a grade AND the one-shot hook is armed: the hook
    // must win (explicit override seam), then clear, so the NEXT run falls
    // back to the clip's own grade — also proving two sequential runs
    // through ONE exporter share m_tempDir safely.
    Unabara::ColorGrade grade;
    grade.enabled = true;
    Unabara::GradeAnchor anchor;
    anchor.params.redRecovery = 0.8;
    grade.anchors << anchor;
    clip.setColorGrade(grade.toJson());

    DiveData dive;
    MarkerBarGenerator marker;
    QTemporaryDir outDir;
    QVERIFY(outDir.isValid());

    VideoExporter exporter;
    exporter.setBaseDirectory(outDir.path());
    exporter.setVideoCodec(QStringLiteral("h264"));
    exporter.setVideoQuality(QStringLiteral("standard"));

    QSignalSpy finishedSpy(&exporter, &VideoExporter::exportFinished);
    QSignalSpy errorSpy(&exporter, &VideoExporter::exportError);

    const double out = 15 / static_cast<double>(kColorFps);

    {
        CommandCapture capture;
        exporter.setColorFilterChain(QStringLiteral("negate"));
        QVERIFY(exporter.exportComposite(&dive, &clip, &marker, nullptr, 0.0, out));
        QCOMPARE(g_capturedCommands.size(), 1);
        QVERIFY2(g_capturedCommands.first().contains(QLatin1String("negate"))
                     && !g_capturedCommands.first().contains(QLatin1String("lut3d")),
                 qPrintable(g_capturedCommands.first()));
        // One-shot: consumed at run start.
        QVERIFY(exporter.colorFilterChain().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0 || errorSpy.count() > 0,
                                 60000);
        QVERIFY2(errorSpy.isEmpty(),
                 qPrintable(errorSpy.isEmpty()
                                ? QString()
                                : errorSpy.first().first().toString()));
        finishedSpy.clear();
    }

    {
        // Second run, hook cleared: the clip's own grade is armed again.
        CommandCapture capture;
        QVERIFY(exporter.exportComposite(&dive, &clip, &marker, nullptr, 0.0, out));
        QCOMPARE(g_capturedCommands.size(), 1);
        QVERIFY2(g_capturedCommands.first().contains(QLatin1String("lut3d"))
                     && !g_capturedCommands.first().contains(QLatin1String("negate")),
                 qPrintable(g_capturedCommands.first()));
        QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0 || errorSpy.count() > 0,
                                 60000);
        QVERIFY2(errorSpy.isEmpty(),
                 qPrintable(errorSpy.isEmpty()
                                ? QString()
                                : errorSpy.first().first().toString()));
    }
}

QTEST_MAIN(CompositeExportTest)
#include "composite_export_test.moc"
