#include "include/core/color_grade.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace Unabara {

namespace {

inline double clamp01(double x)
{
    return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
}

inline double clampRange(double x, double lo, double hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

inline double luma(double r, double g, double b)
{
    return GradeTuning::kLumaR * r + GradeTuning::kLumaG * g + GradeTuning::kLumaB * b;
}

inline double lerp(double a, double b, double w)
{
    return a + (b - a) * w;
}

// Anchor roles derived by sorting on depth, never by storage order. Shared
// by resolveAtDepth() and rampWeightAt() so the readout weight can never
// drift from the blend. Requires anchors.size() >= 2.
void sortedRoles(const QVector<GradeAnchor>& anchors,
                 const GradeAnchor*& shallow, const GradeAnchor*& deep)
{
    shallow = &anchors.at(0);
    deep = &anchors.at(1);
    if (deep->depth < shallow->depth)
        std::swap(shallow, deep);
}

// Only placed anchors participate in the ramp (GradeAnchor::isPlaced —
// an unplaced, slider-created anchor must not act as a phantom 0 m
// shallow anchor while the UI shows it as "not set"). Shared by
// hasDepthRamp()/rampWeightAt()/resolveAtDepth().
QVector<GradeAnchor> placedAnchors(const QVector<GradeAnchor>& anchors)
{
    QVector<GradeAnchor> placed;
    placed.reserve(anchors.size());
    for (const GradeAnchor& a : anchors) {
        if (a.isPlaced())
            placed.append(a);
    }
    return placed;
}

// Luma-normalised Water Tint diagonal — see GradeTuning::kTintStrength.
void tintGains(double waterTint, double& tr, double& tg, double& tb)
{
    const double t = clampRange(waterTint, -1.0, 1.0);
    const double a = GradeTuning::kTintStrength;
    if (t >= 0.0) {
        // Toward magenta (counteracts green water): R and B up, G down.
        tr = 1.0 + a * t;
        tg = 1.0 - a * t;
        tb = 1.0 + a * t;
    } else {
        // Toward warm (counteracts blue water): R up, B down.
        const double u = -t;
        tr = 1.0 + a * u;
        tg = 1.0;
        tb = 1.0 - a * u;
    }
    const double y = luma(tr, tg, tb);
    tr /= y;
    tg /= y;
    tb /= y;
}

} // namespace

void GradeOps::apply(double& r, double& g, double& b) const
{
    // 1. 3×3 matrix, clamped like colorchannelmixer's 8-bit output.
    const double r0 = r, g0 = g, b0 = b;
    double v[3] = {
        matrix[0][0] * r0 + matrix[0][1] * g0 + matrix[0][2] * b0,
        matrix[1][0] * r0 + matrix[1][1] * g0 + matrix[1][2] * b0,
        matrix[2][0] * r0 + matrix[2][1] * g0 + matrix[2][2] * b0,
    };
    for (int i = 0; i < 3; ++i) {
        double x = clamp01(v[i]);
        // 2. Black point.
        if (black[i] > 0.0)
            x = clamp01((x - black[i]) / (1.0 - black[i]));
        // 3. Midtone lift x + g·4x²(1−x): zero slope at x=0 (shadows
        // untouched); the sum can slightly exceed 1 near x≈0.9, so clamp
        // before the S-curve (pow of a negative would be NaN).
        if (lift[i] != 0.0)
            x = clamp01(x + lift[i] * 4.0 * x * x * (1.0 - x));
        // 4. S-contrast about 0.5, maps [0,1]→[0,1] with no clipping.
        if (contrast != 1.0) {
            if (x < 0.5)
                x = 0.5 * std::pow(2.0 * x, contrast);
            else
                x = 1.0 - 0.5 * std::pow(2.0 * (1.0 - x), contrast);
        }
        v[i] = x;
    }
    r = v[0];
    g = v[1];
    b = v[2];
}

WbGains ColorGrade::resolveWbGains(const GradeParams& params)
{
    WbGains wb;
    if (!params.hasNeutralSample)
        return wb;

    const double sr = params.neutralSample[0];
    const double sg = params.neutralSample[1];
    const double sb = params.neutralSample[2];
    const double y = luma(sr, sg, sb);
    const double eps = GradeTuning::kWbEpsilon;

    wb.kG = clampRange(y / std::max(sg, eps), GradeTuning::kWbGainMin, GradeTuning::kWbGainMax);
    wb.kB = clampRange(y / std::max(sb, eps), GradeTuning::kWbGainMin, GradeTuning::kWbGainMax);

    const double kRraw = y / std::max(sr, eps);
    wb.kR = clampRange(kRraw, GradeTuning::kWbGainMin, GradeTuning::kWbGainMax);
    // Empty-red-channel case: the gain the clamp refused is re-expressed as
    // green→red borrowing (research: 20× noise gain otherwise).
    if (kRraw > GradeTuning::kWbGainMax) {
        wb.borrow = clampRange((y - GradeTuning::kWbGainMax * sr) / std::max(sg, eps),
                               0.0, GradeTuning::kWbBorrowMax);
    }
    return wb;
}

GradeOps ColorGrade::resolveWithWb(const GradeParams& params, const WbGains& wb)
{
    GradeOps ops;

    const double s = clamp01(params.redRecovery);
    const double c = clamp01(params.clarity);

    // M_red: R' = (1−k)·R + k·G.
    const double k = GradeTuning::kRedBorrow * s;
    // M_tint: luma-normalised diagonal.
    double tr, tg, tb;
    tintGains(params.waterTint, tr, tg, tb);

    // M = M_red · M_tint · M_wb, with
    //   M_wb = [[kR, borrow, 0], [0, kG, 0], [0, 0, kB]]  (applied first).
    // M_tint·M_wb = [[tr·kR, tr·borrow, 0], [0, tg·kG, 0], [0, 0, tb·kB]].
    ops.matrix[0][0] = (1.0 - k) * tr * wb.kR;
    ops.matrix[0][1] = (1.0 - k) * tr * wb.borrow + k * tg * wb.kG;
    ops.matrix[0][2] = 0.0;
    ops.matrix[1][0] = 0.0;
    ops.matrix[1][1] = tg * wb.kG;
    ops.matrix[1][2] = 0.0;
    ops.matrix[2][0] = 0.0;
    ops.matrix[2][1] = 0.0;
    ops.matrix[2][2] = tb * wb.kB;

    // Validate against colorchannelmixer's [-2,2]: clamp + report.
    const double bound = GradeTuning::kMatrixBound;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (ops.matrix[i][j] < -bound || ops.matrix[i][j] > bound) {
                ops.matrix[i][j] = clampRange(ops.matrix[i][j], -bound, bound);
                ops.matrixClamped = true;
            }
        }
    }
    if (ops.matrixClamped) {
        // Callers see ops.matrixClamped; the log line is only a breadcrumb.
        // Warn once per process: the C3 depth-linked preview resolves per
        // video tick and plan() resolves twice per second of export — an
        // extreme grade would otherwise log hundreds of identical lines.
        static const bool warnedOnce = []() {
            qWarning() << "ColorGrade: matrix entry outside colorchannelmixer's"
                       << "[-2,2] range, clamped (extreme tint/white-balance"
                       << "combination; further occurrences not logged)";
            return true;
        }();
        Q_UNUSED(warnedOnce)
    }

    ops.black[0] = ops.black[1] = ops.black[2] = GradeTuning::kClarityBlack * c;
    ops.lift[0] = GradeTuning::kRedMidtoneLift * s;
    ops.lift[1] = ops.lift[2] = 0.0;
    ops.contrast = 1.0 + GradeTuning::kClarityContrast * c;
    return ops;
}

