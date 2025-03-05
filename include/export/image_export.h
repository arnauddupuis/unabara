#ifndef IMAGE_EXPORT_H
#define IMAGE_EXPORT_H

#include <QObject>
#include <QString>
#include <QDir>
#include <QImage>
#include "include/core/dive_data.h"
#include "include/generators/overlay_gen.h"

class ImageExporter : public QObject
{
    Q_OBJECT
    
    Q_PROPERTY(QString exportPath READ exportPath WRITE setExportPath NOTIFY exportPathChanged)
    Q_PROPERTY(double frameRate READ frameRate WRITE setFrameRate NOTIFY frameRateChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    
public:
    explicit ImageExporter(QObject *parent = nullptr);
    
    // Getters
    QString exportPath() const { return m_exportPath; }
    double frameRate() const { return m_frameRate; }
    int progress() const { return m_progress; }
    bool isBusy() const { return m_busy; }
    
    // Setters
    void setExportPath(const QString &path);
    void setFrameRate(double fps);
    
    // Export methods
    Q_INVOKABLE bool exportImages(DiveData* dive, OverlayGenerator* generator);
    Q_INVOKABLE bool exportImageRange(DiveData* dive, OverlayGenerator* generator,
                                      double startTime, double endTime);
    
    // Create default export directory
    Q_INVOKABLE QString createDefaultExportDir(DiveData* dive);
    
signals:
    void exportPathChanged();
    void frameRateChanged();
    void progressChanged();
    void busyChanged();
    void exportStarted();
    void exportFinished(bool success, const QString &path);
    void exportError(const QString &errorMessage);
    
private:
    QString m_exportPath;
    double m_frameRate;
    int m_progress;
    bool m_busy;
    
    // Helper methods
    QString generateUniqueDirectoryName(DiveData* dive);
    QString sanitizeFileName(const QString &fileName);
};

#endif // IMAGE_EXPORT_H
