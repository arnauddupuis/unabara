// Tests for the parametric colour grade model (include/core/color_grade.h):
// the GradeTuning slider→ops mappings (expected numbers computed by hand in
// comments), the fixed op order of GradeOps::apply() (the "never multiply
// red in the shadows" invariant), matrix bounds validation, eyedropper
// white-balance gain clamps (incl. the empty-red-channel explosion), the
// two-anchor depth interpolation, JSON round-trips with presence-tracked
// keys, and .ucp preset parsing.

#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>

#include "include/core/color_grade.h"

using namespace Unabara;

class ColorGradeTest : public QObject
{
    Q_OBJECT

private slots:
    void tuning_redRecovery();
    void tuning_waterTint_magenta();
    void tuning_waterTint_warm();
    void tuning_clarity();
    void opOrder_blackStaysBlack();
    void apply_monotone();
    void apply_noClipping();
    void matrix_boundsClamped();
    void wbGains_neutralGray();
    void wbGains_greenCast();
    void wbGains_emptyRedChannel();
    void depth_interpolationLinear();
    void depth_clampedOutsideAnchors();
    void depth_closeAnchorsAreStatic();
    void depth_singleAnchorIsStatic();
    void depth_unplacedAnchorSitsOutOfRamp();
    void depth_wbTripleInterpolated();
    void json_roundTrip();
    void json_presenceTrackedKeys();
    void json_versionHandling();
    void preset_bundledFilesParse();
    void preset_errors();
};

void ColorGradeTest::tuning_redRecovery()
{
    // s = 1: borrow k = 0.5·1 = 0.5 → R' = 0.5R + 0.5G (the research's
    // canonical rr=0.5:rg=0.5); red midtone lift g = 0.35·1 = 0.35.
    GradeParams p;
    p.redRecovery = 1.0;
    GradeOps ops = ColorGrade::resolve(p);
    QCOMPARE(ops.matrix[0][0], 0.5);
    QCOMPARE(ops.matrix[0][1], 0.5);
    QCOMPARE(ops.matrix[0][2], 0.0);
    QCOMPARE(ops.matrix[1][1], 1.0);
    QCOMPARE(ops.matrix[2][2], 1.0);
    QCOMPARE(ops.lift[0], 0.35);
    QCOMPARE(ops.lift[1], 0.0);
    QCOMPARE(ops.lift[2], 0.0);

    // s = 0.4: k = 0.2 → [0.8, 0.2, 0]; lift 0.35·0.4 = 0.14.
    p.redRecovery = 0.4;
    ops = ColorGrade::resolve(p);
    QVERIFY(qAbs(ops.matrix[0][0] - 0.8) < 1e-12);
    QVERIFY(qAbs(ops.matrix[0][1] - 0.2) < 1e-12);
    QVERIFY(qAbs(ops.lift[0] - 0.14) < 1e-12);
}

void ColorGradeTest::tuning_waterTint_magenta()
{
    // t = +1 (full magenta push), a = 0.25: raw diagonal (1.25, 0.75, 1.25).
    // Y = 0.2126·1.25 + 0.7152·0.75 + 0.0722·1.25
    //   = 0.26575 + 0.5364 + 0.09025 = 0.8924.
    // Normalised: gR = gB = 1.25/0.8924 = 1.400717…, gG = 0.75/0.8924
    //   = 0.840430…
    GradeParams p;
    p.waterTint = 1.0;
    const GradeOps ops = ColorGrade::resolve(p);
    QVERIFY(qAbs(ops.matrix[0][0] - 1.400717) < 1e-5);
    QVERIFY(qAbs(ops.matrix[1][1] - 0.840430) < 1e-5);
    QVERIFY(qAbs(ops.matrix[2][2] - 1.400717) < 1e-5);
    // The whole point of the luma normalisation: Rec.709 luma of the
    // diagonal gains stays exactly 1 — the tint never changes brightness.
    const double y = GradeTuning::kLumaR * ops.matrix[0][0]
                     + GradeTuning::kLumaG * ops.matrix[1][1]
                     + GradeTuning::kLumaB * ops.matrix[2][2];
    QVERIFY(qAbs(y - 1.0) < 1e-12);
}

