#include "include/export/video_export.h"
#include "include/export/export_math.h"
#include "include/export/ffmpeg_args.h"
#include "include/export/frame_compositor.h"
#include "include/core/ffmpeg_info.h"
#include "include/core/ffmpeg_probe.h"
#include "include/core/video_clip.h"
#include "include/core/video_probe.h"
#include "include/core/color_grade.h"
#include "include/core/color_grade_export.h"
#include "include/core/color_utils.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QTimer>

namespace {
// Deterministic conversion to the pipe's pixel format. A premultiplied
// source converted straight through QImage::convertToFormat() would go
// through Qt's vendor-dependent RCPPS unpremultiply (the golden-render
// CPU-determinism root cause, CLAUDE.md); the placed-overlays mode ships
// straight alpha from a premultiplied compositor plane, so route that
// case through the exact-integer ColorUtils::unpremultipliedArgb32().
// Every other combination convertToFormat performs is a pure byte
// swizzle (or an opaque fill) and stays on the fast path.
QImage toPipeFormat(const QImage& img, QImage::Format target)
{
    if (img.format() == target)
        return img;
    const bool srcPremultiplied =
        img.format() == QImage::Format_ARGB32_Premultiplied
        || img.format() == QImage::Format_RGBA8888_Premultiplied;
    if (srcPremultiplied && target == QImage::Format_RGBA8888) {
        const QImage argbPm =
            img.format() == QImage::Format_ARGB32_Premultiplied
                ? img
                : img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        return Unabara::ColorUtils::unpremultipliedArgb32(argbPm)
            .convertToFormat(QImage::Format_RGBA8888);
    }
    return img.convertToFormat(target);
}
} // namespace

VideoExporter::VideoExporter(QObject *parent)
    : QObject(parent)
    , m_frameRate(30.0)  // Default 30 frames per second for video
    , m_videoQuality("standard") // Quality tier (FFmpegArgs tables); "standard" == the historical fixed CRF 23
    , m_videoCodec("vp9") // Default VP9 codec (supports transparency for compositing)
    , m_progress(0)
    , m_busy(false)
    , m_cancelRequested(false)
    , m_ffmpegProcess(nullptr)
{
    // The base directory the output file lands in is bound from QML
    // (config.lastExportPath) — the exporter never touches the settings
    // store itself. No directory is created here. Frames stream straight to
    // FFmpeg's stdin (PR B2); m_tempDir is the colour-grade work
    // directory (baked .cube + sendcmd files, PR C5 — see the member
    // comment in the header).
}

VideoExporter::~VideoExporter()
{
    // Make sure the FFmpeg process is terminated
    if (m_ffmpegProcess && m_ffmpegProcess->state() != QProcess::NotRunning) {
        m_ffmpegProcess->terminate();
        m_ffmpegProcess->waitForFinished(3000);
        if (m_ffmpegProcess->state() != QProcess::NotRunning) {
            m_ffmpegProcess->kill();
        }
    }
    
    delete m_ffmpegProcess;
}

void VideoExporter::setBaseDirectory(const QString &path)
{
    if (m_baseDirectory != path) {
        m_baseDirectory = path;
        emit baseDirectoryChanged();
    }
}

void VideoExporter::setFrameRate(double fps)
{
    if (m_frameRate != fps) {
        m_frameRate = fps;
        emit frameRateChanged();
    }
}

void VideoExporter::setVideoQuality(const QString &quality)
{
    if (m_videoQuality != quality) {
        m_videoQuality = quality;
        emit videoQualityChanged();
    }
}

void VideoExporter::setColorFilterChain(const QString &snippet)
{
    // One-shot by design (v0.4 plan: "Snippet cleared after each run"):
    // exportComposite() consumes and clears it at run start, so it must be
    // set again before every export it should apply to.
    m_colorFilterChain = snippet;
}

void VideoExporter::setVideoCodec(const QString &codec)
{
    if (m_videoCodec != codec) {
        m_videoCodec = codec;
        emit videoCodecChanged();
    }
}

void VideoExporter::setCustomResolution(const QSize &size)
{
    if (m_customResolution != size && size.isValid()) {
        m_customResolution = size;
        emit customResolutionChanged();
    }
}

bool VideoExporter::isFFmpegAvailable()
{
    QString ffmpegPath = findFFmpegPath();
    return !ffmpegPath.isEmpty();
}

QString VideoExporter::findFFmpegPath()
{
    // Single app-wide locator (discovery order, sibling ffprobe resolution
    // and capability probing all live there).
    return FFmpegProbe::instance()->ffmpegPath();
}

QStringList VideoExporter::getAvailableCodecs()
{
    // Canonical list lives in FfmpegInfo (whose availableCodecs() filters
    // it by the probed encoders for the export dialog).
    return FfmpegInfo::allCodecIds();
}

bool VideoExporter::codecSupportsAlpha(const QString &codec)
{
    return (codec == "prores" || codec == "vp9");
}

QString VideoExporter::getFileExtensionForCodec(const QString &codec)
{
    // Return the appropriate file extension for the selected codec
    if (codec == "h264" || codec == "hevc") {
        return "mp4";
    } else if (codec == "prores") {
        return "mov";
    } else if (codec == "vp9") {
        return "webm";
    }
    
    // Default
    return "mp4";
}

