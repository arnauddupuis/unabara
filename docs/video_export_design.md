## Video Export Feature Design

### Overview
Add functionality to export a video file directly instead of individual image frames. The video will:
- Match the duration of the imported dive footage
- Use the same resolution as the overlay template
- Support transparent background (if possible)
- Use a standard format (MP4 with alpha channel or similar)

### Implementation Approach

1. **Backend Video Generation**
   - Use FFmpeg as the video processing backend
   - Create a QProcess wrapper to handle FFmpeg execution
   - Generate frames in memory or as temporary files
   - Stitch frames together into a video file

2. **UI Changes**
   - Add option in export dialog to select between "Images" and "Video"
   - Add video settings controls (codec, bitrate, resolution)
   - Add preview of estimated file size
   - Show progress during export

3. **Technical Requirements**
   - FFmpeg with libx264 support
   - Qt Process API for executing FFmpeg
   - Temp directory management
   - Video metadata handling

### Class Design

```cpp
// VideoExporter class (new)
class VideoExporter : public QObject
{
    Q_OBJECT
    
    Q_PROPERTY(QString exportPath READ exportPath WRITE setExportPath NOTIFY exportPathChanged)
    Q_PROPERTY(double frameRate READ frameRate WRITE setFrameRate NOTIFY frameRateChanged)
    Q_PROPERTY(int videoBitrate READ videoBitrate WRITE setVideoBitrate NOTIFY videoBitrateChanged)
    Q_PROPERTY(QString videoCodec READ videoCodec WRITE setVideoCodec NOTIFY videoCodecChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    
public:
    explicit VideoExporter(QObject *parent = nullptr);
    
    // Getters/Setters
    QString exportPath() const { return m_exportPath; }
    void setExportPath(const QString &path);
    
    double frameRate() const { return m_frameRate; }
    void setFrameRate(double fps);
    
    int videoBitrate() const { return m_videoBitrate; }
    void setVideoBitrate(int bitrate);
    
    QString videoCodec() const { return m_videoCodec; }
    void setVideoCodec(const QString &codec);
    
    int progress() const { return m_progress; }
    bool isBusy() const { return m_busy; }
    
    // Export methods
    Q_INVOKABLE bool exportVideo(DiveData* dive, OverlayGenerator* generator, 
                               double startTime, double endTime);
    
    // FFmpeg availability check
    Q_INVOKABLE bool isFFmpegAvailable();
    
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
    
private slots:
    void processFFmpegOutput();
    void onFFmpegFinished(int exitCode, QProcess::ExitStatus exitStatus);
    
private:
    QString m_exportPath;
    double m_frameRate;
    int m_videoBitrate;
    QString m_videoCodec;
    int m_progress;
    bool m_busy;
    QProcess* m_ffmpegProcess;
    QString m_tempDir;
    
    // Helper methods
    void cleanupTempDir();
    QString generateFFmpegCommand(const QString &inputPattern, 
                                 const QString &outputFile, 
                                 double duration);
};
```

### Implementation Details

1. **Frame Generation**
   - Use the existing OverlayGenerator to create frames
   - Store frames in a temporary directory
   - Use sequential naming (e.g., frame_000001.png)

2. **FFmpeg Execution**
   - Check if FFmpeg is available on the system
   - Construct a command line for the specific export parameters
   - Monitor FFmpeg output to update progress
   - Parse completion or errors

3. **Output Format Options**
   - MP4 with H.264 (most compatible)
   - ProRes with alpha channel (higher quality)
   - WebM with VP9 (web-friendly, supports alpha)

### User Experience

1. Export dialog with:
   - Radio buttons to select "Export as Images" or "Export as Video"
   - When "Video" is selected, show additional options:
     - Video codec dropdown
     - Quality/bitrate slider
     - Estimated file size
   - "Export" button triggers the appropriate method

2. Progress dialog showing:
   - Frame rendering progress
   - Video encoding progress
   - Cancel button
   - Time remaining estimate

### Dependencies

- FFmpeg installed on the user's system or bundled with the application
- Adequate disk space for temporary frames and output video
- Graphics hardware for faster rendering