#ifndef EXPORT_MATH_H
#define EXPORT_MATH_H

#include <QString>
#include <QStringList>

class DiveData;

// Pure helpers shared by ImageExporter and VideoExporter, extracted so the
// arithmetic and naming rules are unit-testable and cannot drift between the
// two exporters again (the frame-count divide-by-zero guard once existed
// fixed in ImageExporter and unfixed in VideoExporter).
namespace ExportMath {

// Number of frames a range export reports progress against. The export loops
// always write at least one frame (time == startTime), so this is clamped to
// >= 1 — the progress division must never see zero, even for a degenerate
// (empty or inverted) range.
int totalFrames(double startTime, double endTime, double frameRate);

// Timestamp of frame `frameIndex`: startTime + frameIndex/frameRate, computed
// by multiplication — never by accumulating `t += 1/frameRate`, whose float
// drift made the loop produce N or N+1 frames depending on the range's
// absolute values (the frame-count "wobble"). Both exporters drive their
// loops as `for i in [0, totalFrames)` over this. A non-positive frameRate
// returns startTime (totalFrames already clamps that case to one frame).
double frameTime(double startTime, int frameIndex, double frameRate);

// True when a frame's dive time lies before the dive start (t < 0, e.g. an
// "imported video range" export whose clip starts before the dive). Such
// frames must show NO overlay — parity with the tab-2 preview, which hides
// overlays at negative dive time — so the export loops emit a fully
// transparent frame of the run's size instead of calling the generator
// (DiveData::dataAtTime clamps to sample 0, which would burn a frozen t=0
// overlay into the pre-dive footage). The frame is still emitted, never
// skipped: frame count and timing stay exact. Shared by ImageExporter's and
// VideoExporter's loops; FrameCompositor::composeFrame() applies the same
// `timePoint < 0.0` rule for composited exports — keep the three in sync.
bool isPreDiveTime(double diveTime);

// False for paths that are empty or whitespace-only. An empty path would make
// QDir resolve to the process working directory: frames would land there and
// a cancellation's cleanup would delete files from — and try to rmdir —
// whatever directory the app was launched in.
bool isValidExportPath(const QString &path);

// Frame file naming, shared by ImageExporter's frame writer and its
// cancellation cleanup. frameFileName(7) == "frame_000007.png". (The video
// exporter no longer consumes a matching FFmpeg input pattern — PR B2
// streams raw frames over stdin.)
QString frameFileName(int frameIndex);

// Replace characters that aren't allowed in file names with underscores and
// cap the length at 50 characters.
QString sanitizeFileName(const QString &fileName);

// "<date>_<dive name>_<location>_<video stem>_<content type>" (each optional
// part sanitized and skipped when empty) — the shared base name for export
// directories (PNG sequences) and output files (videos). Falls back to the
// current date/time when the dive has no valid start time.
QString exportBaseName(DiveData *dive,
                       const QString &videoFilePath = QString(),
                       const QString &contentType = QString());

// Cancellation cleanup: remove exactly the named files (the ones the current
// run recorded as written — never a name pattern, which could delete
// same-named files from an earlier export into the same directory), then the
// directory itself — but only via rmdir, which fails on a non-empty
// directory, so a pre-existing user-chosen directory holding other files is
// left alone.
void removeWrittenFiles(const QString &dirPath, const QStringList &fileNames);

// The last `maxLines` non-empty (after trimming) lines of a captured tool
// output tail, joined with '\n' — VideoExporter appends this to its
// user-facing FFmpeg error messages (the full 4000-char rolling tail would
// swamp a message dialog; FFmpeg's decisive lines come last). CR and CRLF
// line ends are accepted. Empty result when the tail holds no text.
QString outputTailExcerpt(const QString &tail, int maxLines = 3);

} // namespace ExportMath

#endif // EXPORT_MATH_H