bool VideoExporter::exportVideo(DiveData* dive, QObject* generator,
                              double startTime, double endTime)
{
    qDebug() << "======== EXPORT VIDEO CALLED ========";
    qDebug() << "Parameters:" << startTime << "to" << endTime;

    if (m_busy) {
        emit exportError(tr("Already exporting video"));
        return false;
    }

    IFrameGenerator* gen = dynamic_cast<IFrameGenerator*>(generator);
    if (!dive || !gen) {
        emit exportError(tr("Invalid dive data or generator"));
        return false;
    }
    
    // Check if FFmpeg is available
    if (!isFFmpegAvailable()) {
        emit exportError(tr("FFmpeg is not available. Please install FFmpeg to export videos."));
        return false;
    }
    
    if (!ExportMath::isValidExportPath(m_baseDirectory)) {
        emit exportError(tr("No export directory is set"));
        return false;
    }

    m_busy = true;
    m_cancelRequested = false;
    const int runSerial = ++m_runSerial;
    emit busyChanged();

    // Notify that export has started
    emit exportStarted();
    emit statusUpdate(tr("Rendering and encoding frames..."));

    // Create the export directory if it doesn't exist
    QDir dir(m_baseDirectory);
    if (!dir.exists() && !dir.mkpath(".")) {
        emit exportError(tr("Failed to create export directory: %1")
                             .arg(m_baseDirectory));
        m_busy = false;
        emit busyChanged();
        return false;
    }

    // Use the pre-generated output path (from createDefaultExportFile) or generate a new one
    QString outputPath;
    if (!m_pendingOutputPath.isEmpty()) {
        outputPath = m_pendingOutputPath;
        m_pendingOutputPath.clear();
    } else {
        QString extension = getFileExtensionForCodec(m_videoCodec);
        outputPath = generateUniqueFileName(dive, extension);
    }
    m_lastOutputPath = outputPath;

    // Interleaved render-and-encode run (PR B2): FFmpeg starts first, reading
    // raw RGBA frames from stdin, and the loop below streams each rendered
    // frame into the pipe — no temporary PNG files, one progress phase.
    //
    // Integer frame loop (ExportMath::frameTime): frame i's timestamp is
    // computed by multiplication, so exactly totalFrames frames enter the
    // pipe — a float-accumulated `t += 1/fps` loop drifted into N or N+1
    // frames depending on the range's absolute values. The container's
    // nb_frames is now exact.
    // Captured once: the pumped event loop below could otherwise observe a
    // mid-run setFrameRate() and desync rendered timestamps from the
    // -framerate the pipe was started with (same capture discipline as
    // ImageExporter's destination directory).
    const double fps = m_frameRate;
    const int totalFrames = ExportMath::totalFrames(startTime, endTime, fps);
    m_totalFrames = totalFrames;
    m_renderedFrames = 0;
    m_encodedFrames = 0;
    m_pipeErrorMessage.clear();
    m_progress = 0;
    emit progressChanged();

    qDebug() << "Streaming frames from" << startTime << "to" << endTime
             << "at" << fps << "fps (" << totalFrames << "frames)";

    // Stage any export-only generator state (IFrameGenerator contract;
    // currently a no-op for both generators).
    gen->beginExport();

    // Render the first frame up front: its size defines the raw pipe's
    // WxH (the generator renders at template resolution, fixed per run).
    // Straight (non-premultiplied) RGBA8888, per the -pix_fmt rgba input
    // contract: prores/vp9 carry the alpha through; h264/hevc drop it in
    // the rgba→yuv420p output conversion, exactly as the PNG path did.
    //
    // Pre-dive first frame (ExportMath::isPreDiveTime; mirrored in
    // ImageExporter::exportImageRange and in streamFrames below): the
    // generator is NOT asked for t < 0 — dataAtTime would clamp to sample 0
    // and burn a frozen t=0 overlay. A single sizing render at t = 0 fixes
    // the pipe geometry and frame 0 ships fully transparent. A real image,
    // never a null one: null means "generation failed" here (and "repeat
    // the previous frame" in the loop).
    const double firstTime = ExportMath::frameTime(startTime, 0, fps);
    QImage firstFrame;
    if (ExportMath::isPreDiveTime(firstTime)) {
        const QImage sizing = gen->generate(dive, 0.0);
        if (!sizing.isNull()) {
            firstFrame = QImage(sizing.size(), QImage::Format_RGBA8888);
            firstFrame.fill(Qt::transparent);
        }
    } else {
        firstFrame = toPipeFormat(gen->generate(dive, firstTime),
                                  QImage::Format_RGBA8888);
    }
    if (firstFrame.isNull()) {
        gen->endExport();
        emit exportError(tr("Failed to generate the first frame"));
        m_busy = false;
        emit busyChanged();
        return false;
    }
    const QSize frameSize = firstFrame.size();

    // One arg builder (FFmpegArgs, unit-tested in filtergraph_test): raw
    // RGBA frames over stdin replace the PNG-sequence input. Built from the
    // same captured fps the render loop times frames with.
    const QStringList args = FFmpegArgs::overlayPipeArgs(
        frameSize.width(), frameSize.height(),
        FFmpegArgs::fpsToRational(fps),
        m_videoCodec, m_videoQuality, m_customResolution, outputPath);

    if (!startFFmpegProcess(args)) {
        gen->endExport();
        m_busy = false;
        emit busyChanged();
        return false; // startFFmpegProcess() emitted exportError()
    }

    const StreamResult result = streamFrames(
        dive, gen, totalFrames, frameSize, QImage::Format_RGBA8888,
        [startTime, fps](int i) { return ExportMath::frameTime(startTime, i, fps); },
        firstFrame, runSerial);
    gen->endExport();
    return finishRun(result, runSerial);
}

