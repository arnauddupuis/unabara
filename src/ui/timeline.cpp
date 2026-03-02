#include "include/ui/timeline.h"
#include <QVariantList>
#include <QVariantMap>
#include <cmath>
#include <algorithm>
#include "include/generators/overlay_image_provider.h"

Timeline::Timeline(QObject *parent)
    : QObject(parent)
    , m_diveData(nullptr)
    , m_currentTime(0.0)
    , m_startTime(0.0)
    , m_endTime(0.0)
    , m_zoomFactor(1.0)
    , m_videoOffset(0.0)
    , m_videoDuration(0.0)
{
}

void Timeline::setDiveData(DiveData* data)
{
    if (m_diveData != data) {
        m_diveData = data;
        
        // Reset timeline view when setting new data
        if (m_diveData) {
            m_currentTime = 0.0;
            m_startTime = 0.0;
            m_endTime = m_diveData->durationSeconds();
            m_zoomFactor = 1.0;
        } else {
            m_currentTime = 0.0;
            m_startTime = 0.0;
            m_endTime = 0.0;
            m_zoomFactor = 1.0;
        }
        
        emit diveDataChanged();
        emit currentTimeChanged();
        emit viewRangeChanged();
        emit zoomFactorChanged();
        emit metricsChanged();
    }
}

void Timeline::setCurrentTime(double time)
{
    if (m_diveData) {
        // Ensure time is within valid range
        time = std::max(0.0, std::min(time, static_cast<double>(m_diveData->durationSeconds())));
        
        if (m_currentTime != time) {
            m_currentTime = time;
            ensureTimeIsVisible(time);
            
            // Update the image provider with the new time
            extern OverlayImageProvider* g_imageProvider;
            if (g_imageProvider) {
                g_imageProvider->setCurrentTime(time);
            }
            
            emit currentTimeChanged();
        }
    }
}

void Timeline::setStartTime(double time)
{
    if (m_diveData) {
        // Ensure the start time is valid
        time = std::max(0.0, std::min(time, m_endTime - 1.0));
        
        if (m_startTime != time) {
            m_startTime = time;
            emit viewRangeChanged();
        }
    }
}

void Timeline::setEndTime(double time)
{
    if (m_diveData) {
        // Ensure the end time is valid
        double maxDuration = static_cast<double>(m_diveData->durationSeconds());
        time = std::max(m_startTime + 1.0, std::min(time, maxDuration));
        
        if (m_endTime != time) {
            m_endTime = time;
            emit viewRangeChanged();
        }
    }
}

void Timeline::setZoomFactor(double factor)
{
    // Limit zoom factor to reasonable range
    factor = std::max(0.1, std::min(factor, 10.0));
    
    if (m_zoomFactor != factor) {
        m_zoomFactor = factor;
        updateViewRange();
        emit zoomFactorChanged();
    }
}

void Timeline::setVideoOffset(double offset)
{
    if (m_videoOffset != offset) {
        m_videoOffset = offset;
        emit videoOffsetChanged();
    }
}

void Timeline::setVideoPath(const QString &path)
{
    if (m_videoPath != path) {
        m_videoPath = path;
        
        // If the path is empty, reset video duration
        if (path.isEmpty()) {
            setVideoDuration(0.0);
        }
        
        emit videoPathChanged();
    }
}

void Timeline::setVideoDuration(double duration)
{
    // Ensure duration is non-negative
    duration = std::max(0.0, duration);
    
    if (m_videoDuration != duration) {
        m_videoDuration = duration;
        emit videoDurationChanged();
    }
}

double Timeline::maxDepth() const
{
    if (m_diveData) {
        return m_diveData->maxDepth();
    }
    return 0.0;
}

void Timeline::zoomIn()
{
    setZoomFactor(m_zoomFactor * 1.2);
}

void Timeline::zoomOut()
{
    setZoomFactor(m_zoomFactor / 1.2);
}

void Timeline::resetZoom()
{
    setZoomFactor(1.0);
}

void Timeline::moveLeft()
{
    if (m_diveData) {
        double visibleRange = m_endTime - m_startTime;
        double moveAmount = visibleRange * 0.2; // Move 20% of visible range
        
        setStartTime(m_startTime - moveAmount);
        setEndTime(m_endTime - moveAmount);
    }
}

void Timeline::moveRight()
{
    if (m_diveData) {
        double visibleRange = m_endTime - m_startTime;
        double moveAmount = visibleRange * 0.2; // Move 20% of visible range
        
        setStartTime(m_startTime + moveAmount);
        setEndTime(m_endTime + moveAmount);
    }
}

void Timeline::goToStart()
{
    setCurrentTime(0.0);
}

void Timeline::goToEnd()
{
    if (m_diveData) {
        setCurrentTime(m_diveData->durationSeconds());
    }
}

