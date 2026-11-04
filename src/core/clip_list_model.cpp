#include "include/core/clip_list_model.h"

#include "include/core/dive_data.h"

#include <QDebug>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace {
// Debounce window for syncOffset persistence. A timeline zone drag writes
// the offset continuously through the Timeline facade; each store write is
// a JSON-blob rewrite + disk sync, so coalesce a drag into one write that
// lands shortly after the last movement.
constexpr int kClipStatePersistDebounceMs = 750;
// Settle window for the out-of-dive notification (follow-up K): a zone
// drag streams offset writes and a bulk profile apply writes several clips
// in one turn — both collapse into ONE evaluation once the burst is quiet,
// so a drag that crosses the dive boundary back and forth can't spam
// toasts and a bulk apply reports one aggregated count.
constexpr int kOutsideDiveSettleMs = 400;
}

ClipListModel::ClipListModel(QObject* parent)
    : QAbstractListModel(parent)
    , m_prober(&VideoProbe::probe)
    , m_persistTimer(new QTimer(this))
    , m_outsideCheckTimer(new QTimer(this))
{
    m_persistTimer->setSingleShot(true);
    m_persistTimer->setInterval(kClipStatePersistDebounceMs);
    connect(m_persistTimer, &QTimer::timeout,
            this, &ClipListModel::flushPendingClipStates);

    m_outsideCheckTimer->setSingleShot(true);
    m_outsideCheckTimer->setInterval(kOutsideDiveSettleMs);
    connect(m_outsideCheckTimer, &QTimer::timeout,
            this, &ClipListModel::flushOutsideDiveCheck);
}

ClipListModel::~ClipListModel()
{
    // Don't lose a sync edit made within the debounce window of app exit;
    // the store (installed by main.cpp, declared before the model) is still
    // alive here.
    flushPendingClipStates();
    // A toast at teardown is pointless (and QML may already be gone).
    m_outsideCheckTimer->stop();
}

int ClipListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_clips.size();
}

QVariant ClipListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_clips.size())
        return QVariant();

    const VideoClip* clip = m_clips.at(index.row());
    switch (role) {
    case NameRole:       return clip->displayName();
    case PathRole:       return clip->path();
    case DurationRole:   return clip->durationSeconds();
    case ProbeStateRole: return static_cast<int>(clip->probeState());
    case EnabledRole:    return clip->enabled();
    case SyncedRole:     return clip->syncSource() != VideoClip::Unsynced;
    case SyncSourceRole: return static_cast<int>(clip->syncSource());
    case ActiveRole:     return index.row() == m_activeIndex;
    default:             return QVariant();
    }
}

QHash<int, QByteArray> ClipListModel::roleNames() const
{
    return {
        { NameRole,       "name" },
        { PathRole,       "path" },
        { DurationRole,   "duration" },
        { ProbeStateRole, "probeState" },
        { EnabledRole,    "enabled" },
        { SyncedRole,     "synced" },
        { SyncSourceRole, "syncSource" },
        { ActiveRole,     "active" },
    };
}

VideoClip* ClipListModel::activeClip() const
{
    if (m_activeIndex < 0 || m_activeIndex >= m_clips.size())
        return nullptr;
    return m_clips.at(m_activeIndex);
}

void ClipListModel::setActiveIndex(int index)
{
    const int clamped = (index < 0 || m_clips.isEmpty())
        ? -1
        : qMin(index, m_clips.size() - 1);
    if (clamped == m_activeIndex)
        return;

    const int oldIndex = m_activeIndex;
    m_activeIndex = clamped;

    if (oldIndex >= 0 && oldIndex < m_clips.size()) {
        const QModelIndex idx = this->index(oldIndex);
        emit dataChanged(idx, idx, { ActiveRole });
    }
    if (m_activeIndex >= 0) {
        const QModelIndex idx = this->index(m_activeIndex);
        emit dataChanged(idx, idx, { ActiveRole });
    }
    emit activeIndexChanged();
    saveSessionNow(); // active path is part of the stored session
}

void ClipListModel::setDive(DiveData* dive)
{
    if (m_dive == dive)
        return;

    // Clips follow the dive: persist the outgoing dive's session + clip
    // states, swap the list out, and bring back whatever the incoming dive
    // had last time. A first-ever dive (no stored session) simply starts
    // with an empty list; dive == nullptr only saves and clears.
    saveOutgoingDiveSession();
    clearAllClips();

    m_dive = dive;
    emit diveChanged();

    restoreSessionForCurrentDive();
}

