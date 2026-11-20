#include "include/ui/color_grade_controller.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVideoFrame>
#include <QVideoSink>

#include <algorithm>
#include <cmath>
#include <vector>

#include "include/core/config.h"
#include "include/core/units.h"

using Unabara::ColorGrade;
using Unabara::CubeLut;
using Unabara::GradeAnchor;
using Unabara::GradeOps;
using Unabara::GradeParams;
using Unabara::GradePreset;

namespace {

inline double clampRange(double x, double lo, double hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

} // namespace

ColorGradeController::ColorGradeController(QObject* parent)
    : QObject(parent)
{
    // Start on identity uniforms so the shader renders a no-op even if the
    // effect were ever active before the first clip arrives.
    m_ops = GradeOps();
    // Read-only directory scans (qrc + AppData), deliberately Config-free
    // like the rest of the constructor.
    refreshPresets();
}

void ColorGradeController::setClip(VideoClip* clip)
{
    if (clip == m_clip)
        return;

    if (m_clip)
        disconnect(m_clip, nullptr, this, nullptr);

    m_clip = clip;

    // Transient hold-to-compare state must not carry across clips: a switch
    // landing while the button is held would otherwise start the new clip
    // bypassed until the next press/release.
    if (m_bypass) {
        m_bypass = false;
        emit bypassChanged();
    }

    // Eyedropper mode is per-clip transient state too: an armed crosshair
    // must not survive onto a different clip's frames.
    setPickingActive(false);

    // The last playback sample was taken against the old clip's sync; a
    // "Set from current frame" must not bind its stale video time to the
    // new clip. playbackSampleInvalidated (emitted below, after the
    // reload) asks the QML feed for a fresh sample — the gate's boolean
    // and exactDiveTime may both be unchanged across a clip swap, so
    // neither can be relied on to re-push.
    m_hasPlaybackSample = false;
    m_hasCurrentDepth = false;

    if (m_clip) {
        // External changes to the stored grade (A3 restore re-writing the
        // map, future preset application from elsewhere) reload the
        // controller; our own write-backs are guarded out.
        connect(m_clip, &VideoClip::colorGradeChanged, this, [this]() {
            if (!m_writingBack)
                loadFromClip();
        });
    }

    loadFromClip();
    emit clipChanged();
    emit playbackSampleInvalidated();
}

void ColorGradeController::setDive(DiveData* dive)
{
    if (dive == m_dive)
        return;
    m_dive = dive;
    // Depth samples taken against the old dive are meaningless against
    // the new one; ask the feed for a fresh one (see setClip).
    m_hasCurrentDepth = false;
    m_hasPlaybackSample = false;
    recomputeUniforms();
    emit currentDepthTextChanged();
    emit playbackSampleInvalidated();
}

void ColorGradeController::loadFromClip()
{
    m_grade = m_clip ? ColorGrade::fromJson(m_clip->colorGrade()) : ColorGrade();
    if (m_selectedAnchor != 0) {
        m_selectedAnchor = 0;
        emit selectedAnchorChanged();
    }
    // Per-clip LUT: the strip provider is global, so every grade (re)load
    // re-bakes the new clip's LUT — or resets to identity — before the
    // NOTIFYs below let QML read lutEnabled/lutLabel/lutMissing.
    refreshLutStrip();
    emit gradeChanged();
    recomputeUniforms();
    emit currentDepthTextChanged();
    emitPreviewActiveIfChanged();
}

GradeParams ColorGradeController::selectedParams() const
{
    if (m_selectedAnchor < m_grade.anchors.size())
        return m_grade.anchors.at(m_selectedAnchor).params;
    return GradeParams();
}

GradeParams& ColorGradeController::ensureSelectedAnchor()
{
    while (m_grade.anchors.size() <= m_selectedAnchor) {
        // Slider-created anchors start UNPLACED: they sit out of the depth
        // ramp (no phantom 0 m shallow anchor) until "Set from current
        // frame" binds and places them. The ONLY unplaced-creation site.
        GradeAnchor anchor;
        anchor.placed = false;
        m_grade.anchors.append(anchor);
    }
    return m_grade.anchors[m_selectedAnchor].params;
}

void ColorGradeController::writeBack()
{
    if (!m_clip)
        return;
    m_writingBack = true;
    // Through the opaque map: VideoClip emits colorGradeChanged and the
    // clip model's A3 debounce persists it — no saveConfig(), no extra
    // debounce here.
    m_clip->setColorGrade(m_grade.toJson());
    m_writingBack = false;
}

void ColorGradeController::setEnabled(bool enabled)
{
    if (!m_clip || m_grade.enabled == enabled)
        return;
    m_grade.enabled = enabled;
    writeBack();
    emit gradeChanged();
    emitPreviewActiveIfChanged();
    // Disabling the grade disarms a pending eyedropper pick: the crosshair
    // overlay would otherwise sit over a video the grade no longer touches.
    if (!enabled)
        setPickingActive(false);
}

void ColorGradeController::setDepthLinked(bool linked)
{
    if (!m_clip || m_grade.depthLinked == linked)
        return;
    m_grade.depthLinked = linked;
    writeBack();
    // Unlinking hides/disables the anchor rows and every resolve falls back
    // to anchor 0 (resolveStatic) — a selection left on anchor 1 would make
    // every slider/preset/neutral edit a silent no-op written into anchor 1.
    // Retarget to anchor 0 BEFORE gradeChanged so the sliders re-read it.
    if (!linked && m_selectedAnchor != 0) {
        m_selectedAnchor = 0;
        emit selectedAnchorChanged();
    }
    emit gradeChanged();
    // The resolve mode flips between static and at-depth; the stale depth
    // sample (if any) is refreshed by the QML feed, whose gate reacts to
    // this very property change in the same event turn.
    recomputeUniforms();
    emit currentDepthTextChanged();
}

void ColorGradeController::setSelectedAnchor(int index)
{
    // Transient view state (see the class comment): no clip guard and no
    // write-back — selecting a row must never churn the stored grade.
    const int clamped = qBound(0, index, 1);
    if (clamped == m_selectedAnchor)
        return;
    m_selectedAnchor = clamped;
    emit selectedAnchorChanged();
    // Retarget the sliders: their values share the gradeChanged NOTIFY.
    emit gradeChanged();
    // No recomputeUniforms(): the uniforms depend on the anchors, never on
    // which one the sliders are pointed at.
}

double ColorGradeController::redRecovery() const
{
    return selectedParams().redRecovery;
}

void ColorGradeController::setRedRecovery(double value)
{
    value = clampRange(value, 0.0, 1.0);
    if (!m_clip || qFuzzyCompare(1.0 + redRecovery(), 1.0 + value))
        return;
    ensureSelectedAnchor().redRecovery = value;
    writeBack();
    emit gradeChanged();
    recomputeUniforms();
    // Anchor edits can reshape the ramp (e.g. once the anchor is placed),
    // which moves the readout at a constant depth — keep its NOTIFY
    // complete (cheap getter; the Label dedups identical strings).
    emit currentDepthTextChanged();
}

double ColorGradeController::waterTint() const
{
    return selectedParams().waterTint;
}

void ColorGradeController::setWaterTint(double value)
{
    value = clampRange(value, -1.0, 1.0);
    if (!m_clip || qFuzzyCompare(1.0 + waterTint(), 1.0 + value))
        return;
    ensureSelectedAnchor().waterTint = value;
    writeBack();
    emit gradeChanged();
    recomputeUniforms();
    // Anchor edits can reshape the ramp (e.g. once the anchor is placed),
    // which moves the readout at a constant depth — keep its NOTIFY
    // complete (cheap getter; the Label dedups identical strings).
    emit currentDepthTextChanged();
}

double ColorGradeController::clarity() const
{
    return selectedParams().clarity;
}

void ColorGradeController::setClarity(double value)
{
    value = clampRange(value, 0.0, 1.0);
    if (!m_clip || qFuzzyCompare(1.0 + clarity(), 1.0 + value))
        return;
    ensureSelectedAnchor().clarity = value;
    writeBack();
    emit gradeChanged();
    recomputeUniforms();
    // Anchor edits can reshape the ramp (e.g. once the anchor is placed),
    // which moves the readout at a constant depth — keep its NOTIFY
    // complete (cheap getter; the Label dedups identical strings).
    emit currentDepthTextChanged();
}

void ColorGradeController::setBypass(bool bypass)
{
    if (m_bypass == bypass)
        return;
    m_bypass = bypass;
    emit bypassChanged();
    emitPreviewActiveIfChanged();
}

void ColorGradeController::setSplitPos(double pos)
{
    pos = clampRange(pos, 0.0, 1.0);
    if (qFuzzyCompare(1.0 + m_splitPos, 1.0 + pos))
        return;
    m_splitPos = pos;
    emit splitPosChanged();
}

void ColorGradeController::reset()
{
    if (!m_clip || m_selectedAnchor >= m_grade.anchors.size())
        return;
    GradeParams& params = m_grade.anchors[m_selectedAnchor].params;
    if (params.redRecovery == 0.0 && params.waterTint == 0.0 && params.clarity == 0.0)
        return;
    params.redRecovery = 0.0;
    params.waterTint = 0.0;
    params.clarity = 0.0;
    writeBack();
    emit gradeChanged();
    recomputeUniforms();
}

void ColorGradeController::setCurrentDiveTime(double diveTime, double videoTimeSeconds)
{
    // Always store the sample (it is what setAnchorFromCurrentFrame
    // captures), but pay for the dive scan + resolve only while the depth
    // link is actually driving the uniforms.
    m_currentDiveTime = diveTime;
    m_currentVideoTime = videoTimeSeconds;
    m_hasPlaybackSample = true;
    if (!depthLinkActive())
        return;

    const double depth = m_dive->dataAtTime(diveTime).depth;
    // Epsilon skip: under 1 cm neither the resolve nor the readout changes
    // perceptibly, and this runs at video tick rate (30-60/s) — a paused
    // or flat-profile player must not re-emit identical uniforms per tick.
    if (m_hasCurrentDepth && std::abs(depth - m_currentDepth) < kDepthRefreshEps)
        return;
    m_currentDepth = depth;
    m_hasCurrentDepth = true;
    recomputeUniforms();
    emit currentDepthTextChanged();
}

void ColorGradeController::setAnchorFromCurrentFrame(int index)
{
    if (!m_clip || !m_dive || index < 0 || index > 1)
        return;
    if (!m_hasPlaybackSample || m_currentVideoTime < 0.0)
        return; // no frame to bind to yet (feed not active)

    // Grow to the index if needed: a brand-new anchor gets identity params
    // — exactly what the sliders display for a never-edited anchor. An
    // existing anchor keeps its params: this binds WHERE the grade
    // applies, it never regrades. Growth anchors start UNPLACED (binding
    // anchor 2 first must not spawn a placed identity anchor 1 at 0 m);
    // only anchors[index] is placed below.
    while (m_grade.anchors.size() <= index) {
        GradeAnchor grown;
        grown.placed = false;
        m_grade.anchors.append(grown);
    }
    GradeAnchor& anchor = m_grade.anchors[index];
    anchor.depth = m_dive->dataAtTime(m_currentDiveTime).depth;
    anchor.videoTime = m_currentVideoTime;
    anchor.placed = true; // joins the depth ramp from here on

    writeBack();
    emit gradeChanged();
    // Depth roles/ramp may have changed, so the blend at the current depth
    // (and the readout percent) can too.
    recomputeUniforms();
    emit currentDepthTextChanged();
}

void ColorGradeController::goToAnchor(int index)
{
    if (!anchorHasVideoTime(index))
        return;
    emit seekRequested(m_grade.anchors.at(index).videoTime);
}

void ColorGradeController::clearAnchor(int index)
{
    if (!m_clip || !anchorSet(index))
        return;
    m_grade.anchors.remove(index);
    writeBack();
    emit gradeChanged();
    recomputeUniforms();
    emit currentDepthTextChanged();
}

void ColorGradeController::refreshDepthTexts()
{
    // Unit system changed: every formatted depth string is stale. The
    // sliders re-read identical values off gradeChanged — harmless.
    emit gradeChanged();
    emit currentDepthTextChanged();
}

QString ColorGradeController::anchorDepthText(int index) const
{
    if (!anchorSet(index))
        return QString();
    const GradeAnchor& anchor = m_grade.anchors.at(index);
    // An anchor exists as soon as its sliders are touched; its depth only
    // means something once the anchor is PLACED (GradeAnchor::isPlaced —
    // the same heuristic that gates ramp participation, so this display
    // can never disagree with the blend).
    if (!anchor.isPlaced())
        return QString();
    return Units::formatDepthValue(anchor.depth, Config::instance()->unitSystem());
}

QString ColorGradeController::anchorRoleText(int index) const
{
    if (!anchorSet(index) || !m_grade.hasDepthRamp())
        return QString();
    const bool isDeep =
        m_grade.anchors.at(index).depth > m_grade.anchors.at(1 - index).depth;
    return isDeep ? tr("deep") : tr("shallow");
}

QString ColorGradeController::currentDepthText() const
{
    if (!depthLinkActive() || !m_hasCurrentDepth)
        return QString();
    const QString depthStr =
        Units::formatDepthValue(m_currentDepth, Config::instance()->unitSystem());
    // The percent is ColorGrade's own ramp weight — the readout can never
    // disagree with what resolveAtDepth() actually blended.
    const double weight = m_grade.rampWeightAt(m_currentDepth);
    if (weight < 0.0)
        return tr("Now: %1").arg(depthStr);
    return tr("Now: %1 — %2% toward Deep").arg(depthStr).arg(qRound(weight * 100.0));
}

void ColorGradeController::recomputeUniforms()
{
    // THE single place uniforms are derived. Depth-linked with a live dive
    // and a playback sample → resolve at the current depth; anything else
    // (link off, no dive yet, no sample yet) → the static resolve.
    if (depthLinkActive() && m_hasCurrentDepth)
        m_ops = m_grade.resolveAtDepth(m_currentDepth);
    else
        m_ops = m_grade.resolveStatic();
    emit uniformsChanged();
}

void ColorGradeController::emitPreviewActiveIfChanged()
{
    const bool active = previewActive();
    if (active != m_lastPreviewActive) {
        m_lastPreviewActive = active;
        emit previewActiveChanged();
    }
}

// ── C4: eyedropper ──────────────────────────────────────────────────────

void ColorGradeController::setPickingActive(bool active)
{
    if (m_pickingActive == active)
        return;
    // Arming needs a clip (there is nothing to pick on); disarming is
    // always allowed (clip switch / disable paths call this with false).
    if (active && !m_clip)
        return;
    m_pickingActive = active;
    emit pickingActiveChanged();
}

QColor ColorGradeController::neutralSampleColor() const
{
    const GradeParams params = selectedParams();
    if (!params.hasNeutralSample)
        return QColor();
    return QColor::fromRgbF(float(params.neutralSample[0]),
                            float(params.neutralSample[1]),
                            float(params.neutralSample[2]));
}

QColor ColorGradeController::medianSample(const QImage& image, double nx, double ny)
{
    if (image.isNull())
        return QColor();
    const int w = image.width();
    const int h = image.height();
    const int px = qBound(0, int(nx * w), w - 1);
    const int py = qBound(0, int(ny * h), h - 1);
    const int half = kPickWindow / 2;
    const int x0 = qMax(0, px - half);
    const int x1 = qMin(w - 1, px + half);
    const int y0 = qMax(0, py - half);
    const int y1 = qMin(h - 1, py + half);

    std::vector<int> r, g, b;
    const size_t count = size_t(x1 - x0 + 1) * size_t(y1 - y0 + 1);
    r.reserve(count);
    g.reserve(count);
    b.reserve(count);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const QColor c = image.pixelColor(x, y);
            r.push_back(c.red());
            g.push_back(c.green());
            b.push_back(c.blue());
        }
    }
    // Per-channel MEDIAN, not mean: a few specular/noise outliers in the
    // window must not drag the white balance.
    const auto median = [](std::vector<int>& v) {
        const size_t mid = v.size() / 2;
        std::nth_element(v.begin(), v.begin() + ptrdiff_t(mid), v.end());
        return v[mid];
    };
    return QColor(median(r), median(g), median(b));
}