QVariantList Timeline::getTimelineData(int numPoints)
{
    QVariantList result;
    
    if (!m_diveData || numPoints <= 0) {
        qDebug() << "Timeline::getTimelineData - No dive data or invalid numPoints:" << numPoints;
        return result;
    }
    
    qDebug() << "Timeline::getTimelineData - Getting data for dive:" << m_diveData->diveName()
             << "from" << m_startTime << "to" << m_endTime
             << "with" << numPoints << "points";
    
    // Get data points within the visible time range
    QVector<DiveDataPoint> rangeData = m_diveData->dataInRange(m_startTime, m_endTime);
    
    qDebug() << "Timeline::getTimelineData - Got" << rangeData.size() << "data points in range";
    
    // If we have fewer points than requested, just return all the points we have
    if (rangeData.size() <= numPoints) {
        for (const auto &point : rangeData) {
            QVariantMap pointMap;
            pointMap["timestamp"] = point.timestamp;
            pointMap["depth"] = point.depth;
            pointMap["temperature"] = point.temperature;
            // Use first tank's pressure (index 0) if available
            pointMap["pressure"] = point.getPressure(0);
            pointMap["ndl"] = point.ndl;
            pointMap["ceiling"] = point.ceiling;
            pointMap["o2percent"] = point.o2percent;
            result.append(pointMap);
        }
    } else {
        // We need to resample the data to get exactly numPoints
        double stepSize = static_cast<double>(rangeData.size()) / numPoints;
        
        for (int i = 0; i < numPoints; i++) {
            int index = static_cast<int>(i * stepSize);
            if (index >= rangeData.size()) {
                index = rangeData.size() - 1;
            }
            
            const DiveDataPoint &point = rangeData[index];
            QVariantMap pointMap;
            pointMap["timestamp"] = point.timestamp;
            pointMap["depth"] = point.depth;
            pointMap["temperature"] = point.temperature;
            // Use first tank's pressure (index 0) if available
            pointMap["pressure"] = point.getPressure(0);
            pointMap["ndl"] = point.ndl;
            pointMap["ceiling"] = point.ceiling;
            pointMap["o2percent"] = point.o2percent;
            result.append(pointMap);
        }
    }
    
    // Print the first few points for debugging
    if (!result.isEmpty()) {
        qDebug() << "Timeline::getTimelineData - First point:" 
                 << "time=" << result.first().toMap()["timestamp"].toDouble()
                 << "depth=" << result.first().toMap()["depth"].toDouble();
        
        if (result.size() > 1) {
            qDebug() << "Timeline::getTimelineData - Last point:" 
                     << "time=" << result.last().toMap()["timestamp"].toDouble()
                     << "depth=" << result.last().toMap()["depth"].toDouble();
        }
    } else {
        qDebug() << "Timeline::getTimelineData - No data points generated";
    }
    
    return result;
}

QVariantMap Timeline::getCurrentDataPoint() const
{
    QVariantMap result;
    
    if (m_diveData) {
        DiveDataPoint point = m_diveData->dataAtTime(m_currentTime);
        
        // Add all basic data
        result["timestamp"] = point.timestamp;
        result["depth"] = point.depth;
        result["temperature"] = point.temperature;
        result["ndl"] = point.ndl;
        result["ceiling"] = point.ceiling;
        result["o2percent"] = point.o2percent;
        result["tts"] = point.tts;
        
        // Add tank count
        result["tankCount"] = point.tankCount();
        
        // For backward compatibility, include the first tank's pressure as "pressure"
        result["pressure"] = point.getPressure(0);
    }
    
    return result;
}

void Timeline::updateViewRange()
{
    if (!m_diveData) {
        return;
    }
    
    double fullDuration = static_cast<double>(m_diveData->durationSeconds());
    
    // Adjust the visible time range based on zoom factor
    double visibleDuration = fullDuration / m_zoomFactor;
    
    // Keep the current time centered if possible
    double halfVisible = visibleDuration / 2.0;
    double newStart = m_currentTime - halfVisible;
    double newEnd = m_currentTime + halfVisible;
    
    // Ensure the new range is within the dive duration
    if (newStart < 0.0) {
        newStart = 0.0;
        newEnd = std::min(visibleDuration, fullDuration);
    }
    
    if (newEnd > fullDuration) {
        newEnd = fullDuration;
        newStart = std::max(0.0, fullDuration - visibleDuration);
    }
    
    // Update the view range
    if (m_startTime != newStart || m_endTime != newEnd) {
        m_startTime = newStart;
        m_endTime = newEnd;
        emit viewRangeChanged();
    }
}

void Timeline::ensureTimeIsVisible(double time)
{
    if (!m_diveData) {
        return;
    }
    
    // Check if the time is already within the visible range
    if (time >= m_startTime && time <= m_endTime) {
        return;
    }
    
    // Calculate the visible range
    double visibleRange = m_endTime - m_startTime;
    
    // If time is before the visible range, adjust the start time
    if (time < m_startTime) {
        m_startTime = time;
        m_endTime = m_startTime + visibleRange;
    } 
    // If time is after the visible range, adjust the end time
    else if (time > m_endTime) {
        m_endTime = time;
        m_startTime = m_endTime - visibleRange;
    }
    
    // Ensure the start time is not negative
    if (m_startTime < 0.0) {
        m_startTime = 0.0;
        m_endTime = std::min(visibleRange, static_cast<double>(m_diveData->durationSeconds()));
    }
    
    // Ensure the end time is not beyond the dive duration
    double maxDuration = static_cast<double>(m_diveData->durationSeconds());
    if (m_endTime > maxDuration) {
        m_endTime = maxDuration;
        m_startTime = std::max(0.0, maxDuration - visibleRange);
    }
    
    emit viewRangeChanged();
}

// Video utility functions
double Timeline::getVideoStartTime() const 
{ 
    return m_videoOffset; 
}

double Timeline::getVideoEndTime() const 
{ 
    return m_videoOffset + m_videoDuration; 
}

bool Timeline::isTimeInVideo(double time) const 
{ 
    return (time >= m_videoOffset && time <= (m_videoOffset + m_videoDuration)); 
}