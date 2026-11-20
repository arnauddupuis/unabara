// ColorGradeController unit tests (v0.4 track C, PR C2 + C3).
//
// Covers the controller's contract with the clip model and the shader:
//  - no-clip safety (setters no-op, identity uniforms, previewActive false)
//  - edit -> write-back round trip through VideoClip's opaque colorGradeMap
//  - resolved uniform properties match GradeOps from ColorGrade::resolve()
//    (the pipeline's ground truth) for known parameters
//  - clip switch reloads that clip's stored grade; switching back restores
//  - external colorGrade writes (A3 restore path) reload the controller
//  - bypass/previewActive gating and reset()
// C3 depth link:
//  - selectedAnchor retargets the sliders without touching the other anchor
//  - setAnchorFromCurrentFrame binds depth (DiveData::dataAtTime) + video
//    time and preserves params
//  - depth-linked uniforms match ColorGrade::resolveAtDepth ground truth
//  - the 1 cm epsilon skip coalesces per-tick recomputes
//  - clearAnchor returns to static; goToAnchor emits seekRequested
//  - depthLinked + anchors round-trip through the clip's colorGradeMap
//  - currentDepthText formatting (metric/imperial; Config redirected)
// C4 eyedropper / LUT import / presets:
//  - medianSample: outlier rejection (median, not mean), edge clamping
//  - pickNeutral: QVideoSink frame -> sample on the selected anchor,
//    params-only edit (placement untouched), WB uniforms vs resolveWbGains
//    ground truth, failure paths emit pickFailed
//  - bakeLutStrip matches CubeLut::sample at lattice points (strip layout)
//  - importLut: lutPath + strip bake + revision; invalid .cube -> error,
//    state untouched; clearLut; restart re-bake; missing file on load ->
//    warning state, lutEnabled false, grade still resolves
//  - presets: apply writes ONLY the three sliders; savePreset round trip
//    (sanitized file name, unique-suffix collisions, user-dir seam)

#include "settings_isolation.h"
#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QVector4D>
#include <QVideoFrame>
#include <QVideoSink>

#include <cmath>

#include "include/core/color_grade.h"
#include "include/core/config.h"
#include "include/core/cube_lut.h"
#include "include/core/dive_data.h"
#include "include/core/video_clip.h"
#include "include/ui/color_grade_controller.h"

using namespace Unabara;

namespace {

// The uniform getters narrow double -> float for the GPU, so compare
// against the same narrowing of the ground-truth ops.
void compareUniformsToOps(const ColorGradeController& c, const GradeOps& ops)
{
    const QVector4D rows[3] = { c.matrixR(), c.matrixG(), c.matrixB() };
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            QCOMPARE(rows[i][j], float(ops.matrix[i][j]));
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(c.blackPoint()[i], float(ops.black[i]));
        QCOMPARE(c.lift()[i], float(ops.lift[i]));
    }
    QCOMPARE(c.contrast(), ops.contrast);
}

// Synthetic linear descent: 0 s @ 0 m -> 100 s @ 20 m (0.2 m/s), so
// dataAtTime(t).depth == t / 5 inside the log.
DiveData* makeLinearDive(QObject* parent)
{
    auto* dive = new DiveData(parent);
    for (int i = 0; i <= 10; ++i)
        dive->addDataPoint(DiveDataPoint(i * 10.0, i * 2.0));
    return dive;
}

// 2³ channel-swap LUT: output = (b, g, r). Entries in .cube order (red
// index fastest), so a swapped bake is unmistakable in spot checks.
const char kChannelSwapCube[] =
    "# channel swap: output = (b, g, r)\n"
    "LUT_3D_SIZE 2\n"
    "0 0 0\n"
    "0 0 1\n"
    "0 1 0\n"
    "0 1 1\n"
    "1 0 0\n"
    "1 0 1\n"
    "1 1 0\n"
    "1 1 1\n";

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == bytes.size();
}

} // namespace

class ColorGradeControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void noClipSafety();
    void editWriteBackRoundTrip();
    void uniformsMatchResolve();
    void clipSwitchReload();
    void externalChangeReloads();
    void bypassAndPreviewActive();
    void resetClearsSelectedAnchorSliders();
    void splitPosTransient();
    // C3 depth link
    void selectedAnchorRetargetsSliders();
    void setAnchorFromCurrentFrameBindsDepthAndTime();
    void depthLinkedUniformsMatchResolveAtDepth();
    void unlinkResetsSelectedAnchorToZero();
    void epsilonSkipCoalescesTinyDepthChanges();
    void clearAnchorReturnsToStatic();
    void goToAnchorEmitsSeek();
    void depthLinkRoundTripThroughClipMap();
    void currentDepthTextFormatting();
    // C4 eyedropper / LUT / presets
    void medianSampleRejectsOutliers();
    void medianSampleClampsAtEdges();
    void pickNeutralStoresSampleOnSelectedAnchor();
    void bakeStripMatchesCubeSample();
    void importLutSetsPathBakesStripAndRestores();
    void missingLutOnLoadWarnsButStillGrades();
    void applyPresetWritesOnlySliders();
    void savePresetRoundTripAndCollisions();

private:
    QTemporaryDir m_settingsDir; // Config/QSettings redirect target
};

void ColorGradeControllerTest::initTestCase()
{
    // Redirect QSettings for the currentDepthTextFormatting slot BEFORE
    // the first Config::instance() call anywhere in this process. Same
    // pattern (and same Linux-only caveat) as clip_list_model_test:
    // Config's QSettings(org, app) constructor means NativeFormat, which
    // setPath() only redirects where native == Ini (Linux).
    QVERIFY(m_settingsDir.isValid());
    isolateUserSettings(m_settingsDir.path()); // see settings_isolation.h
}

void ColorGradeControllerTest::noClipSafety()
{
    ColorGradeController c;

    QVERIFY(!c.hasClip());
    QVERIFY(!c.enabled());
    QVERIFY(!c.previewActive());
    QCOMPARE(c.redRecovery(), 0.0);
    QCOMPARE(c.waterTint(), 0.0);
    QCOMPARE(c.clarity(), 0.0);

    // Setters without a clip must not crash and must not take effect.
    c.setEnabled(true);
    c.setRedRecovery(0.8);
    c.setWaterTint(-0.5);
    c.setClarity(0.3);
    c.reset();
    QVERIFY(!c.enabled());
    QCOMPARE(c.redRecovery(), 0.0);

    // Identity uniforms.
    compareUniformsToOps(c, GradeOps());
    QVERIFY(!c.lutEnabled());

    // setClip(nullptr) on an already-detached controller is fine too.
    c.setClip(nullptr);
    QVERIFY(!c.hasClip());
}