void ClipListModel::setGapPolicy(int policy)
{
    if (m_gapPolicy != policy) {
        m_gapPolicy = policy;
        emit gapPolicyChanged();
        saveSessionNow();
    }
}

QString ClipListModel::canonicalKey(const QString& path)
{
    // Canonical path resolves symlinks and redundant components, but is
    // empty for files that don't exist (yet) — fall back to the absolute
    // path so dedupe still works on exact-path matches.
    const QFileInfo fi(path);
    const QString canonical = fi.canonicalFilePath();
    return canonical.isEmpty() ? fi.absoluteFilePath() : canonical;
}

int ClipListModel::indexOfPath(const QString& path) const
{
    if (path.isEmpty())
        return -1;
    const QString key = canonicalKey(path);
    for (int i = 0; i < m_clips.size(); ++i) {
        if (canonicalKey(m_clips.at(i)->path()) == key)
            return i;
    }
    return -1;
}

VideoClip* ClipListModel::addClip(const QString& path)
{
    if (path.isEmpty())
        return nullptr;

    const int existing = indexOfPath(path);
    if (existing >= 0) {
        // Duplicate import: don't re-add, re-activate and let the UI toast.
        setActiveIndex(existing);
        emit duplicateRequested(m_clips.at(existing)->path());
        return m_clips.at(existing);
    }

    VideoClip* clip = new VideoClip(QFileInfo(path).absoluteFilePath(), this);
    const int row = m_clips.size();
    insertClipRow(clip);

    emit countChanged();
    emit clipsChanged();
    setActiveIndex(row);
    // setActiveIndex persisted the session when the index moved; this one
    // covers the (theoretical) case where it did not.
    saveSessionNow();
    startProbe(clip);
    return clip;
}

void ClipListModel::insertClipRow(VideoClip* clip)
{
    const int row = m_clips.size();
    beginInsertRows(QModelIndex(), row, row);
    m_clips.append(clip);
    endInsertRows();
    connectClip(clip);
    // Every clip carries its resolved layout from the moment it joins the
    // list — not only once the Video tab activates it: exports (batch
    // queue included) read clip->overlayLayout() of clips that may never
    // have been active. No resolution is known yet, so this resolves
    // path → last-used → defaults; the probe re-joins the bucket.
    clip->setOverlayLayout(resolveOverlayLayout(clip));
}

void ClipListModel::addClips(const QList<QUrl>& urls)
{
    for (const QUrl& url : urls) {
        // Only local files can become clips; a remote URL would turn into
        // a clip whose "path" is the URL string and whose probe fails with
        // a confusing error (reachable once A2 adds the window DropArea).
        if (!url.isLocalFile()) {
            qWarning() << "ClipListModel: ignoring non-local URL" << url;
            continue;
        }
        addClip(url.toLocalFile());
    }
}

void ClipListModel::removeClip(int index)
{
    if (index < 0 || index >= m_clips.size())
        return;

    VideoClip* clip = m_clips.at(index);

    // Removal keeps the path-keyed clip state (v0.4 plan: re-importing the
    // file restores its sync). Capture its latest state before it leaves
    // the list, and drop it from the debounce queue.
    m_pendingStateSaves.remove(clip);
    saveClipStateNow(clip);

    // Out-of-dive bookkeeping: a removed clip never notifies, and if it was
    // the last restored clip still probing, the restore's aggregated check
    // is now due.
    m_lastOutside.remove(clip);
    m_deferredBucketJoins.removeAll(clip);
    if (m_restorePending.remove(clip) && m_restorePending.isEmpty()) {
        scheduleOutsideDiveCheck();
        seedDeferredOverlayBuckets(); // the restore's last pending probe
    }

    beginRemoveRows(QModelIndex(), index, index);
    m_clips.removeAt(index);
    endRemoveRows();

    // Active-index behavior: removing a clip before the active one shifts
    // the index down; removing the active clip activates its successor
    // (clamped to the new last row, -1 when the list empties). The active
    // *clip* can change while the numeric index stays the same, so signal
    // unconditionally rather than through setActiveIndex's no-op guard.
    int newActive = m_activeIndex;
    if (index < m_activeIndex)
        newActive = m_activeIndex - 1;
    else if (index == m_activeIndex)
        newActive = qMin(index, m_clips.size() - 1);
    m_activeIndex = newActive;
    if (m_activeIndex >= 0) {
        const QModelIndex idx = this->index(m_activeIndex);
        emit dataChanged(idx, idx, { ActiveRole });
    }
    emit activeIndexChanged();

    emit countChanged();
    emit clipsChanged();
    saveSessionNow();

    // deleteLater, not delete: an in-flight probe watcher may still hold a
    // QPointer to it this event-loop turn (the pointer nulls out safely).
    clip->deleteLater();
}