GradeOps ColorGrade::resolve(const GradeParams& params)
{
    return resolveWithWb(params, resolveWbGains(params));
}

GradeOps ColorGrade::resolveStatic() const
{
    if (anchors.isEmpty())
        return GradeOps();
    return resolve(anchors.first().params);
}

bool ColorGrade::hasDepthRamp() const
{
    const QVector<GradeAnchor> placed = placedAnchors(anchors);
    if (placed.size() < 2)
        return false;
    return std::abs(placed.at(0).depth - placed.at(1).depth) >= GradeTuning::kDepthStaticEps;
}

double ColorGrade::rampWeightAt(double depthMeters) const
{
    if (!hasDepthRamp())
        return -1.0;
    const QVector<GradeAnchor> placed = placedAnchors(anchors);
    const GradeAnchor* shallow = nullptr;
    const GradeAnchor* deep = nullptr;
    sortedRoles(placed, shallow, deep);
    return clamp01((depthMeters - shallow->depth) / (deep->depth - shallow->depth));
}

GradeOps ColorGrade::resolveAtDepth(double depthMeters) const
{
    if (anchors.isEmpty())
        return GradeOps();
    const QVector<GradeAnchor> placed = placedAnchors(anchors);
    // Fewer than two PLACED anchors → static. One placed anchor wins
    // (the ramp will exist once the other is bound); none placed keeps
    // the first (edited) anchor so pre-Set grading still previews.
    if (placed.size() < 2)
        return resolve(placed.isEmpty() ? anchors.first().params
                                        : placed.first().params);

    const GradeAnchor* shallow = nullptr;
    const GradeAnchor* deep = nullptr;
    sortedRoles(placed, shallow, deep);

    // Degenerate span → static grade from the shallow anchor.
    if (deep->depth - shallow->depth < GradeTuning::kDepthStaticEps)
        return resolve(shallow->params);

    const double w = rampWeightAt(depthMeters);

    // Parameter-wise linear interpolation of the sliders...
    GradeParams blended;
    blended.redRecovery = lerp(shallow->params.redRecovery, deep->params.redRecovery, w);
    blended.waterTint = lerp(shallow->params.waterTint, deep->params.waterTint, w);
    blended.clarity = lerp(shallow->params.clarity, deep->params.clarity, w);

    // ...plus the RESOLVED white-balance gain triple (interpolating the raw
    // neutral samples instead would bend through unintended hues).
    const WbGains wbS = resolveWbGains(shallow->params);
    const WbGains wbD = resolveWbGains(deep->params);
    WbGains wb;
    wb.kR = lerp(wbS.kR, wbD.kR, w);
    wb.kG = lerp(wbS.kG, wbD.kG, w);
    wb.kB = lerp(wbS.kB, wbD.kB, w);
    wb.borrow = lerp(wbS.borrow, wbD.borrow, w);

    return resolveWithWb(blended, wb);
}

