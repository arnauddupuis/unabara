// Tests for ClipListModel / VideoClip (include/core/clip_list_model.h).
// The async prober is stubbed through setProberForTesting() so no ffprobe
// binary or real video file is needed. Covers: add/dedupe/duplicateRequested,
// activation, removeClip active-index behavior, dive-order derivation from
// sync offsets (never stored), enabled filtering, autoSyncFromTimecode math
// (no negative-offset clamp — the historical main.qml clamp was the bug),
// the toVariantMap/fromVariantMap persistence round-trip including the
// opaque colorGrade, and (PR A3) the dive-session save/restore machinery
// against an in-memory ClipSessionStore: clips follow the dive, restored
// sync suppresses the timecode auto-sync end-to-end, missing files toast
// once, re-probe fills metadata without touching the restored sync, offset
// writes are debounced, and Config's video/clipStates + video/diveSessions
// blobs round-trip through QSettings.

#include "settings_isolation.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSemaphore>
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "include/core/clip_list_model.h"
#include "include/core/config.h"
#include "include/core/dive_data.h"
#include "include/core/video_clip.h"

// In-memory ClipSessionStore: what main.cpp's ConfigClipSessionStore does,
// minus Config/QSettings, so the model's persistence logic is tested pure.
class MemorySessionStore : public ClipSessionStore
{
public:
    QVariantMap loadClipState(const QString& path) const override
    {
        return clipStates.value(path);
    }
    void saveClipState(const QString& path, const QVariantMap& state) override
    {
        clipStates.insert(path, state);
        ++clipStateWrites;
    }
    QVariantMap loadSession(const QString& diveKey) const override
    {
        return sessions.value(diveKey);
    }
    void saveSession(const QString& diveKey, const QVariantMap& session) override
    {
        sessions.insert(diveKey, session);
    }
    QVariantMap loadOverlayLayout(const QString& key) const override
    {
        return layouts.value(key);
    }
    QVariantMap loadLastOverlayLayout() const override
    {
        return lastLayout;
    }
    void saveOverlayLayout(const QStringList& keys, const QVariantMap& layout,
                           bool updateLastUsed) override
    {
        for (const QString& key : keys)
            layouts.insert(key, layout);
        if (updateLastUsed)
            lastLayout = layout;
        ++layoutWrites;
    }

    QHash<QString, QVariantMap> clipStates;
    QHash<QString, QVariantMap> sessions;
    QHash<QString, QVariantMap> layouts; // video/overlayLayouts (path + bucket keys)
    QVariantMap lastLayout;              // video/lastOverlayLayout (empty = none)
    int clipStateWrites = 0;
    int layoutWrites = 0;
};

class ClipListModelTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void addClipProbesAndActivates();
    void dedupeByCanonicalPath();
    void probeFailureReported();
    void failedProbeKeepsRestoredMetadata();
    void nonLocalUrlsIgnored();
    void removeClipActiveIndexBehavior();
    void clipsInDiveOrderDerivedFromOffsets();
    void bandDataReflectsClips();
    void autoSyncFromTimecodeMath();
    void autoSyncAppliedAfterProbe();
    void restoredSyncSuppressesAutoSync();
    void variantMapRoundTrip();

    // PR A3: session persistence
    void sessionSaveRestoreRoundTrip();
    void missingFileOnRestore();
    void reProbeAfterRestore();
    void offsetWritesDebouncedAndFlushed();
    void removalKeepsClipState();
    void configClipStateRoundTrip();

    // Follow-up K: clips must never sit outside the dive
    void divePlacementBoundaryMath();
    void bandDataStacksFullyOutsideClips();
    void stackedLaneScalesToFitDive();
    void outsideNotificationTransitionOnly();
    void outsideNotificationAggregatesBulk();
    void outsideNotificationAfterRestoreOnce();
    void outsideNotificationRestoreStaggeredProbes();
    void noDiveMeansNothingOutside();

    // Follow-up F: shared overlay placement per (dive x resolution)
    void overlayBucketKeyFormat();
    void overlayLayoutResolutionOrder();
    void overlayLayoutBroadcastSameBucketOnly();
    void overlayLayoutProbeRejoinsBucket();
    void overlayLayoutRestoreSeedsDeterministically();
    void configOverlayLayoutBlobCompat();

private:
    QString makeFile(const QString& name);
    // Installs a stub prober serving per-path results from m_stubResults.
    void installStubProber();
    VideoClip* addProbedClip(const QString& path);
    static QString diveKeyFor(const DiveData& dive);
    // Fills `dive` with samples every 10 s over [0, durationSeconds] so its
    // durationSeconds() — the out-of-dive window — is exactly that.
    static void fillDive(DiveData& dive, int durationSeconds);
    static QVariantMap bandFor(ClipListModel* model, const VideoClip* clip);
    // A distinguishable layout: DC rect at (x, 0.1, 0.2, 0.2), profile
    // visibility `profVisible`.
    static VideoOverlayLayout layoutAt(double x, bool profVisible = true);
    static VideoProbe::Result probeOf(int w, int h);

    ClipListModel* m_model = nullptr;
    QTemporaryDir* m_dir = nullptr;
    QHash<QString, VideoProbe::Result> m_stubResults;
    // Probe staggering: the stub prober blocks on the gate for file names in
    // m_gatedFiles (set before the addClip/setDive that starts the workers,
    // read-only on the workers — same discipline as m_stubResults). The
    // 30 s tryAcquire timeout keeps a failed test from deadlocking a worker.
    QSet<QString> m_gatedFiles;
    QSemaphore m_probeGate;
    MemorySessionStore m_store;
    QTemporaryDir m_settingsDir; // Config/QSettings redirect target
};

void ClipListModelTest::initTestCase()
{
    // Redirect QSettings for the configClipStateRoundTrip slot BEFORE the
    // first Config::instance() call anywhere in this process. Same pattern
    // (and same Linux-only caveat) as overlay_geometry_test: Config's
    // QSettings(org, app) constructor means NativeFormat, which setPath()
    // only redirects where native == Ini (Linux).
    QVERIFY(m_settingsDir.isValid());
    isolateUserSettings(m_settingsDir.path()); // see settings_isolation.h

#ifdef Q_OS_LINUX
    // Seed the raw QSettings blobs the two Config slots read
    // (configClipStateRoundTrip, configOverlayLayoutBlobCompat) here,
    // BEFORE any slot can construct the Config singleton — Config parses
    // them exactly once per process. Seeding centrally makes the two slots
    // order-independent (either can run alone via -functions).
    {
        QSettings seed("UnabaraProject", "Unabara");
        seed.setValue("video/clipStates",
                      QStringLiteral("{\"/videos/a.mp4\":{\"path\":\"/videos/a.mp4\","
                                     "\"syncOffset\":12.5,\"syncSource\":\"manual\","
                                     "\"enabled\":true,\"displayName\":\"A\"}}"));
        seed.setValue("video/diveSessions",
                      QStringLiteral("{\"1767000000\":{\"clipPaths\":[\"/videos/a.mp4\"],"
                                     "\"activePath\":\"/videos/a.mp4\",\"gapPolicy\":1}}"));
        // Follow-up F (asserted in configOverlayLayoutBlobCompat): a pre-F
        // path entry, a bucket entry and a last-used layout.
        QJsonObject layouts;
        layouts.insert(QStringLiteral("/videos/legacy.mp4"),
                       QJsonObject::fromVariantMap(layoutAt(0.12).toVariantMap()));
        layouts.insert(QStringLiteral("dive:1767000000|1920x1080"),
                       QJsonObject::fromVariantMap(layoutAt(0.34).toVariantMap()));
        seed.setValue("video/overlayLayouts",
                      QString::fromUtf8(QJsonDocument(layouts).toJson(QJsonDocument::Compact)));
        seed.setValue("video/lastOverlayLayout",
                      QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(
                          layoutAt(0.56).toVariantMap())).toJson(QJsonDocument::Compact)));
        seed.sync();
    }
#endif
}

void ClipListModelTest::init()
{
    m_dir = new QTemporaryDir();
    QVERIFY(m_dir->isValid());
    m_model = new ClipListModel(this);
    m_stubResults.clear();
    m_gatedFiles.clear();
    while (m_probeGate.tryAcquire()) {} // drain leftover permits
    m_store.clipStates.clear();
    m_store.sessions.clear();
    m_store.clipStateWrites = 0;
    m_store.layouts.clear();
    m_store.lastLayout.clear();
    m_store.layoutWrites = 0;
    installStubProber();
}

void ClipListModelTest::cleanup()
{
    // Unblock any still-gated probe worker before the model (and its future
    // watchers) go away.
    m_probeGate.release(8);
    delete m_model;
    m_model = nullptr;
    delete m_dir;
    m_dir = nullptr;
}

QString ClipListModelTest::makeFile(const QString& name)
{
    const QString path = m_dir->filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.write("x");
    f.close();
    return path;
}

void ClipListModelTest::installStubProber()
{
    // The prober runs on a QtConcurrent worker. Reading m_stubResults there
    // is safe for these tests: it is only mutated before the addClip() that
    // starts a worker, and every worker has finished (addProbedClip waits)
    // before the next mutation.
    m_model->setProberForTesting([this](const QString& path) {
        if (m_gatedFiles.contains(QFileInfo(path).fileName()))
            m_probeGate.tryAcquire(1, 30000);
        const auto it = m_stubResults.constFind(QFileInfo(path).fileName());
        if (it != m_stubResults.constEnd())
            return it.value();
        VideoProbe::Result r;
        r.ok = true;
        r.durationSeconds = 60.0;
        r.fpsNum = 30;
        r.fpsDen = 1;
        r.width = 1280;
        r.height = 720;
        return r;
    });
}

VideoClip* ClipListModelTest::addProbedClip(const QString& path)
{
    VideoClip* clip = m_model->addClip(path);
    if (!clip)
        return nullptr;
    // Probing is async (QtConcurrent + QFutureWatcher); wait for it in a
    // void lambda because the QTRY macros return void on failure.
    [clip]() {
        QTRY_VERIFY(clip->probeState() == VideoClip::Probed
                    || clip->probeState() == VideoClip::Failed);
    }();
    return clip;
}