void ColorGradeTest::tuning_waterTint_warm()
{
    // t = −1 (full warm push): raw diagonal (1.25, 1, 0.75).
    // Y = 0.26575 + 0.7152 + 0.05415 = 1.0351.
    // gR = 1.25/1.0351 = 1.207613…, gG = 1/1.0351 = 0.966090…,
    // gB = 0.75/1.0351 = 0.724568…
    GradeParams p;
    p.waterTint = -1.0;
    const GradeOps ops = ColorGrade::resolve(p);
    QVERIFY(qAbs(ops.matrix[0][0] - 1.207613) < 1e-5);
    QVERIFY(qAbs(ops.matrix[1][1] - 0.966090) < 1e-5);
    QVERIFY(qAbs(ops.matrix[2][2] - 0.724568) < 1e-5);
    QVERIFY(ops.matrix[0][0] > 1.0);
    QVERIFY(ops.matrix[2][2] < 1.0);
    const double y = GradeTuning::kLumaR * ops.matrix[0][0]
                     + GradeTuning::kLumaG * ops.matrix[1][1]
                     + GradeTuning::kLumaB * ops.matrix[2][2];
    QVERIFY(qAbs(y - 1.0) < 1e-12);
}

void ColorGradeTest::tuning_clarity()
{
    // c = 1: black point 0.12·1 = 0.12, contrast 1 + 0.6·1 = 1.6.
    GradeParams p;
    p.clarity = 1.0;
    GradeOps ops = ColorGrade::resolve(p);
    QCOMPARE(ops.black[0], 0.12);
    QCOMPARE(ops.black[1], 0.12);
    QCOMPARE(ops.black[2], 0.12);
    QCOMPARE(ops.contrast, 1.6);

    // c = 0.5: 0.06 / 1.3.
    p.clarity = 0.5;
    ops = ColorGrade::resolve(p);
    QCOMPARE(ops.black[0], 0.06);
    QCOMPARE(ops.contrast, 1.3);
}

void ColorGradeTest::opOrder_blackStaysBlack()
{
    // Op order is matrix → black → lift → S-curve. With full red recovery
    // (red lift 0.35) and full clarity (black point 0.12):
    //  - pure black must stay exactly black, with zero red lift (the
    //    zero-slope-at-black lift never touches 0), and
    //  - a gray exactly at the black point must come out exactly 0 in all
    //    channels. If the lift ran BEFORE the black point it would push
    //    0.12 above the threshold and leak red into the shadows — the
    //    "never multiply red in the shadows" invariant this test bites on.
    GradeParams p;
    p.redRecovery = 1.0;
    p.clarity = 1.0;
    const GradeOps ops = ColorGrade::resolve(p);

    double r = 0.0, g = 0.0, b = 0.0;
    ops.apply(r, g, b);
    QCOMPARE(r, 0.0);
    QCOMPARE(g, 0.0);
    QCOMPARE(b, 0.0);

    // Gray at the black point: the matrix preserves it (R' = 0.5·0.12 +
    // 0.5·0.12 = 0.12), the black point maps it to exactly 0, and lift and
    // S-curve both fix 0.
    r = 0.12;
    g = 0.12;
    b = 0.12;
    ops.apply(r, g, b);
    QCOMPARE(r, 0.0);
    QCOMPARE(g, 0.0);
    QCOMPARE(b, 0.0);
}

void ColorGradeTest::apply_monotone()
{
    GradeParams p;
    p.redRecovery = 0.7;
    p.waterTint = 0.4;
    p.clarity = 0.8;
    const GradeOps ops = ColorGrade::resolve(p);

    double prevR = -1.0, prevG = -1.0, prevB = -1.0;
    for (int i = 0; i <= 255; ++i) {
        double r = i / 255.0, g = r, b = r; // gray ramp
        ops.apply(r, g, b);
        QVERIFY(r >= prevR);
        QVERIFY(g >= prevG);
        QVERIFY(b >= prevB);
        prevR = r;
        prevG = g;
        prevB = b;
    }
}

void ColorGradeTest::apply_noClipping()
{
    // The S-curve maps [0,1]→[0,1] with no clipping, and every stage keeps
    // the range — including inputs the matrix pushes above 1.
    GradeParams p;
    p.redRecovery = 1.0;
    p.waterTint = -1.0;
    p.clarity = 1.0;
    const GradeOps ops = ColorGrade::resolve(p);

    const int steps = 10;
    for (int ri = 0; ri <= steps; ++ri) {
        for (int gi = 0; gi <= steps; ++gi) {
            for (int bi = 0; bi <= steps; ++bi) {
                double r = double(ri) / steps;
                double g = double(gi) / steps;
                double b = double(bi) / steps;
                ops.apply(r, g, b);
                QVERIFY(r >= 0.0 && r <= 1.0);
                QVERIFY(g >= 0.0 && g <= 1.0);
                QVERIFY(b >= 0.0 && b <= 1.0);
            }
        }
    }

    // White: the warm tint legitimately dims green/blue (its matrix rows
    // are not sum-1), but red's gain is > 1, clamps to 1 after the matrix
    // and every later stage fixes 1 — so red must come out exactly 1 and
    // nothing may exceed 1.
    double r = 1.0, g = 1.0, b = 1.0;
    ops.apply(r, g, b);
    QCOMPARE(r, 1.0);
    QVERIFY(g <= 1.0 && g >= 0.0);
    QVERIFY(b <= 1.0 && b >= 0.0);
}

