#ifndef CLIP_LIST_MODEL_H
#define CLIP_LIST_MODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QRectF>
#include <QSet>
#include <QUrl>
#include <QVariantList>

#include <functional>

#include "include/core/dive_data.h"
#include "include/core/video_clip.h"
#include "include/core/video_probe.h"

class QFutureWatcherBase;
class QTimer;

// Persistence seam for the clip model (PR A3). ClipListModel never touches
// Config; main.cpp installs a thin adapter over Config's video/clipStates +
// video/diveSessions maps, and tests install an in-memory store. Clip state
// maps are keyed by absolute clip path, session maps by the dive key
// (startTime epoch seconds as a string). An empty map means "nothing
// stored". Implementations write through to durable storage on every save.
class ClipSessionStore
{
public:
    virtual ~ClipSessionStore() = default;
    virtual QVariantMap loadClipState(const QString& path) const = 0;
    virtual void saveClipState(const QString& path, const QVariantMap& state) = 0;
    virtual QVariantMap loadSession(const QString& diveKey) const = 0;
    virtual void saveSession(const QString& diveKey, const QVariantMap& session) = 0;

    // Overlay layouts (follow-up F) — Config's video/overlayLayouts blob,
    // which holds clip-path keys and ClipListModel::overlayBucketKey()
    // bucket keys side by side. loadOverlayLayout returns an EMPTY map when
    // nothing is stored under `key` (no fallback); loadLastOverlayLayout is
    // the last-used layout (the unseen-video default, never empty in the
    // Config adapter). saveOverlayLayout stores `layout` under every key in
    // one write; updateLastUsed also replaces the last-used layout.
    virtual QVariantMap loadOverlayLayout(const QString& key) const = 0;
    virtual QVariantMap loadLastOverlayLayout() const = 0;
    virtual void saveOverlayLayout(const QStringList& keys, const QVariantMap& layout,
                                   bool updateLastUsed) = 0;
};

