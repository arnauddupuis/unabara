#include "include/core/dive_data.h"
#include <algorithm>
#include <cmath>

DiveData::DiveData(QObject *parent)
    : QObject(parent), m_diveNumber(0)
{
}

void DiveData::setDiveName(const QString &name)
{
    if (m_diveName != name) {
        m_diveName = name;
        emit diveNameChanged();
    }
}

void DiveData::setStartTime(const QDateTime &time)
{
    if (m_startTime != time) {
        m_startTime = time;
        emit startTimeChanged();
    }
}

void DiveData::setLocation(const QString &location)
{
    if (m_location != location) {
        m_location = location;
        emit locationChanged();
    }
}

void DiveData::setDiveNumber(int number)
{
    if (m_diveNumber != number) {
        m_diveNumber = number;
        emit diveNumberChanged();
    }
}

void DiveData::setDiveSiteName(const QString &siteName)
{
    if (m_diveSiteName != siteName) {
        m_diveSiteName = siteName;
        emit diveSiteNameChanged();
    }
}

void DiveData::setDiveSiteId(const QString &siteId)
{
    if (m_diveSiteId != siteId) {
        m_diveSiteId = siteId;
        emit diveSiteIdChanged();
    }
}

int DiveData::durationSeconds() const
{
    if (m_dataPoints.isEmpty()) {
        return 0;
    }
    
    // Return the timestamp of the last data point, which represents
    // the total duration of the dive in seconds
    return static_cast<int>(m_dataPoints.last().timestamp);
}

double DiveData::maxDepth() const
{
    if (m_dataPoints.isEmpty()) {
        return 0.0;
    }
    
    double max = 0.0;
    for (const auto &point : m_dataPoints) {
        if (point.depth > max) {
            max = point.depth;
        }
    }
    
    return max;
}

double DiveData::minTemperature() const
{
    if (m_dataPoints.isEmpty()) {
        return 0.0;
    }
    
    double min = m_dataPoints.first().temperature;
    for (const auto &point : m_dataPoints) {
        if (point.temperature < min && point.temperature > 0.0) {
            min = point.temperature;
        }
    }
    
    return min;
}

void DiveData::addCylinder(const CylinderInfo &cylinder)
{
    CylinderInfo newCyl = cylinder;
    newCyl.index = m_cylinders.size(); // Set the index
    m_cylinders.append(newCyl);
    emit dataChanged();
}

const CylinderInfo& DiveData::cylinderInfo(int index) const
{
    static CylinderInfo defaultCylinder;
    if (index >= 0 && index < m_cylinders.size()) {
        return m_cylinders[index];
    }
    return defaultCylinder;
}

QString DiveData::cylinderDescription(int index) const
{
    if (index >= 0 && index < m_cylinders.size()) {
        const CylinderInfo &cyl = m_cylinders[index];
        
        QString desc;
        if (!cyl.description.isEmpty()) {
            desc = cyl.description;
        } else {
            desc = QString("Tank %1").arg(index + 1);
        }
        
        // Add gas mix if available
        if (cyl.hePercent > 0.0) {
            // Trimix
            desc += QString(" (Trimix %1/%2)").arg(qRound(cyl.o2Percent)).arg(qRound(cyl.hePercent));
        } else if (cyl.o2Percent != 21.0) {
            // Non-air (Nitrox or O2)
            desc += QString(" (EAN%1)").arg(qRound(cyl.o2Percent));
        }
        
        return desc;
    }
    return QString("Unknown");
}

void DiveData::addDataPoint(const DiveDataPoint &point)
{
    // Insert the point in the right position to maintain chronological order
    auto it = std::lower_bound(m_dataPoints.begin(), m_dataPoints.end(), point,
                               [](const DiveDataPoint &a, const DiveDataPoint &b) {
                                   return a.timestamp < b.timestamp;
                               });
    
    int index = it - m_dataPoints.begin();
    m_dataPoints.insert(index, point);
    
    emit dataChanged();
    emit durationChanged();
}

void DiveData::clearData()
{
    m_dataPoints.clear();
    emit dataChanged();
    emit durationChanged();
}