void ClipListModelTest::addClipProbesAndActivates()
{
    QSignalSpy probedSpy(m_model, &ClipListModel::clipProbed);
    QSignalSpy clipsSpy(m_model, &ClipListModel::clipsChanged);

    const QString path = makeFile("dive1.mp4");
    VideoProbe::Result stub;
    stub.ok = true;
    stub.durationSeconds = 123.5;
    stub.fpsNum = 30000;
    stub.fpsDen = 1001;
    stub.width = 1920;
    stub.height = 1080;
    m_stubResults.insert(QStringLiteral("dive1.mp4"), stub);

    VideoClip* clip = m_model->addClip(path);
    QVERIFY(clip);
    QCOMPARE(m_model->count(), 1);
    QCOMPARE(m_model->activeIndex(), 0);
    QCOMPARE(m_model->activeClip(), clip);
    QCOMPARE(clip->displayName(), QStringLiteral("dive1"));
    QCOMPARE(clip->probeState(), VideoClip::Probing);
    QVERIFY(clipsSpy.count() >= 1);

    QTRY_COMPARE(clip->probeState(), VideoClip::Probed);
    QCOMPARE(probedSpy.count(), 1);
    QCOMPARE(clip->durationSeconds(), 123.5);
    QCOMPARE(clip->fpsNum(), 30000);
    QCOMPARE(clip->fpsDen(), 1001);
    QCOMPARE(clip->width(), 1920);

    // Model roles for the delegate.
    const QModelIndex idx = m_model->index(0);
    QCOMPARE(m_model->data(idx, ClipListModel::NameRole).toString(),
             QStringLiteral("dive1"));
    QCOMPARE(m_model->data(idx, ClipListModel::DurationRole).toDouble(), 123.5);
    QCOMPARE(m_model->data(idx, ClipListModel::ActiveRole).toBool(), true);
    QCOMPARE(m_model->data(idx, ClipListModel::EnabledRole).toBool(), true);
    QCOMPARE(m_model->data(idx, ClipListModel::SyncedRole).toBool(), false);
}

void ClipListModelTest::dedupeByCanonicalPath()
{
    const QString pathA = makeFile("a.mp4");
    makeFile("b.mp4");

    VideoClip* first = addProbedClip(pathA);
    VideoClip* second = addProbedClip(m_dir->filePath("b.mp4"));
    QCOMPARE(m_model->count(), 2);
    QCOMPARE(m_model->activeClip(), second);

    QSignalSpy duplicateSpy(m_model, &ClipListModel::duplicateRequested);

    // Same file through a different (non-canonical) spelling.
    const QString redundant = m_dir->path() + QStringLiteral("/./a.mp4");
    VideoClip* again = m_model->addClip(redundant);

    QCOMPARE(again, first);              // existing clip returned, not re-added
    QCOMPARE(m_model->count(), 2);
    QCOMPARE(m_model->activeClip(), first); // duplicate import re-activates
    QCOMPARE(duplicateSpy.count(), 1);
    QCOMPARE(duplicateSpy.at(0).at(0).toString(), first->path());
}

void ClipListModelTest::probeFailureReported()
{
    const QString path = makeFile("broken.mp4");
    VideoProbe::Result stub;
    stub.ok = false;
    stub.error = QStringLiteral("no video stream");
    m_stubResults.insert(QStringLiteral("broken.mp4"), stub);

    VideoClip* clip = m_model->addClip(path);
    QTRY_COMPARE(clip->probeState(), VideoClip::Failed);
    QCOMPARE(clip->probeError(), QStringLiteral("no video stream"));
    QCOMPARE(m_model->data(m_model->index(0), ClipListModel::ProbeStateRole).toInt(),
             static_cast<int>(VideoClip::Failed));
}

void ClipListModelTest::failedProbeKeepsRestoredMetadata()
{
    // toVariantMap deliberately persists duration, embeddedTimecode and
    // creationTimeUtc so they stay useful before a re-probe completes. A
    // FAILED re-probe (ffprobe missing on a new machine) must not wipe
    // them — only the state/error block may change.
    VideoClip original(QStringLiteral("/videos/old machine.mp4"));
    original.setDurationSeconds(250.0);
    QVariantMap map = original.toVariantMap();
    map.insert(QStringLiteral("embeddedTimecode"), 38392.0);
    map.insert(QStringLiteral("creationTimeUtc"),
               QStringLiteral("2026-05-01T10:00:00Z"));

    VideoClip* restored = VideoClip::fromVariantMap(map, this);
    QVERIFY(restored);
    QCOMPARE(restored->embeddedTimecode(), 38392.0);
    QVERIFY(restored->creationTimeUtc().isValid());
    QCOMPARE(restored->durationSeconds(), 250.0);

    VideoProbe::Result failed;
    failed.ok = false;
    failed.error = QStringLiteral("ffprobe not found");
    restored->applyProbeResult(failed);

    QCOMPARE(restored->probeState(), VideoClip::Failed);
    QCOMPARE(restored->probeError(), QStringLiteral("ffprobe not found"));
    QCOMPARE(restored->embeddedTimecode(), 38392.0);
    QVERIFY(restored->creationTimeUtc().isValid());
    QCOMPARE(restored->durationSeconds(), 250.0);
    restored->deleteLater();
}

void ClipListModelTest::nonLocalUrlsIgnored()
{
    const QString path = makeFile("local.mp4");
    m_model->addClips({ QUrl(QStringLiteral("https://example.com/clip.mp4")),
                        QUrl::fromLocalFile(path) });
    QCOMPARE(m_model->count(), 1);
    QCOMPARE(m_model->clipAt(0)->path(), path);
}

void ClipListModelTest::removeClipActiveIndexBehavior()
{
    VideoClip* a = addProbedClip(makeFile("a.mp4"));
    VideoClip* b = addProbedClip(makeFile("b.mp4"));
    VideoClip* c = addProbedClip(makeFile("c.mp4"));
    Q_UNUSED(b);
    QCOMPARE(m_model->activeIndex(), 2); // last added

    // Removing before the active clip shifts the index, same clip stays active.
    m_model->removeClip(0);
    QCOMPARE(m_model->count(), 2);
    QCOMPARE(m_model->activeIndex(), 1);
    QCOMPARE(m_model->activeClip(), c);
    Q_UNUSED(a);

    // Removing the active clip activates its successor (here: clamped to the
    // new last row).
    m_model->setActiveIndex(0);
    m_model->removeClip(0);
    QCOMPARE(m_model->count(), 1);
    QCOMPARE(m_model->activeIndex(), 0);
    QCOMPARE(m_model->activeClip(), c);

    // Removing the last clip empties the selection.
    m_model->removeClip(0);
    QCOMPARE(m_model->count(), 0);
    QCOMPARE(m_model->activeIndex(), -1);
    QCOMPARE(m_model->activeClip(), static_cast<VideoClip*>(nullptr));
}

void ClipListModelTest::clipsInDiveOrderDerivedFromOffsets()
{
    VideoClip* a = addProbedClip(makeFile("a.mp4"));
    VideoClip* b = addProbedClip(makeFile("b.mp4"));
    VideoClip* c = addProbedClip(makeFile("c.mp4"));
    a->setSyncOffset(50.0);
    b->setSyncOffset(10.0);
    c->setSyncOffset(30.0);

    QList<VideoClip*> ordered = m_model->clipsInDiveOrderList(false);
    QCOMPARE(ordered, (QList<VideoClip*>{ b, c, a }));

    // Order is derived at call time, never stored: moving a clip reorders.
    b->setSyncOffset(100.0);
    ordered = m_model->clipsInDiveOrderList(false);
    QCOMPARE(ordered, (QList<VideoClip*>{ c, a, b }));

    // enabledOnly filters disabled clips out.
    c->setEnabled(false);
    ordered = m_model->clipsInDiveOrderList(true);
    QCOMPARE(ordered, (QList<VideoClip*>{ a, b }));
    // …while the unfiltered view keeps them.
    QCOMPARE(m_model->clipsInDiveOrderList(false).size(), 3);

    // The QML-facing QVariantList carries the same clips in the same order.
    const QVariantList variantOrdered = m_model->clipsInDiveOrder(true);
    QCOMPARE(variantOrdered.size(), 2);
    QCOMPARE(variantOrdered.at(0).value<VideoClip*>(), a);
    QCOMPARE(variantOrdered.at(1).value<VideoClip*>(), b);
}

void ClipListModelTest::bandDataReflectsClips()
{
    VideoProbe::Result stub;
    stub.ok = true;
    stub.durationSeconds = 90.0;
    m_stubResults.insert(QStringLiteral("a.mp4"), stub);

    VideoClip* a = addProbedClip(makeFile("a.mp4"));
    a->setSyncOffset(120.0);

    const QVariantList bands = m_model->bandData();
    QCOMPARE(bands.size(), 1);
    const QVariantMap band = bands.at(0).toMap();
    QCOMPARE(band.value(QStringLiteral("start")).toDouble(), 120.0);
    QCOMPARE(band.value(QStringLiteral("end")).toDouble(), 210.0);
    QCOMPARE(band.value(QStringLiteral("active")).toBool(), true);
    QCOMPARE(band.value(QStringLiteral("enabled")).toBool(), true);

    QVERIFY(a->containsDiveTime(120.0));
    QVERIFY(a->containsDiveTime(210.0));
    QVERIFY(!a->containsDiveTime(119.9));
    QVERIFY(!a->containsDiveTime(210.1));

    // A rename must repaint the timeline bands: bandData carries
    // displayName (the band label), so displayNameChanged has to emit
    // clipsChanged like every other band-affecting edit (PR A4).
    QSignalSpy renameClipsSpy(m_model, &ClipListModel::clipsChanged);
    a->setDisplayName(QStringLiteral("Renamed"));
    QCOMPARE(renameClipsSpy.count(), 1);
    QCOMPARE(m_model->bandData().at(0).toMap()
                 .value(QStringLiteral("displayName")).toString(),
             QStringLiteral("Renamed"));

    // A syncSource flip must emit clipsChanged on its own: every sync
    // path writes the offset FIRST and flips the source SECOND, so a
    // consumer refreshed only by the offset's emission (SyncSection's
    // unsynced count, bandData's `synced`) would latch pre-flip state.
    QSignalSpy sourceClipsSpy(m_model, &ClipListModel::clipsChanged);
    a->setSyncSource(VideoClip::Manual);
    QCOMPARE(sourceClipsSpy.count(), 1);
    QCOMPARE(m_model->bandData().at(0).toMap()
                 .value(QStringLiteral("synced")).toBool(), true);
}