VideoExporter::StreamResult VideoExporter::streamFrames(
    DiveData *dive, IFrameGenerator *gen, int totalFrames,
    const QSize &frameSize, QImage::Format pixelFormat,
    const std::function<double(int)> &timeOfFrame,
    const QImage &firstFrame, int runSerial)
{
    // THE frame-streaming loop — overlay-only, placed and composited
    // exports all run through here (one loop, per the extraction
    // guardrail). From here on, every terminal outcome (success, error,
    // cancel) is reported by onFFmpegFinished() when the process exits —
    // this loop only feeds it and stops on cancel/death. NOTE (Windows
    // caveat): stdin pipes of ~33 MB/frame at 4K are the least-exercised
    // QProcess path on Windows; the backpressure spin below (bounded
    // buffer + event pumping) is what keeps it safe, but it needs explicit
    // verification on a real Windows box (plan: "Verify on Windows
    // explicitly").
    const qint64 frameBytes = qint64(frameSize.width()) * frameSize.height() * 4;
    QImage previousFrame = firstFrame;
    // Pre-dive frames (ExportMath::isPreDiveTime; mirrored in
    // ImageExporter::exportImageRange): one fully transparent frame in the
    // pipe's exact size and byte format, built once and reused — the
    // generator is never asked for t < 0 (dataAtTime would clamp to sample
    // 0 and burn a frozen t=0 overlay). Applies to every mode through this
    // one loop; composites were already blank there (FrameCompositor gates
    // the same rule), so their pipe bytes are unchanged — all-zero pixels
    // are identical in straight and premultiplied RGBA.
    QImage transparentFrame;
    for (int i = 0; i < totalFrames; ++i) {
        QImage frame;
        if (i == 0) {
            frame = firstFrame;
        } else if (ExportMath::isPreDiveTime(timeOfFrame(i))) {
            if (transparentFrame.isNull()) {
                transparentFrame = QImage(frameSize, pixelFormat);
                transparentFrame.fill(Qt::transparent);
            }
            frame = transparentFrame;
        } else {
            const double time = timeOfFrame(i);
            frame = toPipeFormat(gen->generate(dive, time), pixelFormat);
            if (frame.isNull()) {
                // The pipe's frame count must stay exact (FFmpeg times frame
                // i at i/fps): repeat the previous frame instead of skipping,
                // which would shift every later frame earlier.
                qWarning() << "Failed to generate frame at time:" << time
                           << "- repeating previous frame";
                frame = previousFrame;
            } else if (frame.size() != frameSize) {
                // Defensive: the generators render at a fixed size per run,
                // but the pipe geometry is locked at frame 0 — a mismatched
                // frame would corrupt the whole stream.
                qWarning() << "Frame size changed mid-export:" << frame.size()
                           << "- expected" << frameSize << "- rescaling";
                // Re-convert after scaling: smooth-scaled QImages can come
                // back ARGB32_Premultiplied, whose byte order would corrupt
                // the 4-bytes-per-pixel pipe contract.
                frame = toPipeFormat(frame.scaled(frameSize,
                                                  Qt::IgnoreAspectRatio,
                                                  Qt::SmoothTransformation),
                                     pixelFormat);
            }
        }

        if (!writeFramePixels(frame)) {
            return StreamResult::PipeFailed;
        }
        previousFrame = frame;
        m_renderedFrames = i + 1;
        emitInterleavedStatus();

        // Backpressure per the v0.4 plan: never buffer more than ~2 frames
        // in the write channel; pump events while waiting so the UI, the
        // Cancel button and FFmpeg's stdout/stderr/finished signals stay
        // live even when the encoder is the bottleneck.
        if (!waitForPipeDrain(frameBytes)) {
            return StreamResult::PipeFailed;
        }
        // Even when the encoder keeps up (no drain spin ran), events must be
        // pumped once per frame — this is what delivers a Cancel click.
        QCoreApplication::processEvents();
        // A processEvents/drain batch can deliver this run's death AND let
        // a new export start (busy resets inside onFFmpegFinished). From
        // then on m_ffmpegProcess belongs to the successor run: this loop
        // must not write another byte into it, nor close its channel.
        if (m_runSerial != runSerial)
            return StreamResult::Superseded;
        if (m_cancelRequested) {
            return StreamResult::Cancelled;
        }
    }
    return StreamResult::Completed;
}

bool VideoExporter::finishRun(StreamResult result, int runSerial)
{
    if (result == StreamResult::Superseded) {
        // A successor run owns m_ffmpegProcess now — touch nothing.
        return false;
    }

    if (result == StreamResult::Cancelled) {
        // cancelExport() normally terminated FFmpeg already (the Cancel
        // click was delivered by a processEvents() in the loop, while the
        // process was running), and its finished() delivery runs
        // onFFmpegFinished(), whose cancel branch removes the partial
        // output, resets busy and emits exportCancelled(). Emitting
        // anything here would double-report. But if the flag was set before
        // FFmpeg started (a synchronous subscriber of exportStarted/
        // busyChanged calling cancelExport()), cancelExport()'s terminate
        // found nothing running — terminate here too (idempotent) so
        // onFFmpegFinished() is guaranteed to fire and complete the
        // cancellation.
        if (m_runSerial == runSerial
            && m_ffmpegProcess->state() != QProcess::NotRunning) {
            m_ffmpegProcess->terminate();
        }
        return false;
    }

    if (result == StreamResult::PipeFailed) {
        // FFmpeg died mid-stream (bad args, full disk, crash) or a write
        // failed. If the process already exited, onFFmpegFinished() has run
        // (or will, via the pending finished() delivery) and reports the
        // error with the stderr tail. If a write failed while the process
        // is somehow still alive, kill it so the run still terminates
        // through onFFmpegFinished() instead of finishing a truncated file
        // as a "success". The serial check keeps this run from killing a
        // NEWER run's process (m_ffmpegProcess is reused; our death may
        // already have been reported, m_busy reset, and a new export
        // started inside a processEvents() batch in the loop).
        if (m_runSerial == runSerial
            && m_ffmpegProcess->state() != QProcess::NotRunning) {
            m_pipeErrorMessage = tr("Failed to write frames to FFmpeg");
            m_ffmpegProcess->kill();
        }
        return false;
    }

    // All frames delivered: close stdin so FFmpeg drains its buffer and
    // finalizes the file. Completion arrives via onFFmpegFinished().
    if (m_runSerial != runSerial)
        return false; // a successor run owns the process now
    m_ffmpegProcess->closeWriteChannel();
    emit statusUpdate(tr("Finishing encoding..."));
    return true;
}