void ColorGradeTest::matrix_boundsClamped()
{
    // Full magenta tint (gB = 1.400717) on a nearly blue-less sample:
    // Y = 0.2126·0.5 + 0.7152·0.5 + 0.0722·0.05 = 0.46751, so the blue WB
    // gain Y/0.05 = 9.35 clamps to 2 — but tint·WB = 1.400717·2 = 2.8014
    // still exceeds colorchannelmixer's [-2,2] and must clamp + report.
    GradeParams p;
    p.waterTint = 1.0;
    p.setNeutralSample(0.5, 0.5, 0.05);
    const GradeOps ops = ColorGrade::resolve(p);
    QCOMPARE(ops.matrix[2][2], 2.0);
    QVERIFY(ops.matrixClamped);

    // A plain slider-only grade never clamps.
    GradeParams q;
    q.redRecovery = 1.0;
    q.waterTint = 1.0;
    q.clarity = 1.0;
    QVERIFY(!ColorGrade::resolve(q).matrixClamped);
}

void ColorGradeTest::wbGains_neutralGray()
{
    GradeParams p;
    p.setNeutralSample(0.5, 0.5, 0.5);
    const WbGains wb = ColorGrade::resolveWbGains(p);
    QVERIFY(qAbs(wb.kR - 1.0) < 1e-9);
    QVERIFY(qAbs(wb.kG - 1.0) < 1e-9);
    QVERIFY(qAbs(wb.kB - 1.0) < 1e-9);
    QCOMPARE(wb.borrow, 0.0);

    // No sample at all → identity gains.
    const WbGains none = ColorGrade::resolveWbGains(GradeParams());
    QCOMPARE(none.kR, 1.0);
    QCOMPARE(none.kG, 1.0);
    QCOMPARE(none.kB, 1.0);
    QCOMPARE(none.borrow, 0.0);
}

void ColorGradeTest::wbGains_greenCast()
{
    // Sample (0.2, 0.6, 0.3): Y = 0.04252 + 0.42912 + 0.02166 = 0.4933.
    // kG = 0.4933/0.6 = 0.822167, kB = 0.4933/0.3 = 1.644333.
    // kR raw = 0.4933/0.2 = 2.4665 → clamped to 2, remainder as borrow
    // (0.4933 − 2·0.2)/0.6 = 0.0933/0.6 = 0.1555.
    GradeParams p;
    p.setNeutralSample(0.2, 0.6, 0.3);
    const WbGains wb = ColorGrade::resolveWbGains(p);
    QVERIFY(qAbs(wb.kG - 0.822167) < 1e-5);
    QVERIFY(qAbs(wb.kB - 1.644333) < 1e-5);
    QCOMPARE(wb.kR, 2.0);
    QVERIFY(qAbs(wb.borrow - 0.155500) < 1e-5);
}

void ColorGradeTest::wbGains_emptyRedChannel()
{
    // The research-doc failure mode: an empty red channel makes Y/R explode
    // (20× noise gain). Sample (0, 0.1, 0.9): Y = 0.07152 + 0.06498
    // = 0.1365. kR clamps at 2; the borrow (0.1365 − 0)/0.1 = 1.365 clamps
    // at 0.8; kG = 1.365; kB = 0.1365/0.9 = 0.151667 clamps UP to 0.5.
    GradeParams p;
    p.setNeutralSample(0.0, 0.1, 0.9);
    const WbGains wb = ColorGrade::resolveWbGains(p);
    QCOMPARE(wb.kR, 2.0);
    QCOMPARE(wb.borrow, 0.8);
    QVERIFY(qAbs(wb.kG - 1.365) < 1e-5);
    QCOMPARE(wb.kB, 0.5);
}