void ClipListModelTest::autoSyncFromTimecodeMath()
{
    DiveData dive;
    // Local time (matches the historical QML sites' JS Date getHours()).
    dive.setStartTime(QDateTime(QDate(2026, 1, 10), QTime(10, 0, 0)));
    m_model->setDive(&dive);

    VideoProbe::Result stub;
    stub.ok = true;
    stub.durationSeconds = 60.0;
    stub.timecodeSecondsOfDay = 10 * 3600.0 + 30 * 60.0; // camera clock 10:30:00
    m_stubResults.insert(QStringLiteral("a.mp4"), stub);
    VideoClip* clip = addProbedClip(makeFile("a.mp4"));

    QVERIFY(m_model->autoSyncFromTimecode(clip));
    QCOMPARE(clip->syncOffset(), 1800.0);
    QCOMPARE(clip->syncSource(), VideoClip::Timecode);

    // Camera started BEFORE the dive: negative offset, deliberately NOT
    // clamped (v0.4 plan decision — one of the two old QML sites clamped).
    VideoProbe::Result early;
    early.ok = true;
    early.durationSeconds = 60.0;
    early.timecodeSecondsOfDay = 9 * 3600.0; // camera clock 09:00:00
    m_stubResults.insert(QStringLiteral("b.mp4"), early);
    VideoClip* earlyClip = addProbedClip(makeFile("b.mp4"));

    QVERIFY(m_model->autoSyncFromTimecode(earlyClip));
    QCOMPARE(earlyClip->syncOffset(), -3600.0);

    // No timecode / no dive → declined, clip untouched.
    VideoClip* plain = addProbedClip(makeFile("c.mp4"));
    plain->setSyncOffset(42.0);
    plain->setSyncSource(VideoClip::Manual);
    QVERIFY(!m_model->autoSyncFromTimecode(plain)); // no timecode
    QCOMPARE(plain->syncOffset(), 42.0);
    QCOMPARE(plain->syncSource(), VideoClip::Manual);

    m_model->setDive(nullptr);
    QVERIFY(!m_model->autoSyncFromTimecode(clip)); // no dive
    QVERIFY(!m_model->autoSyncFromTimecode(nullptr));
}

void ClipListModelTest::autoSyncAppliedAfterProbe()
{
    // The on-import behaviour the QML sites implemented: once the probe
    // lands a timecode and a dive is loaded, the offset is set without any
    // further call.
    DiveData dive;
    dive.setStartTime(QDateTime(QDate(2026, 1, 10), QTime(10, 0, 0)));
    m_model->setDive(&dive);

    VideoProbe::Result stub;
    stub.ok = true;
    stub.durationSeconds = 60.0;
    stub.timecodeSecondsOfDay = 10 * 3600.0 + 39 * 60.0 + 52.0;
    m_stubResults.insert(QStringLiteral("gopro.mp4"), stub);

    VideoClip* clip = addProbedClip(makeFile("gopro.mp4"));
    QCOMPARE(clip->syncSource(), VideoClip::Timecode);
    QCOMPARE(clip->syncOffset(), 39 * 60.0 + 52.0);
    QCOMPARE(m_model->data(m_model->index(0), ClipListModel::SyncedRole).toBool(),
             true);
}

void ClipListModelTest::restoredSyncSuppressesAutoSync()
{
    // A clip whose sync was restored from persistence (PR A3) must keep its
    // offset even when the probe finds a timecode.
    DiveData dive;
    dive.setStartTime(QDateTime(QDate(2026, 1, 10), QTime(10, 0, 0)));
    m_model->setDive(&dive);

    VideoProbe::Result stub;
    stub.ok = true;
    stub.durationSeconds = 60.0;
    stub.timecodeSecondsOfDay = 11 * 3600.0;
    m_stubResults.insert(QStringLiteral("restored.mp4"), stub);

    VideoClip* clip = m_model->addClip(makeFile("restored.mp4"));
    clip->setSyncOffset(777.0);
    clip->setSyncSource(VideoClip::Restored);
    QTRY_COMPARE(clip->probeState(), VideoClip::Probed);

    QCOMPARE(clip->syncOffset(), 777.0);
    QCOMPARE(clip->syncSource(), VideoClip::Restored);
}

void ClipListModelTest::variantMapRoundTrip()
{
    VideoClip original(QStringLiteral("/videos/reef dive.mp4"));
    original.setDisplayName(QStringLiteral("Reef — morning"));
    original.setSyncOffset(-12.5);
    original.setSyncSource(VideoClip::Manual);
    original.setEnabled(false);
    original.setDurationSeconds(321.25);

    VideoOverlayLayout layout;
    layout.diveComputer.visible = false;
    layout.diveComputer.normalizedRect = QRectF(0.1, 0.2, 0.25, 0.35);
    layout.diveProfile.normalizedRect = QRectF(0.4, 0.7, 0.5, 0.15);
    original.setOverlayLayout(layout);

    // Opaque colour grade: nested structure track C will define later —
    // the clip model must round-trip it verbatim, never interpret it.
    QJsonObject grade;
    grade.insert(QStringLiteral("version"), 1);
    QJsonObject anchor;
    anchor.insert(QStringLiteral("depth"), 18.5);
    anchor.insert(QStringLiteral("redRecovery"), 0.7);
    grade.insert(QStringLiteral("anchors"), QJsonArray{ anchor });
    grade.insert(QStringLiteral("lutPath"), QStringLiteral("/luts/deep.cube"));
    original.setColorGrade(grade);

    const QVariantMap map = original.toVariantMap();
    VideoClip* restored = VideoClip::fromVariantMap(map);
    QVERIFY(restored);

    QCOMPARE(restored->path(), original.path());
    QCOMPARE(restored->displayName(), QStringLiteral("Reef — morning"));
    QCOMPARE(restored->syncOffset(), -12.5);
    QCOMPARE(restored->syncSource(), VideoClip::Manual);
    QCOMPARE(restored->enabled(), false);
    QCOMPARE(restored->durationSeconds(), 321.25);
    QCOMPARE(restored->overlayLayout().diveComputer.visible, false);
    QCOMPARE(restored->overlayLayout().diveComputer.normalizedRect,
             QRectF(0.1, 0.2, 0.25, 0.35));
    QCOMPARE(restored->overlayLayout().diveProfile.normalizedRect,
             QRectF(0.4, 0.7, 0.5, 0.15));
    QCOMPARE(restored->colorGrade(), grade); // opaque payload intact

    // Unknown/empty maps fail soft.
    QCOMPARE(VideoClip::fromVariantMap(QVariantMap()),
             static_cast<VideoClip*>(nullptr));

    delete restored;
}

// ---- PR A3: session persistence ------------------------------------------

QString ClipListModelTest::diveKeyFor(const DiveData& dive)
{
    return QString::number(dive.startTime().toSecsSinceEpoch());
}