bool VideoExporter::exportComposite(DiveData* dive, VideoClip* clip,
                                    QObject* dcGenerator,
                                    QObject* profileGenerator,
                                    double clipRangeStart, double clipRangeEnd,
                                    bool overlaysOnly)
{
    // Consume the one-shot colour OVERRIDE hook FIRST (v0.4 plan: cleared
    // after each run) — even a run that fails validation must not leave a
    // stale override armed for a later, unrelated export. When empty, the
    // clip's own grade is planned further down (PR C5).
    const QString colorSnippet = m_colorFilterChain;
    m_colorFilterChain.clear();

    if (m_busy) {
        emit exportError(tr("Already exporting video"));
        return false;
    }
    if (!dive || !clip) {
        emit exportError(tr("Invalid dive data or video clip"));
        return false;
    }
    if (clip->width() <= 0 || clip->height() <= 0 || clip->fpsNum() <= 0
        || clip->fpsDen() <= 0 || clip->durationSeconds() <= 0.0) {
        emit exportError(tr("The video clip has no usable metadata yet — wait for probing to finish."));
        return false;
    }
    if (!isFFmpegAvailable()) {
        emit exportError(tr("FFmpeg is not available. Please install FFmpeg to export videos."));
        return false;
    }
    if (!ExportMath::isValidExportPath(m_baseDirectory)) {
        emit exportError(tr("No export directory is set"));
        return false;
    }
    // The composited pipeline supports h264/hevc/prores only (the dialog
    // enforces this; guard the programmatic path too — compositeArgs would
    // fall back to libx264 while the file extension said .webm, handing
    // FFmpeg a doomed mux).
    if (!overlaysOnly
        && m_videoCodec != QLatin1String("h264")
        && m_videoCodec != QLatin1String("hevc")
        && m_videoCodec != QLatin1String("prores")) {
        emit exportError(tr("Codec '%1' is not supported for composited export "
                            "(use h264, hevc or prores).").arg(m_videoCodec));
        return false;
    }

    // The composited canvas is the clip frame; overlays and placements come
    // from the clip's own layout (what the Video tab preview shows).
    IFrameGenerator* dcGen = dynamic_cast<IFrameGenerator*>(dcGenerator);
    IFrameGenerator* profGen = dynamic_cast<IFrameGenerator*>(profileGenerator);
    const QSize frameSize(clip->width(), clip->height());
    const VideoOverlayLayout layout = clip->overlayLayout();
    FrameCompositor compositor(frameSize);
    if (dcGen && layout.diveComputer.visible)
        compositor.addLayer(dcGen, layout.diveComputer);
    if (profGen && layout.diveProfile.visible)
        compositor.addLayer(profGen, layout.diveProfile);
    if (overlaysOnly && compositor.layerCount() == 0) {
        // A composite of just the footage is legal (the pipe streams
        // transparent planes); a placed-overlays export of nothing is not.
        emit exportError(tr("No overlays are visible for this video — nothing to export."));
        return false;
    }
    CompositorFrameSource source(&compositor);

    // Snap the range to the clip's frame grid (clip seconds). endFrame is
    // exclusive; a degenerate/inverted range still exports one frame, same
    // clamp philosophy as ExportMath::totalFrames.
    const int num = clip->fpsNum();
    const int den = clip->fpsDen();
    const double duration = clip->durationSeconds();
    const double clampedStart = qBound(0.0, clipRangeStart, duration);
    const double clampedEnd = qBound(0.0, clipRangeEnd, duration);
    int startFrame = qRound(clampedStart * num / static_cast<double>(den));
    int endFrame = qRound(clampedEnd * num / static_cast<double>(den));
    // durationSeconds comes from format.duration, which covers the LONGEST
    // stream: a clip whose audio outlasts its video would size the pipe
    // beyond the source video's frames — eof_action=endall then ends the
    // graph early, FFmpeg exits 0, and a short file would be reported as
    // success. Bound by the probed video-stream frame count when known.
    if (clip->nbFrames() > 0 && endFrame > clip->nbFrames())
        endFrame = static_cast<int>(clip->nbFrames());
    if (endFrame <= startFrame)
        endFrame = startFrame + 1;
    const int totalFrames = endFrame - startFrame;
    const double inPoint = startFrame * static_cast<double>(den) / num;
    const double outPoint = endFrame * static_cast<double>(den) / num;

    // ── Colour grade (PR C5) ────────────────────────────────────────────
    // The clip's OWN grade is armed here, per run, so the A6 batch queue
    // applies each clip's grade automatically. The one-shot
    // setColorFilterChain() hook, when armed, wins as an explicit override
    // (test/advanced seam). overlaysOnly never grades: there is no footage
    // in that output, and its pipe (overlayPipeArgs) carries no snippet.
    //
    // Frame-pts contract: the plan's sendcmd timestamps are 0-based at the
    // in-point. The graph receives input 0 after `-ss inPoint +
    // sourceStartOffset − frameDur/4`, so the first delivered frame's pts
    // is ~frameDur/4; fps=round=near then snaps it onto the grid at 0, and
    // output frame k reaches the snippet with pts k·den/num. Residual: a
    // command timestamp can land up to a quarter frame after "its" frame's
    // true clip time — at the C1 command file's 0.5 s cadence that phase
    // shift moves a command boundary by ≤8 ms (30 fps), i.e. ≤1 LSB-class
    // on any realistic depth ramp. The end-to-end parity test
    // (composite_export_test::gradedCompositeDepthLinked) is the judge.
    //
    // plan() writes its temp files (fixed names) into m_tempDir — see the
    // member's comment for why sequential runs can't collide.
    QString gradeSnippet = colorSnippet;
    QStringList gradeNotes;
    if (!overlaysOnly && gradeSnippet.isEmpty()) {
        bool gradeOk = false;
        const Unabara::ColorGrade grade =
            Unabara::ColorGrade::fromJson(clip->colorGrade(), &gradeOk);
        if (gradeOk && grade.enabled && !grade.anchors.isEmpty()) {
            if (!m_tempDir.isValid()) {
                emit exportError(tr("Cannot prepare the colour grade: no writable "
                                    "temporary directory"));
                return false;
            }
            const Unabara::ColorGradeExport::Plan gradePlan =
                Unabara::ColorGradeExport::plan(grade, dive, clip->syncOffset(),
                                                inPoint, outPoint, m_tempDir.path());
            if (!gradePlan.isValid()) {
                emit exportError(tr("Colour grade could not be prepared: %1")
                                     .arg(gradePlan.error));
                return false;
            }
            // Non-fatal advisories (missing user LUT, no dive log for the
            // depth link): collected here, toasted AFTER m_busy is set —
            // a synchronous exportWarning subscriber must not find the
            // busy guard open and start a second run on the shared
            // QProcess (the run-serial machinery guards exactly that
            // corruption class).
            gradeNotes = gradePlan.notes;
            gradeSnippet = gradePlan.filterSnippet;
        }
    }

    m_busy = true;
    m_cancelRequested = false;
    const int runSerial = ++m_runSerial;
    emit busyChanged();
    for (const QString &note : std::as_const(gradeNotes))
        emit exportWarning(note);
    emit exportStarted();
    emit statusUpdate(tr("Rendering and encoding frames..."));

    QDir dir(m_baseDirectory);
    if (!dir.exists() && !dir.mkpath(".")) {
        emit exportError(tr("Failed to create export directory: %1")
                             .arg(m_baseDirectory));
        m_busy = false;
        emit busyChanged();
        return false;
    }

    QString outputPath;
    if (!m_pendingOutputPath.isEmpty()) {
        outputPath = m_pendingOutputPath;
        m_pendingOutputPath.clear();
    } else {
        outputPath = generateUniqueFileName(
            dive, getFileExtensionForCodec(m_videoCodec), clip->path(),
            overlaysOnly ? QStringLiteral("overlays_placed")
                         : QStringLiteral("composited"));
    }
    m_lastOutputPath = outputPath;

    m_totalFrames = totalFrames;
    m_renderedFrames = 0;
    m_encodedFrames = 0;
    m_pipeErrorMessage.clear();
    m_progress = 0;
    emit progressChanged();

    // Dive time of frame i (v0.4 plan): syncOffset + in + i*den/num.
    const double syncOffset = clip->syncOffset();
    const auto timeOfFrame = [syncOffset, inPoint, num, den](int i) {
        return syncOffset + inPoint + i * static_cast<double>(den) / num;
    };

    // Pipe byte contract: the composite graph declares
    // overlay=alpha=premultiplied, so composites ship the compositor's
    // premultiplied plane untouched; the placed-overlays mode goes through
    // the straight-alpha overlay pipe (same contract as exportVideo).
    const QImage::Format pixelFormat = overlaysOnly
                                           ? QImage::Format_RGBA8888
                                           : QImage::Format_RGBA8888_Premultiplied;

    source.beginExport();
    QImage firstFrame =
        toPipeFormat(source.generate(dive, timeOfFrame(0)), pixelFormat);
    if (firstFrame.isNull()) {
        source.endExport();
        emit exportError(tr("Failed to generate the first frame"));
        m_busy = false;
        emit busyChanged();
        return false;
    }

    const FFmpegArgs::FpsRational fps{num, den};
    QStringList args;
    if (overlaysOnly) {
        args = FFmpegArgs::overlayPipeArgs(
            frameSize.width(), frameSize.height(), fps,
            m_videoCodec, m_videoQuality, QSize(), outputPath);
    } else {
        FFmpegArgs::CompositePlan plan;
        plan.clipPath = clip->path();
        plan.inPoint = inPoint;
        plan.outPoint = outPoint;
        plan.sourceStartOffset = clip->videoStartTime();
        plan.totalFrames = totalFrames;
        plan.fps = fps;
        plan.width = frameSize.width();
        plan.height = frameSize.height();
        plan.inColorMatrix =
            FFmpegArgs::inColorMatrixFor(clip->colorSpace(), clip->height());
        plan.colorFilterSnippet = gradeSnippet;
        plan.codec = m_videoCodec;
        plan.quality = m_videoQuality;
        plan.hasAudio = clip->hasAudio();
        plan.audioCodec = clip->audioCodec();
        plan.outputPath = outputPath;
        args = FFmpegArgs::compositeArgs(plan);
    }

    if (!startFFmpegProcess(args)) {
        source.endExport();
        m_busy = false;
        emit busyChanged();
        return false; // startFFmpegProcess() emitted exportError()
    }

    const StreamResult result = streamFrames(
        dive, &source, totalFrames, frameSize, pixelFormat, timeOfFrame,
        firstFrame, runSerial);
    source.endExport();
    return finishRun(result, runSerial);
}

