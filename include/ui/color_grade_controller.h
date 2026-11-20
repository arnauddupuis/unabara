#ifndef COLOR_GRADE_CONTROLLER_H
#define COLOR_GRADE_CONTROLLER_H

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QVector4D>

#include <functional>

#include "include/core/color_grade.h"
#include "include/core/cube_lut.h"
#include "include/core/dive_data.h"
#include "include/core/video_clip.h"

// Bridge between the active clip's stored colour grade and the QML colour
// UI (context property `colorGrade`, v0.4 track C).
//
// It follows clipList.activeClip (main.cpp wires setClip() to the model's
// activeIndexChanged/countChanged), owns a Unabara::ColorGrade deserialized
// from the clip's opaque colorGradeMap, and writes every edit straight back
// through that map — VideoClip emits colorGradeChanged and ClipListModel's
// A3 debounce coalesces the persistence, so no extra debounce lives here.
//
// Slider properties read/write the SELECTED anchor (C3): selectedAnchor is
// the storage index [0,1] the sliders explicitly edit — never
// nearest-by-depth, which would switch targets during playback.
// recomputeUniforms() is deliberately the single place uniforms are
// derived: when the grade is depth-linked and a dive + playback sample are
// present it resolves at the current depth (resolveAtDepth), otherwise it
// falls back to the static resolve.
//
// Per-frame input: QML pushes setCurrentDiveTime(exactDiveTime,
// videoPositionSeconds) from the player while the Video tab is
// active and the grade is depth-linked (VideoSyncPlayer gates the feed, so
// hidden tabs never pay for it). The depth scan (DiveData::dataAtTime) is
// skipped entirely when not depth-linked, and a depth change under 1 cm
// (kDepthRefreshEps) skips the recompute — at 30-60 pushes/s a paused or
// flat-profile player would otherwise re-resolve and re-emit identically
// every tick.
//
// bypass (hold-to-compare), splitPos and selectedAnchor are transient
// preview/view state: never persisted (persisting the radio selection
// would churn the clip's stored grade on a mere click), reset on clip
// switch (splitPos keeps its value — it is a view preference, not clip
// state). depthLinked and the anchors persist in the grade JSON.
//
// All depth/readout strings are assembled here in C++ via Units +
// Config::unitSystem() (the project rule: no value formatting in QML).
// Config is only touched inside those text getters — never in the
// constructor — so tests that avoid the text getters stay Config-free;
// main.cpp connects Config::unitSystemChanged to refreshDepthTexts().
class ColorGradeController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool hasClip READ hasClip NOTIFY clipChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY gradeChanged)
    // Depth link (C3). depthLinked persists in the grade; selectedAnchor is
    // transient view state (see the class comment).
    Q_PROPERTY(bool depthLinked READ depthLinked WRITE setDepthLinked NOTIFY gradeChanged)
    Q_PROPERTY(int selectedAnchor READ selectedAnchor WRITE setSelectedAnchor NOTIFY selectedAnchorChanged)
    // Per-anchor UI state, exposed as scalar props (binding-friendly; a
    // list-of-two would force index lookups in QML). anchorNSet = the
    // anchor exists in storage; anchorNHasVideoTime gates "Go to";
    // anchorNDepthText is the C++-formatted depth ("14.2 m" / "46.5 ft",
    // empty until the anchor is bound to a frame); anchorNRoleText is
    // "shallow"/"deep" once both anchors span a real ramp, else empty.
    Q_PROPERTY(bool anchor0Set READ anchor0Set NOTIFY gradeChanged)
    Q_PROPERTY(bool anchor1Set READ anchor1Set NOTIFY gradeChanged)
    Q_PROPERTY(bool anchor0HasVideoTime READ anchor0HasVideoTime NOTIFY gradeChanged)
    Q_PROPERTY(bool anchor1HasVideoTime READ anchor1HasVideoTime NOTIFY gradeChanged)
    Q_PROPERTY(QString anchor0DepthText READ anchor0DepthText NOTIFY gradeChanged)
    Q_PROPERTY(QString anchor1DepthText READ anchor1DepthText NOTIFY gradeChanged)
    Q_PROPERTY(QString anchor0RoleText READ anchor0RoleText NOTIFY gradeChanged)
    Q_PROPERTY(QString anchor1RoleText READ anchor1RoleText NOTIFY gradeChanged)
    // "Now: 14.2 m — 62% toward Deep". Empty when the grade is not
    // depth-linked, no clip/dive is present, or no playback sample has
    // arrived yet. Own NOTIFY: it changes at video tick rate.
    Q_PROPERTY(QString currentDepthText READ currentDepthText NOTIFY currentDepthTextChanged)
    // Selected-anchor slider values. Ranges per GradeTuning's documented
    // slider mappings: redRecovery/clarity in [0,1], waterTint in [-1,+1]
    // (+ = toward magenta / green water, − = toward warm / blue water).
    Q_PROPERTY(double redRecovery READ redRecovery WRITE setRedRecovery NOTIFY gradeChanged)
    Q_PROPERTY(double waterTint READ waterTint WRITE setWaterTint NOTIFY gradeChanged)
    Q_PROPERTY(double clarity READ clarity WRITE setClarity NOTIFY gradeChanged)

    // Transient (not persisted).
    Q_PROPERTY(bool bypass READ bypass WRITE setBypass NOTIFY bypassChanged)
    Q_PROPERTY(double splitPos READ splitPos WRITE setSplitPos NOTIFY splitPosChanged)
    // enabled && clip && !bypass — gates the whole preview effect in QML.
    Q_PROPERTY(bool previewActive READ previewActive NOTIFY previewActiveChanged)

    // Resolved shader uniforms (one resolve() per edit, one batched
    // uniformsChanged). Names map 1:1 onto ShaderEffect properties; vec4
    // packing matches colorgrade.frag's std140 block (xyz used, w spare).
    Q_PROPERTY(QVector4D matrixR READ matrixR NOTIFY uniformsChanged)
    Q_PROPERTY(QVector4D matrixG READ matrixG NOTIFY uniformsChanged)
    Q_PROPERTY(QVector4D matrixB READ matrixB NOTIFY uniformsChanged)
    Q_PROPERTY(QVector4D blackPoint READ blackPoint NOTIFY uniformsChanged)
    Q_PROPERTY(QVector4D lift READ lift NOTIFY uniformsChanged)
    Q_PROPERTY(double contrast READ contrast NOTIFY uniformsChanged)
    Q_PROPERTY(bool lutEnabled READ lutEnabled NOTIFY uniformsChanged)
    // Edge size N of the image://lutstrip/ strip (C2: the identity default).
    Q_PROPERTY(int lutSize READ lutSize NOTIFY uniformsChanged)
    // Cache-buster for the strip Image URL; C4 bumps it on LUT import.
    Q_PROPERTY(int lutRevision READ lutRevision NOTIFY uniformsChanged)

    // ── C4: eyedropper / LUT import / presets ───────────────────────────
    // Eyedropper mode flag, transient view state shared between ColorPanel
    // (the "Pick neutral" toggle) and VideoSyncPlayer (the crosshair
    // MouseArea over the video content area). Cleared on clip switch, on
    // disabling the grade, and by a successful pick (one-click capture).
    Q_PROPERTY(bool pickingActive READ pickingActive WRITE setPickingActive
                   NOTIFY pickingActiveChanged)
    // Selected anchor's eyedropper sample (swatch next to the button).
    Q_PROPERTY(bool hasNeutralSample READ hasNeutralSample NOTIFY gradeChanged)
    Q_PROPERTY(QColor neutralSampleColor READ neutralSampleColor NOTIFY gradeChanged)
    // LUT row state. hasLut = the grade carries a lutPath; lutMissing = that
    // path failed to load (missing/unreadable file) — the panel shows a
    // warning row and lutEnabled stays false so the parametric grade still
    // applies alone. lutLabel is assembled in C++: "deep_blue.cube (33³)",
    // basename only while the file can't be read.
    Q_PROPERTY(bool hasLut READ hasLut NOTIFY gradeChanged)
    Q_PROPERTY(bool lutMissing READ lutMissing NOTIFY gradeChanged)
    Q_PROPERTY(QString lutLabel READ lutLabel NOTIFY gradeChanged)
    // Preset list for the panel's ComboBox: QVariantMaps of
    // {name, path, builtin} — bundled qrc presets first, then the user's
    // AppDataLocation/color_presets files, both sorted by file name.
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)