static ColorGrade makeTwoAnchorGrade()
{
    ColorGrade grade;
    grade.enabled = true;
    grade.depthLinked = true;
    // Stored deep-first on purpose: roles must come from sorting on depth.
    GradeAnchor deep;
    deep.depth = 25.0;
    deep.params.redRecovery = 1.0;
    GradeAnchor shallow;
    shallow.depth = 5.0;
    shallow.params.redRecovery = 0.2;
    grade.anchors << deep << shallow;
    return grade;
}

void ColorGradeTest::depth_interpolationLinear()
{
    const ColorGrade grade = makeTwoAnchorGrade();
    // Midpoint 15 m → w = 0.5 → redRecovery = 0.6 → borrow k = 0.3,
    // red lift 0.35·0.6 = 0.21.
    const GradeOps ops = grade.resolveAtDepth(15.0);
    QVERIFY(qAbs(ops.matrix[0][0] - 0.7) < 1e-12);
    QVERIFY(qAbs(ops.matrix[0][1] - 0.3) < 1e-12);
    QVERIFY(qAbs(ops.lift[0] - 0.21) < 1e-12);

    // Quarter point 10 m → w = 0.25 → rr = 0.4 → lift 0.14.
    const GradeOps quarter = grade.resolveAtDepth(10.0);
    QVERIFY(qAbs(quarter.lift[0] - 0.14) < 1e-12);
}

void ColorGradeTest::depth_clampedOutsideAnchors()
{
    const ColorGrade grade = makeTwoAnchorGrade();
    // Above the shallow anchor → shallow params (rr 0.2 → lift 0.07).
    const GradeOps surface = grade.resolveAtDepth(0.0);
    QVERIFY(qAbs(surface.lift[0] - 0.07) < 1e-12);
    // Below the deep anchor → deep params (rr 1.0 → lift 0.35).
    const GradeOps abyss = grade.resolveAtDepth(100.0);
    QVERIFY(qAbs(abyss.lift[0] - 0.35) < 1e-12);
}

void ColorGradeTest::depth_closeAnchorsAreStatic()
{
    ColorGrade grade = makeTwoAnchorGrade();
    grade.anchors[0].depth = 10.3; // deep (rr 1.0)
    grade.anchors[1].depth = 10.0; // shallow (rr 0.2)
    QVERIFY(!grade.hasDepthRamp());
    // 0.3 m apart (< 0.5 m): static, the shallow anchor wins everywhere.
    for (double depth : {0.0, 10.15, 50.0}) {
        const GradeOps ops = grade.resolveAtDepth(depth);
        QVERIFY(qAbs(ops.lift[0] - 0.07) < 1e-12);
    }
}

void ColorGradeTest::depth_unplacedAnchorSitsOutOfRamp()
{
    // A slider-created anchor (depth 0, videoTime -1 — never "Set") must
    // not act as a phantom 0 m shallow anchor: the UI shows it as "not
    // set" and the blend must agree (GradeAnchor::isPlaced gates ramp
    // participation).
    ColorGrade grade;
    GradeAnchor placed;
    placed.depth = 12.0;
    placed.videoTime = 3.0;
    placed.params.redRecovery = 1.0; // lift 0.35
    GradeAnchor unplaced;             // slider-created, never "Set"
    unplaced.placed = false;
    unplaced.params.redRecovery = 0.2;
    grade.anchors << placed << unplaced;

    QVERIFY(placed.isPlaced());
    QVERIFY(!unplaced.isPlaced());
    QVERIFY(!grade.hasDepthRamp());
    QCOMPARE(grade.rampWeightAt(6.0), -1.0);
    // Static on the PLACED anchor everywhere, regardless of storage order.
    for (double depth : {0.0, 6.0, 25.0}) {
        const GradeOps ops = grade.resolveAtDepth(depth);
        QVERIFY(qAbs(ops.lift[0] - 0.35) < 1e-12);
    }
    // Binding ("Set") places the anchor and turns the ramp on.
    grade.anchors[1].videoTime = 8.0;
    grade.anchors[1].depth = 2.0;
    grade.anchors[1].placed = true;
    QVERIFY(grade.hasDepthRamp());
    QVERIFY(qAbs(grade.rampWeightAt(7.0) - 0.5) < 1e-12);

    // Default-constructed anchors are placed: C++-authored fixtures (the
    // ffmpeg parity test has a legit 0.0 m surface anchor) and pre-flag
    // persisted grades stay in the ramp.
    GradeAnchor external;
    external.depth = 0.0;
    QVERIFY(external.isPlaced());

    // The flag round-trips (presence-tracked, absent = placed).
    ColorGrade persisted;
    GradeAnchor keep; keep.depth = 5.0;
    GradeAnchor pending; pending.placed = false; pending.params.clarity = 0.3;
    persisted.anchors << keep << pending;
    bool ok = false;
    const ColorGrade back = ColorGrade::fromJson(persisted.toJson(), &ok);
    QVERIFY(ok);
    QCOMPARE(back.anchors.size(), 2);
    QVERIFY(back.anchors.at(0).isPlaced());
    QVERIFY(!back.anchors.at(1).isPlaced());
}