void ColorGradeControllerTest::editWriteBackRoundTrip()
{
    VideoClip clip(QStringLiteral("/videos/reef.mp4"));
    ColorGradeController c;
    c.setClip(&clip);
    QVERIFY(c.hasClip());

    QSignalSpy gradeWrites(&clip, &VideoClip::colorGradeChanged);

    c.setEnabled(true);
    c.setRedRecovery(0.7);
    c.setWaterTint(-0.3);
    c.setClarity(0.5);
    QVERIFY(gradeWrites.count() >= 4);

    // The clip's opaque map now round-trips the exact grade.
    bool ok = false;
    const ColorGrade stored = ColorGrade::fromJson(clip.colorGrade(), &ok);
    QVERIFY(ok);
    QVERIFY(stored.enabled);
    QCOMPARE(stored.anchors.size(), 1);
    QCOMPARE(stored.anchors.first().params.redRecovery, 0.7);
    QCOMPARE(stored.anchors.first().params.waterTint, -0.3);
    QCOMPARE(stored.anchors.first().params.clarity, 0.5);

    // Out-of-range writes clamp to GradeTuning's documented slider ranges.
    c.setRedRecovery(1.4);
    QCOMPARE(c.redRecovery(), 1.0);
    c.setWaterTint(-2.0);
    QCOMPARE(c.waterTint(), -1.0);

    // A same-value write must not re-serialize (no redundant store churn).
    const int writes = gradeWrites.count();
    c.setClarity(0.5);
    QCOMPARE(gradeWrites.count(), writes);
}

void ColorGradeControllerTest::uniformsMatchResolve()
{
    VideoClip clip(QStringLiteral("/videos/wreck.mp4"));
    ColorGradeController c;
    c.setClip(&clip);

    QSignalSpy uniformBatches(&c, &ColorGradeController::uniformsChanged);

    c.setEnabled(true);
    c.setRedRecovery(0.6);
    c.setWaterTint(0.4);
    c.setClarity(0.8);

    // Ground truth: one resolve() of the same parameters.
    GradeParams params;
    params.redRecovery = 0.6;
    params.waterTint = 0.4;
    params.clarity = 0.8;
    const GradeOps ops = ColorGrade::resolve(params);

    compareUniformsToOps(c, ops);

    // Sanity against the known mapping constants, so a silently-identity
    // resolve can't pass: clarity drives black point and contrast.
    QCOMPARE(c.contrast(), 1.0 + GradeTuning::kClarityContrast * 0.8);
    QCOMPARE(c.blackPoint()[0], float(GradeTuning::kClarityBlack * 0.8));
    QCOMPARE(c.lift()[0], float(GradeTuning::kRedMidtoneLift * 0.6));

    // One batched uniformsChanged per slider edit (setEnabled recomputes
    // nothing — it only gates previewActive).
    QCOMPARE(uniformBatches.count(), 3);
}

void ColorGradeControllerTest::clipSwitchReload()
{
    VideoClip clipA(QStringLiteral("/videos/a.mp4"));
    VideoClip clipB(QStringLiteral("/videos/b.mp4"));
    ColorGradeController c;

    c.setClip(&clipA);
    c.setEnabled(true);
    c.setRedRecovery(0.9);
    c.setClarity(0.25);

    // Switch to a fresh clip: disabled, identity sliders and uniforms.
    QSignalSpy clipSpy(&c, &ColorGradeController::clipChanged);
    c.setClip(&clipB);
    QCOMPARE(clipSpy.count(), 1);
    QVERIFY(!c.enabled());
    QCOMPARE(c.redRecovery(), 0.0);
    QCOMPARE(c.clarity(), 0.0);
    compareUniformsToOps(c, GradeOps());

    // Re-selecting the same clip is a no-op (no spurious reload signal).
    c.setClip(&clipB);
    QCOMPARE(clipSpy.count(), 1);

    // Back to A: values restored from A's stored map.
    c.setClip(&clipA);
    QVERIFY(c.enabled());
    QCOMPARE(c.redRecovery(), 0.9);
    QCOMPARE(c.clarity(), 0.25);
    GradeParams params;
    params.redRecovery = 0.9;
    params.clarity = 0.25;
    compareUniformsToOps(c, ColorGrade::resolve(params));

    // Detach: back to the no-clip disabled state.
    c.setClip(nullptr);
    QVERIFY(!c.hasClip());
    QVERIFY(!c.previewActive());
    compareUniformsToOps(c, GradeOps());
}

void ColorGradeControllerTest::externalChangeReloads()
{
    VideoClip clip(QStringLiteral("/videos/c.mp4"));
    ColorGradeController c;
    c.setClip(&clip);

    // Simulate A3's restore path writing the stored grade into the clip
    // from outside the controller.
    ColorGrade external;
    external.enabled = true;
    GradeAnchor anchor;
    anchor.params.redRecovery = 0.45;
    anchor.params.waterTint = 0.2;
    external.anchors.append(anchor);
    clip.setColorGrade(external.toJson());

    QVERIFY(c.enabled());
    QCOMPARE(c.redRecovery(), 0.45);
    QCOMPARE(c.waterTint(), 0.2);

    // And the controller's own write-back must NOT bounce back as a reload
    // (the write-back guard): an edit right after still lands.
    c.setClarity(0.1);
    QCOMPARE(c.clarity(), 0.1);
    QCOMPARE(c.redRecovery(), 0.45); // untouched by the guard path
}

void ColorGradeControllerTest::bypassAndPreviewActive()
{
    VideoClip clip(QStringLiteral("/videos/d.mp4"));
    ColorGradeController c;
    c.setClip(&clip);

    QSignalSpy previewSpy(&c, &ColorGradeController::previewActiveChanged);

    QVERIFY(!c.previewActive()); // grade disabled
    c.setEnabled(true);
    QVERIFY(c.previewActive());
    QCOMPARE(previewSpy.count(), 1);

    // Hold-to-compare: bypass suppresses the preview but is transient —
    // it never touches the stored grade.
    QSignalSpy gradeWrites(&clip, &VideoClip::colorGradeChanged);
    c.setBypass(true);
    QVERIFY(!c.previewActive());
    QCOMPARE(previewSpy.count(), 2);
    c.setBypass(false);
    QVERIFY(c.previewActive());
    QCOMPARE(previewSpy.count(), 3);
    QCOMPARE(gradeWrites.count(), 0); // bypass is not persisted

    // A clip switch clears the transient bypass (a switch landing while
    // the button is held must not leave the new clip bypassed).
    c.setBypass(true);
    VideoClip clip2(QStringLiteral("/videos/d2.mp4"));
    c.setClip(&clip2);
    QVERIFY(!c.bypass());

    // Losing the clip kills the preview.
    c.setClip(nullptr);
    QVERIFY(!c.previewActive());
}