void ClipListModelTest::sessionSaveRestoreRoundTrip()
{
    m_model->setSessionStore(&m_store);

    DiveData diveA;
    diveA.setStartTime(QDateTime(QDate(2026, 1, 10), QTime(10, 0, 0)));
    DiveData diveB;
    diveB.setStartTime(QDateTime(QDate(2026, 2, 20), QTime(9, 0, 0)));

    // Both clips' stub probes carry a timecode that WOULD put the offset at
    // 3600 s (camera clock 11:00 vs dive start 10:00) — the round trip must
    // prove the restored sync wins over this auto-sync.
    VideoProbe::Result stub;
    stub.ok = true;
    stub.durationSeconds = 60.0;
    stub.timecodeSecondsOfDay = 11 * 3600.0;
    m_stubResults.insert(QStringLiteral("one.mp4"), stub);
    m_stubResults.insert(QStringLiteral("two.mp4"), stub);

    m_model->setDive(&diveA);
    VideoClip* c1 = addProbedClip(makeFile("one.mp4"));
    VideoClip* c2 = addProbedClip(makeFile("two.mp4"));
    QVERIFY(c1 && c2);
    const QString path1 = c1->path();
    const QString path2 = c2->path();

    // User sync gestures (what the Timeline facade does on drag/SpinBox).
    c1->setSyncOffset(100.0);
    c1->setSyncSource(VideoClip::Manual);
    c2->setSyncOffset(200.0);
    c2->setSyncSource(VideoClip::Manual);
    c2->setDisplayName(QStringLiteral("Wreck pass"));
    c2->setEnabled(false);
    m_model->setGapPolicy(ClipListModel::BlackFrames);

    // Switch to another dive: outgoing session saved, list swapped out.
    m_model->setDive(&diveB);
    QCOMPARE(m_model->count(), 0);
    QCOMPARE(m_model->activeIndex(), -1);

    const QVariantMap sessionA = m_store.sessions.value(diveKeyFor(diveA));
    QVERIFY(!sessionA.isEmpty());
    QCOMPARE(sessionA.value(QStringLiteral("clipPaths")).toStringList(),
             (QStringList{ path1, path2 }));
    QCOMPARE(sessionA.value(QStringLiteral("activePath")).toString(), path2);
    QCOMPARE(sessionA.value(QStringLiteral("gapPolicy")).toInt(),
             static_cast<int>(ClipListModel::BlackFrames));

    // Stored clip state is the plan's subset: sync/user fields only — no
    // probe metadata, no overlay layout (that stays in video/overlayLayouts).
    const QVariantMap state1 = m_store.clipStates.value(path1);
    QCOMPARE(state1.value(QStringLiteral("syncOffset")).toDouble(), 100.0);
    QCOMPARE(state1.value(QStringLiteral("syncSource")).toString(),
             QStringLiteral("manual"));
    QCOMPARE(state1.value(QStringLiteral("enabled")).toBool(), true);
    QVERIFY(!state1.contains(QStringLiteral("durationSeconds")));
    QVERIFY(!state1.contains(QStringLiteral("embeddedTimecode")));
    QVERIFY(!state1.contains(QStringLiteral("creationTimeUtc")));
    QVERIFY(!state1.contains(QStringLiteral("overlayLayout")));

    // Back to dive A: both clips restored in list order with their state,
    // syncSource == Restored, and the re-probe's timecode must NOT move
    // the offsets (auto-sync suppressed by Restored).
    m_model->setDive(&diveA);
    QCOMPARE(m_model->count(), 2);
    VideoClip* r1 = m_model->clipAt(0);
    VideoClip* r2 = m_model->clipAt(1);
    QCOMPARE(r1->path(), path1);
    QCOMPARE(r2->path(), path2);
    QCOMPARE(r1->syncOffset(), 100.0);
    QCOMPARE(r1->syncSource(), VideoClip::Restored);
    QCOMPARE(r2->syncOffset(), 200.0);
    QCOMPARE(r2->syncSource(), VideoClip::Restored);
    QCOMPARE(r2->displayName(), QStringLiteral("Wreck pass"));
    QCOMPARE(r2->enabled(), false);
    QCOMPARE(m_model->activeClip(), r2); // activePath restored
    QCOMPARE(m_model->gapPolicy(), static_cast<int>(ClipListModel::BlackFrames));

    QTRY_COMPARE(r1->probeState(), VideoClip::Probed);
    QTRY_COMPARE(r2->probeState(), VideoClip::Probed);
    QCOMPARE(r1->durationSeconds(), 60.0);   // metadata re-probed…
    QCOMPARE(r1->syncOffset(), 100.0);       // …sync untouched
    QCOMPARE(r1->syncSource(), VideoClip::Restored);
    QCOMPARE(r2->syncOffset(), 200.0);
    QCOMPARE(r2->syncSource(), VideoClip::Restored);

    // nullptr dive: save + clear, nothing restored.
    m_model->setDive(nullptr);
    QCOMPARE(m_model->count(), 0);
    QVERIFY(m_store.sessions.contains(diveKeyFor(diveA)));
}

