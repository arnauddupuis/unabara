#ifndef DIVE_DATA_H
#define DIVE_DATA_H

#include <QObject>
#include <QDateTime>
#include <QVector>
#include <QPair>
#include <QMap>
#include <QString>

// Represents a single data point in a dive
struct DiveDataPoint {
    double timestamp;     // Time in seconds from dive start
    double depth;         // Depth in meters
    double temperature;   // Temperature in Celsius
    double pressure;      // Tank pressure in bar
    double ndl;           // No Decompression Limit in minutes
    double ceiling;       // Decompression ceiling in meters
    double o2percent;     // O2 percentage
    double tts;           // Time To Surface in minutes
    
    // Constructor with default values
    DiveDataPoint(double time = 0.0, double d = 0.0, double temp = 0.0, 
                  double press = 0.0, double n = 0.0, double ceil = 0.0, 
                  double o2 = 21.0, double t = 0.0)
        : timestamp(time), depth(d), temperature(temp),
          pressure(press), ndl(n), ceiling(ceil), o2percent(o2), tts(t) {}
};

class DiveData : public QObject
{
    Q_OBJECT
    
    Q_PROPERTY(QString diveName READ diveName WRITE setDiveName NOTIFY diveNameChanged)
    Q_PROPERTY(QDateTime startTime READ startTime WRITE setStartTime NOTIFY startTimeChanged)
    Q_PROPERTY(int durationSeconds READ durationSeconds NOTIFY durationChanged)
    Q_PROPERTY(double maxDepth READ maxDepth NOTIFY dataChanged)
    Q_PROPERTY(double minTemperature READ minTemperature NOTIFY dataChanged)
    Q_PROPERTY(QString location READ location WRITE setLocation NOTIFY locationChanged)
    
public:
    explicit DiveData(QObject *parent = nullptr);
    
    // Getters
    QString diveName() const { return m_diveName; }
    QDateTime startTime() const { return m_startTime; }
    int durationSeconds() const;
    double maxDepth() const;
    double minTemperature() const;
    QString location() const { return m_location; }
    
    // Setters
    void setDiveName(const QString &name);
    void setStartTime(const QDateTime &time);
    void setLocation(const QString &location);
    
    // Data management
    void addDataPoint(const DiveDataPoint &point);
    void clearData();
    
    // Get data for a specific time point (interpolated if necessary)
    DiveDataPoint dataAtTime(double time) const;
    
    // Get all data points
    const QVector<DiveDataPoint>& allDataPoints() const { return m_dataPoints; }
    
    // Get data within a time range
    QVector<DiveDataPoint> dataInRange(double startTime, double endTime) const;
    
signals:
    void diveNameChanged();
    void startTimeChanged();
    void durationChanged();
    void dataChanged();
    void locationChanged();
    
private:
    QString m_diveName;
    QDateTime m_startTime;
    QString m_location;
    QVector<DiveDataPoint> m_dataPoints;
};

#endif // DIVE_DATA_H