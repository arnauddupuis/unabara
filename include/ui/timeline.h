#ifndef TIMELINE_H
#define TIMELINE_H

#include <QObject>
#include <QQmlListProperty>
#include "include/core/dive_data.h"

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
    
public:
    explicit Timeline(QObject *parent = nullptr);
    
    // Getters
    DiveData* diveData() const { return m_diveData; }
    double currentTime() const { return m_currentTime; }
    double startTime() const { return m_startTime; }
    double endTime() const { return m_endTime; }
    double zoomFactor() const { return m_zoomFactor; }
    double maxDepth() const;
    double videoOffset() const { return m_videoOffset; }
    QString videoPath() const { return m_videoPath; }
    double videoDuration() const { return m_videoDuration; }
    
    // Setters
    void setDiveData(DiveData* data);
    void setCurrentTime(double time);
    void setStartTime(double time);
    void setEndTime(double time);
    void setZoomFactor(double factor);
    void setVideoOffset(double offset);
    void setVideoPath(const QString &path);
    void setVideoDuration(double duration);
    
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
    
private:
    DiveData* m_diveData;
    double m_currentTime;
    double m_startTime;
    double m_endTime;
    double m_zoomFactor;
    double m_videoOffset;
    QString m_videoPath;
    double m_videoDuration;
    
    // Helper methods
    void updateViewRange();
    void ensureTimeIsVisible(double time);
};

#endif // TIMELINE_H