void ColorGradeControllerTest::resetClearsSelectedAnchorSliders()
{
    VideoClip clip(QStringLiteral("/videos/e.mp4"));
    ColorGradeController c;
    c.setClip(&clip);

    c.setEnabled(true);
    c.setRedRecovery(0.7);
    c.setWaterTint(-0.6);
    c.setClarity(0.4);

    c.reset();
    QCOMPARE(c.redRecovery(), 0.0);
    QCOMPARE(c.waterTint(), 0.0);
    QCOMPARE(c.clarity(), 0.0);
    QVERIFY(c.enabled()); // reset touches the sliders, not the enable flag
    compareUniformsToOps(c, GradeOps());

    // The reset is written back to the clip.
    bool ok = false;
    const ColorGrade stored = ColorGrade::fromJson(clip.colorGrade(), &ok);
    QVERIFY(ok);
    QCOMPARE(stored.anchors.size(), 1);
    QCOMPARE(stored.anchors.first().params.redRecovery, 0.0);
}

void ColorGradeControllerTest::splitPosTransient()
{
    VideoClip clip(QStringLiteral("/videos/f.mp4"));
    ColorGradeController c;
    c.setClip(&clip);

    QCOMPARE(c.splitPos(), 1.0); // default: fully graded
    QSignalSpy gradeWrites(&clip, &VideoClip::colorGradeChanged);
    c.setSplitPos(0.5);
    QCOMPARE(c.splitPos(), 0.5);
    c.setSplitPos(1.7);
    QCOMPARE(c.splitPos(), 1.0); // clamped
    QCOMPARE(gradeWrites.count(), 0); // never persisted
}

void ColorGradeControllerTest::selectedAnchorRetargetsSliders()
{
    VideoClip clip(QStringLiteral("/videos/g.mp4"));
    ColorGradeController c;
    c.setClip(&clip);
    c.setEnabled(true);

    c.setRedRecovery(0.8); // lands in anchor 0
    QCOMPARE(c.selectedAnchor(), 0);

    QSignalSpy gradeSpy(&c, &ColorGradeController::gradeChanged);
    QSignalSpy selectedSpy(&c, &ColorGradeController::selectedAnchorChanged);
    c.setSelectedAnchor(1);
    QCOMPARE(c.selectedAnchor(), 1);
    QCOMPARE(selectedSpy.count(), 1);
    // The slider NOTIFY (gradeChanged) fires so bound sliders retarget...
    QCOMPARE(gradeSpy.count(), 1);
    // ...and they now read the (not yet created) anchor 1: identity.
    QCOMPARE(c.redRecovery(), 0.0);

    // Edits land in anchor 1; anchor 0 is untouched.
    c.setRedRecovery(0.3);
    c.setWaterTint(-0.4);
    QCOMPARE(c.grade().anchors.size(), 2);
    QCOMPARE(c.grade().anchors.at(0).params.redRecovery, 0.8);
    QCOMPARE(c.grade().anchors.at(0).params.waterTint, 0.0);
    QCOMPARE(c.grade().anchors.at(1).params.redRecovery, 0.3);
    QCOMPARE(c.grade().anchors.at(1).params.waterTint, -0.4);

    // Switching back re-reads anchor 0's values.
    c.setSelectedAnchor(0);
    QCOMPARE(c.redRecovery(), 0.8);
    QCOMPARE(c.waterTint(), 0.0);

    // Same-value selection is a no-op; out-of-range clamps to [0,1].
    c.setSelectedAnchor(0);
    QCOMPARE(selectedSpy.count(), 2);
    c.setSelectedAnchor(5);
    QCOMPARE(c.selectedAnchor(), 1);
    c.setSelectedAnchor(-3);
    QCOMPARE(c.selectedAnchor(), 0);

    // Selection is transient view state: never written to the clip map,
    // and a clip reload resets it to 0.
    QSignalSpy gradeWrites(&clip, &VideoClip::colorGradeChanged);
    c.setSelectedAnchor(1);
    QCOMPARE(gradeWrites.count(), 0);
    VideoClip clip2(QStringLiteral("/videos/g2.mp4"));
    c.setClip(&clip2);
    QCOMPARE(c.selectedAnchor(), 0);
}

void ColorGradeControllerTest::setAnchorFromCurrentFrameBindsDepthAndTime()
{
    VideoClip clip(QStringLiteral("/videos/h.mp4"));
    DiveData* dive = makeLinearDive(this);
    ColorGradeController c;
    c.setClip(&clip);
    c.setDive(dive);
    c.setEnabled(true);

    // Without a playback sample there is nothing to bind: no-op.
    c.setAnchorFromCurrentFrame(0);
    QVERIFY(!c.anchor0Set());

    // Grade anchor 0 first, then bind it: params must survive the bind.
    c.setRedRecovery(0.9);
    c.setCurrentDiveTime(25.0, 3.5); // depth 5.0 m, player at 3.5 s
    c.setAnchorFromCurrentFrame(0);
    QCOMPARE(c.grade().anchors.size(), 1);
    QCOMPARE(c.grade().anchors.at(0).depth, 5.0);
    QCOMPARE(c.grade().anchors.at(0).videoTime, 3.5);
    QCOMPARE(c.grade().anchors.at(0).params.redRecovery, 0.9);
    QVERIFY(c.anchor0Set());
    QVERIFY(c.anchor0HasVideoTime());

    // Binding a not-yet-existing anchor creates it with identity params
    // (what the sliders show for a never-edited anchor).
    c.setCurrentDiveTime(90.0, 12.0); // depth 18.0 m
    c.setAnchorFromCurrentFrame(1);
    QCOMPARE(c.grade().anchors.size(), 2);
    QCOMPARE(c.grade().anchors.at(1).depth, 18.0);
    QCOMPARE(c.grade().anchors.at(1).videoTime, 12.0);
    QCOMPARE(c.grade().anchors.at(1).params.redRecovery, 0.0);

    // Re-binding moves depth/videoTime only.
    c.setCurrentDiveTime(50.0, 6.25); // depth 10.0 m
    c.setAnchorFromCurrentFrame(0);
    QCOMPARE(c.grade().anchors.at(0).depth, 10.0);
    QCOMPARE(c.grade().anchors.at(0).videoTime, 6.25);
    QCOMPARE(c.grade().anchors.at(0).params.redRecovery, 0.9);

    // Out-of-range indices are rejected.
    c.setAnchorFromCurrentFrame(2);
    c.setAnchorFromCurrentFrame(-1);
    QCOMPARE(c.grade().anchors.size(), 2);
}