QString VideoExporter::compositeAudioText(bool hasAudio,
                                          const QString &audioCodec,
                                          const QString &codec)
{
    switch (FFmpegArgs::decideAudio(codec, hasAudio, audioCodec)) {
    case FFmpegArgs::AudioDecision::Copy:
        return tr("Audio: copied (%1)").arg(audioCodec);
    case FFmpegArgs::AudioDecision::Transcode:
        return tr("Audio: re-encoded to AAC");
    case FFmpegArgs::AudioDecision::None:
        break;
    }
    return tr("No audio track");
}

QString VideoExporter::compositeColorTagText(const QString &colorSpace,
                                             int height)
{
    if (height <= 0)
        return QString(); // unprobed clip: nothing meaningful to say yet
    // Same decision the export takes (FFmpegArgs::inColorMatrixFor): empty
    // = tagged source, decoded as tagged — no assumption to warn about.
    const QString forced = FFmpegArgs::inColorMatrixFor(colorSpace, height);
    if (forced.isEmpty())
        return QString();
    return forced == QLatin1String("bt601")
               ? tr("No colour tags on this clip — assuming BT.601 (SD)")
               : tr("No colour tags on this clip — assuming BT.709 (HD)");
}

QString VideoExporter::compositeGradeText(VideoClip* clip, DiveData* dive)
{
    if (!clip)
        return QString();
    bool ok = false;
    const Unabara::ColorGrade grade =
        Unabara::ColorGrade::fromJson(clip->colorGrade(), &ok);
    if (!ok || !grade.enabled || grade.anchors.isEmpty())
        return QString();
    // usesDepthPath is THE shared decision — this row must never disagree
    // with the route plan() actually takes at export time.
    QString text = Unabara::ColorGradeExport::usesDepthPath(grade, dive)
                       ? tr("Colour grade: applied (depth-linked)")
                       : tr("Colour grade: applied (static)");
    if (!grade.lutPath.isEmpty() && !QFile::exists(grade.lutPath))
        text += tr(" — LUT file missing, parametric grade only");
    return text;
}