QJsonObject ColorGrade::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("version")] = GradeTuning::kGradeJsonVersion;
    obj[QStringLiteral("enabled")] = enabled;
    obj[QStringLiteral("depthLinked")] = depthLinked;
    if (!lutPath.isEmpty())
        obj[QStringLiteral("lutPath")] = lutPath;

    QJsonArray anchorArray;
    for (const GradeAnchor& anchor : anchors) {
        QJsonObject a;
        a[QStringLiteral("redRecovery")] = anchor.params.redRecovery;
        a[QStringLiteral("waterTint")] = anchor.params.waterTint;
        a[QStringLiteral("clarity")] = anchor.params.clarity;
        a[QStringLiteral("depth")] = anchor.depth;
        // Presence-tracked: only written when set (absent ≠ default).
        if (anchor.videoTime >= 0.0)
            a[QStringLiteral("videoTime")] = anchor.videoTime;
        // Presence-tracked, absent = placed: pre-flag grades and
        // externally-authored anchors are real. Only a slider-created,
        // never-Set anchor persists as unplaced.
        if (!anchor.placed)
            a[QStringLiteral("placed")] = false;
        if (anchor.params.hasNeutralSample) {
            QJsonArray sample;
            sample.append(anchor.params.neutralSample[0]);
            sample.append(anchor.params.neutralSample[1]);
            sample.append(anchor.params.neutralSample[2]);
            a[QStringLiteral("neutralSample")] = sample;
        }
        anchorArray.append(a);
    }
    obj[QStringLiteral("anchors")] = anchorArray;
    return obj;
}