bool ColorGradeController::pickNeutral(QObject* videoSink, double nx, double ny)
{
    if (!m_clip)
        return false;

    // The raw frame comes from the sink, NOT grabToImage: grabToImage
    // would return graded/scaled pixels, and the WB math needs the
    // native-resolution originals.
    QImage image;
    if (auto* sink = qobject_cast<QVideoSink*>(videoSink)) {
        const QVideoFrame frame = sink->videoFrame();
        if (frame.isValid())
            image = frame.toImage();
    }
    if (image.isNull()) {
        // HW-decode paths can hand back frames toImage() cannot map,
        // especially mid-playback. Keep pickingActive armed: the user
        // pauses (a software-mapped frame lands) and clicks again.
        emit pickFailed(tr("Could not read the video frame — pause the video and try again."));
        return false;
    }

    const QColor sample = medianSample(image, nx, ny);
    if (!sample.isValid()) {
        emit pickFailed(tr("Could not read the video frame — pause the video and try again."));
        return false;
    }

    // Params edit ONLY: placement/depth/videoTime are untouched, and a
    // missing anchor is created unplaced exactly like a slider edit would.
    GradeParams& params = ensureSelectedAnchor();
    params.setNeutralSample(sample.red() / 255.0, sample.green() / 255.0,
                            sample.blue() / 255.0);
    setPickingActive(false); // one-click capture
    writeBack();
    emit gradeChanged();
    recomputeUniforms(); // resolve() derives the WB gains from the sample
    emit currentDepthTextChanged();
    return true;
}