VideoClip* ClipListModel::clipAt(int index) const
{
    if (index < 0 || index >= m_clips.size())
        return nullptr;
    return m_clips.at(index);
}

QList<VideoClip*> ClipListModel::clipsInDiveOrderList(bool enabledOnly) const
{
    QList<VideoClip*> ordered;
    ordered.reserve(m_clips.size());
    for (VideoClip* clip : m_clips) {
        if (enabledOnly && !clip->enabled())
            continue;
        ordered.append(clip);
    }
    // Derived from the offsets at call time — never stored. Stable so that
    // clips sharing an offset keep their import order.
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const VideoClip* a, const VideoClip* b) {
                         return a->syncOffset() < b->syncOffset();
                     });
    return ordered;
}

QVariantList ClipListModel::clipsInDiveOrder(bool enabledOnly) const
{
    QVariantList list;
    const QList<VideoClip*> ordered = clipsInDiveOrderList(enabledOnly);
    list.reserve(ordered.size());
    for (VideoClip* clip : ordered)
        list.append(QVariant::fromValue(clip));
    return list;
}

ClipListModel::DivePlacement ClipListModel::divePlacement(
    double start, double durationSeconds, double diveDurationSeconds)
{
    if (diveDurationSeconds <= 0.0)
        return InsideDive; // no dive window to be outside of

    if (durationSeconds <= 0.0) {
        // Unknown duration: a point at `start`. [0, D) — a point exactly at
        // the dive end would draw its provisional band past the clamped
        // view range, i.e. invisibly.
        return (start >= 0.0 && start < diveDurationSeconds) ? InsideDive
                                                             : FullyOutsideDive;
    }

    const double end = start + durationSeconds;
    // No positive-length overlap with [0, D]: nothing of the clip would be
    // visible at its real position.
    if (end <= 0.0 || start >= diveDurationSeconds)
        return FullyOutsideDive;
    if (start < 0.0 || end > diveDurationSeconds)
        return PartlyOutsideDive;
    return InsideDive;
}

ClipListModel::DivePlacement ClipListModel::divePlacementOf(const VideoClip* clip) const
{
    if (!clip || !m_dive)
        return InsideDive;
    return divePlacement(clip->syncOffset(), clip->durationSeconds(),
                         static_cast<double>(m_dive->durationSeconds()));
}