void VideoExporter::cancelExport()
{
    if (!m_busy || m_cancelRequested) {
        return;
    }
    m_cancelRequested = true;

    if (m_ffmpegProcess && m_ffmpegProcess->state() != QProcess::NotRunning) {
        // FFmpeg runs for the whole export (frames stream into it), so a
        // cancel in either the render loop or the final drain lands here:
        // ask FFmpeg to stop and return immediately. The normal finished()
        // delivery runs onFFmpegFinished(), which completes the cancellation
        // (partial-file removal, exportCancelled()). No blocking wait on the
        // GUI thread, and no re-entering the UI from inside the Cancel
        // button's handler. If FFmpeg ignores SIGTERM, escalate to kill()
        // after a grace period.
        m_ffmpegProcess->terminate();
        // Tracked (not a fire-and-forget singleShot): m_ffmpegProcess is
        // reused across runs, so this timer must be disarmed in
        // onFFmpegFinished() — otherwise a prompt termination followed by a
        // new export within 3 s would get that new process killed.
        if (!m_killTimer) {
            m_killTimer = new QTimer(this);
            m_killTimer->setSingleShot(true);
            m_killTimer->setInterval(3000);
            connect(m_killTimer, &QTimer::timeout, this, [this]() {
                if (m_ffmpegProcess && m_ffmpegProcess->state() != QProcess::NotRunning) {
                    m_ffmpegProcess->kill();
                }
            });
        }
        m_killTimer->start();
    }
    // If the process isn't running (nothing started yet, or it already
    // finished), there is nothing to stop — the render loop polls
    // m_cancelRequested and unwinds through exportVideo().
}

bool VideoExporter::startFFmpegProcess(const QStringList &args)
{
    // Create a QProcess for FFmpeg if it doesn't exist
    if (!m_ffmpegProcess) {
        m_ffmpegProcess = new QProcess(this);

        // Connect signals for output
        connect(m_ffmpegProcess, &QProcess::readyReadStandardOutput, this, &VideoExporter::processFFmpegOutput);
        connect(m_ffmpegProcess, &QProcess::readyReadStandardError, this, &VideoExporter::processFFmpegOutput);
        connect(m_ffmpegProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                this, &VideoExporter::onFFmpegFinished);
    }

    // Get the path to FFmpeg
    QString ffmpegPath = findFFmpegPath();
    if (ffmpegPath.isEmpty()) {
        emit exportError(tr("FFmpeg executable not found"));
        return false;
    }

    // Log the full command for debugging (args always come from FFmpegArgs
    // — the ONE builder; see the callers in exportVideo/exportComposite)
    QString cmdLog = ffmpegPath;
    for (const QString &arg : args) {
        cmdLog += " " + (arg.contains(" ") ? "\"" + arg + "\"" : arg);
    }
    qInfo() << "FFmpeg command:" << cmdLog;

    // Reset the captured-output buffers for this run.
    m_ffmpegOutputTail.clear();
    m_progressBuffer.clear();

    // Start the FFmpeg process
    m_ffmpegProcess->start(ffmpegPath, args);

    // Wait for the process to start
    if (!m_ffmpegProcess->waitForStarted(5000)) {
        qInfo() << tr("Failed to start FFmpeg: %1").arg(m_ffmpegProcess->errorString());
        emit exportError(tr("Failed to start FFmpeg: %1").arg(m_ffmpegProcess->errorString()));
        return false;
    }

    return true; // Process started successfully
}