void ColorGradeTest::depth_singleAnchorIsStatic()
{
    ColorGrade grade;
    GradeAnchor anchor;
    anchor.depth = 12.0;
    anchor.params.redRecovery = 0.4;
    grade.anchors << anchor;
    QVERIFY(!grade.hasDepthRamp());
    for (double depth : {0.0, 12.0, 40.0}) {
        const GradeOps ops = grade.resolveAtDepth(depth);
        QVERIFY(qAbs(ops.lift[0] - 0.14) < 1e-12);
    }
}

void ColorGradeTest::depth_wbTripleInterpolated()
{
    // The depth link interpolates the RESOLVED WB gain triple, not the raw
    // neutral sample. Shallow anchor: no sample (identity gains). Deep
    // anchor sample (0.5, 0.5, 0.25): Y = 0.1063 + 0.3576 + 0.01805
    // = 0.48195 → kR = kG = 0.9639, kB = 1.9278. All sliders zero so the
    // matrix diagonal IS the lerped gain triple.
    ColorGrade grade;
    grade.depthLinked = true;
    GradeAnchor shallow;
    shallow.depth = 5.0;
    GradeAnchor deep;
    deep.depth = 25.0;
    deep.params.setNeutralSample(0.5, 0.5, 0.25);
    grade.anchors << shallow << deep;

    const GradeOps mid = grade.resolveAtDepth(15.0); // w = 0.5
    QVERIFY(qAbs(mid.matrix[0][0] - (1.0 + 0.9639) / 2.0) < 1e-4);
    QVERIFY(qAbs(mid.matrix[1][1] - (1.0 + 0.9639) / 2.0) < 1e-4);
    QVERIFY(qAbs(mid.matrix[2][2] - (1.0 + 1.9278) / 2.0) < 1e-4);
}

void ColorGradeTest::json_roundTrip()
{
    ColorGrade grade;
    grade.enabled = true;
    grade.depthLinked = true;
    grade.lutPath = QStringLiteral("/somewhere/look.cube");
    GradeAnchor a;
    a.depth = 6.5;
    a.videoTime = 12.5;
    a.params.redRecovery = 0.25;
    a.params.waterTint = -0.5;
    a.params.clarity = 0.75;
    a.params.setNeutralSample(0.31, 0.52, 0.43);
    GradeAnchor b;
    b.depth = 24.0;
    b.params.redRecovery = 0.9;
    grade.anchors << a << b;

    bool ok = false;
    const ColorGrade restored = ColorGrade::fromJson(grade.toJson(), &ok);
    QVERIFY(ok);
    QCOMPARE(restored.enabled, true);
    QCOMPARE(restored.depthLinked, true);
    QCOMPARE(restored.lutPath, grade.lutPath);
    QCOMPARE(restored.anchors.size(), 2);
    QCOMPARE(restored.anchors[0].depth, 6.5);
    QCOMPARE(restored.anchors[0].videoTime, 12.5);
    QCOMPARE(restored.anchors[0].params.redRecovery, 0.25);
    QCOMPARE(restored.anchors[0].params.waterTint, -0.5);
    QCOMPARE(restored.anchors[0].params.clarity, 0.75);
    QVERIFY(restored.anchors[0].params.hasNeutralSample);
    QCOMPARE(restored.anchors[0].params.neutralSample[0], 0.31);
    QCOMPARE(restored.anchors[0].params.neutralSample[1], 0.52);
    QCOMPARE(restored.anchors[0].params.neutralSample[2], 0.43);
    QVERIFY(!restored.anchors[1].params.hasNeutralSample);
    QCOMPARE(restored.anchors[1].videoTime, -1.0);
    QCOMPARE(restored.anchors[1].params.redRecovery, 0.9);
}