QVariantList ClipListModel::bandData() const
{
    // Stacked lane (follow-up K): fully-outside clips, in dive order, get
    // consecutive visual slots from dive time 0. Visual only — offsets are
    // never written here.
    const double diveDuration = m_dive ? static_cast<double>(m_dive->durationSeconds())
                                       : 0.0;
    QHash<const VideoClip*, QPair<double, double>> stackedSlots;
    {
        QList<const VideoClip*> stacked;
        double total = 0.0;
        for (const VideoClip* clip : clipsInDiveOrderList(false)) {
            if (divePlacementOf(clip) != FullyOutsideDive)
                continue;
            stacked.append(clip);
            total += clip->durationSeconds() > 0.0 ? clip->durationSeconds()
                                                   : kStackedProvisionalSeconds;
        }
        // The timeline's view range is clamped to [0, D]: a stack longer
        // than the dive is scaled down so every stacked clip stays visible.
        const double scale = (total > diveDuration && total > 0.0)
            ? diveDuration / total
            : 1.0;
        double cursor = 0.0;
        for (const VideoClip* clip : stacked) {
            const double length = (clip->durationSeconds() > 0.0
                                       ? clip->durationSeconds()
                                       : kStackedProvisionalSeconds) * scale;
            stackedSlots.insert(clip, qMakePair(cursor, cursor + length));
            cursor += length;
        }
    }

    QVariantList bands;
    bands.reserve(m_clips.size());
    for (int i = 0; i < m_clips.size(); ++i) {
        const VideoClip* clip = m_clips.at(i);
        const DivePlacement placement = divePlacementOf(clip);
        const auto slot = stackedSlots.constFind(clip);
        const bool stacked = slot != stackedSlots.constEnd();
        QVariantMap band;
        band.insert(QStringLiteral("path"), clip->path());
        band.insert(QStringLiteral("displayName"), clip->displayName());
        band.insert(QStringLiteral("start"), clip->diveStart());
        band.insert(QStringLiteral("end"), clip->diveEnd());
        band.insert(QStringLiteral("enabled"), clip->enabled());
        band.insert(QStringLiteral("active"), i == m_activeIndex);
        band.insert(QStringLiteral("synced"),
                    clip->syncSource() != VideoClip::Unsynced);
        band.insert(QStringLiteral("probeState"),
                    static_cast<int>(clip->probeState()));
        band.insert(QStringLiteral("outOfDive"), placement != InsideDive);
        band.insert(QStringLiteral("stacked"), stacked);
        band.insert(QStringLiteral("visualStart"),
                    stacked ? slot->first : clip->diveStart());
        band.insert(QStringLiteral("visualEnd"),
                    stacked ? slot->second : clip->diveEnd());
        bands.append(band);
    }
    return bands;
}

// ---- Out-of-dive notification (follow-up K) ------------------------------

void ClipListModel::scheduleOutsideDiveCheck()
{
    m_outsideCheckTimer->start(); // restart: evaluate once the burst settles
}

void ClipListModel::flushOutsideDiveCheck()
{
    m_outsideCheckTimer->stop();

    // Hold the WHOLE evaluation while restored clips are still re-probing:
    // evaluating only the already-landed subset would split the restore's
    // single aggregated emission into several toasts when probes land in
    // staggered batches (slow drive, more clips than pool threads). The
    // last finishProbe() — or removeClip() — that empties the set re-arms
    // the settle timer, so the deferred check still fires exactly once.
    // Probes always finish (failure included), so this cannot wedge.
    if (!m_restorePending.isEmpty())
        return;

    int count = 0;
    int stackedCount = 0;
    QString firstName;
    // Dive order so the single-clip message names the earliest offender.
    for (VideoClip* clip : clipsInDiveOrderList(false)) {
        const DivePlacement placement = divePlacementOf(clip);
        const bool outside = placement != InsideDive;
        const bool wasOutside = m_lastOutside.value(clip, false);
        m_lastOutside.insert(clip, outside);
        if (outside && !wasOutside) {
            if (count == 0)
                firstName = clip->displayName();
            ++count;
            if (placement == FullyOutsideDive)
                ++stackedCount;
        }
    }
    if (count > 0)
        emit clipsOutsideDive(count, stackedCount, firstName);
}

bool ClipListModel::autoSyncFromTimecode(VideoClip* clip)
{
    if (!clip || !m_dive)
        return false;
    const double timecode = clip->embeddedTimecode();
    if (timecode < 0.0)
        return false;

    // The embedded timecode is the camera's wall clock as seconds-of-day, so
    // compare it against the dive start's wall-clock time of day.
    // toLocalTime() matches the historical main.qml implementation (a JS
    // Date's getHours() is local time). Open question, carried over: a FIT
    // dive's start time carries the *watch's* UTC offset as its QTimeZone —
    // if the system timezone differs from the watch's, this conversion
    // shifts the time of day. Same behaviour as the old QML sites; revisit
    // with the FIT timezone question.
    const QTime startOfDay = m_dive->startTime().toLocalTime().time();
    const double diveStartSecs = startOfDay.hour() * 3600.0
        + startOfDay.minute() * 60.0
        + startOfDay.second();

    // Decision (v0.4 plan): no negative-offset clamp. A camera that started
    // recording before the dive legitimately produces a negative offset; one
    // of the two historical QML sites clamped it to 0 and that was the bug.
    clip->setSyncOffset(timecode - diveStartSecs);
    clip->setSyncSource(VideoClip::Timecode);
    return true;
}