public:
    explicit ColorGradeController(QObject* parent = nullptr);

    // Follow the clip model's active clip. Null detaches (disabled state).
    // Reloads the grade from the clip's colorGradeMap and re-emits
    // everything; a repeated call with the same clip is a no-op.
    void setClip(VideoClip* clip);
    VideoClip* clip() const { return m_clip; }

    // Follow the active dive (main.cpp wires it from currentDiveChanged,
    // same pattern as ClipListModel::setDive). Null detaches: the depth
    // link falls back to the static resolve.
    void setDive(DiveData* dive);
    DiveData* dive() const { return m_dive; }

    bool hasClip() const { return m_clip != nullptr; }
    bool enabled() const { return m_grade.enabled; }
    void setEnabled(bool enabled);
    bool depthLinked() const { return m_grade.depthLinked; }
    void setDepthLinked(bool linked);
    int selectedAnchor() const { return m_selectedAnchor; }
    void setSelectedAnchor(int index);
    bool anchor0Set() const { return anchorSet(0); }
    bool anchor1Set() const { return anchorSet(1); }
    bool anchor0HasVideoTime() const { return anchorHasVideoTime(0); }
    bool anchor1HasVideoTime() const { return anchorHasVideoTime(1); }
    QString anchor0DepthText() const { return anchorDepthText(0); }
    QString anchor1DepthText() const { return anchorDepthText(1); }
    QString anchor0RoleText() const { return anchorRoleText(0); }
    QString anchor1RoleText() const { return anchorRoleText(1); }
    QString currentDepthText() const;
    double redRecovery() const;
    void setRedRecovery(double value);
    double waterTint() const;
    void setWaterTint(double value);
    double clarity() const;
    void setClarity(double value);

    bool bypass() const { return m_bypass; }
    void setBypass(bool bypass);
    double splitPos() const { return m_splitPos; }
    void setSplitPos(double pos);
    bool previewActive() const { return m_grade.enabled && m_clip && !m_bypass; }

    QVector4D matrixR() const { return rowVec(0); }
    QVector4D matrixG() const { return rowVec(1); }
    QVector4D matrixB() const { return rowVec(2); }
    QVector4D blackPoint() const
    {
        return QVector4D(float(m_ops.black[0]), float(m_ops.black[1]),
                         float(m_ops.black[2]), 0.0f);
    }
    QVector4D lift() const
    {
        return QVector4D(float(m_ops.lift[0]), float(m_ops.lift[1]),
                         float(m_ops.lift[2]), 0.0f);
    }
    double contrast() const { return m_ops.contrast; }
    // False while the LUT file is missing/unreadable: the shader must not
    // sample a stale strip — the parametric grade applies alone (the
    // plan's "missing file → warning row, grade still applies").
    bool lutEnabled() const { return !m_grade.lutPath.isEmpty() && !m_lutMissing; }
    int lutSize() const { return m_lutSize; }
    int lutRevision() const { return m_lutRevision; }

    bool pickingActive() const { return m_pickingActive; }
    void setPickingActive(bool active);
    bool hasNeutralSample() const { return selectedParams().hasNeutralSample; }
    QColor neutralSampleColor() const;
    bool hasLut() const { return !m_grade.lutPath.isEmpty(); }
    bool lutMissing() const { return m_lutMissing; }
    QString lutLabel() const;
    QVariantList presets() const { return m_presets; }

    // Reset the selected anchor's three sliders to 0 (the plan's reset()
    // invokable; neutral sample and LUT are C4's concern and are kept).
    Q_INVOKABLE void reset();

    // Per-frame playback sample (C3): the current frame's dive-time and
    // raw player position in seconds, pushed by VideoSyncPlayer while its
    // depth-link feed is active. Stores both, then — only when the depth
    // link is live — scans the dive once (dataAtTime) and recomputes the
    // uniforms, with the 1 cm epsilon skip described in the class comment.
    Q_INVOKABLE void setCurrentDiveTime(double diveTime, double videoTimeSeconds);

    // Bind anchor i's depth + videoTime to the last playback sample. The
    // anchor's PARAMS are untouched (re-anchoring keeps the grade); an
    // anchor that doesn't exist yet is created with identity params —
    // exactly what the sliders show for a never-edited anchor. The panel's
    // flow: select the anchor (radio) → "Set from current frame" to bind
    // where it applies → grade it with the sliders. Binding FIRST matters:
    // an unplaced anchor sits out of the ramp (GradeAnchor::isPlaced), so
    // pre-Set slider moves on a second anchor won't preview-blend; once
    // bound at the current depth its weight is ≈1 right there and the
    // sliders give live feedback.
    Q_INVOKABLE void setAnchorFromCurrentFrame(int index);

    // Ask the player to seek to anchor i's bound video time (emits
    // seekRequested; VideoSyncPlayer connects it). No-op when unbound.
    Q_INVOKABLE void goToAnchor(int index);

    // Remove anchor i entirely (params + binding). Removing index 0 while
    // two exist shifts the other anchor into row 1 — storage order, like
    // the rows, is not role order. selectedAnchor stays put (it is a row
    // selection and every row remains editable).
    Q_INVOKABLE void clearAnchor(int index);

    // Re-emit the depth-text properties; main.cpp connects this to
    // Config::unitSystemChanged (kept out of the constructor so the
    // controller stays Config-free until a text getter runs).
    Q_INVOKABLE void refreshDepthTexts();

    // ── C4 invokables ───────────────────────────────────────────────────

    // Eyedropper: sample the CURRENT raw video frame (videoSink must be the
    // VideoOutput's QVideoSink — QVideoFrame::toImage() gives native-
    // resolution ungraded pixels; grabToImage would return graded/scaled
    // ones). nx/ny are the click normalized to the video CONTENT area
    // (0..1 across the frame). Takes the per-channel MEDIAN of a 9×9
    // window (clamped at frame edges) and stores it as the selected
    // anchor's neutral sample — a params edit only: placement, depth and
    // videoTime are untouched, and a missing anchor is created UNPLACED
    // (same as a slider edit). A null/unmappable frame (playing video on
    // HW-decode paths) emits pickFailed and returns false, keeping the
    // pick mode armed so the user can pause and click again; success
    // clears pickingActive (one-click capture).
    Q_INVOKABLE bool pickNeutral(QObject* videoSink, double nx, double ny);
    // Clear the selected anchor's neutral sample (removes its WB).
    Q_INVOKABLE void clearNeutralSample();

    // LUT import: parse the .cube (3D only — CubeLut's error message is
    // forwarded through errorOccurred on failure, state untouched), store
    // its absolute path in the grade, and bake the Hald strip for the
    // shader (strip N is always the provider's 33 — sampling interpolates
    // whatever the LUT's own size is). GUI thread only (strip sink).
    Q_INVOKABLE bool importLut(const QUrl& url);
    // Drop the LUT: clears lutPath and resets the strip to identity.
    Q_INVOKABLE void clearLut();

    // Presets (.ucp, sliders only — never neutral sample, LUT, placement
    // or anchors): apply writes the preset's three slider values into the
    // selected anchor; everything else on the anchor is untouched.
    Q_INVOKABLE bool applyPreset(const QString& path);
    // Save the selected anchor's three sliders as a user preset. The file
    // name is the sanitized display name; a COLLISION GETS A UNIQUE
    // NUMERIC SUFFIX ("_2", "_3", …) — non-destructive by design, no
    // overwrite prompt. Returns the saved path, empty on failure
    // (errorOccurred carries the reason).
    Q_INVOKABLE QString savePreset(const QString& name);
    // Re-scan bundled (qrc) + user preset directories into `presets`.
    Q_INVOKABLE void refreshPresets();

    // Strip hand-off seam: main.cpp installs a lambda forwarding to
    // LutStripImageProvider::setStrip (a null image means identity).
    // Keeps the controller — and the testlib — Quick-free; installing the
    // sink immediately pushes the current strip state, so wiring order
    // against the first clip load doesn't matter.
    void setLutStripSink(std::function<void(const QImage&)> sink);

    // Test seam: overrides the user preset directory (default:
    // AppDataLocation/color_presets, created on demand by savePreset).
    void setPresetDirectory(const QString& dir);

    // Pure helpers, public for tests.
    // Per-channel median of a kPickWindow×kPickWindow window centred on
    // (nx·w, ny·h), clamped to the image bounds; invalid QColor on a null
    // image.
    static QColor medianSample(const QImage& image, double nx, double ny);
    // Bake a user LUT into the shader's Hald strip. Layout MUST match
    // LutStripImageProvider::identityStrip() and sampleLutStrip() in
    // colorgrade.frag: N slices of N×N side by side, pixel (b·N + r, g) =
    // lut.sample(r,g,b lattice), outputs clamped to [0,1].
    static QImage bakeLutStrip(const Unabara::CubeLut& lut, int n = 33);

    // Test/C3 seam: the grade as currently held (not re-read from the clip).
    const Unabara::ColorGrade& grade() const { return m_grade; }