void ClipListModelTest::missingFileOnRestore()
{
    m_model->setSessionStore(&m_store);

    DiveData dive;
    dive.setStartTime(QDateTime(QDate(2026, 3, 5), QTime(14, 0, 0)));

    const QString existing = makeFile("still_here.mp4");
    const QString missing = m_dir->filePath("gone.mp4"); // never created

    QVariantMap session;
    session.insert(QStringLiteral("clipPaths"), QStringList{ existing, missing });
    session.insert(QStringLiteral("activePath"), missing);
    session.insert(QStringLiteral("gapPolicy"), 0);
    m_store.sessions.insert(diveKeyFor(dive), session);
    m_store.clipStates.insert(existing,
                              QVariantMap{ { QStringLiteral("path"), existing },
                                           { QStringLiteral("syncOffset"), 42.0 },
                                           { QStringLiteral("syncSource"), QStringLiteral("manual") } });

    QSignalSpy missingSpy(m_model, &ClipListModel::clipsMissingOnRestore);
    m_model->setDive(&dive);

    QCOMPARE(m_model->count(), 1); // missing file produced no clip
    QCOMPARE(m_model->clipAt(0)->path(), existing);
    QCOMPARE(m_model->clipAt(0)->syncOffset(), 42.0);
    QCOMPARE(m_model->clipAt(0)->syncSource(), VideoClip::Restored);
    QCOMPARE(m_model->activeIndex(), 0); // activePath gone → first clip
    QCOMPARE(missingSpy.count(), 1);     // ONE signal for the whole restore
    QCOMPARE(missingSpy.at(0).at(0).toInt(), 1);

    // A session save while the file is still missing (here: a structural
    // change — toggling the active clip's enabled state would do too, we
    // use gapPolicy) must NOT drop the missing path from the stored
    // session: replugging the drive and reloading the dive brings the
    // clip back.
    m_model->setGapPolicy(1);
    QStringList storedPaths = m_store.sessions.value(diveKeyFor(dive))
                                  .value(QStringLiteral("clipPaths")).toStringList();
    QVERIFY(storedPaths.contains(missing));
    QVERIFY(storedPaths.contains(existing));

    // The missing file's path-keyed state is NOT discarded by the restore
    // (the drive may come back), and the dive-switch save retains the
    // missing path in the session too.
    m_model->setDive(nullptr);
    QVERIFY(!m_store.clipStates.contains(missing)); // never had one…
    QVERIFY(m_store.clipStates.contains(existing)); // …and this one survives
    storedPaths = m_store.sessions.value(diveKeyFor(dive))
                      .value(QStringLiteral("clipPaths")).toStringList();
    QVERIFY(storedPaths.contains(missing));

    // "Drive replugged": create the file and reload the dive — the clip
    // is back, with its session membership intact.
    {
        QFile f(missing);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    m_model->setDive(&dive);
    QCOMPARE(m_model->count(), 2);
    QVERIFY(m_model->indexOfPath(missing) >= 0);
}

void ClipListModelTest::reProbeAfterRestore()
{
    // The A1 guard (failed probe keeps restored data) exercised end-to-end
    // through the restore path, plus the success path filling metadata.
    m_model->setSessionStore(&m_store);

    DiveData dive;
    dive.setStartTime(QDateTime(QDate(2026, 4, 1), QTime(8, 0, 0)));

    const QString okPath = makeFile("probes_ok.mp4");
    const QString badPath = makeFile("probes_bad.mp4");

    VideoProbe::Result ok;
    ok.ok = true;
    ok.durationSeconds = 321.0;
    ok.timecodeSecondsOfDay = 9 * 3600.0; // would auto-sync to 3600
    m_stubResults.insert(QStringLiteral("probes_ok.mp4"), ok);

    VideoProbe::Result bad;
    bad.ok = false;
    bad.error = QStringLiteral("ffprobe not found");
    m_stubResults.insert(QStringLiteral("probes_bad.mp4"), bad);

    QJsonObject grade;
    grade.insert(QStringLiteral("version"), 1);
    grade.insert(QStringLiteral("redRecovery"), 0.6);

    for (const QString& path : { okPath, badPath }) {
        m_store.clipStates.insert(path,
            QVariantMap{ { QStringLiteral("path"), path },
                         { QStringLiteral("displayName"), QStringLiteral("Named by user") },
                         { QStringLiteral("syncOffset"), 55.5 },
                         { QStringLiteral("syncSource"), QStringLiteral("timecode") },
                         { QStringLiteral("enabled"), false },
                         { QStringLiteral("colorGrade"), grade.toVariantMap() } });
    }
    QVariantMap session;
    session.insert(QStringLiteral("clipPaths"), QStringList{ okPath, badPath });
    session.insert(QStringLiteral("activePath"), okPath);
    m_store.sessions.insert(diveKeyFor(dive), session);

    m_model->setDive(&dive);
    QCOMPARE(m_model->count(), 2);
    VideoClip* good = m_model->clipAt(0);
    VideoClip* broken = m_model->clipAt(1);

    QTRY_COMPARE(good->probeState(), VideoClip::Probed);
    QCOMPARE(good->durationSeconds(), 321.0);          // metadata filled
    QCOMPARE(good->syncOffset(), 55.5);                // sync untouched
    QCOMPARE(good->syncSource(), VideoClip::Restored); // not re-auto-synced

    QTRY_COMPARE(broken->probeState(), VideoClip::Failed);
    QCOMPARE(broken->probeError(), QStringLiteral("ffprobe not found"));
    QCOMPARE(broken->displayName(), QStringLiteral("Named by user"));
    QCOMPARE(broken->syncOffset(), 55.5);
    QCOMPARE(broken->syncSource(), VideoClip::Restored);
    QCOMPARE(broken->enabled(), false);
    QCOMPARE(broken->colorGrade(), grade); // opaque payload intact
}

void ClipListModelTest::offsetWritesDebouncedAndFlushed()
{
    m_model->setSessionStore(&m_store);

    DiveData dive;
    dive.setStartTime(QDateTime(QDate(2026, 5, 2), QTime(12, 0, 0)));
    m_model->setDive(&dive);

    VideoClip* clip = addProbedClip(makeFile("drag.mp4"));
    QVERIFY(clip);
    const int writesBefore = m_store.clipStateWrites;

    // A zone drag streams offsets; none of these may hit the store
    // synchronously (per-change JSON rewrite + disk sync was the concern).
    for (int i = 1; i <= 25; ++i)
        clip->setSyncOffset(10.0 * i);
    QCOMPARE(m_store.clipStateWrites, writesBefore);

    // …but the debounce lands the final value shortly after the burst.
    QTRY_VERIFY(m_store.clipStateWrites > writesBefore);
    QCOMPARE(m_store.clipStates.value(clip->path())
                 .value(QStringLiteral("syncOffset")).toDouble(), 250.0);
    const int writesAfterBurst = m_store.clipStateWrites;
    QVERIFY(writesAfterBurst <= writesBefore + 2); // coalesced, not 25 writes

    // A dive switch inside the debounce window must not lose the newest
    // offset: setDive flushes before saving the outgoing session.
    clip->setSyncOffset(999.0);
    m_model->setDive(nullptr);
    QCOMPARE(m_store.clipStates.value(clip->path())
                 .value(QStringLiteral("syncOffset")).toDouble(), 999.0);
}

void ClipListModelTest::removalKeepsClipState()
{
    m_model->setSessionStore(&m_store);

    DiveData dive;
    dive.setStartTime(QDateTime(QDate(2026, 6, 15), QTime(7, 30, 0)));
    m_model->setDive(&dive);

    VideoClip* clip = addProbedClip(makeFile("removed.mp4"));
    QVERIFY(clip);
    const QString path = clip->path();
    clip->setSyncOffset(77.0);
    clip->setSyncSource(VideoClip::Manual);

    m_model->removeClip(0);
    QCOMPARE(m_model->count(), 0);

    // v0.4 plan: removal keeps the path-keyed state (re-import = re-sync
    // for free), while the session no longer lists the clip.
    QCOMPARE(m_store.clipStates.value(path)
                 .value(QStringLiteral("syncOffset")).toDouble(), 77.0);
    QCOMPARE(m_store.sessions.value(diveKeyFor(dive))
                 .value(QStringLiteral("clipPaths")).toStringList(), QStringList());
}

void ClipListModelTest::configClipStateRoundTrip()
{
#ifndef Q_OS_LINUX
    QSKIP("Config's NativeFormat QSettings (registry/plist) cannot be redirected off Linux");
#else
    // Config glue for the two new blobs: load path (raw QSettings JSON was
    // seeded in initTestCase, BEFORE the singleton first constructs, so this
    // slot and configOverlayLayoutBlobCompat are order-independent) and
    // write path (setters write through; verify the raw JSON).
    Config* config = Config::instance(); // first call → loadConfig() parses the seeds

    const QVariantMap loadedState = config->clipState(QStringLiteral("/videos/a.mp4"));
    QCOMPARE(loadedState.value(QStringLiteral("syncOffset")).toDouble(), 12.5);
    QCOMPARE(loadedState.value(QStringLiteral("syncSource")).toString(),
             QStringLiteral("manual"));
    const QVariantMap loadedSession = config->diveSession(QStringLiteral("1767000000"));
    QCOMPARE(loadedSession.value(QStringLiteral("clipPaths")).toStringList(),
             QStringList{ QStringLiteral("/videos/a.mp4") });
    QCOMPARE(loadedSession.value(QStringLiteral("gapPolicy")).toInt(), 1);
    QVERIFY(config->clipState(QStringLiteral("/videos/unknown.mp4")).isEmpty());
    QVERIFY(config->diveSession(QStringLiteral("0")).isEmpty());

    // Write path: targeted write-through (no saveConfig() involved).
    config->setClipState(QStringLiteral("/videos/b.mp4"),
                         QVariantMap{ { QStringLiteral("path"), QStringLiteral("/videos/b.mp4") },
                                      { QStringLiteral("syncOffset"), -3.25 },
                                      { QStringLiteral("syncSource"), QStringLiteral("timecode") } });
    config->setDiveSession(QStringLiteral("1768000000"),
                           QVariantMap{ { QStringLiteral("clipPaths"),
                                          QStringList{ QStringLiteral("/videos/b.mp4") } },
                                        { QStringLiteral("activePath"), QStringLiteral("/videos/b.mp4") },
                                        { QStringLiteral("gapPolicy"), 0 } });

    QSettings raw("UnabaraProject", "Unabara");
    const QJsonObject states = QJsonDocument::fromJson(
        raw.value("video/clipStates").toString().toUtf8()).object();
    QCOMPARE(states.value(QStringLiteral("/videos/b.mp4")).toObject()
                 .value(QStringLiteral("syncOffset")).toDouble(), -3.25);
    QVERIFY(states.contains(QStringLiteral("/videos/a.mp4"))); // seed kept
    const QJsonObject sessions = QJsonDocument::fromJson(
        raw.value("video/diveSessions").toString().toUtf8()).object();
    QCOMPARE(sessions.value(QStringLiteral("1768000000")).toObject()
                 .value(QStringLiteral("activePath")).toString(),
             QStringLiteral("/videos/b.mp4"));
#endif
}

// ---- Follow-up K: clips must never sit outside the dive -------------------

void ClipListModelTest::fillDive(DiveData& dive, int durationSeconds)
{
    for (int t = 0; t <= durationSeconds; t += 10)
        dive.addDataPoint(DiveDataPoint(t, 10.0));
}

QVariantMap ClipListModelTest::bandFor(ClipListModel* model, const VideoClip* clip)
{
    const QVariantList bands = model->bandData();
    for (const QVariant& v : bands) {
        const QVariantMap band = v.toMap();
        if (band.value(QStringLiteral("path")).toString() == clip->path())
            return band;
    }
    return {};
}

void ClipListModelTest::divePlacementBoundaryMath()
{
    using M = ClipListModel;
    const double D = 3000.0;

    // Clip END before the dive start → fully outside. Ending exactly at 0
    // has no visible overlap either (boundary is inclusive on the outside).
    QCOMPARE(M::divePlacement(-500.0, 60.0, D), M::FullyOutsideDive);
    QCOMPARE(M::divePlacement(-60.0, 60.0, D), M::FullyOutsideDive);
    // Clip START after the dive end → fully outside; exactly at D too.
    QCOMPARE(M::divePlacement(3100.0, 60.0, D), M::FullyOutsideDive);
    QCOMPARE(M::divePlacement(3000.0, 60.0, D), M::FullyOutsideDive);

    // Partial overlap at either end, and a clip spanning the whole dive.
    QCOMPARE(M::divePlacement(-30.0, 60.0, D), M::PartlyOutsideDive);
    QCOMPARE(M::divePlacement(-59.5, 60.0, D), M::PartlyOutsideDive);
    QCOMPARE(M::divePlacement(2970.0, 60.0, D), M::PartlyOutsideDive);
    QCOMPARE(M::divePlacement(-10.0, 4000.0, D), M::PartlyOutsideDive);

    // Inside, including spans touching both boundaries exactly.
    QCOMPARE(M::divePlacement(0.0, 60.0, D), M::InsideDive);
    QCOMPARE(M::divePlacement(2940.0, 60.0, D), M::InsideDive);
    QCOMPARE(M::divePlacement(0.0, 3000.0, D), M::InsideDive);

    // Unknown duration: a point at start, inside on [0, D).
    QCOMPARE(M::divePlacement(0.0, -1.0, D), M::InsideDive);
    QCOMPARE(M::divePlacement(1500.0, 0.0, D), M::InsideDive);
    QCOMPARE(M::divePlacement(-0.5, -1.0, D), M::FullyOutsideDive);
    QCOMPARE(M::divePlacement(3000.0, -1.0, D), M::FullyOutsideDive);

    // No dive window (no dive / no samples): nothing is ever outside.
    QCOMPARE(M::divePlacement(-5000.0, 60.0, 0.0), M::InsideDive);
    QCOMPARE(M::divePlacement(99999.0, -1.0, 0.0), M::InsideDive);
}

void ClipListModelTest::bandDataStacksFullyOutsideClips()
{
    DiveData dive;
    fillDive(dive, 3000);
    m_model->setDive(&dive);

    VideoProbe::Result failed; // unknown duration
    failed.ok = false;
    failed.error = QStringLiteral("stub failure");
    m_stubResults.insert(QStringLiteral("e.mp4"), failed);
    VideoProbe::Result longer;
    longer.ok = true;
    longer.durationSeconds = 120.0;
    m_stubResults.insert(QStringLiteral("c.mp4"), longer);

    VideoClip* a = addProbedClip(makeFile("a.mp4")); // 60 s, inside
    VideoClip* b = addProbedClip(makeFile("b.mp4")); // 60 s, ends before 0
    VideoClip* c = addProbedClip(makeFile("c.mp4")); // 120 s, after the end
    VideoClip* d = addProbedClip(makeFile("d.mp4")); // 60 s, straddles 0
    VideoClip* e = addProbedClip(makeFile("e.mp4")); // unknown, after the end
    QVERIFY(a && b && c && d && e);
    QVERIFY(e->durationSeconds() <= 0.0);

    a->setSyncOffset(100.0);
    b->setSyncOffset(-500.0);
    c->setSyncOffset(4000.0);
    d->setSyncOffset(-30.0);
    e->setSyncOffset(5000.0);

    auto num = [](const QVariantMap& m, const char* k) {
        return m.value(QString::fromLatin1(k)).toDouble();
    };
    auto flag = [](const QVariantMap& m, const char* k) {
        return m.value(QString::fromLatin1(k)).toBool();
    };

    const QVariantMap ba = bandFor(m_model, a);
    QCOMPARE(flag(ba, "outOfDive"), false);
    QCOMPARE(flag(ba, "stacked"), false);
    QCOMPARE(num(ba, "visualStart"), 100.0);
    QCOMPARE(num(ba, "visualEnd"), 160.0);

    // Partial overlap keeps its REAL position (it is visible), flagged only.
    const QVariantMap bd = bandFor(m_model, d);
    QCOMPARE(flag(bd, "outOfDive"), true);
    QCOMPARE(flag(bd, "stacked"), false);
    QCOMPARE(num(bd, "visualStart"), -30.0);
    QCOMPARE(num(bd, "visualEnd"), 30.0);

    // Fully-outside clips stack from dive time 0 in DIVE order (b at -500,
    // then c at 4000, then e at 5000), each slot its own length; e's
    // unknown duration takes the provisional length.
    const QVariantMap bb = bandFor(m_model, b);
    QCOMPARE(flag(bb, "stacked"), true);
    QCOMPARE(flag(bb, "outOfDive"), true);
    QCOMPARE(num(bb, "visualStart"), 0.0);
    QCOMPARE(num(bb, "visualEnd"), 60.0);
    const QVariantMap bc = bandFor(m_model, c);
    QCOMPARE(flag(bc, "stacked"), true);
    QCOMPARE(num(bc, "visualStart"), 60.0);
    QCOMPARE(num(bc, "visualEnd"), 180.0);
    const QVariantMap be = bandFor(m_model, e);
    QCOMPARE(flag(be, "stacked"), true);
    QCOMPARE(num(be, "visualStart"), 180.0);
    QCOMPARE(num(be, "visualEnd"), 180.0 + ClipListModel::kStackedProvisionalSeconds);

    // Visual only: the stored offsets (and the real span) are untouched,
    // and dive order still follows the real offsets.
    QCOMPARE(b->syncOffset(), -500.0);
    QCOMPARE(c->syncOffset(), 4000.0);
    QCOMPARE(num(bb, "start"), -500.0);
    QCOMPARE(num(bb, "end"), -440.0);
    QCOMPARE(m_model->clipsInDiveOrderList(false),
             (QList<VideoClip*>{ b, d, a, c, e }));

    // A correct re-sync un-stacks b; the lane closes up behind it.
    b->setSyncOffset(1000.0);
    const QVariantMap bb2 = bandFor(m_model, b);
    QCOMPARE(flag(bb2, "stacked"), false);
    QCOMPARE(flag(bb2, "outOfDive"), false);
    QCOMPARE(num(bb2, "visualStart"), 1000.0);
    const QVariantMap bc2 = bandFor(m_model, c);
    QCOMPARE(num(bc2, "visualStart"), 0.0);
    QCOMPARE(num(bc2, "visualEnd"), 120.0);
}

void ClipListModelTest::stackedLaneScalesToFitDive()
{
    // The timeline's view range is clamped to the dive, so a stack longer
    // than the dive is scaled to fit — every clip must stay visible.
    DiveData dive;
    fillDive(dive, 100);
    m_model->setDive(&dive);

    VideoClip* a = addProbedClip(makeFile("a.mp4")); // 60 s
    VideoClip* b = addProbedClip(makeFile("b.mp4")); // 60 s
    a->setSyncOffset(-1000.0);
    b->setSyncOffset(500.0);

    const QVariantMap ba = bandFor(m_model, a);
    const QVariantMap bb = bandFor(m_model, b);
    QCOMPARE(ba.value(QStringLiteral("visualStart")).toDouble(), 0.0);
    QCOMPARE(ba.value(QStringLiteral("visualEnd")).toDouble(), 50.0);
    QCOMPARE(bb.value(QStringLiteral("visualStart")).toDouble(), 50.0);
    QCOMPARE(bb.value(QStringLiteral("visualEnd")).toDouble(), 100.0);
}

void ClipListModelTest::outsideNotificationTransitionOnly()
{
    DiveData dive;
    fillDive(dive, 3000);
    m_model->setDive(&dive);
    VideoClip* clip = addProbedClip(makeFile("wreck.mp4")); // 60 s at 0
    QSignalSpy spy(m_model, &ClipListModel::clipsOutsideDive);
    m_model->flushOutsideDiveCheck();
    QCOMPARE(spy.count(), 0); // imported inside the dive: silent

    // inside → fully outside: one notification, through the real settle
    // timer (no explicit flush).
    clip->setSyncOffset(-500.0);
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 1);
    QCOMPARE(spy.at(0).at(1).toInt(), 1); // stacked
    QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("wreck"));

    // Further adjustments that keep it outside never re-fire (fully →
    // fully, fully → partly).
    clip->setSyncOffset(-600.0);
    m_model->flushOutsideDiveCheck();
    clip->setSyncOffset(-30.0);
    m_model->flushOutsideDiveCheck();
    QCOMPARE(spy.count(), 1);

    // Back inside: silent. Then inside → partly outside fires again, as a
    // partial (not stacked) one.
    clip->setSyncOffset(100.0);
    m_model->flushOutsideDiveCheck();
    QCOMPARE(spy.count(), 1);
    clip->setSyncOffset(2970.0);
    m_model->flushOutsideDiveCheck();
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(1).toInt(), 0);

    // A continuous drag crossing the boundary back and forth is ONE burst:
    // it notifies at most once when it settles outside…
    clip->setSyncOffset(100.0);
    m_model->flushOutsideDiveCheck();
    for (double off : { -10.0, 50.0, -20.0, 60.0, -40.0 })
        clip->setSyncOffset(off);
    QTRY_COMPARE(spy.count(), 3);
    QTest::qWait(600); // well past the settle window: no trailing repeats
    QCOMPARE(spy.count(), 3);
    // …and not at all when it settles back inside.
    clip->setSyncOffset(100.0);
    m_model->flushOutsideDiveCheck();
    for (double off : { -10.0, 50.0, -500.0, 200.0 })
        clip->setSyncOffset(off);
    m_model->flushOutsideDiveCheck();
    QCOMPARE(spy.count(), 3);
}

