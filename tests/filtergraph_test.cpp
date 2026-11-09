// Tests for the raw-pipe video export (PR B2).
//
// Part 1 — pure argument assembly (FFmpegArgs, include/export/ffmpeg_args.h):
// the ONE arg builder the live VideoExporter path uses. Locks in the rawvideo
// stdin input block, the per-codec output options (including the explicit
// output pixel formats that keep post-B2 files probing identically to the
// PNG-input era), the optional scale filter, and the absence of any PNG-
// sequence remnants.
//
// Part 2 — end-to-end: drives the real VideoExporter (stub IFrameGenerator,
// tiny synthetic dive) through a full raw-pipe export and ffprobe-verifies
// the result: EXACT frame count (nb_read_frames == ExportMath::totalFrames —
// the old PNG path wobbled N/N+1 pre-B1; the raw pipe must stay exact),
// resolution, pix_fmt (yuv420p for h264) and the vp9 alpha_mode tag (alpha
// survived). QSKIPs when FFmpeg/ffprobe are not installed.

#include <QtTest>
#include <QPainter>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "include/export/ffmpeg_args.h"
#include "include/export/export_math.h"
#include "include/export/video_export.h"
#include "include/core/dive_data.h"
#include "include/core/ffmpeg_probe.h"
#include "include/generators/i_frame_generator.h"

// Minimal deterministic frame source: a moving opaque bar over a
// semi-transparent background, so alpha-capable codecs have real alpha to
// carry and every frame differs (keyframe-only streams can hide timing
// bugs).
class StubGenerator : public QObject, public IFrameGenerator
{
    Q_OBJECT
public:
    QImage generate(DiveData *dive, double timePoint) override
    {
        Q_UNUSED(dive);
        QImage img(320, 180, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.fillRect(img.rect(), QColor(10, 60, 120, 128));
        const int x = int(timePoint * 60.0) % (img.width() - 20);
        p.fillRect(QRect(x, 40, 20, 100), QColor(240, 200, 40, 255));
        p.end();
        ++generatedFrames;
        if (cancelTarget && generatedFrames == cancelAfterFrames) {
            cancelTarget->cancelExport();
        }
        return img;
    }
    void beginExport() override { ++beginCalls; }
    void endExport() override { ++endCalls; }

    int generatedFrames = 0;
    int beginCalls = 0;
    int endCalls = 0;
    // When set, generate() calls cancelExport() on this exporter after
    // `cancelAfterFrames` frames — a deterministic stand-in for a Cancel
    // click (the real one is delivered by the same loop's processEvents()).
    VideoExporter *cancelTarget = nullptr;
    int cancelAfterFrames = -1;
};

class FiltergraphTest : public QObject
{
    Q_OBJECT

private slots:
    void fpsRational_data();
    void fpsRational();
    void rawInputBlock();
    void codecOutputArgs_data();
    void codecOutputArgs();
    void qualityTiers();
    void customResolutionScale();
    void noPngRemnants();

