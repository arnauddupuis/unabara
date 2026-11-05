#include "include/core/video_clip.h"
#include "include/core/clip_thumbnail.h"

#include <QFileInfo>

VideoClip::VideoClip(const QString& path, QObject* parent)
    : QObject(parent)
    , m_path(path)
    , m_displayName(QFileInfo(path).completeBaseName())
{
}

void VideoClip::setDisplayName(const QString& name)
{
    if (m_displayName != name) {
        m_displayName = name;
        emit displayNameChanged();
    }
}

void VideoClip::setDurationSeconds(double seconds)
{
    if (m_durationSeconds != seconds) {
        m_durationSeconds = seconds;
        emit probeDataChanged();
    }
}

void VideoClip::setSyncOffset(double offset)
{
    if (m_syncOffset != offset) {
        m_syncOffset = offset;
        emit syncOffsetChanged();
    }
}

void VideoClip::setSyncSource(SyncSource source)
{
    if (m_syncSource != source) {
        m_syncSource = source;
        emit syncSourceChanged();
    }
}

void VideoClip::setOverlayLayout(const VideoOverlayLayout& layout)
{
    // Guarded (follow-up F): the model broadcasts a committed layout to
    // every same-bucket sibling and the Video tab re-applies the active
    // clip's layout on overlayLayoutChanged — an unconditional emit would
    // churn the preview items for no-op writes.
    if (m_overlayLayout == layout)
        return;
    m_overlayLayout = layout;
    emit overlayLayoutChanged();
}

void VideoClip::setOverlayLayoutMap(const QVariantMap& map)
{
    setOverlayLayout(VideoOverlayLayout::fromVariantMap(map));
}

void VideoClip::setColorGrade(const QJsonObject& grade)
{
    if (m_colorGrade != grade) {
        m_colorGrade = grade;
        emit colorGradeChanged();
    }
}

void VideoClip::setColorGradeMap(const QVariantMap& map)
{
    setColorGrade(QJsonObject::fromVariantMap(map));
}

void VideoClip::setEnabled(bool enabled)
{
    if (m_enabled != enabled) {
        m_enabled = enabled;
        emit enabledChanged();
    }
}

void VideoClip::setProbeState(ProbeState state)
{
    if (m_probeState != state) {
        m_probeState = state;
        emit probeStateChanged();
    }
}

void VideoClip::applyProbeResult(const VideoProbe::Result& result)
{
    // A failed probe must not clobber metadata the clip already holds —
    // whether from an earlier successful probe, the player's duration
    // fallback, or a restore (the full toVariantMap() keeps duration,
    // embeddedTimecode and creationTimeUtc for exactly that reason, even
    // though the A3 clip-state store strips them and re-probes instead).
    if (!result.ok) {
        const QString newError = result.error;
        const bool stateBlockChanged =
            (newError != m_probeError) || (m_probeState != Failed);
        m_probeError = newError;
        m_probeState = Failed;
        if (stateBlockChanged)
            emit probeStateChanged();
        return;
    }

    m_creationTimeUtc = result.creationTimeUtc;
    if (result.durationSeconds > 0.0)
        m_durationSeconds = result.durationSeconds;
    m_fpsNum = result.fpsNum;
    m_fpsDen = result.fpsDen;
    m_avgFps = result.avgFps;
    m_vfr = result.vfr;
    m_videoStartTime = result.startTimeSeconds;
    m_width = result.width;
    m_height = result.height;
    m_codec = result.codec;
    m_pixFmt = result.pixFmt;
    m_bitDepth = result.bitDepth;
    m_colorSpace = result.colorSpace;
    m_colorTransfer = result.colorTransfer;
    m_colorPrimaries = result.colorPrimaries;
    m_colorRange = result.colorRange;
    m_hasAudio = result.hasAudio;
    m_audioCodec = result.audioCodec;
    m_embeddedTimecode = result.timecodeSecondsOfDay;
    m_nbFrames = result.nbFrames;
    emit probeDataChanged();

    // probeError shares probeState's NOTIFY, so emit when either changes.
    const bool stateBlockChanged =
        !m_probeError.isEmpty() || (m_probeState != Probed);
    m_probeError.clear();
    m_probeState = Probed;
    if (stateBlockChanged)
        emit probeStateChanged();
}