DiveDataPoint DiveData::dataAtTime(double time) const
{
    if (m_dataPoints.isEmpty()) {
        return DiveDataPoint();
    }
    
    // If time is before the first data point, return the first point
    if (time <= m_dataPoints.first().timestamp) {
        return m_dataPoints.first();
    }
    
    // If time is after the last data point, return the last point
    if (time >= m_dataPoints.last().timestamp) {
        return m_dataPoints.last();
    }
    
    // Find the two data points surrounding the requested time
    int index = 0;
    while (index < m_dataPoints.size() && m_dataPoints[index].timestamp < time) {
        index++;
    }
    
    // Points surrounding the time
    const DiveDataPoint &prev = m_dataPoints[index - 1];
    const DiveDataPoint &next = m_dataPoints[index];
    
    // Calculate interpolation factor (0.0 to 1.0)
    double factor = (time - prev.timestamp) / (next.timestamp - prev.timestamp);
    
    // Linearly interpolate all values
    DiveDataPoint result;
    result.timestamp = time;
    result.depth = prev.depth + factor * (next.depth - prev.depth);
    result.temperature = prev.temperature + factor * (next.temperature - prev.temperature);
    result.ndl = prev.ndl + factor * (next.ndl - prev.ndl);
    result.o2percent = prev.o2percent + factor * (next.o2percent - prev.o2percent);
    result.tts = prev.tts + factor * (next.tts - prev.tts);

    // Do NOT interpolate ceiling - use the value from the previous point
    // Ceiling is a state that persists until changed
    result.ceiling = prev.ceiling;
    
    // For in_deco state, we also shouldn't interpolate (though it's not part of DiveDataPoint directly)
    
    // Interpolate all tank pressures
    // Get the maximum number of tanks between both points
    int maxTanks = qMax(prev.tankCount(), next.tankCount());
    for (int i = 0; i < maxTanks; i++) {
        double prevPressure = prev.getPressure(i);
        double nextPressure = next.getPressure(i);
        double interpolatedPressure = prevPressure + factor * (nextPressure - prevPressure);
        result.addPressure(interpolatedPressure, i);
    }
    
    return result;
}

QVector<DiveDataPoint> DiveData::dataInRange(double startTime, double endTime) const
{
    QVector<DiveDataPoint> result;
    
    qDebug() << "DiveData::dataInRange - Requested data from" << startTime << "to" << endTime;
    qDebug() << "DiveData::dataInRange - Total data points available:" << m_dataPoints.size();
    
    // Find the first point after or at startTime
    auto startIt = std::lower_bound(m_dataPoints.begin(), m_dataPoints.end(), DiveDataPoint(startTime),
                                    [](const DiveDataPoint &a, const DiveDataPoint &b) {
                                        return a.timestamp < b.timestamp;
                                    });
    
    // Find the first point after endTime
    auto endIt = std::upper_bound(m_dataPoints.begin(), m_dataPoints.end(), DiveDataPoint(endTime),
                                  [](const DiveDataPoint &a, const DiveDataPoint &b) {
                                      return a.timestamp < b.timestamp;
                                  });
    
    // Insert starting point at exact startTime (interpolated if necessary)
    if (startIt != m_dataPoints.begin() && startIt != m_dataPoints.end() && startIt->timestamp != startTime) {
        result.append(dataAtTime(startTime));
    }
    
    // Copy all points in range
    for (auto it = startIt; it != endIt; ++it) {
        result.append(*it);
    }
    
    // Insert ending point at exact endTime (interpolated if necessary)
    if (endIt != m_dataPoints.begin() && endIt != m_dataPoints.end() && (endIt - 1)->timestamp != endTime) {
        result.append(dataAtTime(endTime));
    }
    
    qDebug() << "DiveData::dataInRange - Returning" << result.size() << "data points";
    
    // Print some sample data for debugging
    if (!result.isEmpty()) {
        qDebug() << "DiveData::dataInRange - First point:" 
                 << "time=" << result.first().timestamp 
                 << "depth=" << result.first().depth;
        
        if (result.size() > 1) {
            qDebug() << "DiveData::dataInRange - Last point:" 
                     << "time=" << result.last().timestamp 
                     << "depth=" << result.last().depth;
        }
    } else {
        qDebug() << "DiveData::dataInRange - No data points in range";
    }
    
    return result;
}

void DiveData::addGasSwitch(double timestamp, int cylinderIndex) {
    if (cylinderIndex < 0 || cylinderIndex >= m_cylinders.size()) {
        return;  // Invalid cylinder index
    }
    
    GasSwitch gasSwitch;
    gasSwitch.timestamp = timestamp;
    gasSwitch.cylinderIndex = cylinderIndex;
    m_gasSwitches.append(gasSwitch);
    
    // Keep gas switches sorted by timestamp
    std::sort(m_gasSwitches.begin(), m_gasSwitches.end(), 
              [](const GasSwitch &a, const GasSwitch &b) {
                  return a.timestamp < b.timestamp;
              });
}

bool DiveData::isCylinderActiveAtTime(int cylinderIndex, double timestamp) const {
    if (cylinderIndex < 0 || cylinderIndex >= m_cylinders.size()) {
        return false;  // Invalid cylinder index
    }
    
    // If we have no gas switches, assume first cylinder is always active
    if (m_gasSwitches.isEmpty()) {
        return cylinderIndex == 0;
    }
    
    // Find the active cylinder at this timestamp
    int activeCylinder = 0; // Default to first cylinder
    
    for (const GasSwitch &gasSwitch : m_gasSwitches) {
        if (gasSwitch.timestamp <= timestamp) {
            activeCylinder = gasSwitch.cylinderIndex;
        } else {
            // Gas switches are sorted by time, so once we find one
            // that's after our timestamp, we can stop looking
            break;
        }
    }
    
    return (cylinderIndex == activeCylinder);
}

