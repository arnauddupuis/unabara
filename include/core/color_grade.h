#ifndef COLOR_GRADE_H
#define COLOR_GRADE_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

// Unabara's parametric underwater colour grade (v0.4 track C).
//
// Stored state is diver-semantic: three sliders plus an optional eyedropper
// neutral sample per ANCHOR, and whole-grade flags (enabled, depthLinked,
// 1..2 anchors, optional user LUT path). resolve() turns one anchor's
// parameters into concrete per-pixel operations (GradeOps); the depth link
// interpolates between the two anchors' parameters (and their *resolved*
// white-balance gain triples) by the current depth.
//
// GradeOps::apply() is THE ground truth of the whole colour pipeline — see
// its comment. The JSON schema (toJson()/fromJson(), version 1) is owned
// here; VideoClip::colorGrade round-trips it opaquely.

namespace Unabara {

// ---------------------------------------------------------------------------
// GradeTuning: every slider→operation mapping constant, in one place.
//
// FROZEN AT 0.4.0. These constants define the look of every saved grade
// (.ucp presets, per-clip grades in Config): changing any of them silently
// regrades existing footage. Any change requires bumping kGradeJsonVersion
// and migrating or versioning saved grades.
// ---------------------------------------------------------------------------
namespace GradeTuning {

// Red Recovery (slider s in [0,1]):
//   green→red borrow k = kRedBorrow·s, applied as matrix row
//   R' = (1−k)·R + k·G  (research's canonical rr=0.5:rg=0.5 at s=1),
//   plus a red midtone lift g_r = kRedMidtoneLift·s (see GradeOps::lift).
inline constexpr double kRedBorrow = 0.5;
inline constexpr double kRedMidtoneLift = 0.35;

// Water Tint (slider t in [-1,+1], signed):
//   t > 0 pushes toward MAGENTA (counteracts green water): raw diagonal
//          (1 + a·t, 1 − a·t, 1 + a·t);
//   t < 0 pushes toward WARM (counteracts blue water): raw diagonal
//          (1 + a·|t|, 1, 1 − a·|t|);
// with a = kTintStrength, then luma-normalised (divided by the Rec.709 luma
// of the raw gains) so the push never changes overall brightness.
inline constexpr double kTintStrength = 0.25;

// Clarity (slider c in [0,1]): black point kClarityBlack·c and S-contrast
// exponent 1 + kClarityContrast·c (see GradeOps).
inline constexpr double kClarityBlack = 0.12;
inline constexpr double kClarityContrast = 0.6;

// Rec.709 luma weights, used by the tint normalisation and the eyedropper.
inline constexpr double kLumaR = 0.2126;
inline constexpr double kLumaG = 0.7152;
inline constexpr double kLumaB = 0.0722;

// Eyedropper white balance: per-channel gains k = Y/channel clamped to
// [kWbGainMin, kWbGainMax]. A red gain that would exceed the clamp (empty
// red channel — Y/R explodes into 20× noise gain) is expressed instead as
// green→red borrowing clamp((Y − kWbGainMax·R)/G, 0, kWbBorrowMax).
inline constexpr double kWbGainMin = 0.5;
inline constexpr double kWbGainMax = 2.0;
inline constexpr double kWbBorrowMax = 0.8;
inline constexpr double kWbEpsilon = 1e-4;

// FFmpeg colorchannelmixer accepts coefficients in [-2,2] only; resolve()
// clamps the composed matrix into this bound (and flags it).
inline constexpr double kMatrixBound = 2.0;

// Anchors closer than this (metres) degenerate to a static grade (the
// shallow anchor wins).
inline constexpr double kDepthStaticEps = 0.5;

// toJson()/fromJson() schema version.
inline constexpr int kGradeJsonVersion = 1;

} // namespace GradeTuning

// One anchor's stored parameters.
struct GradeParams {
    double redRecovery = 0.0; // [0,1]
    double waterTint = 0.0;   // [-1,+1]; + = toward magenta, − = toward warm
    double clarity = 0.0;     // [0,1]

    // Eyedropper neutral sample (C4 fills it; the math lives here).
    // Presence-tracked: hasNeutralSample=false means "never sampled", which
    // is different from a sampled mid-gray. Values are normalized [0,1] RGB.
    bool hasNeutralSample = false;
    double neutralSample[3] = {0.0, 0.0, 0.0};