QString VideoClip::syncSourceToString(SyncSource source)
{
    switch (source) {
    case Timecode:      return QStringLiteral("timecode");
    case CameraProfile: return QStringLiteral("cameraProfile");
    case Manual:        return QStringLiteral("manual");
    case Restored:      return QStringLiteral("restored");
    case Unsynced:      break;
    }
    return QStringLiteral("unsynced");
}

VideoClip::SyncSource VideoClip::syncSourceFromString(const QString& s)
{
    if (s == QLatin1String("timecode"))      return Timecode;
    if (s == QLatin1String("cameraProfile")) return CameraProfile;
    if (s == QLatin1String("manual"))        return Manual;
    if (s == QLatin1String("restored"))      return Restored;
    return Unsynced;
}

QVariantMap VideoClip::toVariantMap() const
{
    // Persistence primitive (PR A3): user/sync state plus the metadata that
    // is useful before a re-probe completes. Sync sources are stored as
    // strings so the file format survives enum reordering.
    QVariantMap m;
    m.insert(QStringLiteral("path"), m_path);
    m.insert(QStringLiteral("displayName"), m_displayName);
    m.insert(QStringLiteral("syncOffset"), m_syncOffset);
    m.insert(QStringLiteral("syncSource"), syncSourceToString(m_syncSource));
    m.insert(QStringLiteral("enabled"), m_enabled);
    m.insert(QStringLiteral("durationSeconds"), m_durationSeconds);
    m.insert(QStringLiteral("embeddedTimecode"), m_embeddedTimecode);
    if (m_creationTimeUtc.isValid())
        m.insert(QStringLiteral("creationTimeUtc"),
                 m_creationTimeUtc.toString(Qt::ISODateWithMs));
    m.insert(QStringLiteral("overlayLayout"), m_overlayLayout.toVariantMap());
    // Opaque: round-tripped verbatim, never interpreted here (track C owns
    // the contents).
    m.insert(QStringLiteral("colorGrade"), m_colorGrade.toVariantMap());
    return m;
}

VideoClip* VideoClip::fromVariantMap(const QVariantMap& map, QObject* parent)
{
    const QString path = map.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
        return nullptr;

    VideoClip* clip = new VideoClip(path, parent);
    const QString name = map.value(QStringLiteral("displayName")).toString();
    if (!name.isEmpty())
        clip->m_displayName = name;
    clip->m_syncOffset = map.value(QStringLiteral("syncOffset"), 0.0).toDouble();
    clip->m_syncSource = syncSourceFromString(
        map.value(QStringLiteral("syncSource")).toString());
    clip->m_enabled = map.value(QStringLiteral("enabled"), true).toBool();
    clip->m_durationSeconds =
        map.value(QStringLiteral("durationSeconds"), -1.0).toDouble();
    clip->m_embeddedTimecode =
        map.value(QStringLiteral("embeddedTimecode"), -1.0).toDouble();
    const QString creation = map.value(QStringLiteral("creationTimeUtc")).toString();
    if (!creation.isEmpty()) {
        QDateTime dt = QDateTime::fromString(creation, Qt::ISODateWithMs);
        if (dt.isValid())
            clip->m_creationTimeUtc = dt.toUTC();
    }
    if (map.contains(QStringLiteral("overlayLayout")))
        clip->m_overlayLayout = VideoOverlayLayout::fromVariantMap(
            map.value(QStringLiteral("overlayLayout")).toMap());
    clip->m_colorGrade = QJsonObject::fromVariantMap(
        map.value(QStringLiteral("colorGrade")).toMap());
    return clip;
}

QString VideoClip::thumbnailUrl() const
{
    return ClipThumbnail::imageUrl(m_path);
}