void ColorGradeTest::json_presenceTrackedKeys()
{
    // Absent ≠ default: unset neutralSample / videoTime / lutPath must not
    // be written at all (the OverlayTemplate presence-tracking precedent).
    ColorGrade grade;
    grade.enabled = true;
    GradeAnchor anchor;
    grade.anchors << anchor;

    const QJsonObject json = grade.toJson();
    QCOMPARE(json.value(QStringLiteral("version")).toInt(), 1);
    QVERIFY(!json.contains(QStringLiteral("lutPath")));
    const QJsonObject a =
        json.value(QStringLiteral("anchors")).toArray().at(0).toObject();
    QVERIFY(!a.contains(QStringLiteral("neutralSample")));
    QVERIFY(!a.contains(QStringLiteral("videoTime")));

    // And the sampled anchor writes them.
    grade.anchors[0].params.setNeutralSample(0.4, 0.5, 0.6);
    grade.anchors[0].videoTime = 3.0;
    grade.lutPath = QStringLiteral("/x.cube");
    const QJsonObject json2 = grade.toJson();
    QVERIFY(json2.contains(QStringLiteral("lutPath")));
    const QJsonObject a2 =
        json2.value(QStringLiteral("anchors")).toArray().at(0).toObject();
    QVERIFY(a2.contains(QStringLiteral("neutralSample")));
    QVERIFY(a2.contains(QStringLiteral("videoTime")));
}

void ColorGradeTest::json_versionHandling()
{
    // Empty / non-grade object → ok = false.
    bool ok = true;
    ColorGrade::fromJson(QJsonObject(), &ok);
    QVERIFY(!ok);

    // A newer version is tolerated with a warning; known fields load.
    ColorGrade grade;
    grade.enabled = true;
    GradeAnchor anchor;
    anchor.params.clarity = 0.5;
    grade.anchors << anchor;
    QJsonObject json = grade.toJson();
    json[QStringLiteral("version")] = 99;
    json[QStringLiteral("fancyFutureKey")] = QStringLiteral("ignored");
    ok = false;
    const ColorGrade restored = ColorGrade::fromJson(json, &ok);
    QVERIFY(ok);
    QVERIFY(restored.enabled);
    QCOMPARE(restored.anchors.size(), 1);
    QCOMPARE(restored.anchors[0].params.clarity, 0.5);

    // A missing version key means version 1 (the first schema).
    json.remove(QStringLiteral("version"));
    ok = false;
    ColorGrade::fromJson(json, &ok);
    QVERIFY(ok);
}

void ColorGradeTest::preset_bundledFilesParse()
{
    const QStringList files = {QStringLiteral("Tropical_Blue_Shallow.ucp"),
                               QStringLiteral("Tropical_Blue_Deep.ucp"),
                               QStringLiteral("Temperate_Green_Shallow.ucp"),
                               QStringLiteral("Temperate_Green_Deep.ucp"),
                               QStringLiteral("Lake_Quarry_Shallow.ucp"),
                               QStringLiteral("Lake_Quarry_Deep.ucp")};
    for (const QString& name : files) {
        QFile file(QStringLiteral(COLOR_PRESETS_DIR "/") + name);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(name));
        GradePreset preset;
        QString error;
        QVERIFY2(parsePreset(file.readAll(), &preset, &error), qPrintable(error));
        QVERIFY(!preset.name.isEmpty());
        QVERIFY(!preset.description.isEmpty());
        QVERIFY(preset.redRecovery >= 0.0 && preset.redRecovery <= 1.0);
        QVERIFY(preset.waterTint >= -1.0 && preset.waterTint <= 1.0);
        QVERIFY(preset.clarity >= 0.0 && preset.clarity <= 1.0);
    }
}

void ColorGradeTest::preset_errors()
{
    GradePreset preset;
    QString error;
    QVERIFY(!parsePreset("this is not json", &preset, &error));
    QVERIFY(!error.isEmpty());

    QVERIFY(!parsePreset("[1,2,3]", &preset, &error));

    // Missing name is an error.
    QVERIFY(!parsePreset(R"({"description":"x","redRecovery":0.5})", &preset, &error));
    QVERIFY(error.contains(QStringLiteral("name")));

    // Out-of-range slider values are clamped, not fatal.
    QVERIFY(parsePreset(R"({"name":"X","redRecovery":5,"waterTint":-9,"clarity":2})",
                        &preset, &error));
    QCOMPARE(preset.redRecovery, 1.0);
    QCOMPARE(preset.waterTint, -1.0);
    QCOMPARE(preset.clarity, 1.0);
}

QTEST_GUILESS_MAIN(ColorGradeTest)
#include "color_grade_test.moc"