void ClipListModelTest::outsideNotificationAggregatesBulk()
{
    DiveData dive;
    fillDive(dive, 3000);
    m_model->setDive(&dive);
    VideoClip* a = addProbedClip(makeFile("a.mp4"));
    VideoClip* b = addProbedClip(makeFile("b.mp4"));
    VideoClip* c = addProbedClip(makeFile("c.mp4"));
    m_model->flushOutsideDiveCheck();
    QSignalSpy spy(m_model, &ClipListModel::clipsOutsideDive);

    // What SyncSection's bulk profile apply does in one JS loop: several
    // clips written in the same event-loop turn → ONE aggregated signal.
    a->setSyncOffset(4000.0);   // fully outside
    a->setSyncSource(VideoClip::CameraProfile);
    b->setSyncOffset(-20.0);    // partly outside
    b->setSyncSource(VideoClip::CameraProfile);
    c->setSyncOffset(1000.0);   // inside
    c->setSyncSource(VideoClip::CameraProfile);
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 2);
    QCOMPARE(spy.at(0).at(1).toInt(), 1);
    QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("b")); // dive order
    QTest::qWait(600);
    QCOMPARE(spy.count(), 1);
}

void ClipListModelTest::outsideNotificationAfterRestoreOnce()
{
    m_model->setSessionStore(&m_store);
    DiveData dive;
    dive.setStartTime(QDateTime(QDate(2026, 4, 1), QTime(9, 0, 0)));
    fillDive(dive, 3000);

    const QString early = makeFile("early.mp4");
    const QString late = makeFile("late.mp4");
    const QString fine = makeFile("fine.mp4");
    auto state = [](const QString& path, double offset) {
        return QVariantMap{ { QStringLiteral("path"), path },
                            { QStringLiteral("syncOffset"), offset },
                            { QStringLiteral("syncSource"), QStringLiteral("manual") } };
    };
    m_store.clipStates.insert(early, state(early, -500.0));
    m_store.clipStates.insert(late, state(late, 2990.0)); // partial ONLY once
                                                          // the 60 s duration
                                                          // is probed
    m_store.clipStates.insert(fine, state(fine, 100.0));
    m_store.sessions.insert(diveKeyFor(dive),
                            QVariantMap{ { QStringLiteral("clipPaths"),
                                           QStringList{ early, late, fine } },
                                         { QStringLiteral("activePath"), fine },
                                         { QStringLiteral("gapPolicy"), 0 } });

    QSignalSpy spy(m_model, &ClipListModel::clipsOutsideDive);
    m_model->setDive(&dive);
    QCOMPARE(m_model->count(), 3);
    // Restored clips are held out of the check while their re-probe is
    // pending (no duration yet) — an early evaluation must stay silent.
    m_model->flushOutsideDiveCheck();
    QCOMPARE(spy.count(), 0);

    for (int i = 0; i < m_model->count(); ++i)
        QTRY_COMPARE(m_model->clipAt(i)->probeState(), VideoClip::Probed);
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 2); // early (fully) + late (partly)
    QCOMPARE(spy.at(0).at(1).toInt(), 1);
    QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("early"));
    // Restore never touched the stored offsets.
    QCOMPARE(m_model->clipAt(0)->syncOffset(), -500.0);
    QTest::qWait(600);
    QCOMPARE(spy.count(), 1);

    // A dive switch is a lifecycle boundary: an armed check must not fire
    // for the outgoing list.
    m_model->clipAt(2)->setSyncOffset(-900.0);
    m_model->setDive(nullptr);
    QTest::qWait(600);
    QCOMPARE(spy.count(), 1);
}

void ClipListModelTest::outsideNotificationRestoreStaggeredProbes()
{
    // Regression (review of follow-up K): restore re-probes land in
    // staggered batches on slow drives / more clips than pool threads. The
    // settle timer armed by the FIRST batch must not evaluate the landed
    // subset — that would split the restore's single aggregated toast.
    m_model->setSessionStore(&m_store);
    DiveData dive;
    dive.setStartTime(QDateTime(QDate(2026, 4, 1), QTime(9, 0, 0)));
    fillDive(dive, 3000);

    const QString fast = makeFile("fast.mp4");  // -500 → fully outside
    const QString slow = makeFile("slow.mp4");  // 5000 → fully outside
    auto state = [](const QString& path, double offset) {
        return QVariantMap{ { QStringLiteral("path"), path },
                            { QStringLiteral("syncOffset"), offset },
                            { QStringLiteral("syncSource"), QStringLiteral("manual") } };
    };
    m_store.clipStates.insert(fast, state(fast, -500.0));
    m_store.clipStates.insert(slow, state(slow, 5000.0));
    m_store.sessions.insert(diveKeyFor(dive),
                            QVariantMap{ { QStringLiteral("clipPaths"),
                                           QStringList{ fast, slow } },
                                         { QStringLiteral("activePath"), fast },
                                         { QStringLiteral("gapPolicy"), 0 } });
    m_gatedFiles.insert(QStringLiteral("slow.mp4")); // before the workers start

    QSignalSpy spy(m_model, &ClipListModel::clipsOutsideDive);
    m_model->setDive(&dive);
    QCOMPARE(m_model->count(), 2);

    // First batch lands while the second is still gated.
    QTRY_COMPARE(m_model->clipAt(0)->probeState(), VideoClip::Probed);
    QCOMPARE(m_model->clipAt(1)->probeState(), VideoClip::Probing);
    // Deterministic flush (simulated timer fire) and the real settle timer
    // armed by the first finishProbe: both must stay silent mid-restore.
    m_model->flushOutsideDiveCheck();
    QCOMPARE(spy.count(), 0);
    QTest::qWait(600);
    QCOMPARE(spy.count(), 0);

    // Second batch lands → exactly one aggregated emission for both clips.
    m_probeGate.release();
    QTRY_COMPARE(m_model->clipAt(1)->probeState(), VideoClip::Probed);
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 2);
    QCOMPARE(spy.at(0).at(1).toInt(), 2); // both fully outside → both stacked
    QTest::qWait(600);
    QCOMPARE(spy.count(), 1);
}

