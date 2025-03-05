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
    void parseSampleElement(QXmlStreamReader &xml, DiveData* dive);
    
    QString m_lastError;
    bool m_busy;
};

#endif // LOG_PARSER_H