double DiveData::interpolateCylinderPressure(int cylinderIndex, double timestamp) const
{
    qDebug() << "INTERPOLATION FUNCTION CALLED for cylinder" << cylinderIndex << "at time" << timestamp;

    // if (cylinderIndex < 0 || cylinderIndex >= m_cylinders.size()) {
    //     qDebug() << "Invalid cylinder index for interpolation:" << cylinderIndex;
    //     return 0.0;
    // }
    
    // const CylinderInfo &cylinder = m_cylinders[cylinderIndex];
    
    // // For debugging
    // qDebug() << "Interpolating pressure for" << cylinder.description
    //          << "with start pressure:" << cylinder.startPressure
    //          << "end pressure:" << cylinder.endPressure
    //          << "at time:" << timestamp << "/" << this->durationSeconds()
    //          << "(" << (timestamp * 100.0 / std::max(1.0, static_cast<double>(this->durationSeconds()))) << "%)";
    
    // if (cylinder.startPressure <= 0.0 || cylinder.endPressure <= 0.0) {
    //     qDebug() << "Cannot interpolate: missing start or end pressure";
    //     return 0.0;
    // }
    
    // double diveDuration = this->durationSeconds();
    // if (diveDuration <= 0.0) {
    //     qDebug() << "Cannot interpolate: dive duration is zero";
    //     return cylinder.startPressure;
    // }
    
    // // Calculate how far we are through the dive (0.0 to 1.0)
    // double diveFraction = timestamp / diveDuration;
    
    // // Cap to range [0,1]
    // diveFraction = std::max(0.0, std::min(1.0, diveFraction));
    
    // // Linear interpolation between start and end pressures
    // double interpolatedPressure = cylinder.startPressure - 
    //                              (diveFraction * (cylinder.startPressure - cylinder.endPressure));
    
    // qDebug() << "Interpolated pressure:" << interpolatedPressure
    //          << "at fraction:" << diveFraction;
    
    // return interpolatedPressure;
    if (cylinderIndex < 0 || cylinderIndex >= m_cylinders.size()) {
        qDebug() << "Invalid cylinder index for interpolation:" << cylinderIndex;
        return 0.0;
    }
    
    const CylinderInfo &cylinder = m_cylinders[cylinderIndex];
    
    if (cylinder.startPressure <= 0.0 || cylinder.endPressure <= 0.0) {
        qDebug() << "Cannot interpolate: missing start or end pressure";
        return 0.0;
    }
    
    // Determine the time range when this cylinder was active
    double startTime = 0.0;
    double endTime = this->durationSeconds();
    
    // For multiple tanks, find when this cylinder became active
    if (!m_gasSwitches.isEmpty()) {
        bool foundActiveSwitch = false;
        
        // Find when this cylinder started being used
        for (int i = 0; i < m_gasSwitches.size(); i++) {
            if (m_gasSwitches[i].cylinderIndex == cylinderIndex) {
                startTime = m_gasSwitches[i].timestamp;
                foundActiveSwitch = true;
                break;
            }
        }
        
        // Find when this cylinder stopped being used
        if (foundActiveSwitch) {
            for (int i = 0; i < m_gasSwitches.size(); i++) {
                if (m_gasSwitches[i].timestamp > startTime && 
                    m_gasSwitches[i].cylinderIndex != cylinderIndex) {
                    endTime = m_gasSwitches[i].timestamp;
                    break;
                }
            }
        }
    }
    
    qDebug() << "Interpolating pressure for" << cylinder.description
             << "with start pressure:" << cylinder.startPressure
             << "O2:" << cylinder.o2Percent
             << "He:" << cylinder.hePercent
             << "end pressure:" << cylinder.endPressure
             << "from time" << startTime << "to" << endTime
             << "at time:" << timestamp;
    
    // If timestamp is outside the active period
    if (timestamp < startTime) {
        qDebug() << "Timestamp before cylinder active period, returning start pressure:" 
                 << cylinder.startPressure;
        return cylinder.startPressure;
    }
    
    if (timestamp > endTime) {
        qDebug() << "Timestamp after cylinder active period, returning end pressure:" 
                 << cylinder.endPressure;
        return cylinder.endPressure;
    }
    
    // Calculate fraction within the active period (0.0 to 1.0)
    double usageRange = endTime - startTime;
    if (usageRange <= 0.0) {
        qDebug() << "Zero usage range, returning start pressure:" << cylinder.startPressure;
        return cylinder.startPressure;
    }
    
    double usageFraction = (timestamp - startTime) / usageRange;
    
    // Cap to range [0,1]
    usageFraction = std::max(0.0, std::min(1.0, usageFraction));
    
    // Linear interpolation between start and end pressures
    double interpolatedPressure = cylinder.startPressure - 
                                (usageFraction * (cylinder.startPressure - cylinder.endPressure));
    
    qDebug() << "Interpolated pressure:" << interpolatedPressure
             << "at usage fraction:" << usageFraction;
    
    // Store the result for future reference
    m_lastInterpolatedPressures[cylinderIndex] = interpolatedPressure;

    return interpolatedPressure;
}

double DiveData::getLastInterpolatedPressure(int cylinderIndex) const
{
    return m_lastInterpolatedPressures.value(cylinderIndex, 0.0);
}