void ClipListModel::setProberForTesting(
    std::function<VideoProbe::Result(const QString&)> prober)
{
    m_prober = prober ? std::move(prober)
                      : std::function<VideoProbe::Result(const QString&)>(&VideoProbe::probe);
}

void ClipListModel::notifyRow(VideoClip* clip, const QVector<int>& roles)
{
    const int row = m_clips.indexOf(clip);
    if (row < 0)
        return;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, roles);
}

void ClipListModel::connectClip(VideoClip* clip)
{
    // Persistent-state changes write through to the session store. The
    // rare, discrete ones (rename, enable toggle, sync source, colour
    // grade) persist immediately; syncOffset is debounced because a zone
    // drag streams it continuously through the Timeline facade. A drag's
    // first movement also flips syncSource to Manual, so the immediate
    // syncSourceChanged save captures the offset at drag start and the
    // debounce captures where it settled.
    connect(clip, &VideoClip::displayNameChanged, this, [this, clip]() {
        notifyRow(clip, { NameRole });
        emit clipsChanged(); // band label (bandData carries displayName)
        saveClipStateNow(clip);
    });
    connect(clip, &VideoClip::enabledChanged, this, [this, clip]() {
        notifyRow(clip, { EnabledRole });
        emit clipsChanged(); // band dimming
        saveClipStateNow(clip);
    });
    connect(clip, &VideoClip::probeStateChanged, this, [this, clip]() {
        notifyRow(clip, { ProbeStateRole });
    });
    connect(clip, &VideoClip::syncSourceChanged, this, [this, clip]() {
        notifyRow(clip, { SyncedRole, SyncSourceRole });
        // clipsChanged too: every sync path writes syncOffset BEFORE
        // flipping syncSource, so consumers refreshed only by the offset's
        // clipsChanged (SyncSection's unsynced count, bandData's `synced`
        // field) would otherwise latch the pre-flip state.
        emit clipsChanged();
        saveClipStateNow(clip);
    });
    connect(clip, &VideoClip::colorGradeChanged, this, [this, clip]() {
        // Debounced like syncOffset: the colour panel's slider drags stream
        // a write-back per tick through ColorGradeController; the burst
        // lands as one store write after the drag ends (flushed at every
        // lifecycle boundary by flushPendingClipStates()).
        scheduleClipStateSave(clip);
    });
    connect(clip, &VideoClip::probeDataChanged, this, [this, clip]() {
        notifyRow(clip, { DurationRole });
        emit clipsChanged(); // band length
        scheduleOutsideDiveCheck(); // a late duration can cross the dive end
    });
    connect(clip, &VideoClip::syncOffsetChanged, this, [this, clip]() {
        emit clipsChanged(); // band position / dive order
        scheduleClipStateSave(clip);
        scheduleOutsideDiveCheck();
    });
}

void ClipListModel::startProbe(VideoClip* clip)
{
    clip->setProbeState(VideoClip::Probing);

    auto* watcher = new QFutureWatcher<VideoProbe::Result>(this);
    m_watchers.insert(watcher);

    QPointer<VideoClip> guard(clip);
    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, guard]() {
                m_watchers.remove(watcher);
                watcher->deleteLater();
                if (!guard)
                    return; // clip removed while probing
                finishProbe(guard.data(), watcher->result());
            });

    // Capture by value: the worker must not touch the model or the clip.
    const QString path = clip->path();
    const auto prober = m_prober;
    watcher->setFuture(QtConcurrent::run(
        [prober, path]() { return prober(path); }));
}

void ClipListModel::finishProbe(VideoClip* clip, const VideoProbe::Result& result)
{
    // A watcher's finished event can already be queued when removeClip()
    // runs; the clip is then alive (deleteLater) but no longer ours. Don't
    // touch it, and don't emit clipProbed with a dying pointer.
    if (!m_clips.contains(clip))
        return;

    clip->applyProbeResult(result);

    // Auto-sync freshly imported clips from their embedded timecode (the
    // historical on-import behaviour). Clips that already carry a sync —
    // e.g. Restored from persistence (PR A3) — are left alone.
    if (result.ok && clip->syncSource() == VideoClip::Unsynced)
        autoSyncFromTimecode(clip);

    // Out-of-dive check (follow-up K): the probe brought the duration and
    // maybe a timecode sync. A restored clip only re-joins the evaluation
    // here; when it was the last one of its restore still probing, the
    // restore's single aggregated check is due.
    const bool wasRestorePending = m_restorePending.remove(clip);
    scheduleOutsideDiveCheck();

    // Follow-up F: the probe brought the display resolution, so the clip
    // (re)joins its dive x resolution layout bucket — also on a re-probe
    // that changed its identity. Restored clips defer seeding an empty
    // bucket until the whole restore has landed (deterministic seeder).
    joinOverlayBucket(clip, wasRestorePending);
    if (wasRestorePending && m_restorePending.isEmpty())
        seedDeferredOverlayBuckets();

    emit clipProbed(clip);
}

