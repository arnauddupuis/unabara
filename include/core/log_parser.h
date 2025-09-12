#ifndef LOG_PARSER_H
#define LOG_PARSER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QFile>
#include <QXmlStreamReader>
#include "include/core/dive_data.h"

class LogParser : public QObject
{
    Q_OBJECT
    
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    
public:
    explicit LogParser(QObject *parent = nullptr);
    
    // Import a dive log file
    Q_INVOKABLE bool importFile(const QString &filePath);
    
    // Import a specific dive from the log
    Q_INVOKABLE bool importDive(const QString &filePath, int diveNumber);
    
    // Get the list of dives from a log file without importing them
    Q_INVOKABLE QList<QString> getDiveList(const QString &filePath);
    
    // Getters
    QString lastError() const { return m_lastError; }
    bool isBusy() const { return m_busy; }
    
signals:
    void diveImported(DiveData* dive);
    void multipleImported(QList<DiveData*> dives);
    void errorOccurred(const QString &error);
    void busyChanged();
    
private:
    // Parse Subsurface XML format
    bool parseSubsurfaceXML(QFile &file, QList<DiveData*> &result, int specificDive = -1);
    
    // Helper functions for XML parsing
    DiveData* parseDiveElement(QXmlStreamReader &xml);
    void parseDiveComputerElement(QXmlStreamReader &xml, DiveData* dive, int &sampleCount);
    void parseSampleElement(QXmlStreamReader &xml, DiveData* dive, double &lastTemperature, double &lastNDL, double &lastTTS, QMap<int, double> &lastPressures, QMap<int, double> &lastPO2Sensors);
    void parseCylinderElement(QXmlStreamReader &xml, DiveData* dive);
    void parseDiveSites(QXmlStreamReader &xml);
    bool isCylinderActiveAtTime(int cylinderIndex, double timestamp) const;

    struct GasSwitch {
        double timestamp;  // Time in minutes when switch occurred
        int cylinderIndex; // Which cylinder was switched to
    };
    
    struct DiveSite {
        QString uuid;       // Unique identifier from the XML
        QString name;       // Site name
        QString gps;        // GPS coordinates
        QString description; // Site description
    };
    
    QString m_lastError;
    bool m_busy;
    QMap<int, double> m_initialCylinderPressures; // Initial pressures for all tanks
    double m_lastCeiling; // Last parsed ceiling depth, puting it here because it's a state that persists between points.
    QList<GasSwitch> m_gasSwitches; // List of gas switches for the current dive
    double m_diveDuration; // Store the total dive duration
    QMap<QString, DiveSite> m_diveSites; // Map of dive site UUID to dive site info
};

#endif // LOG_PARSER_H