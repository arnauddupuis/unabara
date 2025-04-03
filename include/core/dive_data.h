#ifndef DIVE_DATA_H
#define DIVE_DATA_H

#include <QObject>
#include <QDateTime>
#include <QVector>
#include <QPair>
#include <QMap>
#include <QString>


struct DiveDataPoint {
    double timestamp;           // Time in seconds from dive start
    double depth;               // Depth in meters
    double temperature;         // Temperature in Celsius
    QVector<double> pressures;  // Tank pressures in bar (multiple cylinders)
    double ndl;                 // No Decompression Limit in minutes
    double ceiling;             // Decompression ceiling in meters
    double o2percent;           // O2 percentage
    double tts;                 // Time To Surface in minutes
    
    // Constructor with default values
    DiveDataPoint(double time = 0.0, double d = 0.0, double temp = 0.0, 
                  double n = 0.0, double ceil = 0.0, 
                  double o2 = 21.0, double t = 0.0)
        : timestamp(time), depth(d), temperature(temp),
          ndl(n), ceiling(ceil), o2percent(o2), tts(t) {}
          
    // Get the pressure for a specific tank (returns 0 if tank doesn't exist)
    Q_INVOKABLE double getPressure(int tankIndex = 0) const {
        return (tankIndex >= 0 && tankIndex < pressures.size()) ? pressures[tankIndex] : 0.0;
    }
    
    // Add a pressure value for a tank
    void addPressure(double pressure, int tankIndex = 0) {
        // Resize the vector if necessary
        if (tankIndex >= pressures.size()) {
            pressures.resize(tankIndex + 1, 0.0);
        }
        pressures[tankIndex] = pressure;
    }
    
    // Get the number of tanks with pressure data
    Q_INVOKABLE int tankCount() const {
        return pressures.size();
    }
};

// Represents a cylinder (tank) used in a dive
struct CylinderInfo {
    int index;              // Index of this cylinder in the dive
    QString description;    // Cylinder description (e.g., "AL80")
    double size;            // Cylinder size in liters
    double workPressure;    // Working pressure in bar
    double o2Percent;       // O2 percentage
    double hePercent;       // Helium percentage (for trimix)
    double startPressure;   // Starting pressure in bar
    double endPressure;     // Ending pressure in bar
    
    CylinderInfo() 
        : index(0), size(0.0), workPressure(0.0), o2Percent(21.0), hePercent(0.0),
          startPressure(0.0), endPressure(0.0) {}
};

struct GasSwitch {
    double timestamp;  // Time in minutes when switch occurred
    int cylinderIndex; // Which cylinder was switched to
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
    Q_PROPERTY(int cylinderCount READ cylinderCount NOTIFY dataChanged)
    
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

    // Cylinder management
    int cylinderCount() const { return m_cylinders.size(); }
    void addCylinder(const CylinderInfo &cylinder);
    const CylinderInfo& cylinderInfo(int index) const;
    QVector<CylinderInfo> cylinders() const { return m_cylinders; }
    bool isCylinderActiveAtTime(int cylinderIndex, double timestamp) const;
    double interpolateCylinderPressure(int cylinderIndex, double timestamp) const;
    double getLastInterpolatedPressure(int cylinderIndex) const;

    // Get a descriptive string for a cylinder
    QString cylinderDescription(int index) const;
    
    // Data management
    void addDataPoint(const DiveDataPoint &point);
    void clearData();
    
    // Get data for a specific time point (interpolated if necessary)
    DiveDataPoint dataAtTime(double time) const;
    
    // Get all data points
    const QVector<DiveDataPoint>& allDataPoints() const { return m_dataPoints; }
    
    // Get data within a time range
    QVector<DiveDataPoint> dataInRange(double startTime, double endTime) const;

    // Public method to add a gas switch
    void addGasSwitch(double timestamp, int cylinderIndex);
    
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
    QVector<CylinderInfo> m_cylinders;
    QList<GasSwitch> m_gasSwitches;
    mutable QMap<int, double> m_lastInterpolatedPressures;
};

#endif // DIVE_DATA_H