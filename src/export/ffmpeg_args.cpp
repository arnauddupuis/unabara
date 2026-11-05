#include "include/export/ffmpeg_args.h"

#include <QtGlobal>
#include <cmath>
#include <numeric>

namespace FFmpegArgs {

FpsRational fpsToRational(double fps)
{
    if (fps <= 0.0) {
        // Degenerate rate: ExportMath::totalFrames clamps the loop to one
        // frame; time that single frame at 1 fps.
        return {1, 1};
    }
    // Millisecond precision covers every dialog-enterable rate exactly and
    // matches the double the render loop uses to within container timestamp
    // resolution.
    qint64 num = qRound64(fps * 1000.0);
    qint64 den = 1000;
    const qint64 g = std::gcd(num, den);
    return {static_cast<int>(num / g), static_cast<int>(den / g)};
}

QString fpsString(const FpsRational &fps)
{
    if (fps.den == 1)
        return QString::number(fps.num);
    return QStringLiteral("%1/%2").arg(fps.num).arg(fps.den);
}

// Quality-tier tables (see the header's matrix). "standard" is both the
// fallback for unknown ids and byte-identical to the pre-B3 fixed values.
int qualityCrf(const QString &quality)
{
    if (quality == QLatin1String("best"))
        return 18;
    if (quality == QLatin1String("high"))
        return 20;
    if (quality == QLatin1String("small"))
        return 28;
    return 23; // standard + fallback (the historical fixed -crf 23)
}

int proresProfile422(const QString &quality)
{
    // ProRes 422 family for the composite path: profile 3 = 422 HQ,
    // profile 2 = 422. (The alpha overlay-only path stays profile 4444.)
    if (quality == QLatin1String("best") || quality == QLatin1String("high"))
        return 3;
    return 2;
}

int vp9BitrateKbps(const QString &quality)
{
    if (quality == QLatin1String("best"))
        return 12000;
    if (quality == QLatin1String("high"))
        return 8000; // the historical slider default
    if (quality == QLatin1String("small"))
        return 2000;
    return 4000; // standard + fallback
}

QStringList rawInputArgs(int width, int height, const FpsRational &fps)
{
    QStringList args;
    args << "-f" << "rawvideo"
         << "-pix_fmt" << "rgba"
         << "-s" << QStringLiteral("%1x%2").arg(width).arg(height)
         << "-framerate" << fpsString(fps)
         << "-i" << "pipe:0";
    return args;
}

QStringList outputArgs(const QString &codec, const QString &quality,
                       const QSize &customResolution,
                       const QString &outputPath)
{
    QStringList args;

    // Optional downscale (e.g. to the imported video's resolution) — same
    // placement and spelling as the PNG-input path.
    if (customResolution.isValid()
        && customResolution.width() > 0 && customResolution.height() > 0) {
        args << "-vf" << QStringLiteral("scale=%1:%2")
                             .arg(customResolution.width())
                             .arg(customResolution.height());
    }

    // Codec options: same shape as the pre-raw-pipe strings (the output
    // -pix_fmt makes the implicit rgba→target conversion explicit per
    // codec, so output files probe the same as before), with the quality
    // tier replacing the bitrate slider (PR B3): CRF for h264/hevc — whose
    // dead -b:v the fixed CRF always overrode anyway — and a real bitrate
    // for vp9.
    QString options;
    if (codec == "prores") {
        options = QStringLiteral("-c:v prores_ks -profile:v 4444 -pix_fmt yuva444p10le "
                                 "-alpha_bits 16 -bits_per_mb 8000 -vendor ap10");
    } else if (codec == "vp9") {
        options = QStringLiteral("-c:v libvpx-vp9 -pix_fmt yuva420p -b:v %1k "
                                 "-deadline good -cpu-used 2")
                      .arg(vp9BitrateKbps(quality));
    } else if (codec == "hevc") {
        options = QStringLiteral("-c:v libx265 -preset medium -crf %1 -pix_fmt yuv420p "
                                 "-tag:v hvc1").arg(qualityCrf(quality));
    } else {
        // h264, and the historical fallback for unknown codec ids.
        options = QStringLiteral("-c:v libx264 -preset medium -crf %1 -pix_fmt yuv420p "
                                 "-movflags +faststart").arg(qualityCrf(quality));
    }
    args.append(options.split(QLatin1Char(' '), Qt::SkipEmptyParts));

    args << outputPath;
    return args;
}

QStringList overlayPipeArgs(int width, int height, const FpsRational &fps,
                            const QString &codec, const QString &quality,
                            const QSize &customResolution,
                            const QString &outputPath)
{
    // `-progress pipe:1` streams machine-readable key=value lines (frame=N,
    // ...) to stdout for the interleaved progress display; `-nostats` keeps
    // the human-readable status line off stderr so the rolling error tail
    // holds only warnings/errors.
    QStringList args;
    args << "-y"
         << "-progress" << "pipe:1"
         << "-nostats";
    args += rawInputArgs(width, height, fps);
    args += outputArgs(codec, quality, customResolution, outputPath);
    return args;
}

AudioDecision decideAudio(const QString &videoCodec, bool hasAudio,
                          const QString &audioCodec)
{
    if (!hasAudio)
        return AudioDecision::None;

    // Container follows the video codec (getFileExtensionForCodec):
    // h264/hevc → mp4, prores → mov. Copy-safe codec sets per the plan.
    const bool mov = (videoCodec == QLatin1String("prores"));
    if (audioCodec == QLatin1String("aac") || audioCodec == QLatin1String("mp3")
        || audioCodec == QLatin1String("ac3") || audioCodec == QLatin1String("eac3")) {
        return AudioDecision::Copy;
    }
    if (mov && (audioCodec == QLatin1String("alac")
                || audioCodec.startsWith(QLatin1String("pcm_")))) {
        return AudioDecision::Copy;
    }
    return AudioDecision::Transcode;
}

QString inColorMatrixFor(const QString &colorSpace, int height)
{
    // Tagged source → decode as tagged (swscale reads the frame's own
    // colour properties; forcing a matrix would be wrong for e.g. bt2020).
    if (!colorSpace.isEmpty() && colorSpace != QLatin1String("unknown"))
        return QString();
    // Untagged → resolution convention: HD material is bt709, SD is bt601.
    return height >= 720 ? QStringLiteral("bt709") : QStringLiteral("bt601");
}

QString buildCompositeFilterGraph(const CompositePlan &plan)
{
    // Stage 1: conform the source to the exact output frame grid
    // (fps=NUM/DEN:round=near — also what conforms VFR material) and
    // convert to rgb24. When a matrix must be forced (untagged source) the
    // explicit scale instance does the YUV→RGB conversion itself, because
    // the format=rgb24 right after it constrains its output pad to rgb24.
    QString src = QStringLiteral("[0:v]fps=%1:round=near").arg(fpsString(plan.fps));
    if (!plan.inColorMatrix.isEmpty())
        src += QStringLiteral(",scale=in_color_matrix=%1").arg(plan.inColorMatrix);
    src += QStringLiteral(",format=rgb24[src]");

    QStringList chains;
    chains << src;

    // Stage 2 (optional): the track-C colour snippet, inserted VERBATIM
    // between format=rgb24 and overlay — the setColorFilterChain contract.
    // Empty snippet = overlay wired straight to [src].
    QString overlayIn = QStringLiteral("[src]");
    if (!plan.colorFilterSnippet.isEmpty()) {
        chains << QStringLiteral("[src]%1[graded]").arg(plan.colorFilterSnippet);
        overlayIn = QStringLiteral("[graded]");
    }

    // Stage 3: composite the premultiplied RGBA overlay plane from pipe:0.
    // eof_action=endall ends the output when the pipe's N frames run out —
    // one of the three mechanisms (with -frames:v N and the exact pipe
    // frame count) that pin the output to exactly N frames.
    chains << overlayIn
                  + QStringLiteral("[1:v]overlay=alpha=premultiplied:format=rgb:"
                                   "eof_action=endall[comp]");

    // Stage 4: back to YUV, always tagged-and-converted bt709/tv. The
    // scale sets the frame-level matrix/range properties; primaries and
    // transfer must ALSO be stamped as frame properties (setparams) —
    // since FFmpeg 6 the CLI's -color_primaries/-color_trc output options
    // lose to unset frame properties, so without setparams the output
    // probes "unknown" there (verified on 7.1.5).
    const QString outFormat = plan.codec == QLatin1String("prores")
                                  ? QStringLiteral("yuv422p10le")
                                  : QStringLiteral("yuv420p");
    chains << QStringLiteral("[comp]scale=out_color_matrix=bt709:out_range=tv,"
                             "setparams=color_primaries=bt709:color_trc=bt709,"
                             "format=%1[out]").arg(outFormat);

    return chains.join(QLatin1Char(';'));
}

QStringList compositeArgs(const CompositePlan &plan)
{
    // -ss/-to spelling: the plan's in/out points sit EXACTLY on frame
    // boundaries (startFrame*den/num) in CLIP time; the stream adds
    // sourceStartOffset on top (see the struct comment). They go onto the
    // command line as rounded decimals, and with -accurate_seek a value
    // that rounds a microsecond ABOVE the boundary frame's pts would drop
    // that frame (and one rounding below the end would cut the last one).
    // Back off by a quarter frame on each side: far enough from both
    // boundaries that decimal/timebase rounding can never flip a frame,
    // and far enough from the half-frame midpoint that fps=round=near has
    // no tie to break. The exact output frame count is owned by -frames:v
    // N + the pipe's N frames + eof_action=endall, never by the seek
    // window.
    const double frameDur = plan.fps.num > 0
                                ? static_cast<double>(plan.fps.den) / plan.fps.num
                                : 0.0;
    const double seekIn =
        qMax(0.0, plan.inPoint + plan.sourceStartOffset - 0.25 * frameDur);
    const double seekOut = plan.outPoint + plan.sourceStartOffset + 0.25 * frameDur;

    QStringList args;
    args << "-y" << "-hide_banner" << "-loglevel" << "warning"
         << "-progress" << "pipe:1"
         << "-nostats"
         << "-accurate_seek"
         << "-ss" << QString::number(seekIn, 'f', 6)
         << "-to" << QString::number(seekOut, 'f', 6)
         << "-i" << plan.clipPath;

    // Input 1: the pre-placed full-frame premultiplied RGBA overlay plane,
    // streamed over stdin at the clip's exact rational rate and size.
    args += rawInputArgs(plan.width, plan.height, plan.fps);

    args << "-filter_complex" << buildCompositeFilterGraph(plan)
         << "-map" << "[out]"
         << "-map" << "0:a?";

    if (plan.codec == QLatin1String("prores")) {
        args << "-c:v" << "prores_ks"
             << "-profile:v" << QString::number(proresProfile422(plan.quality))
             << "-vendor" << "ap10";
    } else if (plan.codec == QLatin1String("hevc")) {
        args << "-c:v" << "libx265" << "-preset" << "medium"
             << "-crf" << QString::number(qualityCrf(plan.quality))
             << "-tag:v" << "hvc1";
    } else {
        // h264, and the fallback for unknown codec ids (as everywhere).
        args << "-c:v" << "libx264" << "-preset" << "medium"
             << "-crf" << QString::number(qualityCrf(plan.quality))
             << "-movflags" << "+faststart";
    }

    switch (decideAudio(plan.codec, plan.hasAudio, plan.audioCodec)) {
    case AudioDecision::Copy:
        args << "-c:a" << "copy";
        break;
    case AudioDecision::Transcode:
        args << "-c:a" << "aac" << "-b:a" << "256k";
        break;
    case AudioDecision::None:
        break; // -map 0:a? simply matches nothing
    }

    // Output colour metadata: the graph converted to bt709/tv, tag it so.
    //
    // Deliberately NO -shortest: the output window is already bounded on
    // both sides (-to caps the audio at a quarter frame past the last
    // video frame; -frames:v caps the video), and -shortest truncates the
    // delayed video-encoder tail whenever the source's audio stream is
    // marginally SHORTER than its video — measured 694/697 frames on a
    // real GoPro-style clip whose AAC track ends 44 ms early.
    args << "-colorspace" << "bt709"
         << "-color_primaries" << "bt709"
         << "-color_trc" << "bt709"
         << "-color_range" << "tv"
         << "-frames:v" << QString::number(plan.totalFrames)
         << plan.outputPath;
    return args;
}

} // namespace FFmpegArgs
