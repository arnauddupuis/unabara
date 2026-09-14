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

// False for paths that are empty or whitespace-only. An empty path would make
// QDir resolve to the process working directory: frames would land there and
// a cancellation's cleanup would delete files from — and try to rmdir —
// whatever directory the app was launched in.
bool isValidExportPath(const QString &path);

// Frame file naming, shared by the frame writers, the cancellation cleanup
// and FFmpeg's input pattern. frameFileName(7) == "frame_000007.png".
QString frameFileName(int frameIndex);

// printf-style pattern matching frameFileName(), passed to FFmpeg as the
// image-sequence input.
QString framePattern();

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

} // namespace ExportMath

#endif // EXPORT_MATH_H