void ColorGradeControllerTest::depthLinkedUniformsMatchResolveAtDepth()
{
    VideoClip clip(QStringLiteral("/videos/i.mp4"));
    DiveData* dive = makeLinearDive(this);
    ColorGradeController c;
    c.setClip(&clip);
    c.setDive(dive);
    c.setEnabled(true);

    // Shallow anchor at 2 m: warm, lifted. Deep anchor at 18 m: distinct.
    c.setRedRecovery(0.9);
    c.setWaterTint(-0.2);
    c.setClarity(0.1);
    c.setCurrentDiveTime(10.0, 1.0); // depth 2 m
    c.setAnchorFromCurrentFrame(0);

    c.setSelectedAnchor(1);
    c.setRedRecovery(0.1);
    c.setWaterTint(0.5);
    c.setClarity(0.8);
    c.setCurrentDiveTime(90.0, 9.0); // depth 18 m
    c.setAnchorFromCurrentFrame(1);

    // Not linked yet: uniforms are the static resolve (anchor 0).
    compareUniformsToOps(c, c.grade().resolveStatic());

    c.setDepthLinked(true);
    QVERIFY(c.depthLinked());

    // Mid-ramp: depth 10 m -> weight (10-2)/(18-2) = 0.5. Ground truth is
    // resolveAtDepth on the grade REBUILT from the clip's stored JSON, so
    // persistence and the live resolve are checked against each other.
    c.setCurrentDiveTime(50.0, 5.0);
    bool ok = false;
    const ColorGrade stored = ColorGrade::fromJson(clip.colorGrade(), &ok);
    QVERIFY(ok);
    QCOMPARE(stored.rampWeightAt(10.0), 0.5);
    compareUniformsToOps(c, stored.resolveAtDepth(10.0));

    // At/beyond the anchors the weight clamps.
    c.setCurrentDiveTime(5.0, 0.5); // depth 1 m, shallower than anchor 0
    compareUniformsToOps(c, stored.resolveAtDepth(1.0));
    c.setCurrentDiveTime(100.0, 10.0); // depth 20 m, deeper than anchor 1
    compareUniformsToOps(c, stored.resolveAtDepth(20.0));

    // Turning the link off returns to the static resolve.
    c.setDepthLinked(false);
    compareUniformsToOps(c, stored.resolveStatic());
}

void ColorGradeControllerTest::unlinkResetsSelectedAnchorToZero()
{
    // Regression (external review 2026-10-16): unlinking left the selection
    // on anchor 1 while ColorPanel disables the anchor rows and every resolve
    // (preview + export) uses anchor 0 — slider edits became silent no-ops
    // persisted into anchor 1.
    VideoClip clip(QStringLiteral("/videos/unlink.mp4"));
    DiveData* dive = makeLinearDive(this);
    ColorGradeController c;
    c.setClip(&clip);
    c.setDive(dive);
    c.setEnabled(true);
    c.setDepthLinked(true);

    c.setRedRecovery(0.6); // anchor 0
    c.setSelectedAnchor(1);
    c.setRedRecovery(0.2); // anchor 1
    QCOMPARE(c.selectedAnchor(), 1);

    QSignalSpy selectedSpy(&c, &ColorGradeController::selectedAnchorChanged);
    // Sliders must re-read AFTER the retarget: record the selection seen by
    // each gradeChanged (the sliders' NOTIFY).
    QList<int> selectionAtGradeChanged;
    connect(&c, &ColorGradeController::gradeChanged, this,
            [&] { selectionAtGradeChanged.append(c.selectedAnchor()); });
    c.setDepthLinked(false);
    QCOMPARE(c.selectedAnchor(), 0);
    QCOMPARE(selectedSpy.count(), 1);
    QVERIFY(!selectionAtGradeChanged.isEmpty());
    QCOMPARE(selectionAtGradeChanged.last(), 0);
    QCOMPARE(c.redRecovery(), 0.6); // sliders show anchor 0

    // A slider write now lands in anchor 0 (what resolveStatic uses), and
    // anchor 1 keeps its value.
    c.setRedRecovery(0.75);
    bool ok = false;
    const ColorGrade stored = ColorGrade::fromJson(clip.colorGrade(), &ok);
    QVERIFY(ok);
    QCOMPARE(stored.anchors.size(), 2);
    QCOMPARE(stored.anchors.at(0).params.redRecovery, 0.75);
    QCOMPARE(stored.anchors.at(1).params.redRecovery, 0.2);
    compareUniformsToOps(c, stored.resolveStatic());

    // Unlinking with anchor 0 already selected emits no selection change.
    c.setDepthLinked(true);
    c.setDepthLinked(false);
    QCOMPARE(selectedSpy.count(), 1);
}

void ColorGradeControllerTest::epsilonSkipCoalescesTinyDepthChanges()
{
    VideoClip clip(QStringLiteral("/videos/j.mp4"));
    DiveData* dive = makeLinearDive(this);
    ColorGradeController c;
    c.setClip(&clip);
    c.setDive(dive);
    c.setEnabled(true);

    c.setRedRecovery(0.5);
    c.setCurrentDiveTime(10.0, 1.0);
    c.setAnchorFromCurrentFrame(0);
    c.setSelectedAnchor(1);
    c.setClarity(0.6);
    c.setCurrentDiveTime(90.0, 9.0);
    c.setAnchorFromCurrentFrame(1);
    c.setDepthLinked(true);

    QSignalSpy uniformSpy(&c, &ColorGradeController::uniformsChanged);
    QSignalSpy textSpy(&c, &ColorGradeController::currentDepthTextChanged);

    c.setCurrentDiveTime(50.0, 5.0); // depth 10 m: first sample -> recompute
    QCOMPARE(uniformSpy.count(), 1);
    QCOMPARE(textSpy.count(), 1);

    // 0.01 s later the linear profile moved 0.002 m — under the 1 cm
    // epsilon, so no recompute and no text churn.
    c.setCurrentDiveTime(50.01, 5.01);
    c.setCurrentDiveTime(50.02, 5.02);
    QCOMPARE(uniformSpy.count(), 1);
    QCOMPARE(textSpy.count(), 1);

    // A real move recomputes again.
    c.setCurrentDiveTime(60.0, 6.0); // depth 12 m
    QCOMPARE(uniformSpy.count(), 2);
    QCOMPARE(textSpy.count(), 2);

    // Not depth-linked: the scan and recompute are skipped entirely.
    c.setDepthLinked(false);
    const int afterUnlink = uniformSpy.count();
    c.setCurrentDiveTime(70.0, 7.0);
    QCOMPARE(uniformSpy.count(), afterUnlink);
}