void ClipListModelTest::noDiveMeansNothingOutside()
{
    // No dive (or a dive without samples): there is no window — no
    // stacking, no notification, historical behaviour.
    VideoClip* clip = addProbedClip(makeFile("solo.mp4"));
    QSignalSpy spy(m_model, &ClipListModel::clipsOutsideDive);
    clip->setSyncOffset(-5000.0);
    m_model->flushOutsideDiveCheck();
    QCOMPARE(spy.count(), 0);
    const QVariantMap band = bandFor(m_model, clip);
    QCOMPARE(band.value(QStringLiteral("stacked")).toBool(), false);
    QCOMPARE(band.value(QStringLiteral("outOfDive")).toBool(), false);
    QCOMPARE(band.value(QStringLiteral("visualStart")).toDouble(), -5000.0);
}

// ---- Follow-up F: shared overlay placement per (dive x resolution) -------

VideoOverlayLayout ClipListModelTest::layoutAt(double x, bool profVisible)
{
    VideoOverlayLayout l;
    l.diveComputer = OverlayPlacement{ true, QRectF(x, 0.1, 0.2, 0.2) };
    l.diveProfile = OverlayPlacement{ profVisible, QRectF(0.3, 0.8, 0.6, 0.15) };
    return l;
}

VideoProbe::Result ClipListModelTest::probeOf(int w, int h)
{
    VideoProbe::Result r;
    r.ok = true;
    r.durationSeconds = 60.0;
    r.fpsNum = 30;
    r.fpsDen = 1;
    r.width = w;
    r.height = h;
    return r;
}

void ClipListModelTest::overlayBucketKeyFormat()
{
    DiveData dive;
    dive.setStartTime(QDateTime(QDate(2026, 5, 1), QTime(8, 0, 0)));
    // Portrait phone clip: the probe reports rotation-APPLIED display
    // dimensions, so the bucket follows the display size.
    m_stubResults.insert(QStringLiteral("portrait.mp4"), probeOf(1080, 1920));
    m_gatedFiles.insert(QStringLiteral("portrait.mp4"));
    m_model->setDive(&dive);
    VideoClip* clip = m_model->addClip(makeFile("portrait.mp4"));
    QVERIFY(clip);
    // Unprobed: resolution unknown → no bucket (per-path behaviour).
    QVERIFY(m_model->overlayBucketKey(clip).isEmpty());
    m_probeGate.release();
    QTRY_COMPARE(clip->probeState(), VideoClip::Probed);
    QCOMPARE(m_model->overlayBucketKey(clip),
             QStringLiteral("dive:%1|1080x1920").arg(diveKeyFor(dive)));
    // Same derivation as video/diveSessions: no valid start time → no
    // dive identity → no bucket.
    DiveData undated;
    QVERIFY(ClipListModel::overlayBucketKey(&undated, clip).isEmpty());
    QVERIFY(ClipListModel::overlayBucketKey(nullptr, clip).isEmpty());
}

void ClipListModelTest::overlayLayoutResolutionOrder()
{
    // bucket → own path entry → last-used → defaults.
    m_model->setSessionStore(&m_store);
    DiveData dive;
    dive.setStartTime(QDateTime(QDate(2026, 5, 2), QTime(8, 0, 0)));
    m_model->setDive(&dive);

    const VideoOverlayLayout bucketL = layoutAt(0.11);
    const VideoOverlayLayout pathL = layoutAt(0.22);
    const VideoOverlayLayout lastL = layoutAt(0.33);
    const QString bucketKey = QStringLiteral("dive:%1|1920x1080").arg(diveKeyFor(dive));

    const QString pathA = makeFile("a.mp4");
    m_store.layouts.insert(bucketKey, bucketL.toVariantMap());
    m_store.layouts.insert(QFileInfo(pathA).absoluteFilePath(), pathL.toVariantMap());
    m_store.lastLayout = lastL.toVariantMap();
    m_stubResults.insert(QStringLiteral("a.mp4"), probeOf(1920, 1080));
    m_stubResults.insert(QStringLiteral("b.mp4"), probeOf(1920, 1080));
    m_stubResults.insert(QStringLiteral("c.mp4"), probeOf(640, 480));

    // Before the probe lands: resolution unknown → its own path entry.
    m_gatedFiles.insert(QStringLiteral("a.mp4"));
    VideoClip* a = m_model->addClip(pathA);
    QVERIFY(a);
    QCOMPARE(a->overlayLayout(), pathL);
    // Probe lands → the bucket beats the path entry.
    m_probeGate.release();
    QTRY_COMPARE(a->probeState(), VideoClip::Probed);
    QCOMPARE(a->overlayLayout(), bucketL);
    QCOMPARE(m_model->resolveOverlayLayout(a), bucketL);

    // No path entry: bucket still wins over last-used.
    VideoClip* b = addProbedClip(makeFile("b.mp4"));
    QCOMPARE(b->overlayLayout(), bucketL);

    // No bucket, no path entry → last-used (and, as first joiner of its
    // 640x480 bucket, it seeds that bucket with it).
    VideoClip* c = addProbedClip(makeFile("c.mp4"));
    QCOMPARE(c->overlayLayout(), lastL);
    QCOMPARE(m_store.layouts.value(
                 QStringLiteral("dive:%1|640x480").arg(diveKeyFor(dive))),
             lastL.toVariantMap());
    QCOMPARE(m_store.lastLayout, lastL.toVariantMap()); // a seed is no user edit

    // Nothing stored at all → VideoOverlayLayout defaults.
    m_store.layouts.clear();
    m_store.lastLayout.clear();
    m_stubResults.insert(QStringLiteral("d.mp4"), probeOf(320, 240));
    VideoClip* d = addProbedClip(makeFile("d.mp4"));
    QCOMPARE(d->overlayLayout(), VideoOverlayLayout());
}

void ClipListModelTest::overlayLayoutBroadcastSameBucketOnly()
{
    m_model->setSessionStore(&m_store);
    m_stubResults.insert(QStringLiteral("hd1.mp4"), probeOf(1920, 1080));
    m_stubResults.insert(QStringLiteral("hd2.mp4"), probeOf(1920, 1080));
    m_stubResults.insert(QStringLiteral("sd.mp4"), probeOf(1280, 720));
    m_stubResults.insert(QStringLiteral("other.mp4"), probeOf(1920, 1080));

    // Dive B first: a same-resolution clip of ANOTHER dive with its own
    // bucket layout, which a dive-A edit must never reach.
    DiveData diveA;
    diveA.setStartTime(QDateTime(QDate(2026, 6, 1), QTime(10, 0, 0)));
    DiveData diveB;
    diveB.setStartTime(QDateTime(QDate(2026, 6, 2), QTime(10, 0, 0)));
    const VideoOverlayLayout bLayout = layoutAt(0.44);
    m_store.lastLayout = bLayout.toVariantMap();
    m_model->setDive(&diveB);
    VideoClip* other = addProbedClip(makeFile("other.mp4"));
    QCOMPARE(other->overlayLayout(), bLayout); // seeded dive B's 1920x1080 bucket
    const QString otherPath = other->path();

    m_model->setDive(&diveA);
    VideoClip* hd1 = addProbedClip(makeFile("hd1.mp4"));
    VideoClip* hd2 = addProbedClip(makeFile("hd2.mp4"));
    VideoClip* sd = addProbedClip(makeFile("sd.mp4"));
    QVERIFY(hd1 && hd2 && sd);
    QCOMPARE(m_model->activeClip(), sd); // hd2 is never the active clip
    const VideoOverlayLayout sdBefore = sd->overlayLayout();
    const QString sdBucket = m_model->overlayBucketKey(sd);
    const QVariantMap sdBucketBefore = m_store.layouts.value(sdBucket);

    QSignalSpy hd2Spy(hd2, &VideoClip::overlayLayoutChanged);
    QSignalSpy sdSpy(sd, &VideoClip::overlayLayoutChanged);
    const int writesBefore = m_store.layoutWrites;

    const VideoOverlayLayout edit = layoutAt(0.55, /*profVisible=*/false);
    m_model->commitOverlayLayout(hd1, edit.toVariantMap());

    // Same dive + same display resolution: the never-activated sibling
    // follows live (exports read clip->overlayLayout()).
    QCOMPARE(hd1->overlayLayout(), edit);
    QCOMPARE(hd2->overlayLayout(), edit);
    QCOMPARE(hd2Spy.count(), 1);
    // Different resolution: untouched, in memory and in the store.
    QCOMPARE(sd->overlayLayout(), sdBefore);
    QCOMPARE(sdSpy.count(), 0);
    QCOMPARE(m_store.layouts.value(sdBucket), sdBucketBefore);

    // ONE persistence write: bucket + the edited clip's path + last-used;
    // no per-sibling path write.
    QCOMPARE(m_store.layoutWrites, writesBefore + 1);
    const QString hdBucket = m_model->overlayBucketKey(hd1);
    QCOMPARE(hdBucket, QStringLiteral("dive:%1|1920x1080").arg(diveKeyFor(diveA)));
    QCOMPARE(m_store.layouts.value(hdBucket), edit.toVariantMap());
    QCOMPARE(m_store.layouts.value(hd1->path()), edit.toVariantMap());
    QVERIFY(!m_store.layouts.contains(hd2->path()));
    QCOMPARE(m_store.lastLayout, edit.toVariantMap());

    // Different dive, same resolution: dive B's bucket is untouched, and
    // its clip comes back with dive B's layout.
    QCOMPARE(m_store.layouts.value(
                 QStringLiteral("dive:%1|1920x1080").arg(diveKeyFor(diveB))),
             bLayout.toVariantMap());
    m_model->setDive(&diveB);
    QCOMPARE(m_model->count(), 1);
    VideoClip* restoredOther = m_model->clipAt(0);
    QCOMPARE(restoredOther->path(), otherPath);
    QTRY_COMPARE(restoredOther->probeState(), VideoClip::Probed);
    QCOMPARE(restoredOther->overlayLayout(), bLayout);

    // Back to dive A (restart-equivalent): both HD clips restore the
    // bucket layout — hd2 never had a path entry of its own.
    m_model->setDive(&diveA);
    QCOMPARE(m_model->count(), 3);
    for (int i = 0; i < 3; ++i)
        QTRY_COMPARE(m_model->clipAt(i)->probeState(), VideoClip::Probed);
    QCOMPARE(m_model->clipAt(0)->overlayLayout(), edit);
    QCOMPARE(m_model->clipAt(1)->overlayLayout(), edit);
    QCOMPARE(m_model->clipAt(2)->overlayLayout(), sdBefore);
}