// ---- Session persistence (PR A3) -----------------------------------------

void ClipListModel::setSessionStore(ClipSessionStore* store)
{
    if (m_store == store)
        return;
    flushPendingClipStates(); // never leave writes pending for a dead store
    m_store = store;
}

QString ClipListModel::diveKey(const DiveData* dive)
{
    // Keyed by the dive's start instant (v0.4 plan: not dive number or
    // site — those collide across logs). An invalid start time would make
    // every undated dive share one session, so such dives get none.
    if (!dive || !dive->startTime().isValid())
        return QString();
    return QString::number(dive->startTime().toSecsSinceEpoch());
}

QVariantMap ClipListModel::persistentClipState(const VideoClip* clip)
{
    // Filter the full serializer down to the stored subset instead of
    // listing fields a second time: key names and the syncSource string
    // encoding stay defined in exactly one place (VideoClip::toVariantMap).
    QVariantMap state = clip->toVariantMap();
    state.remove(QStringLiteral("durationSeconds"));
    state.remove(QStringLiteral("embeddedTimecode"));
    state.remove(QStringLiteral("creationTimeUtc"));
    // Layout persistence stays in Config's video/overlayLayouts (A3 keeps
    // it untouched for backward compatibility both ways); storing it here
    // too would create a second source of truth.
    state.remove(QStringLiteral("overlayLayout"));
    return state;
}

void ClipListModel::saveClipStateNow(VideoClip* clip)
{
    if (!m_store || m_restoring || !clip)
        return;
    m_pendingStateSaves.remove(clip); // this write supersedes a pending one
    m_store->saveClipState(clip->path(), persistentClipState(clip));
}

void ClipListModel::scheduleClipStateSave(VideoClip* clip)
{
    if (!m_store || m_restoring || !clip)
        return;
    m_pendingStateSaves.insert(clip);
    m_persistTimer->start(); // restart: the write lands after the burst ends
}

void ClipListModel::flushPendingClipStates()
{
    m_persistTimer->stop();
    if (m_pendingStateSaves.isEmpty())
        return;
    const QSet<VideoClip*> pending = std::move(m_pendingStateSaves);
    m_pendingStateSaves.clear();
    if (!m_store)
        return;
    for (VideoClip* clip : pending) {
        // Only clips still in the list: removeClip/clearAllClips already
        // took them out of the pending set, so a stale pointer here would
        // be a bug — but stay defensive, it's a deleteLater world.
        if (m_clips.contains(clip))
            m_store->saveClipState(clip->path(), persistentClipState(clip));
    }
}

void ClipListModel::saveSessionNow()
{
    if (!m_store || m_restoring || !m_dive)
        return;
    const QString key = diveKey(m_dive);
    if (key.isEmpty())
        return;

    QStringList paths;
    paths.reserve(m_clips.size() + m_missingSessionPaths.size());
    for (const VideoClip* clip : m_clips)
        paths.append(clip->path());
    // Clips whose file was missing at restore stay in the stored session
    // (unplugged-drive case): replugging the drive and reloading the dive
    // brings them back. A re-import of the same path lands in m_clips and
    // wins over its missing entry.
    for (const QString& missingPath : m_missingSessionPaths) {
        if (!paths.contains(missingPath))
            paths.append(missingPath);
    }

    QVariantMap session;
    session.insert(QStringLiteral("clipPaths"), paths);
    session.insert(QStringLiteral("activePath"),
                   activeClip() ? activeClip()->path() : QString());
    session.insert(QStringLiteral("gapPolicy"), m_gapPolicy);
    m_store->saveSession(key, session);
}

