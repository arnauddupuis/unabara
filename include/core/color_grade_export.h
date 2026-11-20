#ifndef COLOR_GRADE_EXPORT_H
#define COLOR_GRADE_EXPORT_H

#include <QString>
#include <QStringList>

#include "include/core/color_grade.h"

class DiveData;

namespace Unabara {

// Turns a ColorGrade into the FFmpeg filter snippet the composite export
// (track B) inserts between `format=rgb24` and `overlay` — the C-side of
// the `setColorFilterChain()` contract.
//
// Static grade  → a 33³ .cube baked from GradeOps::apply() (user LUT
//                 composed in) written into workDir, referenced as
//                 lut3d=file='…':interp=tetrahedral. Parity ≤ 1 LSB.
// Depth-linked  → sendcmd=f='<workDir>/unabara_grade.cmd',
//                 colorchannelmixer@cc=…,lutrgb@cv=r='…':g='…':b='…'
//                 [,lut3d=<user LUT>], with the command file sampled at
//                 0.5 s cadence over [in,out] and deduplicated. The lutrgb
//                 expressions implement black point, midtone lift and the
//                 S-curve; lutrgb TRUNCATES its result (verified on FFmpeg
//                 7.1.5), so every expression ends in `+0.5`. colorlevels
//                 is NOT used — it quantises the black point.
//
// Frame pts entering the snippet start at 0 at the in-point, so the dive
// time of command timestamp t is clipOffset + in + t.
class ColorGradeExport
{
public:
    struct Plan {
        // The verbatim filtergraph insert; empty = no grading (disabled
        // grade or no anchors). Never includes the surrounding format
        // conversions — those belong to track B's graph.
        QString filterSnippet;
        // Files written into workDir that must outlive the FFmpeg run
        // (baked .cube, sendcmd command file).
        QStringList tempFiles;
        // True only when the grade is depth-linked AND the emitted
        // commands actually vary over [in,out].
        bool timeVarying = false;
        // Fatal: snippet unusable (e.g. workDir not writable).
        QString error;
        // Non-fatal advisories (e.g. a missing user LUT: the grade still
        // applies without it; the UI shows these as warning rows).
        QStringList notes;

        bool isValid() const { return error.isEmpty(); }
    };

    // Build the export plan. `in`/`out` are clip-local seconds of the
    // export range, `clipOffset` is the clip's syncOffset (dive time at
    // clip t=0). `dive` may be null: no dive (or depthLinked=false) takes
    // the static path. workDir must exist and be writable.
    static Plan plan(const ColorGrade& grade, const DiveData* dive,
                     double clipOffset, double in, double out,
                     const QString& workDir);

    // THE depth-path decision: true when plan() will take the sendcmd
    // (time-varying) route rather than baking a static cube. Shared by
    // plan() and the export dialog's "applied (depth-linked)" row so the
    // two can never drift (extraction guardrail).
    static bool usesDepthPath(const ColorGrade& grade, const DiveData* dive);

    // Escape a value for use as an FFmpeg filter OPTION inside a
    // filtergraph string: the documented two-level escaping (option level
    // escapes \ ' : — then graph level escapes \ ' , ; [ ]). Correct for
    // Windows paths ("C:\…" with backslashes), quotes, commas and spaces;
    // verified against FFmpeg 7.1.5. Reusable for any path-valued option.
    static QString escapeFilterOption(const QString& value);

    // Escape a command argument for a sendcmd FILE: commands are split on
    // unescaped `,` and intervals on `;`, so \ , ; get backslash-escaped.
    static QString escapeSendcmdArg(const QString& value);

    // The lutrgb expression (input `val` in [0,255]) implementing
    // black point → midtone lift → S-curve exactly as GradeOps::apply(),
    // ending in `+0.5` because lutrgb truncates. Exposed for tests.
    static QString lutrgbExpression(double black, double lift, double contrast);

private:
    ColorGradeExport() = delete;
};

} // namespace Unabara

#endif // COLOR_GRADE_EXPORT_H
