#ifndef FFMPEG_ARGS_H
#define FFMPEG_ARGS_H

#include <QSize>
#include <QString>
#include <QStringList>

// Pure FFmpeg command-line assembly for the export pipeline, extracted from
// VideoExporter so the argument lists are unit-testable (filtergraph_test,
// composite_export_test) and exist in exactly ONE place — the dead duplicate
// arg-builders this file replaces (createFFmpegArgs/generateFFmpegCommand,
// deleted in PR B1) drifted from the live inline code for months.
// VideoExporter calls these; nothing may rebuild the same strings elsewhere.
namespace FFmpegArgs {

// Frame rate as an exact rational for FFmpeg's -framerate. The overlay-only
// export dialog produces integer rates today, but the composite path (PR B3)
// needs clip-native rationals, so the arg builders take NUM/DEN from the
// start.
struct FpsRational {
    int num = 30;
    int den = 1;
};

// Millisecond-precision rational for a dialog-chosen double rate: the value
// FFmpeg times frames with must match the value ExportMath::frameTime()
// drives the render loop with, so this is round(fps*1000)/1000 reduced —
// NOT a lookup of broadcast rationals (29.97 maps to 2997/100, not
// 30000/1001, because 29.97 is what the frame loop actually uses).
FpsRational fpsToRational(double fps);

// "30" or "2997/100" — how FpsRational is spelled on the command line.
QString fpsString(const FpsRational &fps);

// ---- Quality tiers (PR B3) -------------------------------------------
// The export dialog's quality combo {Best, High, Standard, Small} replaced
// the historical bitrate slider (which never worked for x264/x265: the
// fixed -crf 23 always won over -b:v). One id set, lower-case, stored in
// Config::videoQuality. Unknown ids fall back to "standard" everywhere.
//
//   quality    h264/hevc CRF   prores profile    vp9 bitrate
//   best       18              3 (422 HQ)        12000k
//   high       20              3 (422 HQ)         8000k
//   standard   23              2 (422)            4000k
//   small      28              2 (422)            2000k
//
// "standard" h264/hevc is byte-identical to the pre-B3 fixed -crf 23.
// The prores profile column applies to the composite path's ProRes 422
// family only; the alpha-capable overlay-only export keeps ProRes 4444
// (profile locked — alpha is the point of that path).
int qualityCrf(const QString &quality);
int proresProfile422(const QString &quality);
int vp9BitrateKbps(const QString &quality);

// Input block for raw RGBA frames streamed over stdin:
//   -f rawvideo -pix_fmt rgba -s WxH -framerate N[/D] -i pipe:0
// The byte layout is the same for straight and premultiplied alpha — the
// overlay-only pipe streams straight RGBA8888, the composite pipe streams
// RGBA8888_Premultiplied and tells the overlay filter so
// (overlay=alpha=premultiplied).
QStringList rawInputArgs(int width, int height, const FpsRational &fps);

// Per-codec output options + optional scale filter + output path for the
// overlay-only export. The option strings match the pre-raw-pipe PNG path,
// including the output -pix_fmt (yuv420p for h264/hevc — FFmpeg auto-inserts
// the rgba→yuv420p conversion exactly as it did for PNG input, dropping
// alpha; yuva444p10le / yuva420p keep alpha for prores / vp9), except that
// the bitrate slider's -b:v became the quality tier above. Unknown codec
// falls back to h264, as before.
QStringList outputArgs(const QString &codec, const QString &quality,
                       const QSize &customResolution,
                       const QString &outputPath);

// The full argument list for the overlay-only raw-pipe export:
// global flags (-y, -progress pipe:1, -nostats) + rawInputArgs + outputArgs.
QStringList overlayPipeArgs(int width, int height, const FpsRational &fps,
                            const QString &codec, const QString &quality,
                            const QSize &customResolution,
                            const QString &outputPath);

// ---- Composited export (PR B3) ---------------------------------------

// Whether the source clip's audio can be stream-copied into the output
// container, must be re-encoded, or is absent entirely. Exposed (through
// VideoExporter::compositeAudioText) so the export dialog can say which.
enum class AudioDecision { None, Copy, Transcode };

// Copy is safe per (container, codec): the container follows the video
// codec (h264/hevc → mp4, prores → mov); mp4 accepts aac/mp3/ac3/eac3,
// mov additionally pcm*/alac. Everything else → -c:a aac -b:a 256k.
AudioDecision decideAudio(const QString &videoCodec, bool hasAudio,
                          const QString &audioCodec);

// Colour decision for the source → RGB conversion (v0.4 plan, verbatim):
// a TAGGED source (non-empty, non-"unknown" colorSpace) is decoded as
// tagged — swscale reads the frame's own colour tags, so no explicit
// matrix is forced and the return is empty. An UNTAGGED source gets the
// matrix forced by resolution convention: "bt709" when height >= 720,
// "bt601" below (SD material predates bt709). The output side is always
// converted and tagged bt709/tv regardless.
QString inColorMatrixFor(const QString &colorSpace, int height);

// Everything buildCompositeFilterGraph/compositeArgs need, precomputed by
// VideoExporter::exportComposite. All times are CLIP seconds.
struct CompositePlan {
    QString clipPath;
    // Export range snapped to the clip's frame grid: inPoint/outPoint are
    // startFrame*den/num and endFrame*den/num exactly, and totalFrames is
    // endFrame-startFrame — the authoritative output frame count
    // (-frames:v), matched one-for-one by the frames streamed into pipe:0.
    double inPoint = 0.0;
    double outPoint = 0.0;
    int totalFrames = 0;
    // The source VIDEO STREAM's start_time (VideoProbe/VideoClip). The
    // clip-time grid (frame k at k*den/num, what the player and the sync
    // offset work in) sits on top of this container offset, so the -ss/-to
    // window must add it back — a stream that starts at +23 ms (AAC-priming
    // muxer shift) would otherwise come out one frame late off the fps
    // grid. 0 for the common video-starts-at-zero case.
    double sourceStartOffset = 0.0;
    FpsRational fps;          // clip-native rational (never a double)
    int width = 0;            // rotation-applied clip size = canvas size
    int height = 0;
    QString inColorMatrix;    // from inColorMatrixFor(); empty = tagged
    // Track-C colour snippet inserted verbatim between format=rgb24 and
    // overlay. Empty = no grading: the overlay is wired straight to [src]
    // (no `null` passthrough stage — one fewer filter instance).
    QString colorFilterSnippet;
    QString codec;            // "h264" | "hevc" | "prores"
    QString quality;          // quality tier id, see above
    bool hasAudio = false;
    QString audioCodec;       // probe's codec_name, e.g. "aac", "pcm_s16le"
    QString outputPath;
};

// The -filter_complex graph, exactly the v0.4 plan's shape:
//   [0:v]fps=NUM/DEN:round=near[,scale=in_color_matrix=…],format=rgb24[src]
//   [src]<colour snippet>[graded]                      (only when non-empty)
//   [graded|src][1:v]overlay=alpha=premultiplied:format=rgb:eof_action=endall[comp]
//   [comp]scale=out_color_matrix=bt709:out_range=tv,format=yuv420p[out]
// (prores: yuv422p10le). format=rgb24 directly after the scale constrains
// that scale instance's output to rgb24, so the forced in_color_matrix is
// used for the YUV→RGB conversion itself.
QString buildCompositeFilterGraph(const CompositePlan &plan);

// The full composite argument list:
//   -y -hide_banner -loglevel warning -progress pipe:1 -nostats
//   -accurate_seek -ss <in> -to <out> -i <clip>
//   <rawInputArgs(pipe:0)> -filter_complex <graph> -map [out] -map 0:a?
//   <codec opts> <audio opts> <bt709/tv colour tags> -frames:v N <out>
// The -ss/-to spellings back off from the exact frame boundary by a
// quarter frame (see compositeArgs' implementation comment) so decimal
// rounding can never drop/duplicate the boundary frame; exact frame
// alignment is still guaranteed by -frames:v N + the pipe's frame count +
// eof_action=endall. No -shortest — it cost real frames (see the
// implementation comment).
QStringList compositeArgs(const CompositePlan &plan);

} // namespace FFmpegArgs

#endif // FFMPEG_ARGS_H