void ClipListModelTest::overlayLayoutProbeRejoinsBucket()
{
    // An unprobed clip edits per-path; when its probe lands it re-joins
    // the bucket, and an existing bucket layout WINS over its own.
    m_model->setSessionStore(&m_store);
    DiveData dive;
    dive.setStartTime(QDateTime(QDate(2026, 6, 3), QTime(10, 0, 0)));
    m_model->setDive(&dive);
    m_stubResults.insert(QStringLiteral("first.mp4"), probeOf(1920, 1080));
    m_stubResults.insert(QStringLiteral("late.mp4"), probeOf(1920, 1080));

    VideoClip* first = addProbedClip(makeFile("first.mp4"));
    const VideoOverlayLayout shared = layoutAt(0.61);
    m_model->commitOverlayLayout(first, shared.toVariantMap());

    m_gatedFiles.insert(QStringLiteral("late.mp4"));
    VideoClip* late = m_model->addClip(makeFile("late.mp4"));
    QVERIFY(late);
    // Pre-probe edit: no bucket yet → path key only, no broadcast.
    const VideoOverlayLayout own = layoutAt(0.72);
    m_model->commitOverlayLayout(late, own.toVariantMap());
    QCOMPARE(late->overlayLayout(), own);
    QCOMPARE(first->overlayLayout(), shared);
    QCOMPARE(m_store.layouts.value(late->path()), own.toVariantMap());

    QSignalSpy spy(late, &VideoClip::overlayLayoutChanged); // the live preview hook
    m_probeGate.release();
    QTRY_COMPARE(late->probeState(), VideoClip::Probed);
    QCOMPARE(late->overlayLayout(), shared);
    QCOMPARE(spy.count(), 1);
    // The bucket was not overwritten by the late joiner.
    QCOMPARE(m_store.layouts.value(m_model->overlayBucketKey(first)),
             shared.toVariantMap());
}

void ClipListModelTest::overlayLayoutRestoreSeedsDeterministically()
{
    // First restore after the upgrade: two same-resolution clips with
    // DIFFERENT pre-F path layouts and no bucket yet. Probes land in
    // arbitrary order; the bucket must be seeded by the ACTIVE clip (its
    // preview must not jump), never by whichever probe lands first.
    m_model->setSessionStore(&m_store);
    DiveData dive;
    dive.setStartTime(QDateTime(QDate(2026, 6, 4), QTime(10, 0, 0)));
    const QString p1 = makeFile("p1.mp4");
    const QString p2 = makeFile("p2.mp4");
    m_stubResults.insert(QStringLiteral("p1.mp4"), probeOf(1920, 1080));
    m_stubResults.insert(QStringLiteral("p2.mp4"), probeOf(1920, 1080));
    const VideoOverlayLayout l1 = layoutAt(0.15);
    const VideoOverlayLayout l2 = layoutAt(0.25);
    m_store.layouts.insert(p1, l1.toVariantMap());
    m_store.layouts.insert(p2, l2.toVariantMap());
    m_store.sessions.insert(diveKeyFor(dive),
                            QVariantMap{ { QStringLiteral("clipPaths"), QStringList{ p1, p2 } },
                                         { QStringLiteral("activePath"), p2 },
                                         { QStringLiteral("gapPolicy"), 0 } });
    // Adverse order: the NON-active clip lands first.
    m_gatedFiles.insert(QStringLiteral("p2.mp4"));

    m_model->setDive(&dive);
    QCOMPARE(m_model->count(), 2);
    VideoClip* c1 = m_model->clipAt(0);
    VideoClip* c2 = m_model->clipAt(1);
    QCOMPARE(m_model->activeClip(), c2);
    // Restored clips carry their resolved (path) layouts immediately.
    QCOMPARE(c1->overlayLayout(), l1);
    QCOMPARE(c2->overlayLayout(), l2);

    QTRY_COMPARE(c1->probeState(), VideoClip::Probed);
    const QString bucket = QStringLiteral("dive:%1|1920x1080").arg(diveKeyFor(dive));
    QVERIFY(!m_store.layouts.contains(bucket)); // deferred while p2 probes
    QCOMPARE(c1->overlayLayout(), l1);

    m_probeGate.release();
    QTRY_COMPARE(c2->probeState(), VideoClip::Probed);
    QCOMPARE(m_store.layouts.value(bucket), l2.toVariantMap());
    QCOMPARE(c2->overlayLayout(), l2);
    QCOMPARE(c1->overlayLayout(), l2);
}

void ClipListModelTest::configOverlayLayoutBlobCompat()
{
#ifndef Q_OS_LINUX
    QSKIP("Config's NativeFormat QSettings (registry/plist) cannot be redirected off Linux");
#else
    // Config's side of follow-up F. The legacy blob (path key + bucket key +
    // last-used) was seeded in initTestCase, BEFORE the singleton's first
    // construction — Config loads once per process, so this slot runs alone
    // (-functions) just as well as in suite order.
    Config* config = Config::instance();
    const QString legacyPath = QStringLiteral("/videos/legacy.mp4");
    const QString bucket = QStringLiteral("dive:1767000000|1920x1080");
    if (config->storedVideoOverlayLayout(legacyPath).isEmpty())
        QFAIL("legacy overlayLayouts seed not loaded — the initTestCase "
              "seeding must run before the Config singleton is built");

    // Pre-F path entry: still readable (migration fallback), raw and via
    // the historical API.
    const QVariantMap legacy = config->storedVideoOverlayLayout(legacyPath);
    QCOMPARE(VideoOverlayLayout::fromVariantMap(legacy), layoutAt(0.12));
    QCOMPARE(VideoOverlayLayout::fromVariantMap(config->videoOverlayLayout(legacyPath)),
             layoutAt(0.12));
    QCOMPARE(VideoOverlayLayout::fromVariantMap(config->storedVideoOverlayLayout(bucket)),
             layoutAt(0.34));
    // Raw accessor: absent is EMPTY (no fallback) — unlike the historical
    // videoOverlayLayout(), which falls back to the last-used layout.
    QVERIFY(config->storedVideoOverlayLayout(QStringLiteral("/videos/none.mp4")).isEmpty());
    QCOMPARE(VideoOverlayLayout::fromVariantMap(config->lastVideoOverlayLayout()),
             layoutAt(0.56));
    QCOMPARE(VideoOverlayLayout::fromVariantMap(
                 config->videoOverlayLayout(QStringLiteral("/videos/none.mp4"))),
             layoutAt(0.56));

    // Seed-style write (no last-used update), then an edit-style write.
    const QString bucket2 = QStringLiteral("dive:1767000000|1280x720");
    config->setVideoOverlayLayoutEntries(QStringList{ bucket2 }, layoutAt(0.78).toVariantMap(),
                                         false);
    QCOMPARE(VideoOverlayLayout::fromVariantMap(config->lastVideoOverlayLayout()),
             layoutAt(0.56));
    const QString newPath = QStringLiteral("/videos/new.mp4");
    config->setVideoOverlayLayoutEntries(QStringList{ bucket, newPath },
                                         layoutAt(0.9).toVariantMap(), true);
    QCOMPARE(VideoOverlayLayout::fromVariantMap(config->lastVideoOverlayLayout()),
             layoutAt(0.9));

    // Raw blob: ONE JSON object holding path and bucket keys side by side,
    // every value the plain {diveComputer, diveProfile} shape — exactly
    // what a pre-F build's loader accepts (it inserts every object-valued
    // key and re-writes them all), so old builds keep bucket entries
    // intact and still find their path entries.
    QSettings raw("UnabaraProject", "Unabara");
    const QJsonObject root = QJsonDocument::fromJson(
        raw.value("video/overlayLayouts").toString().toUtf8()).object();
    for (const QString& key : { legacyPath, bucket, bucket2, newPath })
        QVERIFY2(root.contains(key), qPrintable(key));
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        QVERIFY(it.value().isObject());
        const QStringList entryKeys = it.value().toObject().keys();
        QCOMPARE(QSet<QString>(entryKeys.cbegin(), entryKeys.cend()),
                 (QSet<QString>{ QStringLiteral("diveComputer"),
                                 QStringLiteral("diveProfile") }));
    }
    QCOMPARE(VideoOverlayLayout::fromVariantMap(root.value(bucket).toObject().toVariantMap()),
             layoutAt(0.9));
    QCOMPARE(VideoOverlayLayout::fromVariantMap(root.value(legacyPath).toObject().toVariantMap()),
             layoutAt(0.12)); // untouched
    const QJsonObject last = QJsonDocument::fromJson(
        raw.value("video/lastOverlayLayout").toString().toUtf8()).object();
    QCOMPARE(VideoOverlayLayout::fromVariantMap(last.toVariantMap()), layoutAt(0.9));
#endif
}

QTEST_GUILESS_MAIN(ClipListModelTest)
#include "clip_list_model_test.moc"