bool VideoExporter::writeFramePixels(const QImage &frame)
{
    if (m_ffmpegProcess->state() != QProcess::Running) {
        return false;
    }

    const qint64 rowBytes = qint64(frame.width()) * 4;
    if (frame.bytesPerLine() == rowBytes) {
        const qint64 total = rowBytes * frame.height();
        return m_ffmpegProcess->write(
                   reinterpret_cast<const char*>(frame.constBits()), total) == total;
    }

    // QImage scanlines are 32-bit aligned, so bytesPerLine() can exceed
    // width*4 — streaming the raw buffer would feed the pad bytes into the
    // pixel stream and shear every row. (At 4 bytes/pixel the stride is in
    // fact always tight, but this is the documented QImage contract, not a
    // guarantee — keep the row-wise fallback.)
    for (int y = 0; y < frame.height(); ++y) {
        if (m_ffmpegProcess->write(
                reinterpret_cast<const char*>(frame.constScanLine(y)), rowBytes) != rowBytes) {
            return false;
        }
    }
    return true;
}

bool VideoExporter::waitForPipeDrain(qint64 frameBytes)
{
    // Spin while more than two frames sit in the write channel. Each pass
    // flushes for up to 20 ms, then pumps events — which keeps the UI live,
    // delivers Cancel clicks, and (crucially) delivers errorOccurred()/
    // finished() so a dead FFmpeg flips state() and ends the spin instead
    // of hanging the writer forever.
    while (m_ffmpegProcess->bytesToWrite() > 2 * frameBytes) {
        if (m_ffmpegProcess->state() != QProcess::Running) {
            return false;
        }
        if (m_cancelRequested) {
            return true; // caller handles cancellation
        }
        m_ffmpegProcess->waitForBytesWritten(20);
        QCoreApplication::processEvents();
    }
    return m_ffmpegProcess->state() == QProcess::Running;
}

void VideoExporter::emitInterleavedStatus()
{
    emit statusUpdate(tr("Rendered %1 / encoded %2 of %3 frames")
                          .arg(m_renderedFrames)
                          .arg(m_encodedFrames)
                          .arg(m_totalFrames));
}

void VideoExporter::processFFmpegOutput()
{
    if (!m_ffmpegProcess) {
        return;
    }

    // stdout: the machine-readable `-progress pipe:1` stream. Buffer and
    // split on newlines (chunks can end mid-line), then take the last
    // complete `frame=N` of this batch and report it against m_totalFrames
    // — never against a temp-dir file count, which cost a directory listing
    // per chunk.
    m_progressBuffer += m_ffmpegProcess->readAllStandardOutput();
    int currentFrame = -1;
    int newline = -1;
    while ((newline = m_progressBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_progressBuffer.left(newline).trimmed();
        m_progressBuffer.remove(0, newline + 1);
        if (line.startsWith("frame=")) {
            bool ok = false;
            const int frame = line.mid(6).trimmed().toInt(&ok);
            if (ok) {
                currentFrame = frame;
            }
        }
    }
    if (currentFrame >= 0 && m_totalFrames > 0) {
        // One interleaved phase (PR B2): frames render and encode
        // concurrently, so the encoded count IS the progress, mapped 0-100
        // over m_totalFrames.
        const int encoded = qMin(currentFrame, m_totalFrames);
        if (encoded != m_encodedFrames) {
            m_encodedFrames = encoded;
            emitInterleavedStatus();
        }
        const int newProgress = qMin((currentFrame * 100) / m_totalFrames, 100);
        if (newProgress != m_progress) {
            m_progress = newProgress;
            emit progressChanged();
        }
    }

    // stderr: warnings/errors only (-nostats). Retain a rolling tail so the
    // real error message survives until onFFmpegFinished(); cap it so a
    // long-running encode can't grow it unbounded.
    const QByteArray errOutput = m_ffmpegProcess->readAllStandardError();
    if (!errOutput.isEmpty()) {
        m_ffmpegOutputTail += QString::fromUtf8(errOutput);
        const int kMaxTail = 4000;
        if (m_ffmpegOutputTail.size() > kMaxTail) {
            m_ffmpegOutputTail = m_ffmpegOutputTail.right(kMaxTail);
        }
    }
}

void VideoExporter::onFFmpegFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_killTimer) {
        m_killTimer->stop();
    }

    if (m_cancelRequested) {
        // Cancelled mid-export: the exit status only reflects the kill we
        // sent, so don't report it as an FFmpeg error. Discard the
        // partially-written output file — a truncated video is useless.
        if (!m_lastOutputPath.isEmpty()) {
            QFile::remove(m_lastOutputPath);
        }
        m_busy = false;
        emit busyChanged();
        emit exportCancelled();
        return;
    }

    bool success = false;
    QString errorMessage;

    if (!m_pipeErrorMessage.isEmpty()) {
        // The render loop killed the process after a failed stdin write;
        // report that, not the kill-induced crash exit.
        qWarning().noquote() << m_pipeErrorMessage
                             << "- captured output tail:\n" << m_ffmpegOutputTail;
        errorMessage = m_pipeErrorMessage;
    } else if (exitStatus == QProcess::CrashExit) {
        qWarning().noquote() << "FFmpeg crashed. Captured output tail:\n" << m_ffmpegOutputTail;
        errorMessage = tr("FFmpeg process crashed");
    } else if (exitCode != 0) {
        qWarning().noquote() << "FFmpeg exited with code" << exitCode
                             << "- captured output tail:\n" << m_ffmpegOutputTail;
        errorMessage = tr("FFmpeg exited with error code: %1").arg(exitCode);
    } else {
        m_progress = 100; // Ensure we reach 100%
        emit progressChanged();
        emit statusUpdate(tr("Video encoding completed successfully"));
        success = true;
    }

    if (!success) {
        // Shared failure path (pipe error / crash / nonzero exit). The full
        // tail went to qWarning above; the user-facing message carries
        // FFmpeg's last few non-empty stderr lines (e.g. a codec/container
        // mismatch or a filter-config error) — the generic text or a bare
        // exit code alone is undiagnosable. Empty tail → message unchanged.
        const QString excerpt = ExportMath::outputTailExcerpt(m_ffmpegOutputTail);
        if (!excerpt.isEmpty())
            errorMessage += QStringLiteral("\n\n") + excerpt;
        // A failed encode leaves a truncated or empty (0-byte header-less)
        // output file behind — useless, so discard it like the cancel
        // branch does. Only this run's own output path, nothing else.
        if (!m_lastOutputPath.isEmpty() && QFile::exists(m_lastOutputPath))
            QFile::remove(m_lastOutputPath);
        emit exportError(errorMessage);
    }

    m_busy = false;
    emit busyChanged();

    if (success) {
        emit exportFinished(true, m_lastOutputPath);
    }
}