void ColorGradeController::clearNeutralSample()
{
    if (!m_clip || m_selectedAnchor >= m_grade.anchors.size())
        return;
    GradeParams& params = m_grade.anchors[m_selectedAnchor].params;
    if (!params.hasNeutralSample)
        return;
    params.clearNeutralSample();
    writeBack();
    emit gradeChanged();
    recomputeUniforms();
    emit currentDepthTextChanged();
}

// ── C4: LUT import ──────────────────────────────────────────────────────

QImage ColorGradeController::bakeLutStrip(const CubeLut& lut, int n)
{
    // Layout MUST match LutStripImageProvider::identityStrip() and the
    // shader's sampleLutStrip(): the image is n²×n, pixel (b·n + r, g)
    // holds the LUT output for lattice input (r, g, b) — red fastest
    // horizontally inside a slice, green down, blue = slice index. The
    // strip is always baked at the provider's N (33) regardless of the
    // LUT's own size; sample() interpolates.
    QImage img(n * n, n, QImage::Format_RGB32);
    const double step = 1.0 / (n - 1);
    for (int b = 0; b < n; ++b) {
        const int xBase = b * n;
        for (int g = 0; g < n; ++g) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(g)) + xBase;
            for (int r = 0; r < n; ++r) {
                double outR = 0.0, outG = 0.0, outB = 0.0;
                lut.sample(r * step, g * step, b * step, outR, outG, outB);
                line[r] = qRgb(
                    int(std::lround(clampRange(outR, 0.0, 1.0) * 255.0)),
                    int(std::lround(clampRange(outG, 0.0, 1.0) * 255.0)),
                    int(std::lround(clampRange(outB, 0.0, 1.0) * 255.0)));
            }
        }
    }
    return img;
}