ColorGrade ColorGrade::fromJson(const QJsonObject& json, bool* ok)
{
    ColorGrade grade;
    if (ok)
        *ok = false;
    if (json.isEmpty())
        return grade;

    // Missing version = version 1 (the first shipped schema). A newer
    // version is tolerated with a warning: known fields load, unknown
    // fields are ignored.
    const int version = json.value(QStringLiteral("version")).toInt(GradeTuning::kGradeJsonVersion);
    if (version > GradeTuning::kGradeJsonVersion) {
        qWarning() << "ColorGrade: grade was saved by a newer Unabara (schema version"
                   << version << ">" << GradeTuning::kGradeJsonVersion
                   << ") — loading the fields this version knows";
    }

    grade.enabled = json.value(QStringLiteral("enabled")).toBool(false);
    grade.depthLinked = json.value(QStringLiteral("depthLinked")).toBool(false);
    // Presence-tracked: absent key = no LUT, never a default path.
    grade.lutPath = json.value(QStringLiteral("lutPath")).toString();

    const QJsonArray anchorArray = json.value(QStringLiteral("anchors")).toArray();
    for (const QJsonValue& value : anchorArray) {
        if (grade.anchors.size() >= 2) {
            qWarning() << "ColorGrade: more than 2 anchors in saved grade, extra ignored";
            break;
        }
        const QJsonObject a = value.toObject();
        GradeAnchor anchor;
        anchor.params.redRecovery = a.value(QStringLiteral("redRecovery")).toDouble(0.0);
        anchor.params.waterTint = a.value(QStringLiteral("waterTint")).toDouble(0.0);
        anchor.params.clarity = a.value(QStringLiteral("clarity")).toDouble(0.0);
        anchor.depth = a.value(QStringLiteral("depth")).toDouble(0.0);
        // Presence-tracked: absent videoTime stays -1 (unset).
        anchor.videoTime = a.value(QStringLiteral("videoTime")).toDouble(-1.0);
        anchor.placed = a.value(QStringLiteral("placed")).toBool(true);
        const QJsonValue sampleValue = a.value(QStringLiteral("neutralSample"));
        if (sampleValue.isArray()) {
            const QJsonArray sample = sampleValue.toArray();
            if (sample.size() == 3) {
                anchor.params.setNeutralSample(sample.at(0).toDouble(),
                                               sample.at(1).toDouble(),
                                               sample.at(2).toDouble());
            }
        }
        grade.anchors.append(anchor);
    }

    if (ok)
        *ok = true;
    return grade;
}

bool parsePreset(const QByteArray& json, GradePreset* preset, QString* error)
{
    Q_ASSERT(preset);
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (doc.isNull()) {
        if (error)
            *error = QStringLiteral("Not valid JSON: %1").arg(parseError.errorString());
        return false;
    }
    if (!doc.isObject()) {
        if (error)
            *error = QStringLiteral("Preset root must be a JSON object");
        return false;
    }
    const QJsonObject obj = doc.object();
    const QString name = obj.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
        if (error)
            *error = QStringLiteral("Preset has no name");
        return false;
    }
    preset->name = name;
    preset->description = obj.value(QStringLiteral("description")).toString();
    preset->redRecovery =
        qBound(0.0, obj.value(QStringLiteral("redRecovery")).toDouble(0.0), 1.0);
    preset->waterTint =
        qBound(-1.0, obj.value(QStringLiteral("waterTint")).toDouble(0.0), 1.0);
    preset->clarity = qBound(0.0, obj.value(QStringLiteral("clarity")).toDouble(0.0), 1.0);
    return true;
}

} // namespace Unabara
