#ifndef VIDEO_EXPORT_H
#define VIDEO_EXPORT_H

#include <QObject>
#include <QString>
#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <QSize>
#include "include/core/dive_data.h"
#include "include/generators/overlay_gen.h"

class VideoExporter : public QObject
{
    Q_OBJECT
    
    Q_PROPERTY(QString exportPath READ exportPath WRITE setExportPath NOTIFY exportPathChanged)
    Q_PROPERTY(double frameRate READ frameRate WRITE setFrameRate NOTIFY frameRateChanged)
    Q_PROPERTY(int videoBitrate READ videoBitrate WRITE setVideoBitrate NOTIFY videoBitrateChanged)
    Q_PROPERTY(QString videoCodec READ videoCodec WRITE setVideoCodec NOTIFY videoCodecChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(QSize customResolution READ customResolution WRITE setCustomResolution NOTIFY customResolutionChanged)
    
public:
    explicit VideoExporter(QObject *parent = nullptr);
    ~VideoExporter();
    
    // Codec options
    enum VideoCodec {
        H264,      // Standard MP4 H.264, most compatible
        ProRes,    // Apple ProRes, higher quality with alpha
        VP9,       // WebM VP9, good for web
        HEVC       // H.265, better compression
    };
    Q_ENUM(VideoCodec)
    
    // Getters
    QString exportPath() const { return m_exportPath; }
    double frameRate() const { return m_frameRate; }
    int videoBitrate() const { return m_videoBitrate; }
    QString videoCodec() const { return m_videoCodec; }
    int progress() const { return m_progress; }
    bool isBusy() const { return m_busy; }
    QSize customResolution() const { return m_customResolution; }
    
    // Setters
    void setExportPath(const QString &path);
    void setFrameRate(double fps);
    void setVideoBitrate(int bitrate);
    void setVideoCodec(const QString &codec);
    void setCustomResolution(const QSize &size);
    
    // Export methods
    Q_INVOKABLE bool exportVideo(DiveData* dive, OverlayGenerator* generator,
                                double startTime, double endTime);
    Q_INVOKABLE void cancelExport();
    
    // Helper methods
    Q_INVOKABLE bool isFFmpegAvailable();
    Q_INVOKABLE QString createDefaultExportFile(DiveData* dive);
    Q_INVOKABLE QSize detectVideoResolution(const QString &videoPath);
    
    // Get information about available codecs and formats
    Q_INVOKABLE QStringList getAvailableCodecs();
    Q_INVOKABLE QString getFileExtensionForCodec(const QString &codec);
    
signals:
    void exportPathChanged();
    void frameRateChanged();
    void videoBitrateChanged();
    void videoCodecChanged();
    void progressChanged();
    void busyChanged();
    void exportStarted();
    void exportFinished(bool success, const QString &path);
    void exportError(const QString &errorMessage);
    void statusUpdate(const QString &message);
    void customResolutionChanged();
    
private slots:
    void processFFmpegOutput();
    void onFFmpegFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void updateEncodingProgress();
    
private:
    QString m_exportPath;
    double m_frameRate;
    int m_videoBitrate;
    QString m_videoCodec;
    int m_progress;
    bool m_busy;
    QSize m_customResolution;
    QString m_lastOutputPath;
    double m_exportStartTime;
    double m_exportEndTime;
    
    QProcess* m_ffmpegProcess;
    QTimer* m_progressTimer;
    QTemporaryDir m_tempDir;
    // QThread* m_workerThread;
    // QObject* m_worker;
    
    // Frame generation stages
    bool generateFrames(DiveData* dive, OverlayGenerator* generator,
                        double startTime, double endTime);
    bool encodeFramesToVideo(const QString &outputPath);
    
    // Helper methods
    QString findFFmpegPath();
    QString generateFFmpegCommand(const QString &inputPattern, 
                                 const QString &outputFile);
    QString getFormatOptions(const QString &codec);
    QString sanitizeFileName(const QString &fileName);
    QString generateUniqueFileName(DiveData* dive, const QString &extension);
    void cleanupTempFiles();
    QSize getDefaultOverlaySize();
    QStringList createFFmpegArgs(const QString &outputPath);
};

#endif // VIDEO_EXPORT_H