void ClipListModel::saveOutgoingDiveSession()
{
    // Clip states are path-keyed and dive-independent, so pending debounce
    // writes are flushed even with no outgoing dive (a clip imported and
    // nudged before any dive log must not lose its offset). With a dive,
    // every clip's state is written (superseding the flush) plus the
    // session index.
    flushPendingClipStates();
    if (!m_store || !m_dive) {
        m_missingSessionPaths.clear();
        return;
    }
    for (VideoClip* clip : m_clips)
        saveClipStateNow(clip);
    saveSessionNow();
    m_missingSessionPaths.clear(); // they belonged to the outgoing session
}

void ClipListModel::clearAllClips()
{
    // Lifecycle boundary for the out-of-dive notification: the outgoing
    // list never toasts, and the incoming one starts from a clean slate.
    m_outsideCheckTimer->stop();
    m_lastOutside.clear();
    m_restorePending.clear();
    m_deferredBucketJoins.clear();

    if (m_clips.isEmpty())
        return;

    beginResetModel();
    const QList<VideoClip*> old = std::move(m_clips);
    m_clips.clear();
    m_activeIndex = -1;
    m_pendingStateSaves.clear();
    endResetModel();

    emit activeIndexChanged();
    emit countChanged();
    emit clipsChanged();

    // deleteLater for the same reason as removeClip: in-flight probe
    // watchers may still hold QPointers this event-loop turn.
    for (VideoClip* clip : old)
        clip->deleteLater();
}

void ClipListModel::restoreSessionForCurrentDive()
{
    if (!m_store || !m_dive)
        return;
    const QString key = diveKey(m_dive);
    if (key.isEmpty())
        return;
    const QVariantMap session = m_store->loadSession(key);
    if (session.isEmpty())
        return; // first-ever session for this dive: keep the empty list

    // The restore itself must not echo into the store: a half-built list
    // would overwrite the stored session, and setSyncSource(Restored)
    // below would rewrite every clip state.
    m_restoring = true;

    const QStringList paths = session.value(QStringLiteral("clipPaths")).toStringList();
    m_missingSessionPaths.clear();
    int missing = 0;
    for (const QString& path : paths) {
        if (path.isEmpty())
            continue;
        if (!QFileInfo::exists(path)) {
            // Keep the path-keyed clip state AND the session membership
            // (the file may be on an unplugged drive): saveSessionNow()
            // re-appends these paths so a later save for this dive can't
            // silently drop them. It just doesn't become a clip this
            // session.
            m_missingSessionPaths.append(path);
            ++missing;
            continue;
        }
        QVariantMap state = m_store->loadClipState(path);
        state.insert(QStringLiteral("path"), path); // key is authoritative
        VideoClip* clip = VideoClip::fromVariantMap(state, this);
        if (!clip)
            continue;
        // A stored sync — whatever its original source — comes back as
        // Restored, which suppresses the timecode auto-sync after the
        // metadata re-probe. A clip stored unsynced stays Unsynced so the
        // auto-sync can still do its job.
        if (clip->syncSource() != VideoClip::Unsynced)
            clip->setSyncSource(VideoClip::Restored);
        insertClipRow(clip);
        // Held out of the out-of-dive check until its re-probe lands (the
        // duration isn't persisted): one aggregated notification per
        // restore, never one per clip.
        m_restorePending.insert(clip);
    }

    if (!m_clips.isEmpty()) {
        int active = indexOfPath(session.value(QStringLiteral("activePath")).toString());
        if (active < 0)
            active = 0;
        setActiveIndex(active);
    }
    m_gapPolicy = session.value(QStringLiteral("gapPolicy"), HoldLastFrame).toInt();
    emit gapPolicyChanged();

    m_restoring = false;

    emit countChanged();
    emit clipsChanged();
    if (missing > 0)
        emit clipsMissingOnRestore(missing);

    // Re-probe for metadata (duration, timecode, colour tags are not
    // persisted). Started after m_restoring drops so a probe that lands
    // synchronously-ish can persist what it learns (e.g. a fresh timecode
    // auto-sync on a clip stored unsynced).
    for (VideoClip* clip : m_clips)
        startProbe(clip);
}

// ---- Shared overlay placement per (dive x resolution) — follow-up F ------