void ColorGradeControllerTest::clearAnchorReturnsToStatic()
{
    VideoClip clip(QStringLiteral("/videos/k.mp4"));
    DiveData* dive = makeLinearDive(this);
    ColorGradeController c;
    c.setClip(&clip);
    c.setDive(dive);
    c.setEnabled(true);

    c.setRedRecovery(0.7);
    c.setCurrentDiveTime(10.0, 1.0);
    c.setAnchorFromCurrentFrame(0);
    c.setSelectedAnchor(1);
    c.setClarity(0.9);
    c.setCurrentDiveTime(90.0, 9.0);
    c.setAnchorFromCurrentFrame(1);
    c.setDepthLinked(true);
    c.setCurrentDiveTime(50.0, 5.0);
    QVERIFY(c.anchor1Set());

    // Clearing the deep anchor kills the ramp: back to the static
    // (single-anchor) behavior even though the link stays on.
    c.clearAnchor(1);
    QVERIFY(!c.anchor1Set());
    QVERIFY(!c.anchor1HasVideoTime());
    QCOMPARE(c.grade().anchors.size(), 1);
    compareUniformsToOps(c, c.grade().resolveStatic());

    // The removal persists.
    bool ok = false;
    QCOMPARE(ColorGrade::fromJson(clip.colorGrade(), &ok).anchors.size(), 1);
    QVERIFY(ok);

    // Clearing the last anchor leaves identity; clearing nonsense is safe.
    c.clearAnchor(0);
    QCOMPARE(c.grade().anchors.size(), 0);
    compareUniformsToOps(c, GradeOps());
    c.clearAnchor(0);
    c.clearAnchor(-1);
    c.clearAnchor(7);
}

void ColorGradeControllerTest::goToAnchorEmitsSeek()
{
    VideoClip clip(QStringLiteral("/videos/l.mp4"));
    DiveData* dive = makeLinearDive(this);
    ColorGradeController c;
    c.setClip(&clip);
    c.setDive(dive);
    c.setEnabled(true);

    QSignalSpy seekSpy(&c, &ColorGradeController::seekRequested);

    // Unset / unbound anchors never emit.
    c.goToAnchor(0);
    QCOMPARE(seekSpy.count(), 0);
    c.setRedRecovery(0.2); // creates anchor 0 without a videoTime
    c.goToAnchor(0);
    QCOMPARE(seekSpy.count(), 0);

    c.setCurrentDiveTime(25.0, 3.5);
    c.setAnchorFromCurrentFrame(0);
    c.goToAnchor(0);
    QCOMPARE(seekSpy.count(), 1);
    QCOMPARE(seekSpy.last().at(0).toDouble(), 3.5);
}

void ColorGradeControllerTest::depthLinkRoundTripThroughClipMap()
{
    VideoClip clip(QStringLiteral("/videos/m.mp4"));
    DiveData* dive = makeLinearDive(this);

    {
        ColorGradeController c;
        c.setClip(&clip);
        c.setDive(dive);
        c.setEnabled(true);
        c.setRedRecovery(0.6);
        c.setCurrentDiveTime(20.0, 2.0); // depth 4 m
        c.setAnchorFromCurrentFrame(0);
        c.setSelectedAnchor(1);
        c.setWaterTint(0.7);
        c.setCurrentDiveTime(80.0, 8.0); // depth 16 m
        c.setAnchorFromCurrentFrame(1);
        c.setDepthLinked(true);
    }

    // A fresh controller (the restart path) rebuilds the full depth link
    // from the clip's opaque map alone.
    ColorGradeController c2;
    c2.setClip(&clip);
    c2.setDive(dive);
    QVERIFY(c2.enabled());
    QVERIFY(c2.depthLinked());
    QCOMPARE(c2.grade().anchors.size(), 2);
    QCOMPARE(c2.grade().anchors.at(0).depth, 4.0);
    QCOMPARE(c2.grade().anchors.at(0).videoTime, 2.0);
    QCOMPARE(c2.grade().anchors.at(0).params.redRecovery, 0.6);
    QCOMPARE(c2.grade().anchors.at(1).depth, 16.0);
    QCOMPARE(c2.grade().anchors.at(1).videoTime, 8.0);
    QCOMPARE(c2.grade().anchors.at(1).params.waterTint, 0.7);
    QVERIFY(c2.anchor0HasVideoTime());
    QVERIFY(c2.anchor1HasVideoTime());

    // And it resolves depth-linked immediately once a sample arrives.
    c2.setCurrentDiveTime(50.0, 5.0); // depth 10 m
    compareUniformsToOps(c2, c2.grade().resolveAtDepth(10.0));
}

void ColorGradeControllerTest::currentDepthTextFormatting()
{
#ifndef Q_OS_LINUX
    QSKIP("Config's NativeFormat QSettings (registry/plist) cannot be redirected off Linux");
#endif
    VideoClip clip(QStringLiteral("/videos/n.mp4"));
    DiveData* dive = makeLinearDive(this);
    ColorGradeController c;
    c.setClip(&clip);
    c.setDive(dive);
    c.setEnabled(true);

    Config::instance()->setUnitSystem(Units::UnitSystem::Metric);

    // Not depth-linked -> empty.
    QCOMPARE(c.currentDepthText(), QString());

    c.setRedRecovery(0.4);
    c.setCurrentDiveTime(10.0, 1.0); // depth 2 m
    c.setAnchorFromCurrentFrame(0);
    c.setDepthLinked(true);

    // Single anchor: depth only, no percent (no ramp).
    c.setCurrentDiveTime(50.0, 5.0); // depth 10 m
    QCOMPARE(c.currentDepthText(), QStringLiteral("Now: 10.0 m"));
    QCOMPARE(c.anchor0DepthText(), QStringLiteral("2.0 m"));
    QCOMPARE(c.anchor0RoleText(), QString());

    // Second anchor at 18 m: 10 m sits at 50% toward Deep.
    c.setSelectedAnchor(1);
    c.setClarity(0.5);
    c.setCurrentDiveTime(90.0, 9.0);
    c.setAnchorFromCurrentFrame(1);
    c.setCurrentDiveTime(50.0, 5.0);
    QCOMPARE(c.currentDepthText(), QStringLiteral("Now: 10.0 m — 50% toward Deep"));
    QCOMPARE(c.anchor0RoleText(), QStringLiteral("shallow"));
    QCOMPARE(c.anchor1RoleText(), QStringLiteral("deep"));

    // Imperial: same depths, formatted via Units (10 m = 32.8 ft).
    Config::instance()->setUnitSystem(Units::UnitSystem::Imperial);
    QCOMPARE(c.currentDepthText(), QStringLiteral("Now: 32.8 ft — 50% toward Deep"));
    QCOMPARE(c.anchor1DepthText(), QStringLiteral("59.1 ft"));

    Config::instance()->setUnitSystem(Units::UnitSystem::Metric);

    // Link off -> empty again.
    c.setDepthLinked(false);
    QCOMPARE(c.currentDepthText(), QString());
}