    void endToEndExport_data();
    void endToEndExport();
    void endToEndCancel();

private:
    // ffprobe one video: returns the key=value map of the requested stream
    // entries (nb_read_frames, pix_fmt, width, height, TAG:alpha_mode).
    QMap<QString, QString> probeVideo(const QString &path);
    // Decode the first frame to raw straight-alpha RGBA bytes (W*H*4).
    // forceLibvpx selects the libvpx-vp9 decoder (the native one ignores
    // the alpha plane). Empty on failure.
    QByteArray decodeFirstFrameRgba(const QString &path, bool forceLibvpx);
};

void FiltergraphTest::fpsRational_data()
{
    QTest::addColumn<double>("fps");
    QTest::addColumn<QString>("spelled");

    QTest::newRow("integer 30") << 30.0 << "30";
    QTest::newRow("integer 60") << 60.0 << "60";
    QTest::newRow("ntsc-ish 29.97") << 29.97 << "2997/100";
    QTest::newRow("23.976") << 23.976 << "2997/125";
    QTest::newRow("half") << 0.5 << "1/2";
    // Degenerate rate: the frame loop clamps to one frame; time it at 1 fps
    // instead of handing FFmpeg a zero/negative rational.
    QTest::newRow("zero") << 0.0 << "1";
    QTest::newRow("negative") << -5.0 << "1";
}

void FiltergraphTest::fpsRational()
{
    QFETCH(double, fps);
    QFETCH(QString, spelled);
    QCOMPARE(FFmpegArgs::fpsString(FFmpegArgs::fpsToRational(fps)), spelled);
}

void FiltergraphTest::rawInputBlock()
{
    const QStringList args = FFmpegArgs::overlayPipeArgs(
        1920, 1080, FFmpegArgs::fpsToRational(30.0),
        QStringLiteral("h264"), QStringLiteral("standard"), QSize(),
        QStringLiteral("/tmp/out.mp4"));

    // Global flags first.
    QCOMPARE(args.at(0), QStringLiteral("-y"));
    QVERIFY(args.contains(QStringLiteral("-progress")));
    QVERIFY(args.contains(QStringLiteral("-nostats")));

    // The rawvideo input block, in order, all BEFORE "-i pipe:0" (FFmpeg
    // input options must precede their -i).
    const int iIdx = args.indexOf(QStringLiteral("-i"));
    QVERIFY(iIdx > 0);
    QCOMPARE(args.at(iIdx + 1), QStringLiteral("pipe:0"));

    const int fIdx = args.indexOf(QStringLiteral("-f"));
    QVERIFY(fIdx >= 0 && fIdx < iIdx);
    QCOMPARE(args.at(fIdx + 1), QStringLiteral("rawvideo"));

    const int pixIdx = args.indexOf(QStringLiteral("-pix_fmt"));
    QVERIFY(pixIdx >= 0 && pixIdx < iIdx);
    QCOMPARE(args.at(pixIdx + 1), QStringLiteral("rgba"));

    const int sIdx = args.indexOf(QStringLiteral("-s"));
    QVERIFY(sIdx >= 0 && sIdx < iIdx);
    QCOMPARE(args.at(sIdx + 1), QStringLiteral("1920x1080"));

    const int frIdx = args.indexOf(QStringLiteral("-framerate"));
    QVERIFY(frIdx >= 0 && frIdx < iIdx);
    QCOMPARE(args.at(frIdx + 1), QStringLiteral("30"));

    // Output path is the final argument.
    QCOMPARE(args.last(), QStringLiteral("/tmp/out.mp4"));
}

void FiltergraphTest::codecOutputArgs_data()
{
    QTest::addColumn<QString>("codec");
    QTest::addColumn<QString>("encoder");
    QTest::addColumn<QString>("outPixFmt");

    QTest::newRow("h264") << "h264" << "libx264" << "yuv420p";
    QTest::newRow("hevc") << "hevc" << "libx265" << "yuv420p";
    QTest::newRow("prores") << "prores" << "prores_ks" << "yuva444p10le";
    QTest::newRow("vp9") << "vp9" << "libvpx-vp9" << "yuva420p";
    // Unknown codec id falls back to h264, as the inline builder always did.
    QTest::newRow("unknown->h264") << "av1" << "libx264" << "yuv420p";
}

void FiltergraphTest::codecOutputArgs()
{
    QFETCH(QString, codec);
    QFETCH(QString, encoder);
    QFETCH(QString, outPixFmt);

    const QStringList args = FFmpegArgs::overlayPipeArgs(
        1280, 720, FFmpegArgs::fpsToRational(25.0),
        codec, QStringLiteral("standard"), QSize(), QStringLiteral("/tmp/out.bin"));

    const int cIdx = args.indexOf(QStringLiteral("-c:v"));
    QVERIFY(cIdx >= 0);
    QCOMPARE(args.at(cIdx + 1), encoder);

    // The OUTPUT -pix_fmt (after -i pipe:0) pins the conversion from rgba:
    // non-alpha codecs get the same yuv420p the PNG input auto-converted to,
    // alpha codecs keep alpha. The input block's own "-pix_fmt rgba" sits
    // before -i, so look only after it.
    const int iIdx = args.indexOf(QStringLiteral("-i"));
    const int outPixIdx = args.indexOf(QStringLiteral("-pix_fmt"), iIdx);
    QVERIFY(outPixIdx > iIdx);
    QCOMPARE(args.at(outPixIdx + 1), outPixFmt);

    // Codec-specific markers that must not drift.
    if (codec == "hevc") {
        const int tagIdx = args.indexOf(QStringLiteral("-tag:v"));
        QVERIFY(tagIdx > iIdx);
        QCOMPARE(args.at(tagIdx + 1), QStringLiteral("hvc1"));
    }
    if (codec == "h264") {
        QVERIFY(args.contains(QStringLiteral("-movflags")));
    }
    if (codec == "prores") {
        QVERIFY(args.contains(QStringLiteral("-alpha_bits")));
    }
    if (codec == "vp9") {
        // vp9 is the one codec still driven by bitrate — the quality tier
        // maps to it ("standard" → 4000k).
        const int bIdx = args.indexOf(QStringLiteral("-b:v"));
        QVERIFY(bIdx > iIdx);
        QCOMPARE(args.at(bIdx + 1), QStringLiteral("4000k"));
    } else {
        // The quality combo replaced the bitrate slider (PR B3): no -b:v
        // anywhere else — h264/hevc are pure CRF ("standard" → 23, the
        // historical fixed value).
        QVERIFY(!args.contains(QStringLiteral("-b:v")));
        if (codec != "prores") {
            const int crfIdx = args.indexOf(QStringLiteral("-crf"));
            QVERIFY(crfIdx > iIdx);
            QCOMPARE(args.at(crfIdx + 1), QStringLiteral("23"));
        }
    }
}

void FiltergraphTest::qualityTiers()
{
    // The documented quality matrix (ffmpeg_args.h) — every branch.
    QCOMPARE(FFmpegArgs::qualityCrf(QStringLiteral("best")), 18);
    QCOMPARE(FFmpegArgs::qualityCrf(QStringLiteral("high")), 20);
    QCOMPARE(FFmpegArgs::qualityCrf(QStringLiteral("standard")), 23);
    QCOMPARE(FFmpegArgs::qualityCrf(QStringLiteral("small")), 28);
    QCOMPARE(FFmpegArgs::qualityCrf(QStringLiteral("bogus")), 23); // fallback

    QCOMPARE(FFmpegArgs::proresProfile422(QStringLiteral("best")), 3);
    QCOMPARE(FFmpegArgs::proresProfile422(QStringLiteral("high")), 3);
    QCOMPARE(FFmpegArgs::proresProfile422(QStringLiteral("standard")), 2);
    QCOMPARE(FFmpegArgs::proresProfile422(QStringLiteral("small")), 2);

    QCOMPARE(FFmpegArgs::vp9BitrateKbps(QStringLiteral("best")), 12000);
    QCOMPARE(FFmpegArgs::vp9BitrateKbps(QStringLiteral("high")), 8000);
    QCOMPARE(FFmpegArgs::vp9BitrateKbps(QStringLiteral("standard")), 4000);
    QCOMPARE(FFmpegArgs::vp9BitrateKbps(QStringLiteral("small")), 2000);

    // The tier reaches the h264 output args.
    const QStringList best = FFmpegArgs::overlayPipeArgs(
        320, 180, FFmpegArgs::fpsToRational(30.0),
        QStringLiteral("h264"), QStringLiteral("best"), QSize(),
        QStringLiteral("/tmp/q.mp4"));
    const int crfIdx = best.indexOf(QStringLiteral("-crf"));
    QVERIFY(crfIdx >= 0);
    QCOMPARE(best.at(crfIdx + 1), QStringLiteral("18"));
}

void FiltergraphTest::customResolutionScale()
{
    const QStringList args = FFmpegArgs::overlayPipeArgs(
        1920, 1080, FFmpegArgs::fpsToRational(30.0),
        QStringLiteral("vp9"), QStringLiteral("standard"), QSize(1280, 720),
        QStringLiteral("/tmp/o.webm"));
    const int vfIdx = args.indexOf(QStringLiteral("-vf"));
    QVERIFY(vfIdx >= 0);
    QCOMPARE(args.at(vfIdx + 1), QStringLiteral("scale=1280:720"));

    // And absent when no custom resolution is set.
    const QStringList noScale = FFmpegArgs::overlayPipeArgs(
        1920, 1080, FFmpegArgs::fpsToRational(30.0),
        QStringLiteral("vp9"), QStringLiteral("standard"), QSize(),
        QStringLiteral("/tmp/o.webm"));
    QVERIFY(!noScale.contains(QStringLiteral("-vf")));
}

void FiltergraphTest::noPngRemnants()
{
    for (const QString &codec : {QStringLiteral("h264"), QStringLiteral("hevc"),
                                 QStringLiteral("prores"), QStringLiteral("vp9")}) {
        const QStringList args = FFmpegArgs::overlayPipeArgs(
            1920, 1080, FFmpegArgs::fpsToRational(30.0),
            codec, QStringLiteral("standard"), QSize(), QStringLiteral("/tmp/out.bin"));
        for (const QString &arg : args) {
            QVERIFY2(!arg.contains(QStringLiteral(".png")),
                     qPrintable(QStringLiteral("PNG-sequence remnant in %1 args: %2")
                                    .arg(codec, arg)));
            QVERIFY2(!arg.contains(QStringLiteral("%06d")),
                     qPrintable(QStringLiteral("frame-pattern remnant in %1 args: %2")
                                    .arg(codec, arg)));
        }
    }
}

QByteArray FiltergraphTest::decodeFirstFrameRgba(const QString &path, bool forceLibvpx)
{
    const QString ffmpeg = FFmpegProbe::instance()->ffmpegPath();
    if (ffmpeg.isEmpty())
        return QByteArray();
    QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                     QStringLiteral("error")};
    if (forceLibvpx)
        args << QStringLiteral("-c:v") << QStringLiteral("libvpx-vp9");
    args << QStringLiteral("-i") << path << QStringLiteral("-frames:v")
         << QStringLiteral("1") << QStringLiteral("-f")
         << QStringLiteral("rawvideo") << QStringLiteral("-pix_fmt")
         << QStringLiteral("rgba") << QStringLiteral("-");
    QProcess proc;
    proc.start(ffmpeg, args);
    if (!proc.waitForFinished(15000) || proc.exitCode() != 0)
        return QByteArray();
    return proc.readAllStandardOutput();
}

