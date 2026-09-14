#ifndef IMAGE_EXPORT_H
#define IMAGE_EXPORT_H

#include <QObject>
#include <QString>
#include <QDir>
#include <QImage>
#include "include/core/dive_data.h"
#include "include/generators/i_frame_generator.h"

class ImageExporter : public QObject
{
    Q_OBJECT
    
    Q_PROPERTY(QString exportPath READ exportPath WRITE setExportPath NOTIFY exportPathChanged)
    // Base directory that createDefaultExportDir() creates per-dive subfolders
    // under. Bound from QML (config.lastExportPath) so the exporter itself has
    // no dependency on the settings store.
    Q_PROPERTY(QString baseDirectory READ baseDirectory WRITE setBaseDirectory NOTIFY baseDirectoryChanged)
    Q_PROPERTY(double frameRate READ frameRate WRITE setFrameRate NOTIFY frameRateChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    
public:
    explicit ImageExporter(QObject *parent = nullptr);
    
    // Getters
    QString exportPath() const { return m_exportPath; }
    QString baseDirectory() const { return m_baseDirectory; }
    double frameRate() const { return m_frameRate; }
    int progress() const { return m_progress; }
    bool isBusy() const { return m_busy; }
    
    // Setters
    void setExportPath(const QString &path);
    void setBaseDirectory(const QString &path);
    void setFrameRate(double fps);
    
    // Export methods. `generator` is accepted as a QObject* so QML can pass
    // either OverlayGenerator or ProfileGenerator transparently (the meta-
    // object system can't convert to a non-QObject interface). Internally
    // we dynamic_cast to IFrameGenerator and fail fast if the cast doesn't
    // succeed.
    Q_INVOKABLE bool exportImages(DiveData* dive, QObject* generator);
    Q_INVOKABLE bool exportImageRange(DiveData* dive, QObject* generator,
                                      double startTime, double endTime);

    // Request cancellation of a running export. The export loop runs on the
    // GUI thread and pumps the event loop between frames, which is what
    // delivers this call; the loop then stops, removes the frames written so
    // far and emits exportCancelled().
    Q_INVOKABLE void cancelExport();

    // Create default export directory. `contentType` (e.g. "dive_computer",
    // "dive_profile") is appended to the directory name so that exports of
    // different overlays for the same dive end up in distinct directories.
    Q_INVOKABLE QString createDefaultExportDir(DiveData* dive,
                                               const QString &videoFilePath = QString(),
                                               const QString &contentType = QString());
    
signals:
    void exportPathChanged();
    void baseDirectoryChanged();
    void frameRateChanged();
    void progressChanged();
    void busyChanged();
    void exportStarted();
    void exportFinished(bool success, const QString &path);
    void exportError(const QString &errorMessage);
    void exportCancelled();

private:
    QString m_exportPath;
    QString m_baseDirectory;
    double m_frameRate;
    int m_progress;
    bool m_busy;
    bool m_cancelRequested;
};

#endif // IMAGE_EXPORT_H