QSize VideoExporter::getDefaultOverlaySize()
{
    // Load the default overlay image and get its dimensions
    QImage defaultOverlay(":/images/DC_Faces/unabara_round_ocean.png");
    if (!defaultOverlay.isNull()) {
        QSize size = defaultOverlay.size();
        qDebug() << "Using default overlay size:" << size;
        return size;
    }
    
    // If default overlay can't be loaded for some reason, fall back to 16:9 HD
    qWarning() << "Could not load default overlay image, using fallback size 1280x720";
    return QSize(1280, 720);
}

double VideoExporter::extractVideoTimecode(const QString &videoPath)
{
    // Thin delegate to the single-ffprobe-call prober (PR A1). Same
    // semantics as the old `ffmpeg -i` stderr grep — seconds-of-day,
    // frames ignored, -1 when absent — plus drop-frame (HH:MM:SS;FF)
    // support. PR A2/B1 move the QML callers onto the clip model and
    // retire this shim.
    return VideoProbe::probe(videoPath).timecodeSecondsOfDay;
}

double VideoExporter::extractVideoCreationTime(const QString &videoPath)
{
    // Thin delegate (PR A1): epoch seconds of the container's creation_time
    // tag, -1 when absent/unreadable — identical to the old inline ffprobe
    // call, but through the sibling-resolved ffprobe path (the historical
    // "ffmpeg"→"ffprobe" string swap broke on paths like
    // "C:/Program Files/ffmpeg/bin/", which silently disabled camera
    // pairing on Windows).
    const VideoProbe::Result r = VideoProbe::probe(videoPath);
    if (!r.creationTimeUtc.isValid()) {
        qDebug() << "extractVideoCreationTime: no creation time for" << videoPath
                 << (r.ok ? QString() : r.error);
        return -1.0;
    }
    return static_cast<double>(r.creationTimeUtc.toSecsSinceEpoch());
}

QSize VideoExporter::detectVideoResolution(const QString &videoPath)
{
    // Thin delegate (PR A1): one ffprobe call replaces the historical
    // three-approach cascade. Dimensions are now rotation-applied — a
    // portrait phone clip with a 90° display rotation reports 1080x1920,
    // where the old stream-level parse returned the landscape coded size.
    const VideoProbe::Result r = VideoProbe::probe(videoPath);
    if (r.ok && r.width > 0 && r.height > 0) {
        qDebug() << "Detected video resolution:" << r.width << "x" << r.height;
        return QSize(r.width, r.height);
    }

    qWarning() << "Video resolution detection failed for" << videoPath
               << "-" << r.error << "- using default overlay size";
    return getDefaultOverlaySize();
}

QString VideoExporter::createDefaultExportFile(DiveData* dive,
                                               const QString &videoFilePath,
                                               const QString &contentType)
{
    if (!dive) {
        return QString();
    }

    // Generate a unique file name for this dive
    QString extension = getFileExtensionForCodec(m_videoCodec);
    m_pendingOutputPath = generateUniqueFileName(dive, extension, videoFilePath, contentType);
    return m_pendingOutputPath;
}

QString VideoExporter::generateUniqueFileName(DiveData* dive,
                                              const QString &extension,
                                              const QString &videoFilePath,
                                              const QString &contentType)
{
    // Shared base-name builder (same naming as ImageExporter's directories),
    // plus the codec-derived extension.
    QString baseName = ExportMath::exportBaseName(dive, videoFilePath, contentType);
    baseName += "." + extension;

    // Create full path under the QML-bound base export directory - make
    // sure the directory exists before returning the file path
    QDir dir(m_baseDirectory);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // Create and return the full path
    return QDir(m_baseDirectory).filePath(baseName);
}