// The list of imported video clips (context property `clipList`). Owns the
// VideoClip instances, probes them asynchronously (QtConcurrent + a
// QFutureWatcher per clip), and derives dive-order/band views from the
// clips' sync offsets at call time — clip order is NEVER stored.
//
// Clips follow the dive (v0.4 behaviour change): setDive() saves the
// outgoing dive's session (clip paths in list order, active path, gap
// policy) plus every clip's persistent state to the ClipSessionStore, then
// clears the list and restores the incoming dive's stored session. Restored
// clips re-probe for metadata but keep their stored sync (syncSource ==
// Restored suppresses the timecode auto-sync).
class ClipListModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int activeIndex READ activeIndex WRITE setActiveIndex NOTIFY activeIndexChanged)
    Q_PROPERTY(VideoClip* activeClip READ activeClip NOTIFY activeIndexChanged)
    Q_PROPERTY(DiveData* dive READ dive WRITE setDive NOTIFY diveChanged)
    // Stitched-export hook (v0.5): what fills the gap between clips. No UI
    // yet; persisted with the session in PR A3.
    Q_PROPERTY(int gapPolicy READ gapPolicy WRITE setGapPolicy NOTIFY gapPolicyChanged)
    // The active overlay template's .utp path (bound from
    // config.activeTemplatePath in main.qml). Feeds the per-template
    // placement memory ("Overlay Positions", Loïc's request): layouts are
    // ALSO stored under "tpl:<path>" in the same video/overlayLayouts blob
    // as the path and dive-bucket keys. The model never reads Config or the
    // generator itself — the binding pushes the value in.
    Q_PROPERTY(QString activeTemplatePath READ activeTemplatePath
                   WRITE setActiveTemplatePath NOTIFY activeTemplatePathChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PathRole,
        DurationRole,
        ProbeStateRole,
        EnabledRole,
        SyncedRole,      // bool: syncSource != Unsynced
        SyncSourceRole,  // VideoClip::SyncSource as int
        ActiveRole
    };

    enum GapPolicy {
        HoldLastFrame = 0,
        BlackFrames = 1
    };
    Q_ENUM(GapPolicy)

    explicit ClipListModel(QObject* parent = nullptr);
    ~ClipListModel() override;

    // QAbstractListModel
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_clips.size(); }
    int activeIndex() const { return m_activeIndex; }
    void setActiveIndex(int index);
    VideoClip* activeClip() const;
    DiveData* dive() const { return m_dive; }
    void setDive(DiveData* dive);
    int gapPolicy() const { return m_gapPolicy; }
    void setGapPolicy(int policy);
    QString activeTemplatePath() const { return m_activeTemplatePath; }
    void setActiveTemplatePath(const QString& templatePath);

    // Adds (and activates) a clip. Duplicates — same canonical path — are
    // not re-added: the existing clip is re-activated, duplicateRequested()
    // fires, and the existing clip is returned.
    Q_INVOKABLE VideoClip* addClip(const QString& path);
    Q_INVOKABLE void addClips(const QList<QUrl>& urls);
    Q_INVOKABLE void removeClip(int index);
    Q_INVOKABLE VideoClip* clipAt(int index) const;
    Q_INVOKABLE int indexOfPath(const QString& path) const;

    // Clips sorted by syncOffset (ties keep insertion order), derived at
    // call time. QVariantList of VideoClip* so QML can iterate it directly.
    Q_INVOKABLE QVariantList clipsInDiveOrder(bool enabledOnly) const;
    // Typed convenience for C++ callers/tests.
    QList<VideoClip*> clipsInDiveOrderList(bool enabledOnly) const;

    // Where a clip's span sits relative to the dive window [0, D] (follow-up
    // K). Pure boundary math, static so it is unit-testable on its own.
    //  - duration > 0: FullyOutside when the span has no positive-length
    //    overlap with the dive (end <= 0 or start >= D — a clip that only
    //    touches a boundary has nothing visible on the timeline); otherwise
    //    PartlyOutside when start < 0 or end > D; else Inside.
    //  - duration unknown (<= 0): the clip is a point at `start`, Inside
    //    when 0 <= start < D, FullyOutside otherwise (so a fresh, unsynced
    //    clip at offset 0 is Inside).
    //  - diveDuration <= 0 (no dive, or a dive without samples): there is
    //    no window to be outside of — always Inside.
    enum DivePlacement {
        InsideDive = 0,
        PartlyOutsideDive = 1,
        FullyOutsideDive = 2
    };
    Q_ENUM(DivePlacement)
    static DivePlacement divePlacement(double start, double durationSeconds,
                                       double diveDurationSeconds);
    // Placement of a clip against the current dive.
    DivePlacement divePlacementOf(const VideoClip* clip) const;

    // Visual length a fully-outside clip of unknown duration occupies in
    // the stacked lane (same 5-minute provisional length the timeline
    // historically gave unknown-duration bands).
    static constexpr double kStackedProvisionalSeconds = 300.0;

    // Per-clip band rectangles for the timeline canvas: list of maps with
    // path, displayName, start, end, enabled, active, synced, probeState,
    // plus (follow-up K) outOfDive (any part outside the dive window),
    // stacked (FULLY outside: drawn in the stacked lane, never at its real
    // offset), and visualStart/visualEnd (dive-time span to DRAW; equal to
    // start/end unless stacked). Stacked clips are laid one after another
    // from dive time 0 in dive order, each slot as long as the clip (or
    // kStackedProvisionalSeconds while its duration is unknown); when the
    // stack would run past the dive end, every slot is scaled down
    // proportionally so the whole stack fits in [0, D] — the timeline's
    // view range is clamped to the dive, and every clip must stay visible.
    // Visual only: the stored syncOffset is never touched, and dive order
    // (clipsInDiveOrder, exports) still follows the real offsets.
    Q_INVOKABLE QVariantList bandData() const;

    // ---- Shared overlay placement per (dive x resolution) — follow-up F --
    //
    // Within one dive, clips of the same DISPLAY resolution (rotation-
    // applied VideoClip::width()/height(), the FrameCompositor canvas) share
    // one overlay layout. Bucket key: "dive:<diveKey>|<W>x<H>" (diveKey =
    // the video/diveSessions key, start epoch seconds), stored in the SAME
    // video/overlayLayouts blob as the legacy path keys — `dive:` can never
    // be an absolute path on any OS, and the value has exactly the path
    // entries' shape, so older builds load, ignore and re-write it intact.
    // Empty when the clip has no known resolution (unprobed / probe gave no
    // size) or the dive has no valid start time: such clips stay per-path.
    static QString overlayBucketKey(const DiveData* dive, const VideoClip* clip);
    QString overlayBucketKey(const VideoClip* clip) const
    {
        return overlayBucketKey(m_dive, clip);
    }

    // Layout resolution order for `clip`: its dive x resolution bucket →
    // its own path entry (pre-F fallback / migration) → the last-used
    // layout → VideoOverlayLayout defaults. Read-only.
    VideoOverlayLayout resolveOverlayLayout(const VideoClip* clip) const;

    // The Video tab's commit channel for a user layout edit (drag/resize
    // release, overlay visibility toggle): sets the clip's layout,
    // re-broadcasts it to every other clip of this dive in the same bucket
    // (enabled or not — their overlayLayoutChanged refreshes a live
    // preview, and exports read clip->overlayLayout()), and persists ONE
    // write: bucket key + the clip's own path key (kept for old-build
    // compatibility and as the fallback) + last-used. Siblings get no
    // Config write of their own — the bucket entry covers them.
    Q_INVOKABLE void commitOverlayLayout(VideoClip* clip, const QVariantMap& layout);

    // ---- Per-template placement memory ("Overlay Positions") -------------
    //
    // Key for a template's stored placement: "tpl:" + the template path,
    // qrc:/ normalized to :/ (the generator's own normalization) so one
    // template cannot grow two memories. Lives in the same
    // video/overlayLayouts blob as the path and dive-bucket keys — "tpl:"
    // can never be an absolute path, the value shape is identical, so older
    // builds load, ignore and re-write it intact. Path (not display name):
    // collision-free across bundled + user templates; moving the user
    // template directory orphans the memory, which simply reseeds on the
    // next edit. Empty for an empty path.
    static QString templateLayoutKey(const QString& templatePath);

    // Reading a tpl: entry: parses the stored map and RESETS both visible
    // flags to the default — only the rects are template state; visibility
    // is clip state and must never leak from one dive into another through
    // the template memory. The one read boundary for template entries
    // (resolveOverlayLayout and applyActiveTemplatePlacement go through it).
    static VideoOverlayLayout templateRectsFromMap(const QVariantMap& stored);

    // Applies the active template's remembered placement RECTS to every
    // clip in the list ("all the clips in the ClipStrip" — Arnaud),
    // preserving every clip's own visible flags (visibility is clip state,
    // not template placement). NOT a user edit: it sets each clip's layout
    // directly and persists one write per distinct bucket (plus one per
    // unbucketed clip's path) with updateLastUsed=false — it never rewrites
    // the tpl: entry and never touches the last-used layout. No-op when
    // nothing is stored for the template, there are no clips, or there is
    // no active template. Called from main.qml's
    // overlayGenerator.onTemplateLoaded handler — which never fires on undo
    // restores (the loadTemplate()/loadTemplateFromFile() asymmetry) — so
    // undo cannot move overlays.
    Q_INVOKABLE void applyActiveTemplatePlacement();

    // templateSaved (Save As) hook: stores the ACTIVE clip's current layout
    // under `templatePath`'s key (store write only, nothing moves) so a
    // freshly saved template captures the placement it was saved with.
    Q_INVOKABLE void seedTemplatePlacement(const QString& templatePath);

    // The compiled-in default placements, for the Overlay Positions "Reset"
    // button — QML must not duplicate the default numbers.
    Q_INVOKABLE QVariantMap defaultOverlayLayout() const
    {
        return VideoOverlayLayout().toVariantMap();
    }

    // Sync offset from the clip's embedded timecode vs the dive start's
    // local time of day. Ported from the two duplicated main.qml sites;
    // decision (v0.4 plan): NO negative-offset clamp. Returns false when the
    // clip, dive or timecode is missing.
    Q_INVOKABLE bool autoSyncFromTimecode(VideoClip* clip);

    // Test seam: replaces VideoProbe::probe for the async probing. The
    // function runs on a worker thread, so it must be self-contained.
    void setProberForTesting(std::function<VideoProbe::Result(const QString&)> prober);

    // Installs the persistence backend (not owned; must outlive the model).
    // Null (the default) disables persistence entirely. Install it before
    // the first setDive() or the first dive's session can't be restored.
    void setSessionStore(ClipSessionStore* store);

    // Writes every debounce-pending clip state to the store now and disarms
    // the debounce timer. Called at the lifecycle boundaries — setDive
    // (via saveOutgoingDiveSession), setSessionStore, destruction; NOT by
    // removeClip, which only drops the removed clip from the pending set
    // and saves it immediately (other clips' pending writes still flush
    // later). Safe to call any time.
    void flushPendingClipStates();

    // Evaluates the out-of-dive notification now (normally run by the
    // settle timer after a burst of offset/duration changes) and disarms
    // the timer. Emits clipsOutsideDive() when at least one clip went from
    // inside the dive to (partly or fully) outside since the previous
    // evaluation. Public for tests; safe to call any time.
    void flushOutsideDiveCheck();

    // The subset of VideoClip::toVariantMap() that video/clipStates stores
    // (v0.4 plan field list): path, displayName, syncOffset, syncSource,
    // enabled, colorGrade. Metadata re-probes on restore and the overlay
    // layout stays in Config's video/overlayLayouts (resolved through the
    // store's layout methods, see overlayBucketKey()).
    static QVariantMap persistentClipState(const VideoClip* clip);