void ColorGradeControllerTest::medianSampleRejectsOutliers()
{
    // Null image -> invalid colour (pickNeutral treats it as a failure).
    QVERIFY(!ColorGradeController::medianSample(QImage(), 0.5, 0.5).isValid());

    QImage img(100, 50, QImage::Format_RGB32);
    img.fill(QColor(100, 150, 200));

    // Window for a (0.5, 0.5) pick: px=50, py=25 -> x 46..54, y 21..29
    // (81 pixels). 40 outliers INCLUDING the centre pixel (so a plain
    // centre grab fails too): the per-channel median (rank 41) must
    // still land on the background — a MEAN would be dragged far off.
    int outliers = 0;
    const int rows[] = {25, 21, 22, 23, 24}; // centre row first
    for (int y : rows) {
        for (int x = 46; x <= 54 && outliers < 40; ++x) {
            img.setPixelColor(x, y, QColor(255, 0, 0));
            ++outliers;
        }
    }
    QCOMPARE(outliers, 40);
    QCOMPARE(img.pixelColor(50, 25), QColor(255, 0, 0)); // centre is an outlier
    QCOMPARE(ColorGradeController::medianSample(img, 0.5, 0.5), QColor(100, 150, 200));

    // The 41st outlier flips the majority — proves it really is a median.
    img.setPixelColor(50, 24, QColor(255, 0, 0));
    QCOMPARE(ColorGradeController::medianSample(img, 0.5, 0.5), QColor(255, 0, 0));
}

void ColorGradeControllerTest::medianSampleClampsAtEdges()
{
    QImage img(20, 10, QImage::Format_RGB32);
    img.fill(QColor(10, 20, 30));
    // Top-left 5×5 patch: the (0,0) pick's 9×9 window clamps to exactly
    // that region, so its median is the patch colour even though the
    // image majority is not.
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x)
            img.setPixelColor(x, y, QColor(200, 100, 50));
    QCOMPARE(ColorGradeController::medianSample(img, 0.0, 0.0), QColor(200, 100, 50));

    // At/beyond the far edge the window clamps to the last pixels.
    QCOMPARE(ColorGradeController::medianSample(img, 1.0, 1.0), QColor(10, 20, 30));
    QCOMPARE(ColorGradeController::medianSample(img, 2.0, -1.0), QColor(10, 20, 30));
}

void ColorGradeControllerTest::pickNeutralStoresSampleOnSelectedAnchor()
{
    VideoClip clip(QStringLiteral("/videos/o.mp4"));
    ColorGradeController c;
    c.setClip(&clip);
    c.setEnabled(true);

    QSignalSpy failSpy(&c, &ColorGradeController::pickFailed);

    // Failure paths: null / not-a-sink / sink without a frame. The plan's
    // "pause the video and try again" toast path — grade untouched.
    QObject notASink;
    QVERIFY(!c.pickNeutral(nullptr, 0.5, 0.5));
    QVERIFY(!c.pickNeutral(&notASink, 0.5, 0.5));
    QCOMPARE(failSpy.count(), 2);
    QVideoSink sink;
    QVERIFY(!c.pickNeutral(&sink, 0.5, 0.5)); // no frame delivered yet
    QCOMPARE(failSpy.count(), 3);
    QCOMPARE(c.grade().anchors.size(), 0);

    // Bluish frame with specular/noise outliers right at the pick point.
    QImage frame(64, 64, QImage::Format_RGB32);
    frame.fill(QColor(60, 110, 190));
    frame.setPixelColor(32, 32, QColor(255, 255, 255));
    frame.setPixelColor(31, 32, QColor(255, 0, 0));
    frame.setPixelColor(33, 33, QColor(0, 0, 0));
    QVideoFrame videoFrame(frame);
    if (!videoFrame.isValid())
        QSKIP("QVideoFrame(QImage) is unsupported in this environment");
    sink.setVideoFrame(videoFrame);

    c.setPickingActive(true);
    QVERIFY(c.pickingActive());
    QVERIFY(c.pickNeutral(&sink, 0.5, 0.5));
    QVERIFY(!c.pickingActive()); // one-click capture exits the mode
    QCOMPARE(failSpy.count(), 3);

    QCOMPARE(c.grade().anchors.size(), 1);
    const Unabara::GradeParams params = c.grade().anchors.at(0).params;
    QVERIFY(params.hasNeutralSample);
    QCOMPARE(params.neutralSample[0], 60.0 / 255.0);  // median rejected the
    QCOMPARE(params.neutralSample[1], 110.0 / 255.0); // outliers
    QCOMPARE(params.neutralSample[2], 190.0 / 255.0);
    QVERIFY(c.hasNeutralSample());
    QCOMPARE(c.neutralSampleColor(), QColor(60, 110, 190));

    // Params edit ONLY: the pick-created anchor is unplaced (same as a
    // slider edit) and the sliders themselves are untouched.
    QVERIFY(!c.grade().anchors.at(0).isPlaced());
    QCOMPARE(c.redRecovery(), 0.0);

    // The WB drives the uniforms: resolveWbGains/resolve ground truth.
    compareUniformsToOps(c, ColorGrade::resolve(params));
    const WbGains wb = ColorGrade::resolveWbGains(params);
    QVERIFY(wb.kR > 1.0); // bluish sample -> red gain up
    QCOMPARE(c.matrixR()[0], float(wb.kR)); // tint/red-recovery at identity

    // Persisted through the clip's opaque map.
    bool ok = false;
    const ColorGrade stored = ColorGrade::fromJson(clip.colorGrade(), &ok);
    QVERIFY(ok);
    QVERIFY(stored.anchors.at(0).params.hasNeutralSample);

    // Clearing removes the WB, returns to identity, persists.
    c.clearNeutralSample();
    QVERIFY(!c.hasNeutralSample());
    compareUniformsToOps(c, GradeOps());
    QVERIFY(!ColorGrade::fromJson(clip.colorGrade()).anchors.at(0).params.hasNeutralSample);

    // Picking on a PLACED anchor edits params only: placement, depth and
    // videoTime binding all survive.
    DiveData* dive = makeLinearDive(this);
    c.setDive(dive);
    c.setCurrentDiveTime(50.0, 5.0); // depth 10 m
    c.setAnchorFromCurrentFrame(0);
    QVERIFY(c.grade().anchors.at(0).isPlaced());
    QVERIFY(c.pickNeutral(&sink, 0.25, 0.75));
    QVERIFY(c.grade().anchors.at(0).isPlaced());
    QCOMPARE(c.grade().anchors.at(0).depth, 10.0);
    QCOMPARE(c.grade().anchors.at(0).videoTime, 5.0);
}