void ColorGradeController::setLutStripSink(std::function<void(const QImage&)> sink)
{
    m_stripSink = std::move(sink);
    // Push the current state immediately so install order (controller may
    // have loaded a clip grade before the provider existed) doesn't matter.
    if (m_stripSink)
        m_stripSink(m_bakedStrip);
}

void ColorGradeController::pushStrip(const QImage& strip)
{
    m_bakedStrip = strip;
    ++m_lutRevision; // busts the QML Image URL on the next uniformsChanged
    if (m_stripSink)
        m_stripSink(strip);
}

// Re-check boundary: lutPath existence is (re)tested only here — on grade
// load (clip switch, external write) and after importLut(). A missing
// .cube that reappears on disk while the same clip stays active is NOT
// re-detected live; the warning row tells the user to re-import or switch
// clips (deliberate: no filesystem watcher for this).
void ColorGradeController::refreshLutStrip()
{
    m_lutMissing = false;
    m_lutFileSize = 0;
    QImage strip; // null = identity
    if (!m_grade.lutPath.isEmpty()) {
        QString error;
        const CubeLut lut = CubeLut::parseFile(m_grade.lutPath, &error);
        if (lut.isValid()) {
            m_lutFileSize = lut.size();
            strip = bakeLutStrip(lut);
        } else {
            // Missing or unreadable: warning row, grade applies without
            // the LUT (lutEnabled() gates on m_lutMissing). Not an error
            // dialog — this is the restore path, not a user action.
            m_lutMissing = true;
            qWarning() << "ColorGrade: user LUT unavailable, grading without it:" << error;
        }
    }
    pushStrip(strip);
}