signals:
    void countChanged();
    void activeIndexChanged();
    void diveChanged();
    void gapPolicyChanged();
    void activeTemplatePathChanged();
    // Structural or band-affecting change (add/remove, offset/enabled/
    // duration edits) — the timeline repaints off this.
    void clipsChanged();
    void clipProbed(VideoClip* clip);
    void duplicateRequested(const QString& path);
    // Restore found stored clip paths whose files no longer exist; `count`
    // of them were skipped. QML shows ONE toast per restore off this.
    void clipsMissingOnRestore(int count);
    // Follow-up K: `count` clips went from inside the dive to outside it
    // (partly or fully) through the last burst of sync adjustments —
    // timecode auto-sync, camera-profile apply (single or bulk), a manual
    // drag/SpinBox/nudge, a late duration, or a session restore (one
    // aggregated emission once every restored clip's re-probe has landed).
    // Emitted on TRANSITION only, after the burst settles: a zone drag
    // that keeps a clip outside, or crosses back and forth, fires at most
    // once; a bulk apply fires once with the total. `stackedCount` of them
    // are fully outside (drawn stacked); `firstName` is the display name of
    // the first one in dive order (for the single-clip message).
    void clipsOutsideDive(int count, int stackedCount, const QString& firstName);

private:
    // qrc:/ → :/ (the generator's own normalization), shared by the
    // activeTemplatePath setter and templateLayoutKey() — one spelling per
    // template, defined once.
    static QString normalizeTemplatePath(const QString& templatePath);

    void connectClip(VideoClip* clip);
    void startProbe(VideoClip* clip);
    void finishProbe(VideoClip* clip, const VideoProbe::Result& result);
    void notifyRow(VideoClip* clip, const QVector<int>& roles);
    static QString canonicalKey(const QString& path);

    // Persistence helpers (all no-ops while m_store is null or a restore is
    // in progress — the restore itself must never echo back into the store).
    static QString diveKey(const DiveData* dive);
    // What video/clipStates actually receives: persistentClipState() plus
    // "diveKey", the dive the state was saved under. syncOffset is relative
    // to a dive's start, but the state is keyed by path alone — the tag is
    // what lets a re-import tell "same dive, sync still valid" from "this
    // file was last synced against another dive" (addClip()).
    QVariantMap storedClipState(const VideoClip* clip) const;
    // The ONE stored-state → clip mapping, shared by the session restore
    // and a re-import: the store key is the authoritative path, and a stored
    // sync comes back as SyncSource::Restored (which suppresses the timecode
    // auto-sync after the re-probe) while a clip stored unsynced stays
    // Unsynced. The sync is kept only when it belongs to the current dive:
    // the state's "diveKey" tag must equal the current dive's key, and an
    // empty key (no dive, or an undated dive — all of which would share it)
    // never matches. A state with no tag at all predates the tag:
    // `trustUntaggedSync` decides — true for the session restore (the
    // session lists the clip under this dive, and dropping would wipe every
    // upgrading user's syncs), false for a re-import (nothing says which
    // dive that offset was for). A dropped sync imports Unsynced, so the
    // timecode auto-sync runs against the current dive.
    VideoClip* clipFromStoredState(const QString& path, QVariantMap state,
                                   bool trustUntaggedSync);
    void saveClipStateNow(VideoClip* clip);
    void scheduleClipStateSave(VideoClip* clip);
    void saveSessionNow();
    void saveOutgoingDiveSession();
    void restoreSessionForCurrentDive();
    void clearAllClips();
    void insertClipRow(VideoClip* clip);

    // Follow-up F: a probed clip (re)joins its layout bucket — an existing
    // bucket layout wins over the clip's own; an empty bucket is seeded
    // with the clip's resolved layout (first joiner seeds). During a
    // session restore, joins against an empty bucket are deferred until
    // every restored clip's probe has landed, then seeded deterministically
    // (active clip first, then list order) — probe completion order would
    // otherwise pick the seeding clip at random.
    void joinOverlayBucket(VideoClip* clip, bool deferSeed);
    void seedDeferredOverlayBuckets();
    QList<VideoClip*> m_deferredBucketJoins;
    // A template placement must survive the probe gap. A clip has no
    // bucket key until its probe lands (every clip right after a session
    // restore), so applyActiveTemplatePlacement() can only write such a
    // clip's path entry — and the bucket it joins later still holds the
    // pre-template rects, which used to win the join and silently undo the
    // template load. The template's rects are therefore remembered here per
    // bucketless clip, with a stamp from one counter that buckets share:
    // joinOverlayBucket() applies them over the bucket (refreshing it and
    // its already-joined siblings) unless something newer has reached that
    // bucket since — a later template load or a user edit on a bucketed
    // clip — in which case the bucket wins as usual. A bucket with no stamp
    // only has its stored layout (older than any event of this list).
    // Deliberately NOT extended to user edits on bucketless clips: those
    // stay per-path and yield to the bucket at join, the long-standing
    // contract (overlayLayoutProbeRejoinsBucket). Both containers are
    // emptied in removeClip() / clearAllClips() — raw pointers, never stale.
    struct PendingTemplatePlacement {
        quint64 stamp = 0;
        QRectF diveComputer;
        QRectF diveProfile;
    };
    QHash<VideoClip*, PendingTemplatePlacement> m_pendingTemplatePlacements;
    QHash<QString, quint64> m_bucketPlacementStamp;
    quint64 m_placementStamp = 0;

    QList<VideoClip*> m_clips;
    int m_activeIndex = -1;
    QPointer<DiveData> m_dive;
    int m_gapPolicy = HoldLastFrame;
    QString m_activeTemplatePath;
    std::function<VideoProbe::Result(const QString&)> m_prober;
    QSet<QFutureWatcherBase*> m_watchers;

    ClipSessionStore* m_store = nullptr;
    // Debounce for syncOffset writes (zone drag streams them through the
    // Timeline facade). Member timer, disarmed by flushPendingClipStates()
    // at every lifecycle boundary — never a fire-and-forget singleShot.
    QTimer* m_persistTimer = nullptr;
    QSet<VideoClip*> m_pendingStateSaves;
    // Paths from the current dive's stored session whose files were
    // missing at restore time: saveSessionNow() re-appends them so they
    // survive later session saves (unplugged-drive case).
    QStringList m_missingSessionPaths;
    bool m_restoring = false;

    // Out-of-dive notification (follow-up K). Offset/duration changes
    // restart the settle timer; on timeout flushOutsideDiveCheck() compares
    // each clip's placement to the outside-state recorded at the previous
    // evaluation (absent = inside) and emits on inside→outside transitions.
    // Member timer, disarmed at every lifecycle boundary (clearAllClips,
    // setDive, destruction) — never a fire-and-forget singleShot.
    void scheduleOutsideDiveCheck();
    QTimer* m_outsideCheckTimer = nullptr;
    QHash<VideoClip*, bool> m_lastOutside;
    // Restored clips whose re-probe has not landed: their duration (not
    // persisted) is unknown, so they are left out of the evaluation until
    // the LAST one finishes — one aggregated notification per restore.
    QSet<VideoClip*> m_restorePending;
};

#endif // CLIP_LIST_MODEL_H
