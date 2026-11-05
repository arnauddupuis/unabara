#ifndef VIDEO_CLIP_H
#define VIDEO_CLIP_H

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

#include "include/core/video_overlay_layout.h"
#include "include/core/video_probe.h"

// One imported video clip: file identity, probed metadata, sync state
// (syncOffset = dive time at clip t=0 — the semantics of the historical
// Timeline::videoOffset), its overlay layout and an *opaque* colour grade.
// Instances are owned and created by ClipListModel; the type is registered
// uncreatable in QML so the SyncSource/ProbeState enums are visible.
class VideoClip : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(QUrl url READ url CONSTANT)
    Q_PROPERTY(QString displayName READ displayName WRITE setDisplayName NOTIFY displayNameChanged)

    // Probe-derived metadata. All read-only and updated together by
    // applyProbeResult(), so they share one NOTIFY (same pattern as the
    // generator's shadow block).
    Q_PROPERTY(QDateTime creationTimeUtc READ creationTimeUtc NOTIFY probeDataChanged)
    // Epoch-seconds view of creationTimeUtc for QML arithmetic (camera
    // pairing); -1 when the file carries no creation time.
    Q_PROPERTY(qint64 creationTimeSecs READ creationTimeSecs NOTIFY probeDataChanged)
    Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY probeDataChanged)
    Q_PROPERTY(int fpsNum READ fpsNum NOTIFY probeDataChanged)
    Q_PROPERTY(int fpsDen READ fpsDen NOTIFY probeDataChanged)
    Q_PROPERTY(double fps READ fps NOTIFY probeDataChanged)
    Q_PROPERTY(double avgFps READ avgFps NOTIFY probeDataChanged)
    Q_PROPERTY(bool vfr READ vfr NOTIFY probeDataChanged)
    Q_PROPERTY(int width READ width NOTIFY probeDataChanged)
    Q_PROPERTY(int height READ height NOTIFY probeDataChanged)
    Q_PROPERTY(QString codec READ codec NOTIFY probeDataChanged)
    Q_PROPERTY(QString pixFmt READ pixFmt NOTIFY probeDataChanged)
    Q_PROPERTY(int bitDepth READ bitDepth NOTIFY probeDataChanged)
    Q_PROPERTY(QString colorSpace READ colorSpace NOTIFY probeDataChanged)
    Q_PROPERTY(QString colorTransfer READ colorTransfer NOTIFY probeDataChanged)
    Q_PROPERTY(QString colorPrimaries READ colorPrimaries NOTIFY probeDataChanged)
    Q_PROPERTY(QString colorRange READ colorRange NOTIFY probeDataChanged)
    Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY probeDataChanged)
    Q_PROPERTY(QString audioCodec READ audioCodec NOTIFY probeDataChanged)
    // Seconds-of-day, -1 = none.
    Q_PROPERTY(double embeddedTimecode READ embeddedTimecode NOTIFY probeDataChanged)

    Q_PROPERTY(double syncOffset READ syncOffset WRITE setSyncOffset NOTIFY syncOffsetChanged)
    Q_PROPERTY(SyncSource syncSource READ syncSource WRITE setSyncSource NOTIFY syncSourceChanged)
    Q_PROPERTY(QVariantMap overlayLayoutMap READ overlayLayoutMap WRITE setOverlayLayoutMap NOTIFY overlayLayoutChanged)
    // Opaque colour grade (track C defines the contents; the clip model only
    // round-trips it).
    Q_PROPERTY(QVariantMap colorGradeMap READ colorGradeMap WRITE setColorGradeMap NOTIFY colorGradeChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(ProbeState probeState READ probeState NOTIFY probeStateChanged)
    Q_PROPERTY(QString probeError READ probeError NOTIFY probeStateChanged)

public:
    enum SyncSource {
        Unsynced,
        Timecode,
        CameraProfile,
        Manual,
        Restored
    };
    Q_ENUM(SyncSource)

    enum ProbeState {
        NotProbed,
        Probing,
        Probed,
        Failed
    };
    Q_ENUM(ProbeState)

    explicit VideoClip(const QString& path, QObject* parent = nullptr);

    QString path() const { return m_path; }
    QUrl url() const { return QUrl::fromLocalFile(m_path); }

    // image://clipthumb/ source for the ClipStrip card background
    // (follow-up C). Invokable, not a CONSTANT property: each call stats the
    // file so an overwritten clip yields a new url (Qt Quick caches per url).
    Q_INVOKABLE QString thumbnailUrl() const;

    QString displayName() const { return m_displayName; }
    void setDisplayName(const QString& name);

    QDateTime creationTimeUtc() const { return m_creationTimeUtc; }
    qint64 creationTimeSecs() const
    {
        return m_creationTimeUtc.isValid() ? m_creationTimeUtc.toSecsSinceEpoch() : -1;
    }
    double durationSeconds() const { return m_durationSeconds; }
    // Fallback channel for when ffprobe is unavailable and the duration comes
    // from the media player instead (PR A2: VideoSyncPlayer feeds the real
    // MediaPlayer's duration, hence Q_INVOKABLE). Emits probeDataChanged.
    Q_INVOKABLE void setDurationSeconds(double seconds);
    int fpsNum() const { return m_fpsNum; }
    int fpsDen() const { return m_fpsDen; }
    double fps() const { return m_fpsDen > 0 ? static_cast<double>(m_fpsNum) / m_fpsDen : 0.0; }
    double avgFps() const { return m_avgFps; }
    bool vfr() const { return m_vfr; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    QString codec() const { return m_codec; }
    QString pixFmt() const { return m_pixFmt; }
    int bitDepth() const { return m_bitDepth; }
    QString colorSpace() const { return m_colorSpace; }
    QString colorTransfer() const { return m_colorTransfer; }
    QString colorPrimaries() const { return m_colorPrimaries; }
    QString colorRange() const { return m_colorRange; }
    bool hasAudio() const { return m_hasAudio; }
    QString audioCodec() const { return m_audioCodec; }
    double embeddedTimecode() const { return m_embeddedTimecode; }
    // Video STREAM start_time (seconds; 0 when none/unknown). Consumed by
    // the composited export's seek window; not UI-facing, so plain C++
    // accessor only, and not persisted (probe data is re-probed).
    double videoStartTime() const { return m_videoStartTime; }
    // Probed video-stream frame count; -1 until a successful probe. Not
    // persisted (probe metadata is re-probed on restore).
    qint64 nbFrames() const { return m_nbFrames; }

    double syncOffset() const { return m_syncOffset; }
    void setSyncOffset(double offset);
    SyncSource syncSource() const { return m_syncSource; }
    void setSyncSource(SyncSource source);

    VideoOverlayLayout overlayLayout() const { return m_overlayLayout; }
    void setOverlayLayout(const VideoOverlayLayout& layout);
    QVariantMap overlayLayoutMap() const { return m_overlayLayout.toVariantMap(); }
    void setOverlayLayoutMap(const QVariantMap& map);

    QJsonObject colorGrade() const { return m_colorGrade; }
    void setColorGrade(const QJsonObject& grade);
    QVariantMap colorGradeMap() const { return m_colorGrade.toVariantMap(); }
    void setColorGradeMap(const QVariantMap& map);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    ProbeState probeState() const { return m_probeState; }
    void setProbeState(ProbeState state);
    QString probeError() const { return m_probeError; }

    // Copies all probe-derived fields and flips probeState to Probed/Failed.
    void applyProbeResult(const VideoProbe::Result& result);

    // Dive-time span helpers. diveStart() is just syncOffset; diveEnd() adds
    // the duration (equal to diveStart() while the duration is unknown).
    Q_INVOKABLE double diveStart() const { return m_syncOffset; }
    Q_INVOKABLE double diveEnd() const
    {
        return m_syncOffset + (m_durationSeconds > 0.0 ? m_durationSeconds : 0.0);
    }
    Q_INVOKABLE bool containsDiveTime(double t) const
    {
        return m_durationSeconds > 0.0 && t >= diveStart() && t <= diveEnd();
    }

    // Persistence primitives (PR A3 stores these under video/clipStates).
    QVariantMap toVariantMap() const;
    static VideoClip* fromVariantMap(const QVariantMap& map, QObject* parent = nullptr);

    static QString syncSourceToString(SyncSource source);
    static SyncSource syncSourceFromString(const QString& s);

signals:
    void displayNameChanged();
    void probeDataChanged();
    void syncOffsetChanged();
    void syncSourceChanged();
    void overlayLayoutChanged();
    void colorGradeChanged();
    void enabledChanged();
    void probeStateChanged();

private:
    QString m_path;
    QString m_displayName;

    QDateTime m_creationTimeUtc;
    double m_durationSeconds = -1.0;
    int m_fpsNum = 0;
    int m_fpsDen = 1;
    double m_avgFps = -1.0;
    bool m_vfr = false;
    int m_width = 0;
    int m_height = 0;
    QString m_codec;
    QString m_pixFmt;
    int m_bitDepth = 8;
    QString m_colorSpace;
    QString m_colorTransfer;
    QString m_colorPrimaries;
    QString m_colorRange;
    bool m_hasAudio = false;
    QString m_audioCodec;
    double m_embeddedTimecode = -1.0;
    double m_videoStartTime = 0.0;
    qint64 m_nbFrames = -1;

    double m_syncOffset = 0.0;
    SyncSource m_syncSource = Unsynced;
    VideoOverlayLayout m_overlayLayout;
    QJsonObject m_colorGrade;
    bool m_enabled = true;
    ProbeState m_probeState = NotProbed;
    QString m_probeError;
};

#endif // VIDEO_CLIP_H