signals:
    void clipChanged();
    void gradeChanged();
    void bypassChanged();
    void splitPosChanged();
    void previewActiveChanged();
    void uniformsChanged();
    void selectedAnchorChanged();
    void currentDepthTextChanged();
    // "Go to" on an anchor row: QML seeks the player to this clip-local
    // position (seconds).
    void seekRequested(double videoTimeSeconds);

    // C4: eyedropper mode toggles (panel button ↔ player MouseArea).
    void pickingActiveChanged();
    // The eyedropper could not read a frame — main.qml toasts the reason.
    void pickFailed(const QString& reason);
    // LUT import / preset file errors — main.qml toasts the message.
    void errorOccurred(const QString& message);
    // A user preset was written (main.qml toasts a confirmation).
    void presetSaved(const QString& name);
    void presetsChanged();

    // The playback sample was reset by a clip/dive switch: the QML feed
    // should push a fresh (exactDiveTime, videoPositionSeconds) pair if
    // the depth link is active. Neither the feed gate's boolean value nor
    // exactDiveTime necessarily changes across a switch, so the feed
    // cannot rely on its own triggers for this.
    void playbackSampleInvalidated();

private:
    // Depth change below this (metres) skips the per-tick recompute.
    static constexpr double kDepthRefreshEps = 0.01; // 1 cm
    // Eyedropper sampling window edge (the plan's 9×9 median).
    static constexpr int kPickWindow = 9;

    QVector4D rowVec(int row) const
    {
        return QVector4D(float(m_ops.matrix[row][0]), float(m_ops.matrix[row][1]),
                         float(m_ops.matrix[row][2]), 0.0f);
    }

    // Selected anchor's params; a default-constructed set when none exists
    // yet (identity sliders).
    Unabara::GradeParams selectedParams() const;
    // Grows anchors so the selected index exists (first edit creates it).
    Unabara::GradeParams& ensureSelectedAnchor();
    void loadFromClip();
    void writeBack();
    // THE single place uniforms are derived (C3 swaps the resolve call for
    // resolveAtDepth(currentDepth) here). One resolve, one batched emit.
    void recomputeUniforms();
    void emitPreviewActiveIfChanged();
    // True when resolveAtDepth() should drive the uniforms at all.
    bool depthLinkActive() const { return m_grade.depthLinked && m_clip && m_dive; }
    bool anchorSet(int index) const { return index >= 0 && index < m_grade.anchors.size(); }
    bool anchorHasVideoTime(int index) const
    {
        return anchorSet(index) && m_grade.anchors.at(index).videoTime >= 0.0;
    }
    QString anchorDepthText(int index) const;
    QString anchorRoleText(int index) const;
    // Re-parse + re-bake (or reset) the strip from m_grade.lutPath; sets
    // m_lutMissing / m_lutFileSize. Called on every grade (re)load and by
    // importLut/clearLut through pushStrip.
    void refreshLutStrip();
    // Hand a strip to the provider sink (null = identity) and bump the
    // revision so the QML Image re-requests.
    void pushStrip(const QImage& strip);
    QString userPresetDir() const;

    QPointer<VideoClip> m_clip;
    QPointer<DiveData> m_dive;
    Unabara::ColorGrade m_grade;
    int m_selectedAnchor = 0; // which anchor the sliders edit (transient)
    Unabara::GradeOps m_ops;
    // Last playback sample from the QML feed; depth is scanned lazily from
    // it only while the depth link is live.
    double m_currentDiveTime = 0.0;
    double m_currentVideoTime = -1.0;
    bool m_hasPlaybackSample = false;
    double m_currentDepth = 0.0;
    bool m_hasCurrentDepth = false;
    bool m_bypass = false;
    double m_splitPos = 1.0; // 1.0 = fully graded (graded left of split)
    int m_lutSize = 33;      // LutStripImageProvider::kDefaultSize
    int m_lutRevision = 0;
    bool m_writingBack = false;
    bool m_lastPreviewActive = false;
    // C4 state.
    bool m_pickingActive = false; // transient, like bypass
    bool m_lutMissing = false;    // lutPath set but unreadable/missing
    int m_lutFileSize = 0;        // the .cube's own N, for the label
    QImage m_bakedStrip;          // last pushed strip; null = identity
    std::function<void(const QImage&)> m_stripSink;
    QVariantList m_presets;
    QString m_presetDirOverride; // test seam; empty = AppDataLocation
};

#endif // COLOR_GRADE_CONTROLLER_H