void ColorGradeControllerTest::bakeStripMatchesCubeSample()
{
    QString error;
    const CubeLut lut = CubeLut::parse(QByteArray(kChannelSwapCube), &error);
    QVERIFY2(lut.isValid(), qPrintable(error));

    const int n = 33;
    const QImage strip = ColorGradeController::bakeLutStrip(lut, n);
    QCOMPARE(strip.width(), n * n);
    QCOMPARE(strip.height(), n);

    // Lattice spot checks against CubeLut::sample — corners plus interior
    // points exercising the tetrahedral interpolation. Strip layout: pixel
    // (b·N + r, g), the identity generator's exact indexing.
    const int spots[][3] = {{0, 0, 0},    {32, 0, 0},  {0, 32, 0},   {0, 0, 32},
                            {32, 32, 32}, {5, 7, 9},   {16, 16, 16}, {31, 1, 17}};
    for (const auto& s : spots) {
        const int r = s[0], g = s[1], b = s[2];
        double outR = 0.0, outG = 0.0, outB = 0.0;
        lut.sample(r / 32.0, g / 32.0, b / 32.0, outR, outG, outB);
        const QRgb px = strip.pixel(b * n + r, g);
        QCOMPARE(qRed(px), int(std::lround(outR * 255.0)));
        QCOMPARE(qGreen(px), int(std::lround(outG * 255.0)));
        QCOMPARE(qBlue(px), int(std::lround(outB * 255.0)));
    }

    // And the swap semantics directly: input (1,0,0) -> (0,0,1), etc.
    QCOMPARE(strip.pixel(0 * n + 32, 0), qRgb(0, 0, 255));
    QCOMPARE(strip.pixel(32 * n + 0, 0), qRgb(255, 0, 0));
    QCOMPARE(strip.pixel(0 * n + 0, 32), qRgb(0, 255, 0));
}

void ColorGradeControllerTest::importLutSetsPathBakesStripAndRestores()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString cubePath = dir.filePath(QStringLiteral("swap.cube"));
    QVERIFY(writeFile(cubePath, QByteArray(kChannelSwapCube)));

    VideoClip clip(QStringLiteral("/videos/p.mp4"));
    ColorGradeController c;
    QImage pushedStrip;
    int pushes = 0;
    c.setLutStripSink([&pushedStrip, &pushes](const QImage& img) {
        pushedStrip = img;
        ++pushes;
    });
    QCOMPARE(pushes, 1);           // installing pushes the current state...
    QVERIFY(pushedStrip.isNull()); // ...identity before any LUT
    c.setClip(&clip);
    c.setEnabled(true);
    const int pushesAfterLoad = pushes;

    QVERIFY(!c.hasLut());
    QVERIFY(!c.lutEnabled());
    QCOMPARE(c.lutLabel(), QString());

    QSignalSpy errSpy(&c, &ColorGradeController::errorOccurred);
    QSignalSpy uniformSpy(&c, &ColorGradeController::uniformsChanged);
    const int revBefore = c.lutRevision();

    QVERIFY(c.importLut(QUrl::fromLocalFile(cubePath)));
    QCOMPARE(errSpy.count(), 0);
    QVERIFY(c.hasLut());
    QVERIFY(c.lutEnabled());
    QVERIFY(!c.lutMissing());
    QCOMPARE(c.grade().lutPath, QFileInfo(cubePath).absoluteFilePath());
    QCOMPARE(c.lutLabel(), QStringLiteral("swap.cube (2³)"));
    QVERIFY(c.lutRevision() > revBefore); // URL cache-buster moved
    QVERIFY(uniformSpy.count() >= 1);     // lutEnabled/lutRevision NOTIFY
    QVERIFY(pushes > pushesAfterLoad);
    QVERIFY(!pushedStrip.isNull());
    QCOMPARE(pushedStrip.width(), 33 * 33);

    // The pushed strip is the bake of the parsed LUT (spot check).
    double outR = 0.0, outG = 0.0, outB = 0.0;
    QString error;
    const CubeLut lut = CubeLut::parseFile(cubePath, &error);
    QVERIFY2(lut.isValid(), qPrintable(error));
    lut.sample(16 / 32.0, 8 / 32.0, 24 / 32.0, outR, outG, outB);
    QCOMPARE(pushedStrip.pixel(24 * 33 + 16, 8),
             qRgb(int(std::lround(outR * 255.0)), int(std::lround(outG * 255.0)),
                  int(std::lround(outB * 255.0))));

    // lutPath persists in the clip map; a fresh controller (the restart
    // path) re-bakes the identical strip for the restored clip.
    {
        ColorGradeController c2;
        QImage strip2;
        c2.setLutStripSink([&strip2](const QImage& img) { strip2 = img; });
        c2.setClip(&clip);
        QVERIFY(c2.lutEnabled());
        QCOMPARE(c2.grade().lutPath, QFileInfo(cubePath).absoluteFilePath());
        QVERIFY(!strip2.isNull());
        QCOMPARE(strip2, pushedStrip);
    }

    // Corrupt .cube (1D is explicitly rejected): error signal with
    // CubeLut's message, previous LUT fully intact.
    const QString badPath = dir.filePath(QStringLiteral("bad.cube"));
    QVERIFY(writeFile(badPath, QByteArrayLiteral("LUT_1D_SIZE 4\n0 0 0\n")));
    const int pushesBefore = pushes;
    const int revBeforeBad = c.lutRevision();
    QVERIFY(!c.importLut(QUrl::fromLocalFile(badPath)));
    QCOMPARE(errSpy.count(), 1);
    QVERIFY(!errSpy.last().at(0).toString().isEmpty());
    QCOMPARE(c.grade().lutPath, QFileInfo(cubePath).absoluteFilePath());
    QVERIFY(c.lutEnabled());
    QCOMPARE(pushes, pushesBefore);
    QCOMPARE(c.lutRevision(), revBeforeBad);

    // Clear: lutPath empty, identity strip, revision bump, persisted.
    const int revBeforeClear = c.lutRevision();
    c.clearLut();
    QVERIFY(!c.hasLut());
    QVERIFY(!c.lutEnabled());
    QVERIFY(pushedStrip.isNull());
    QVERIFY(c.lutRevision() > revBeforeClear);
    QVERIFY(ColorGrade::fromJson(clip.colorGrade()).lutPath.isEmpty());
}

void ColorGradeControllerTest::missingLutOnLoadWarnsButStillGrades()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    VideoClip clip(QStringLiteral("/videos/q.mp4"));
    ColorGradeController c;
    QImage pushedStrip(1, 1, QImage::Format_RGB32); // non-null sentinel
    c.setLutStripSink([&pushedStrip](const QImage& img) { pushedStrip = img; });

    // A restored clip whose .cube has vanished: warning state, NOT an
    // error dialog — and the parametric grade still applies.
    ColorGrade external;
    external.enabled = true;
    GradeAnchor anchor;
    anchor.params.clarity = 0.5;
    external.anchors.append(anchor);
    external.lutPath = dir.filePath(QStringLiteral("vanished.cube"));
    clip.setColorGrade(external.toJson());
    c.setClip(&clip);

    QVERIFY(c.hasLut());      // the grade still carries the path
    QVERIFY(c.lutMissing());  // warning row in the panel
    QVERIFY(!c.lutEnabled()); // the shader must not sample a stale strip
    QCOMPARE(c.lutLabel(), QStringLiteral("vanished.cube")); // size unknown
    QVERIFY(pushedStrip.isNull()); // identity strip pushed
    compareUniformsToOps(c, ColorGrade::resolve(anchor.params));

    // The path is NOT dropped from the grade (the file may come back).
    QCOMPARE(ColorGrade::fromJson(clip.colorGrade()).lutPath, external.lutPath);
}