QString ColorGradeController::lutLabel() const
{
    if (m_grade.lutPath.isEmpty())
        return QString();
    const QString name = QFileInfo(m_grade.lutPath).fileName();
    if (m_lutFileSize > 0)
        return QStringLiteral("%1 (%2³)").arg(name).arg(m_lutFileSize);
    return name; // size unknown while the file can't be read
}

bool ColorGradeController::importLut(const QUrl& url)
{
    if (!m_clip)
        return false;
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QString error;
    const CubeLut lut = CubeLut::parseFile(path, &error);
    if (!lut.isValid()) {
        // Parse error → message, state untouched (previous LUT keeps
        // applying).
        emit errorOccurred(error);
        return false;
    }
    m_grade.lutPath = QFileInfo(path).absoluteFilePath();
    m_lutMissing = false;
    m_lutFileSize = lut.size();
    pushStrip(bakeLutStrip(lut));
    writeBack();
    emit gradeChanged();
    // The ops are unchanged — only lutEnabled/lutRevision (which share the
    // uniforms NOTIFY) moved, so no recompute.
    emit uniformsChanged();
    return true;
}

void ColorGradeController::clearLut()
{
    if (!m_clip || m_grade.lutPath.isEmpty())
        return;
    m_grade.lutPath.clear();
    m_lutMissing = false;
    m_lutFileSize = 0;
    pushStrip(QImage()); // identity
    writeBack();
    emit gradeChanged();
    emit uniformsChanged();
}