QMap<QString, QString> FiltergraphTest::probeVideo(const QString &path)
{
    QMap<QString, QString> result;
    const QString ffprobe = FFmpegProbe::instance()->ffprobePath();
    if (ffprobe.isEmpty())
        return result;

    QProcess proc;
    proc.start(ffprobe,
               {QStringLiteral("-v"), QStringLiteral("error"),
                QStringLiteral("-count_frames"),
                QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                QStringLiteral("-show_entries"),
                QStringLiteral("stream=nb_read_frames,pix_fmt,width,height"
                               ":stream_tags=alpha_mode"),
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

void FiltergraphTest::endToEndExport_data()
{
    QTest::addColumn<QString>("codec");
    QTest::addColumn<QString>("encoder");
    QTest::addColumn<QString>("expectedPixFmt");
    QTest::addColumn<bool>("expectAlpha");

    // h264: alpha dropped in the rgba→yuv420p conversion (as the PNG path
    // did). vp9: ffprobe's native decoder reports the YUV planes as yuv420p
    // and flags the carried alpha with the webm alpha_mode=1 tag.
    QTest::newRow("h264") << "h264" << "libx264" << "yuv420p" << false;
    QTest::newRow("vp9") << "vp9" << "libvpx-vp9" << "yuv420p" << true;
}

void FiltergraphTest::endToEndExport()
{
    QFETCH(QString, codec);
    QFETCH(QString, encoder);
    QFETCH(QString, expectedPixFmt);
    QFETCH(bool, expectAlpha);

    if (!FFmpegProbe::instance()->isAvailable())
        QSKIP("FFmpeg not found — end-to-end export test skipped");
    if (FFmpegProbe::instance()->ffprobePath().isEmpty())
        QSKIP("ffprobe not found — end-to-end export test skipped");
    if (!FFmpegProbe::instance()->hasEncoder(encoder))
        QSKIP(qPrintable(QStringLiteral("FFmpeg lacks %1 — skipped").arg(encoder)));

    QTemporaryDir outDir;
    QVERIFY(outDir.isValid());

    DiveData dive;
    dive.setDiveName(QStringLiteral("PipeTest"));

    StubGenerator gen;
    VideoExporter exporter;
    exporter.setBaseDirectory(outDir.path());
    exporter.setFrameRate(30.0);
    exporter.setVideoCodec(codec);
    exporter.setVideoQuality(QStringLiteral("small"));

    const double startTime = 0.0;
    const double endTime = 1.0; // 30 frames at 30 fps
    const int expectedFrames = ExportMath::totalFrames(startTime, endTime, 30.0);
    QCOMPARE(expectedFrames, 30);

    QSignalSpy finishedSpy(&exporter, &VideoExporter::exportFinished);
    QSignalSpy errorSpy(&exporter, &VideoExporter::exportError);
    QSignalSpy cancelledSpy(&exporter, &VideoExporter::exportCancelled);

    // exportVideo pumps events while streaming; true means every frame was
    // delivered and FFmpeg is finalizing.
    QVERIFY(exporter.exportVideo(&dive, &gen, startTime, endTime));

    // Completion is asynchronous (FFmpeg drains stdin after
    // closeWriteChannel) — wait for the terminal signal.
    QTRY_VERIFY_WITH_TIMEOUT(
        finishedSpy.count() > 0 || errorSpy.count() > 0 || cancelledSpy.count() > 0,
        30000);
    QVERIFY2(errorSpy.isEmpty(),
             qPrintable(errorSpy.isEmpty()
                            ? QString()
                            : errorSpy.first().first().toString()));
    QCOMPARE(cancelledSpy.count(), 0);
    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(!exporter.isBusy());

    // Generator contract: begin/end symmetric, one generate per frame.
    QCOMPARE(gen.beginCalls, 1);
    QCOMPARE(gen.endCalls, 1);
    QCOMPARE(gen.generatedFrames, expectedFrames);

    const QList<QVariant> finished = finishedSpy.takeFirst();
    QVERIFY(finished.at(0).toBool());
    const QString outputPath = finished.at(1).toString();
    QVERIFY2(QFile::exists(outputPath), qPrintable(outputPath));
    QVERIFY(QFileInfo(outputPath).size() > 0);

    const QMap<QString, QString> probed = probeVideo(outputPath);
    QVERIFY2(!probed.isEmpty(), "ffprobe produced no stream info");
    QCOMPARE(probed.value(QStringLiteral("width")).toInt(), 320);
    QCOMPARE(probed.value(QStringLiteral("height")).toInt(), 180);
    QCOMPARE(probed.value(QStringLiteral("pix_fmt")), expectedPixFmt);
    // EXACT frame count — the raw pipe writes precisely totalFrames frames
    // (the pre-B1 PNG path wobbled N/N+1).
    QCOMPARE(probed.value(QStringLiteral("nb_read_frames")).toInt(), expectedFrames);
    if (expectAlpha) {
        QCOMPARE(probed.value(QStringLiteral("TAG:alpha_mode")), QStringLiteral("1"));
    }

    // Premul/straight regression guard: the stub's background is
    // HALF-TRANSPARENT blue — QColor(10,60,120,128), stored premultiplied
    // as (5,30,60,128) inside the generator's image. The pipe contract is
    // STRAIGHT alpha, so a decoded background pixel must read the straight
    // values: a pipeline that shipped premultiplied bytes (or
    // double-unpremultiplied) would be ~30-60 off per channel, far outside
    // the lossy-codec tolerance below. Pixel (5,5) sits above the moving
    // bar on frame 0 (bar x 0-19, y 40-139).
    const QByteArray rgba = decodeFirstFrameRgba(outputPath, expectAlpha);
    if (rgba.size() == 320 * 180 * 4) {
        const int off = (5 * 320 + 5) * 4;
        const auto *px = reinterpret_cast<const unsigned char *>(rgba.constData()) + off;
        const int tol = 12; // generous for 4:2:0 chroma + lossy encode
        QVERIFY2(qAbs(int(px[0]) - 10) <= tol && qAbs(int(px[1]) - 60) <= tol
                     && qAbs(int(px[2]) - 120) <= tol,
                 qPrintable(QStringLiteral("background RGB (%1,%2,%3) != straight (10,60,120)")
                                .arg(px[0]).arg(px[1]).arg(px[2])));
        if (expectAlpha)
            QVERIFY2(qAbs(int(px[3]) - 128) <= tol,
                     qPrintable(QStringLiteral("background alpha %1 != 128").arg(px[3])));
    } else {
        qWarning() << "decodeFirstFrameRgba unavailable (decoder missing?) —"
                   << "straight-alpha pixel check skipped";
    }
}

void FiltergraphTest::endToEndCancel()
{
    if (!FFmpegProbe::instance()->isAvailable())
        QSKIP("FFmpeg not found — cancel test skipped");
    if (!FFmpegProbe::instance()->hasEncoder(QStringLiteral("libx264")))
        QSKIP("FFmpeg lacks libx264 — cancel test skipped");

    QTemporaryDir outDir;
    QVERIFY(outDir.isValid());

    DiveData dive;
    dive.setDiveName(QStringLiteral("PipeCancel"));

    StubGenerator gen;
    VideoExporter exporter;
    exporter.setBaseDirectory(outDir.path());
    exporter.setFrameRate(30.0);
    exporter.setVideoCodec(QStringLiteral("h264"));
    exporter.setVideoQuality(QStringLiteral("small"));
    // Cancel mid-stream, well before the 300-frame range completes.
    gen.cancelTarget = &exporter;
    gen.cancelAfterFrames = 10;

    QSignalSpy finishedSpy(&exporter, &VideoExporter::exportFinished);
    QSignalSpy errorSpy(&exporter, &VideoExporter::exportError);
    QSignalSpy cancelledSpy(&exporter, &VideoExporter::exportCancelled);

    // Returns false: the run did not deliver all frames.
    QVERIFY(!exporter.exportVideo(&dive, &gen, 0.0, 10.0));

    // The cancellation completes asynchronously through onFFmpegFinished()'s
    // cancel branch: exportCancelled (not exportError), partial file gone.
    QTRY_COMPARE_WITH_TIMEOUT(cancelledSpy.count(), 1, 15000);
    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(finishedSpy.count(), 0);
    QVERIFY(!exporter.isBusy());
    // begin/end stayed symmetric even on the cancel path.
    QCOMPARE(gen.beginCalls, 1);
    QCOMPARE(gen.endCalls, 1);
    // No partial output file left behind.
    const QStringList leftovers =
        QDir(outDir.path()).entryList(QDir::Files | QDir::NoDotAndDotDot);
    QVERIFY2(leftovers.isEmpty(), qPrintable(leftovers.join(QStringLiteral(", "))));
}

QTEST_MAIN(FiltergraphTest)
#include "filtergraph_test.moc"