    void setNeutralSample(double r, double g, double b)
    {
        neutralSample[0] = r;
        neutralSample[1] = g;
        neutralSample[2] = b;
        hasNeutralSample = true;
    }
    void clearNeutralSample()
    {
        neutralSample[0] = neutralSample[1] = neutralSample[2] = 0.0;
        hasNeutralSample = false;
    }
};

// Resolved white-balance gains (the "WB gain triple" the depth link
// interpolates — never the raw neutral sample).
struct WbGains {
    double kR = 1.0;
    double kG = 1.0;
    double kB = 1.0;
    double borrow = 0.0; // green→red borrow replacing red gain beyond the clamp
};

// Concrete per-pixel operations resolved from one parameter set.
struct GradeOps {
    // Row-major 3×3: out_r = m[0][0]·r + m[0][1]·g + m[0][2]·b, etc.
    // Composed as M = M_red · M_tint · M_wb (white balance applied first).
    double matrix[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    double black[3] = {0, 0, 0};    // per-channel black point
    double lift[3] = {0, 0, 0};     // per-channel midtone lift g
    double contrast = 1.0;          // S-curve exponent about 0.5, >= 1
    bool matrixClamped = false;     // true when entries hit ±kMatrixBound

    // GROUND TRUTH of the entire colour pipeline. The QML shader (C2), the
    // LUT baker (CubeLut::bake), the FFmpeg filter strings
    // (ColorGradeExport) and every parity test must reproduce exactly this
    // function in their own precision — fix behaviour HERE, then mirror it.
    //
    // Inputs/outputs are double precision on [0,1]. Fixed op order:
    //   1. matrix, output clamped to [0,1] (colorchannelmixer clamps too)
    //   2. black point: x = clamp((x − b)/(1 − b), 0, 1)
    //   3. midtone lift: x = x + g·4x²(1−x), clamped to [0,1] — the zero
    //      slope at x=0 is the "never multiply red in the shadows"
    //      invariant: shadow noise is untouched by Red Recovery
    //   4. S-contrast about 0.5 (exponent c, maps [0,1]→[0,1], no clipping):
    //      x < 0.5 → 0.5·(2x)^c, else 1 − 0.5·(2(1−x))^c
    // (A user LUT, when present, is applied AFTER these ops, outside
    // GradeOps — see CubeLut::bake and ColorGradeExport.)
    void apply(double& r, double& g, double& b) const;
};

// One depth anchor: parameters + where it was set.
struct GradeAnchor {
    GradeParams params;
    double depth = 0.0;      // metres
    double videoTime = -1.0; // clip-local seconds the anchor was set at; -1 = unset

    // An anchor "participates" in the depth ramp only once it has been
    // placed. Explicit flag, not a depth/videoTime heuristic: a legit
    // surface anchor at exactly 0.0 m with no bound frame must stay in
    // the ramp (the ffmpeg parity fixture has one), while a freshly
    // slider-created anchor must NOT act as a phantom 0 m shallow anchor
    // — the UI shows it as "not set" and the blend must agree. Defaults
    // to true (C++-authored and pre-flag persisted anchors are real);
    // ONLY the controller's ensureSelectedAnchor() creates unplaced
    // anchors, and "Set from current frame" places them. THE single
    // source — the controller's "not set" display reads it too.
    bool placed = true;
    bool isPlaced() const { return placed; }
};

class ColorGrade
{
public:
    bool enabled = false;
    bool depthLinked = false;
    QVector<GradeAnchor> anchors; // 1..2; roles derived by sorting on depth
    QString lutPath;              // absolute path to a user .cube; empty = none

    // --- Resolution ---
    static WbGains resolveWbGains(const GradeParams& params);
    // Full resolve of one parameter set (derives WB gains from the params'
    // neutral sample, identity when absent).
    static GradeOps resolve(const GradeParams& params);
    // Resolve with explicit WB gains (used by the depth link, which
    // interpolates the gains — params' own neutral sample is ignored).
    static GradeOps resolveWithWb(const GradeParams& params, const WbGains& wb);

    // Static (non-depth-linked) ops: the first anchor, identity when none.
    GradeOps resolveStatic() const;

    // Depth-linked ops at a given depth (metres). Anchor roles come from
    // sorting on depth; interpolation is parameter-wise linear (sliders)
    // plus linear on the resolved WB gain triples, weight clamped to [0,1]
    // outside the anchor span. Single anchor, or anchors closer than
    // GradeTuning::kDepthStaticEps, degrade to the shallow anchor (static).
    GradeOps resolveAtDepth(double depthMeters) const;

    // True when the grade has two anchors far enough apart for the depth
    // ramp to exist at all.
    bool hasDepthRamp() const;

    // Interpolation weight toward the DEEP anchor at depthMeters, clamped
    // to [0,1] — the exact weight resolveAtDepth() blends with (C3's
    // "N % toward Deep" readout must never disagree with the resolve).
    // Returns -1.0 when hasDepthRamp() is false.
    double rampWeightAt(double depthMeters) const;

    // --- Persistence (schema version 1, owned here; VideoClip round-trips
    // the object opaquely). Presence-tracked keys follow the
    // OverlayTemplate precedent: neutralSample/videoTime/lutPath are only
    // written when set, and absent on read means "not set" — never a
    // default value. A newer version than kGradeJsonVersion is tolerated
    // with a warning (known fields load, unknown ones are ignored).
    QJsonObject toJson() const;
    static ColorGrade fromJson(const QJsonObject& json, bool* ok = nullptr);
};

// A .ucp preset: name, description and the three slider values only (no
// neutral sample, LUT or anchors — presets are water-type looks, the depth
// comes from telemetry).
struct GradePreset {
    QString name;
    QString description;
    double redRecovery = 0.0;
    double waterTint = 0.0;
    double clarity = 0.0;
};

// Parse a .ucp preset file's bytes. Returns false (with *error set) on
// malformed JSON, a non-object root, or a missing/empty name; slider values
// are clamped into their documented ranges.
bool parsePreset(const QByteArray& json, GradePreset* preset, QString* error = nullptr);

} // namespace Unabara

#endif // COLOR_GRADE_H