// ── C4: presets ─────────────────────────────────────────────────────────

QString ColorGradeController::userPresetDir() const
{
    if (!m_presetDirOverride.isEmpty())
        return m_presetDirOverride;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/color_presets");
}

void ColorGradeController::setPresetDirectory(const QString& dir)
{
    m_presetDirOverride = dir;
    refreshPresets();
}

void ColorGradeController::refreshPresets()
{
    QVariantList list;
    const auto appendDir = [&list](const QString& dirPath, bool builtin) {
        const QFileInfoList files = QDir(dirPath).entryInfoList(
            {QStringLiteral("*.ucp")}, QDir::Files, QDir::Name);
        for (const QFileInfo& info : files) {
            QFile file(info.filePath());
            if (!file.open(QIODevice::ReadOnly))
                continue;
            GradePreset preset;
            QString error;
            if (!Unabara::parsePreset(file.readAll(), &preset, &error)) {
                qWarning() << "ColorGrade: skipping malformed preset"
                           << info.filePath() << "-" << error;
                continue;
            }
            QVariantMap entry;
            entry[QStringLiteral("name")] = preset.name;
            entry[QStringLiteral("path")] = info.filePath();
            entry[QStringLiteral("builtin")] = builtin;
            list.append(entry);
        }
    };
    appendDir(QStringLiteral(":/color_presets"), true);
    appendDir(userPresetDir(), false);
    if (list != m_presets) {
        m_presets = list;
        emit presetsChanged();
    }
}