void ColorGradeControllerTest::applyPresetWritesOnlySliders()
{
    // Bundled placeholder read from the source tree (the qrc is not
    // compiled into the testlib) — also locks the shipped file's parse.
    const QString bundled = QStringLiteral(COLOR_PRESETS_DIR "/Tropical_Blue_Shallow.ucp");
    QVERIFY(QFile::exists(bundled));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString cubePath = dir.filePath(QStringLiteral("swap.cube"));
    QVERIFY(writeFile(cubePath, QByteArray(kChannelSwapCube)));

    VideoClip clip(QStringLiteral("/videos/r.mp4"));
    ColorGradeController c;
    c.setClip(&clip);
    c.setEnabled(true);

    // Anchor with EVERYTHING set — sample, placement, binding — plus a
    // grade-level LUT. The preset must move none of it.
    ColorGrade external;
    external.enabled = true;
    GradeAnchor anchor;
    anchor.params.redRecovery = 0.1;
    anchor.params.setNeutralSample(0.3, 0.5, 0.7);
    anchor.depth = 12.0;
    anchor.videoTime = 3.25;
    anchor.placed = true;
    external.anchors.append(anchor);
    external.lutPath = cubePath;
    clip.setColorGrade(external.toJson());

    QVERIFY(c.applyPreset(bundled));
    // The three sliders carry the bundled preset's values (calibrated in
    // C6 — keep in sync with color_presets/Tropical_Blue_Shallow.ucp)...
    QCOMPARE(c.redRecovery(), 0.5);
    QCOMPARE(c.waterTint(), -0.25);
    QCOMPARE(c.clarity(), 0.75);
    // ...and NOTHING else moved (regression-lock: presets store only the
    // sliders — never neutral sample, LUT, placement or binding).
    const GradeAnchor restored = c.grade().anchors.at(0);
    QVERIFY(restored.params.hasNeutralSample);
    QCOMPARE(restored.params.neutralSample[0], 0.3);
    QCOMPARE(restored.params.neutralSample[1], 0.5);
    QCOMPARE(restored.params.neutralSample[2], 0.7);
    QVERIFY(restored.isPlaced());
    QCOMPARE(restored.depth, 12.0);
    QCOMPARE(restored.videoTime, 3.25);
    QCOMPARE(c.grade().lutPath, cubePath);
    QVERIFY(c.lutEnabled());

    // Nonexistent preset: error signal, sliders unchanged.
    QSignalSpy errSpy(&c, &ColorGradeController::errorOccurred);
    QVERIFY(!c.applyPreset(dir.filePath(QStringLiteral("nope.ucp"))));
    QCOMPARE(errSpy.count(), 1);
    QCOMPARE(c.redRecovery(), 0.5);
}

void ColorGradeControllerTest::savePresetRoundTripAndCollisions()
{
    QTemporaryDir userDir;
    QVERIFY(userDir.isValid());

    VideoClip clip(QStringLiteral("/videos/s.mp4"));
    ColorGradeController c;
    c.setPresetDirectory(userDir.path()); // AppData seam
    c.setClip(&clip);
    c.setEnabled(true);
    c.setRedRecovery(0.33);
    c.setWaterTint(-0.2);
    c.setClarity(0.75);

    QSignalSpy savedSpy(&c, &ColorGradeController::presetSaved);
    QSignalSpy listSpy(&c, &ColorGradeController::presetsChanged);
    QSignalSpy errSpy(&c, &ColorGradeController::errorOccurred);

    // A display name full of characters a file name can't hold.
    const QString saved = c.savePreset(QStringLiteral("  My Test: Preset?  "));
    QVERIFY(!saved.isEmpty());
    QVERIFY(QFile::exists(saved));
    QCOMPARE(QFileInfo(saved).fileName(), QStringLiteral("My_Test__Preset_.ucp"));
    QCOMPARE(savedSpy.count(), 1);
    QCOMPARE(savedSpy.last().at(0).toString(), QStringLiteral("My Test: Preset?"));
    QVERIFY(listSpy.count() >= 1);

    // The file round-trips through parsePreset with the display name and
    // slider values intact.
    QFile file(saved);
    QVERIFY(file.open(QIODevice::ReadOnly));
    GradePreset preset;
    QString error;
    QVERIFY2(parsePreset(file.readAll(), &preset, &error), qPrintable(error));
    QCOMPARE(preset.name, QStringLiteral("My Test: Preset?"));
    QCOMPARE(preset.redRecovery, 0.33);
    QCOMPARE(preset.waterTint, -0.2);
    QCOMPARE(preset.clarity, 0.75);

    // It shows up in the preset list as a user (non-builtin) entry.
    bool found = false;
    const QVariantList entries = c.presets();
    for (const QVariant& entry : entries) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("path")).toString() == saved) {
            found = true;
            QCOMPARE(map.value(QStringLiteral("name")).toString(),
                     QStringLiteral("My Test: Preset?"));
            QVERIFY(!map.value(QStringLiteral("builtin")).toBool());
        }
    }
    QVERIFY(found);

    // Collision policy (documented in the header): a second save under
    // the same name gets a unique numeric suffix — never an overwrite.
    const QString saved2 = c.savePreset(QStringLiteral("My Test: Preset?"));
    QVERIFY(saved2 != saved);
    QCOMPARE(QFileInfo(saved2).fileName(), QStringLiteral("My_Test__Preset__2.ucp"));

    // Applying the saved preset onto the OTHER anchor moves only the
    // sliders; the fresh anchor stays unplaced and unsampled.
    c.setSelectedAnchor(1);
    QCOMPARE(c.redRecovery(), 0.0);
    QVERIFY(c.applyPreset(saved));
    QCOMPARE(c.redRecovery(), 0.33);
    QCOMPARE(c.waterTint(), -0.2);
    QCOMPARE(c.clarity(), 0.75);
    QVERIFY(!c.grade().anchors.at(1).params.hasNeutralSample);
    QVERIFY(!c.grade().anchors.at(1).isPlaced());

    // Empty name is refused with a reason.
    QVERIFY(c.savePreset(QStringLiteral("   ")).isEmpty());
    QCOMPARE(errSpy.count(), 1);
}

QTEST_GUILESS_MAIN(ColorGradeControllerTest)
#include "color_grade_controller_test.moc"