QString ClipListModel::overlayBucketKey(const DiveData* dive, const VideoClip* clip)
{
    // Same dive identity as video/diveSessions. width()/height() are the
    // probe's rotation-APPLIED display size — the FrameCompositor canvas
    // and the preview's content rect — so a 90°-rotated 1920x1080 stream
    // buckets as 1080x1920.
    if (!clip || clip->width() <= 0 || clip->height() <= 0)
        return QString();
    const QString dive_ = diveKey(dive);
    if (dive_.isEmpty())
        return QString();
    return QStringLiteral("dive:%1|%2x%3").arg(dive_).arg(clip->width()).arg(clip->height());
}

VideoOverlayLayout ClipListModel::resolveOverlayLayout(const VideoClip* clip) const
{
    if (!clip || !m_store)
        return VideoOverlayLayout();
    const QString bucket = overlayBucketKey(clip);
    if (!bucket.isEmpty()) {
        const QVariantMap stored = m_store->loadOverlayLayout(bucket);
        if (!stored.isEmpty())
            return VideoOverlayLayout::fromVariantMap(stored);
    }
    // Pre-F path entry: the migration source, and the only key for clips
    // without a bucket (unprobed, or a dive without a valid start time).
    const QVariantMap own = m_store->loadOverlayLayout(clip->path());
    if (!own.isEmpty())
        return VideoOverlayLayout::fromVariantMap(own);
    const QVariantMap last = m_store->loadLastOverlayLayout();
    if (!last.isEmpty())
        return VideoOverlayLayout::fromVariantMap(last);
    return VideoOverlayLayout();
}

void ClipListModel::joinOverlayBucket(VideoClip* clip, bool deferSeed)
{
    const QString bucket = overlayBucketKey(clip);
    if (bucket.isEmpty() || !m_store)
        return; // no resolution / no dive identity: stays per-path
    const QVariantMap stored = m_store->loadOverlayLayout(bucket);
    if (!stored.isEmpty()) {
        // The bucket wins over whatever the clip resolved per-path.
        clip->setOverlayLayout(VideoOverlayLayout::fromVariantMap(stored));
        return;
    }
    if (deferSeed) {
        if (!m_deferredBucketJoins.contains(clip))
            m_deferredBucketJoins.append(clip);
        return;
    }
    // First joiner seeds the bucket with its own resolved layout. Not a
    // user edit, so the last-used layout is left alone.
    m_store->saveOverlayLayout(QStringList{ bucket },
                               clip->overlayLayout().toVariantMap(), false);
}

void ClipListModel::seedDeferredOverlayBuckets()
{
    if (m_deferredBucketJoins.isEmpty())
        return;
    // Deterministic seeder per bucket: the active clip first (its preview
    // must not jump to a sibling's layout), then list order. The first
    // join of a bucket seeds it; its later siblings find it and adopt it.
    QList<VideoClip*> order;
    VideoClip* active = activeClip();
    if (active && m_deferredBucketJoins.contains(active))
        order.append(active);
    for (VideoClip* clip : std::as_const(m_clips)) {
        if (clip != active && m_deferredBucketJoins.contains(clip))
            order.append(clip);
    }
    m_deferredBucketJoins.clear();
    for (VideoClip* clip : order)
        joinOverlayBucket(clip, false);
}

void ClipListModel::commitOverlayLayout(VideoClip* clip, const QVariantMap& layoutMap)
{
    if (!clip || !m_clips.contains(clip))
        return;
    const VideoOverlayLayout layout = VideoOverlayLayout::fromVariantMap(layoutMap);
    clip->setOverlayLayout(layout);

    const QString bucket = overlayBucketKey(clip);
    if (!bucket.isEmpty()) {
        // Live re-broadcast to every same-bucket sibling (the list only
        // ever holds the current dive's clips, so same key = same dive and
        // same display resolution). Enabled or not: a disabled clip
        // re-enabled later must already carry the shared placement.
        for (VideoClip* other : std::as_const(m_clips)) {
            if (other != clip && overlayBucketKey(other) == bucket)
                other->setOverlayLayout(layout);
        }
    }

    if (!m_store || m_restoring)
        return;
    QStringList keys;
    if (!bucket.isEmpty())
        keys.append(bucket);
    // The path key keeps being written: it is the fallback for clips that
    // have no bucket, and what an older build (path-only lookup) reads.
    keys.append(clip->path());
    m_store->saveOverlayLayout(keys, layout.toVariantMap(), true);
}