bool ColorGradeController::applyPreset(const QString& path)
{
    if (!m_clip)
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(tr("Could not open preset: %1").arg(path));
        return false;
    }
    GradePreset preset;
    QString error;
    if (!Unabara::parsePreset(file.readAll(), &preset, &error)) {
        emit errorOccurred(error);
        return false;
    }
    // Presets store ONLY the three slider values; the anchor's neutral
    // sample, placement, depth/videoTime binding and the grade's LUT are
    // all untouched (plan: "presets are water-type looks, the depth comes
    // from telemetry").
    GradeParams& params = ensureSelectedAnchor();
    params.redRecovery = preset.redRecovery;
    params.waterTint = preset.waterTint;
    params.clarity = preset.clarity;
    writeBack();
    emit gradeChanged();
    recomputeUniforms();
    emit currentDepthTextChanged();
    return true;
}

QString ColorGradeController::savePreset(const QString& name)
{
    if (!m_clip)
        return QString();
    const QString displayName = name.trimmed();
    if (displayName.isEmpty()) {
        emit errorOccurred(tr("Preset name is empty."));
        return QString();
    }

    QDir dir(userPresetDir());
    if (!dir.mkpath(QStringLiteral("."))) {
        emit errorOccurred(tr("Could not create the preset folder: %1").arg(dir.path()));
        return QString();
    }

    // File name = sanitized display name (spaces → '_', anything outside
    // [A-Za-z0-9._-] → '_', matching the bundled presets' naming).
    // Collisions get a unique numeric suffix — never an overwrite.
    QString base = displayName;
    base.replace(QLatin1Char(' '), QLatin1Char('_'));
    static const QRegularExpression invalidChars(QStringLiteral("[^A-Za-z0-9._-]"));
    base.replace(invalidChars, QStringLiteral("_"));
    QString path = dir.filePath(base + QStringLiteral(".ucp"));
    for (int suffix = 2; QFile::exists(path); ++suffix)
        path = dir.filePath(base + QStringLiteral("_%1.ucp").arg(suffix));

    const GradeParams params = selectedParams();
    QJsonObject obj;
    obj[QStringLiteral("name")] = displayName;
    obj[QStringLiteral("description")] = QString();
    obj[QStringLiteral("redRecovery")] = params.redRecovery;
    obj[QStringLiteral("waterTint")] = params.waterTint;
    obj[QStringLiteral("clarity")] = params.clarity;

    // QSaveFile: a failed/partial write (disk full, NFS flush) must not
    // leave a truncated .ucp behind AND claim success — commit() is
    // atomic and reports the failure the plain write() path swallowed.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        emit errorOccurred(tr("Could not write preset: %1").arg(path));
        return QString();
    }
    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit()) {
        emit errorOccurred(tr("Could not write preset: %1").arg(path));
        return QString();
    }

    refreshPresets();
    emit presetSaved(displayName);
    return path;
}
