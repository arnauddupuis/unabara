#ifndef TIMELINE_H
#define TIMELINE_H

#include <QObject>
#include <QPointer>
#include <QQmlListProperty>
#include "include/core/dive_data.h"
// Full includes, not forward declarations: moc-generated metatype
// registration for the ClipListModel*/VideoClip* Q_PROPERTYs needs the
// complete types.
#include "include/core/clip_list_model.h"
#include "include/core/video_clip.h"

// Timeline state for the shared bottom pane. Since PR A2 the video properties
// (videoPath / videoOffset / videoDuration) are a FACADE over the clip model's
// active clip: they keep their historical names, read/write semantics and
// NOTIFY signals so every existing QML binding (zone drag, offset SpinBox,
// export ranges, videoSyncMode, hidden-tab guards) is untouched, but the data
// lives on the active VideoClip. With no active clip the reads return
// "" / 0 / 0 and writes are dropped with a qWarning. A videoOffset write
// through the facade is, by definition, a manual sync gesture (zone drag,
// SpinBox, nudge, dive-time field) and marks the clip SyncSource::Manual;
// programmatic syncs (timecode auto-sync, camera profile apply) set the
// clip's offset + source directly and bypass the facade.
class Timeline : public QObject
{
    Q_OBJECT

    Q_PROPERTY(DiveData* diveData READ diveData WRITE setDiveData NOTIFY diveDataChanged)
    Q_PROPERTY(double currentTime READ currentTime WRITE setCurrentTime NOTIFY currentTimeChanged)
    Q_PROPERTY(double startTime READ startTime WRITE setStartTime NOTIFY viewRangeChanged)
    Q_PROPERTY(double endTime READ endTime WRITE setEndTime NOTIFY viewRangeChanged)
    Q_PROPERTY(double zoomFactor READ zoomFactor WRITE setZoomFactor NOTIFY zoomFactorChanged)
    Q_PROPERTY(double maxDepth READ maxDepth NOTIFY metricsChanged)
    Q_PROPERTY(double videoOffset READ videoOffset WRITE setVideoOffset NOTIFY videoOffsetChanged)
    Q_PROPERTY(QString videoPath READ videoPath WRITE setVideoPath NOTIFY videoPathChanged)
    Q_PROPERTY(double videoDuration READ videoDuration WRITE setVideoDuration NOTIFY videoDurationChanged)
    // The clip model this facade reads/writes through; bound to the
    // `clipList` context property where the Timeline is instantiated
    // (TimelineView.qml).
    Q_PROPERTY(ClipListModel* clipModel READ clipModel WRITE setClipModel NOTIFY clipModelChanged)
    Q_PROPERTY(VideoClip* activeClip READ activeClip NOTIFY activeClipChanged)

public:
    explicit Timeline(QObject *parent = nullptr);

    // Getters
    DiveData* diveData() const { return m_diveData; }
    double currentTime() const { return m_currentTime; }
    double startTime() const { return m_startTime; }
    double endTime() const { return m_endTime; }
    double zoomFactor() const { return m_zoomFactor; }
    double maxDepth() const;
    double videoOffset() const;
    QString videoPath() const;
    double videoDuration() const;
    ClipListModel* clipModel() const { return m_clipModel; }
    VideoClip* activeClip() const;

    // Setters
    void setDiveData(DiveData* data);
    void setCurrentTime(double time);
    void setStartTime(double time);
    void setEndTime(double time);
    void setZoomFactor(double factor);
    void setVideoOffset(double offset);
    void setVideoPath(const QString &path);
    void setVideoDuration(double duration);
    void setClipModel(ClipListModel* model);
    
    // Timeline manipulation
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void resetZoom();
    Q_INVOKABLE void moveLeft();
    Q_INVOKABLE void moveRight();
    Q_INVOKABLE void goToStart();
    Q_INVOKABLE void goToEnd();
    
    // Data access for QML
    Q_INVOKABLE QVariantList getTimelineData(int numPoints);
    Q_INVOKABLE QVariantMap getCurrentDataPoint() const;

    // Video functions
    Q_INVOKABLE double getVideoStartTime() const;
    Q_INVOKABLE double getVideoEndTime() const;
    Q_INVOKABLE bool isTimeInVideo(double time) const;
    
signals:
    void diveDataChanged();
    void currentTimeChanged();
    void viewRangeChanged();
    void zoomFactorChanged();
    void metricsChanged();
    void videoOffsetChanged();
    void videoPathChanged();
    void videoDurationChanged();
    void clipModelChanged();
    void activeClipChanged();

private:
    DiveData* m_diveData;
    double m_currentTime;
    double m_startTime;
    double m_endTime;
    double m_zoomFactor;
    QPointer<ClipListModel> m_clipModel;
    // The clip whose change signals are currently re-emitted as the facade
    // NOTIFYs; tracked so the connections move when the active clip does.
    QPointer<VideoClip> m_facadeClip;

    // Helper methods
    void updateViewRange();
    void ensureTimeIsVisible(double time);
    // Re-targets m_facadeClip's connections and re-emits every facade NOTIFY
    // (path/offset/duration can all change when the active clip does).
    void refreshActiveClip();

    // Standardize time precision to milliseconds (3 decimals)
    static inline double roundToMilliseconds(double time) {
        return qRound(time * 1000.0) / 1000.0;
    }
};

#endif // TIMELINE_H