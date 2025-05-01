#include "include/export/video_export.h"
#include <QDateTime>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDebug>
#include <QTimer>

VideoExporter::VideoExporter(QObject *parent)
    : QObject(parent)
    , m_frameRate(30.0)  // Default 30 frames per second for video
    , m_videoBitrate(8000) // Default 8Mbps
    , m_videoCodec("h264") // Default H.264 codec
    , m_progress(0)
    , m_busy(false)
    , m_ffmpegProcess(nullptr)
{
    // Set default export path to Videos/Unabara folder
    m_exportPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Unabara";
    QDir dir;
    if (!dir.exists(m_exportPath)) {
        dir.mkpath(m_exportPath);
    }
    
    // Create the temporary directory
    if (!m_tempDir.isValid()) {
        qWarning() << "Failed to create temporary directory for frame storage";
    }

    m_progressTimer = new QTimer(this);
    connect(m_progressTimer, &QTimer::timeout, this, &VideoExporter::updateEncodingProgress);

}

VideoExporter::~VideoExporter()
{
    // Make sure the FFmpeg process is terminated
    if (m_ffmpegProcess && m_ffmpegProcess->state() != QProcess::NotRunning) {
        m_ffmpegProcess->terminate();
        m_ffmpegProcess->waitForFinished(3000);
        if (m_ffmpegProcess->state() != QProcess::NotRunning) {
            m_ffmpegProcess->kill();
        }
    }
    
    delete m_ffmpegProcess;
}

void VideoExporter::setExportPath(const QString &path)
{
    if (m_exportPath != path) {
        m_exportPath = path;
        emit exportPathChanged();
    }
}

void VideoExporter::setFrameRate(double fps)
{
    if (m_frameRate != fps) {
        m_frameRate = fps;
        emit frameRateChanged();
    }
}

void VideoExporter::setVideoBitrate(int bitrate)
{
    if (m_videoBitrate != bitrate) {
        m_videoBitrate = bitrate;
        emit videoBitrateChanged();
    }
}

void VideoExporter::setVideoCodec(const QString &codec)
{
    if (m_videoCodec != codec) {
        m_videoCodec = codec;
        emit videoCodecChanged();
    }
}

void VideoExporter::setCustomResolution(const QSize &size)
{
    if (m_customResolution != size && size.isValid()) {
        m_customResolution = size;
        emit customResolutionChanged();
    }
}

bool VideoExporter::isFFmpegAvailable()
{
    QString ffmpegPath = findFFmpegPath();
    return !ffmpegPath.isEmpty();
}

QString VideoExporter::ffmpegCommandName()
{
#if Q_OS_WIN
    return "ffmpeg.exe";
#else
    return "ffmpeg";
#endif
}

QStringList VideoExporter::listFfmpegPossiblePaths()
{
#ifdef Q_OS_WIN
    return {
        "C:/Program Files/ffmpeg/bin/",
        "C:/Program Files (x86)/ffmpeg/bin/"
    };
#endif
#ifdef Q_OS_APPLE
    return {
        "/opt/homebrew/bin/",
        "/usr/local/bin/",
    };
#else
    return {
        "/usr/local/bin/",
    };
#endif
}

QString VideoExporter::findFFmpegPath()
{
    QString ffmpegCommand = ffmpegCommandName();

    // First, try to find FFmpeg packaged in our app directory
    QString appDir = QCoreApplication::applicationDirPath();
    QFileInfo ffmpegFile(appDir + "/" + ffmpegCommand);

    if (ffmpegFile.exists() && ffmpegFile.isExecutable()) {
        return ffmpegFile.absoluteFilePath();
    }

    // Next, try "standard" user install paths like brew dest dir or /usr/local
    // for manual builds when you donàt override the defaults
    QStringList userPaths = listFfmpegPossiblePaths();
    QString path = QStandardPaths::findExecutable(ffmpegCommand, userPaths);
    if (!path.isEmpty()) {
        return path;
    }

    // Finally, try standard system paths
    path = QStandardPaths::findExecutable(ffmpegCommand);
    if (!path.isEmpty()) {
        return path;
    }

    return QString();
}

QStringList VideoExporter::getAvailableCodecs()
{
    // Return a list of supported codecs
    return QStringList() << "h264" << "prores" << "vp9" << "hevc";
}

QString VideoExporter::getFileExtensionForCodec(const QString &codec)
{
    // Return the appropriate file extension for the selected codec
    if (codec == "h264" || codec == "hevc") {
        return "mp4";
    } else if (codec == "prores") {
        return "mov";
    } else if (codec == "vp9") {
        return "webm";
    }
    
    // Default
    return "mp4";
}

bool VideoExporter::exportVideo(DiveData* dive, OverlayGenerator* generator,
                              double startTime, double endTime)
{
    qDebug() << "======== EXPORT VIDEO CALLED ========";
    qDebug() << "Parameters:" << startTime << "to" << endTime;
    
    m_exportStartTime = startTime;
    m_exportEndTime = endTime;

    if (m_busy) {
        emit exportError(tr("Already exporting video"));
        return false;
    }
    
    if (!dive || !generator) {
        emit exportError(tr("Invalid dive data or overlay generator"));
        return false;
    }
    
    // Check if FFmpeg is available
    if (!isFFmpegAvailable()) {
        emit exportError(tr("FFmpeg is not available. Please install FFmpeg to export videos."));
        return false;
    }
    
    m_busy = true;
    emit busyChanged();
    
    // Notify that export has started
    emit exportStarted();
    emit statusUpdate(tr("Generating frames..."));
    
    // Create the export directory if it doesn't exist
    QDir dir(m_exportPath);
    if (!dir.exists() && !dir.mkpath(".")) {
        emit exportError(tr("Failed to create export directory: %1").arg(m_exportPath));
        m_busy = false;
        emit busyChanged();
        return false;
    }
    
    // Generate a unique output filename
    QString extension = getFileExtensionForCodec(m_videoCodec);
    QString outputPath = generateUniqueFileName(dive, extension);
    m_lastOutputPath = outputPath;
    
    // Create a new temporary directory for this export
    // Make sure we create a fresh temporary directory for each export attempt
    cleanupTempFiles();
    m_tempDir = QTemporaryDir();
    
    if (!m_tempDir.isValid()) {
        emit exportError(tr("Failed to create temporary directory for frame storage"));
        m_busy = false;
        emit busyChanged();
        return false;
    }
    
    // First generate all the frames
    bool framesGenerated = generateFrames(dive, generator, startTime, endTime);
    if (!framesGenerated) {
        cleanupTempFiles();
        m_busy = false;
        emit busyChanged();
        return false;
    }
    
    // Then encode the frames to video
    emit statusUpdate(tr("Encoding video..."));
    bool videoEncoded = encodeFramesToVideo(outputPath);
    
    if (!videoEncoded) {
        cleanupTempFiles();
        m_busy = false;
        emit busyChanged();
        return false;
    }
    
    return true;
}

void VideoExporter::cancelExport()
{
    if (m_busy && m_ffmpegProcess) {
        // Terminate the FFmpeg process gracefully first
        m_ffmpegProcess->terminate();
        
        // Wait a bit for graceful termination
        if (!m_ffmpegProcess->waitForFinished(3000)) {
            // If it doesn't terminate gracefully, force kill it
            m_ffmpegProcess->kill();
        }
        
        emit exportError(tr("Export cancelled by user"));
        
        // Clean up any temporary files
        cleanupTempFiles();
        
        m_busy = false;
        emit busyChanged();
    }
}

void VideoExporter::cleanupTempFiles()
{
    if (m_tempDir.isValid()) {
        // Get path before removing directory
        QString tempPath = m_tempDir.path();
        
        // Count files to be removed for logging
        QDir dir(tempPath);
        int count = dir.entryList(QDir::Files).count();
        
        qDebug() << "Cleaning up" << count << "temporary files from" << tempPath;
        
        // Ensure the QTemporaryDir is properly cleaned up
        if (!m_tempDir.remove()) {
            qWarning() << "Failed to clean up some temporary files. They will be removed when the application exits.";
        }
    }
}

bool VideoExporter::generateFrames(DiveData* dive, OverlayGenerator* generator,
                                 double startTime, double endTime)
{
    // Calculate the number of frames to generate
    double timeStep = 1.0 / m_frameRate;
    int totalFrames = qRound((endTime - startTime) * m_frameRate);
    int processedFrames = 0;
    
    qDebug() << "Generating frames from" << startTime << "to" << endTime 
             << "at" << m_frameRate << "fps (" << totalFrames << "frames)";
    
    // Clear any previous temp files and ensure the temp directory exists
    if (!m_tempDir.isValid()) {
        emit exportError(tr("Failed to create temporary directory for frame storage"));
        return false;
    }
    
    QString tempDirPath = m_tempDir.path();
    
    // Generate and save frames
    for (double time = startTime; time <= endTime; time += timeStep) {
        // Generate the overlay for this time point
        QImage overlay = generator->generateOverlay(dive, time);
        
        if (overlay.isNull()) {
            qWarning() << "Failed to generate overlay at time:" << time;
            continue;
        }
        
        // Create a filename with the frame number
        QString frameNumberStr = QString("%1").arg(processedFrames, 6, 10, QChar('0'));
        QString filename = QString("frame_%1.png").arg(frameNumberStr);
        QString filePath = QDir(tempDirPath).filePath(filename);
        
        // Save the image
        if (!overlay.save(filePath, "PNG")) {
            emit exportError(tr("Failed to save frame: %1").arg(filePath));
            return false;
        }
        
        // Update progress
        processedFrames++;
        m_progress = (processedFrames * 50) / totalFrames; // Frame generation is 50% of total progress
        emit progressChanged();
        
        // Process events to keep UI responsive
        QCoreApplication::processEvents();
    }
    
    return true;
}

bool VideoExporter::encodeFramesToVideo(const QString &outputPath)
{
    // Create a QProcess for FFmpeg if it doesn't exist
    if (!m_ffmpegProcess) {
        m_ffmpegProcess = new QProcess(this);
        
        // Connect signals for output
        connect(m_ffmpegProcess, &QProcess::readyReadStandardOutput, this, &VideoExporter::processFFmpegOutput);
        connect(m_ffmpegProcess, &QProcess::readyReadStandardError, this, &VideoExporter::processFFmpegOutput);
        connect(m_ffmpegProcess, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), 
                this, &VideoExporter::onFFmpegFinished);
    }
    
    // Get the path to FFmpeg
    QString ffmpegPath = findFFmpegPath();
    if (ffmpegPath.isEmpty()) {
        emit exportError(tr("FFmpeg executable not found"));
        return false;
    }
    
    // Set up the FFmpeg command with arguments
    QStringList args;
    args << "-y"
         << "-progress" << "-" // Output progress info to stdout
         << "-stats" // Show stats
         << "-framerate" << QString::number(m_frameRate) 
         << "-i" << QString("%1/frame_%06d.png").arg(m_tempDir.path());
    
    // Add scale filter if custom resolution is set
    if (m_customResolution.isValid() && m_customResolution.width() > 0 && m_customResolution.height() > 0) {
        args << "-vf" << QString("scale=%1:%2").arg(m_customResolution.width()).arg(m_customResolution.height());
    }
    
    // Add codec-specific options
    QString formatOptions = getFormatOptions(m_videoCodec);
    QStringList formatArgs = formatOptions.split(" ", Qt::SkipEmptyParts);
    args.append(formatArgs);
    
    // Add output file
    args << outputPath;
    
    // Log the full command for debugging
    QString cmdLog = ffmpegPath;
    for (const QString &arg : args) {
        cmdLog += " " + (arg.contains(" ") ? "\"" + arg + "\"" : arg);
    }
    qInfo() << "FFmpeg command:" << cmdLog;
    
    // Start progress timer for animation
    m_progressTimer->start(500);
    qDebug() << "Started progress timer";
    
    // Start the FFmpeg process
    m_ffmpegProcess->start(ffmpegPath, args);
    
    // Wait for the process to start
    if (!m_ffmpegProcess->waitForStarted(5000)) {
        qInfo() << tr("Failed to start FFmpeg: %1").arg(m_ffmpegProcess->errorString());
        emit exportError(tr("Failed to start FFmpeg: %1").arg(m_ffmpegProcess->errorString()));
        return false;
    }
    
    return true; // Process started successfully
}

QStringList VideoExporter::createFFmpegArgs(const QString &outputPath)
{
    QStringList args;
    args << "-y"
         << "-progress" << "-" // Output progress info to stdout
         << "-stats" // Show stats
         << "-framerate" << QString::number(m_frameRate) 
         << "-i" << QString("%1/frame_%06d.png").arg(m_tempDir.path());
    
    // Add scale filter if custom resolution is set
    if (m_customResolution.isValid() && m_customResolution.width() > 0 && m_customResolution.height() > 0) {
        args << "-vf" << QString("scale=%1:%2").arg(m_customResolution.width()).arg(m_customResolution.height());
    }
    
    // Add codec-specific options
    QString formatOptions = getFormatOptions(m_videoCodec);
    QStringList formatArgs = formatOptions.split(" ", Qt::SkipEmptyParts);
    args.append(formatArgs);
    
    // Add output file
    args << outputPath;
    
    return args;
}

void VideoExporter::updateEncodingProgress()
{
    // If FFmpeg isn't running, there's nothing to update
    if (!m_ffmpegProcess || m_ffmpegProcess->state() != QProcess::Running) {
        return;
    }
    
    // If the progress hasn't changed recently and we're in the encoding phase (>50%),
    // gently increase it to show activity
    static int lastProgress = 0;
    static int unchangedCount = 0;
    
    if (m_progress >= 50 && m_progress < 99 && lastProgress == m_progress) {
        unchangedCount++;
        if (unchangedCount > 4) { // After ~2 seconds of no updates
            // Small increment just to show activity
            m_progress++;
            lastProgress = m_progress;
            unchangedCount = 0;
            emit progressChanged();
            qDebug() << "Incrementing progress to show activity:" << m_progress << "%";
        }
    } else if (lastProgress != m_progress) {
        lastProgress = m_progress;
        unchangedCount = 0;
    }
    
    // Send periodic status updates to keep UI responsive
    static int statusUpdateCount = 0;
    statusUpdateCount = (statusUpdateCount + 1) % 5;
    if (statusUpdateCount == 0) {
        QString statusMsg = tr("Encoding video... %1%").arg(m_progress);
        emit statusUpdate(statusMsg);
    }
}

void VideoExporter::processFFmpegOutput()
{
    // Process the FFmpeg stdout and stderr
    if (m_ffmpegProcess) {
        QByteArray output = m_ffmpegProcess->readAllStandardOutput();
        output.append(m_ffmpegProcess->readAllStandardError());
        
        if (output.isEmpty()) {
            return;
        }
        
        QString outputStr = QString::fromUtf8(output);
        
        // Parse frame information (most reliable for progress)
        QRegularExpression frameRegex("frame=\\s*(\\d+)");
        QRegularExpressionMatch frameMatch = frameRegex.match(outputStr);
        
        if (frameMatch.hasMatch()) {
            int currentFrame = frameMatch.captured(1).toInt();
            int totalFrames = QDir(m_tempDir.path()).entryList(QStringList() << "*.png").count();
            
            if (totalFrames > 0) {
                // Calculate progress (50% for generation, 50% for encoding)
                int encodingProgress = (currentFrame * 50) / totalFrames;
                int newProgress = 50 + qMin(encodingProgress, 50);
                
                if (newProgress != m_progress) {
                    m_progress = newProgress;
                    emit progressChanged();
                    emit statusUpdate(tr("Encoding frame %1 of %2").arg(currentFrame).arg(totalFrames));
                    qDebug() << "Progress:" << m_progress << "% (Frame:" << currentFrame << "/" << totalFrames << ")";
                }
            }
        }
    }
}

void VideoExporter::onFFmpegFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    m_progressTimer->stop();
    
    bool success = false;
    
    if (exitStatus == QProcess::CrashExit) {
        emit exportError(tr("FFmpeg process crashed"));
    } else if (exitCode != 0) {
        emit exportError(tr("FFmpeg exited with error code: %1").arg(exitCode));
    } else {
        m_progress = 100; // Ensure we reach 100%
        emit progressChanged();
        emit statusUpdate(tr("Video encoding completed successfully"));
        success = true;
    }
    
    // Clean up temp files
    cleanupTempFiles();
    
    m_busy = false;
    emit busyChanged();
    
    if (success) {
        emit exportFinished(true, m_lastOutputPath);
    }
}

QString VideoExporter::generateFFmpegCommand(const QString &inputPattern, const QString &outputFile)
{
    QString ffmpegPath = findFFmpegPath();
    
    // Prepare arguments as separate list items instead of a single string
    QStringList args;
    
    // Add basic arguments
    args << "-y" << "-framerate" << QString::number(m_frameRate) << "-i" << inputPattern;
    
    // Add scale filter if custom resolution is set
    if (m_customResolution.isValid() && m_customResolution.width() > 0 && m_customResolution.height() > 0) {
        args << "-vf" << QString("scale=%1:%2").arg(m_customResolution.width()).arg(m_customResolution.height());
    }
    
    // Add codec-specific options
    QString formatOptions = getFormatOptions(m_videoCodec);
    // Split by space but respect quoted sections
    QRegularExpression re("\\s+(?=(?:[^\"]*\"[^\"]*\")*[^\"]*$)");
    QStringList formatArgs = formatOptions.split(re, Qt::SkipEmptyParts);
    args.append(formatArgs);
    
    // Add output file
    args << outputFile;
    
    // Store the output path for later reference
    m_lastOutputPath = outputFile;
    
    // Build full command for logging
    QString command = ffmpegPath;
    for (const QString &arg : args) {
        if (arg.contains(" ")) {
            command += " \"" + arg + "\"";
        } else {
            command += " " + arg;
        }
    }
    
    return command;
}

QString VideoExporter::getFormatOptions(const QString &codec)
{
    if (codec == "h264") {
        return QString("-c:v libx264 -preset medium -crf 23 -pix_fmt yuv420p -movflags +faststart -b:v %1k").arg(m_videoBitrate);
    } else if (codec == "prores") {
        return QString("-c:v prores_ks -profile:v 4444 -pix_fmt yuva444p10le -alpha_bits 16 -bits_per_mb 8000 -vendor ap10");
    } else if (codec == "vp9") {
        return QString("-c:v libvpx-vp9 -pix_fmt yuva420p -b:v %1k -deadline good -cpu-used 2").arg(m_videoBitrate);
    } else if (codec == "hevc") {
        return QString("-c:v libx265 -preset medium -crf 23 -pix_fmt yuv420p -tag:v hvc1 -b:v %1k").arg(m_videoBitrate);
    }
    
    // Default to H.264
    return QString("-c:v libx264 -preset medium -crf 23 -pix_fmt yuv420p -movflags +faststart -b:v %1k").arg(m_videoBitrate);
}

QSize VideoExporter::getDefaultOverlaySize()
{
    // Load the default overlay image and get its dimensions
    QImage defaultOverlay(":/default_overlay.png");
    if (!defaultOverlay.isNull()) {
        QSize size = defaultOverlay.size();
        qDebug() << "Using default overlay size:" << size;
        return size;
    }
    
    // If default overlay can't be loaded for some reason, fall back to 16:9 HD
    qWarning() << "Could not load default overlay image, using fallback size 1280x720";
    return QSize(1280, 720);
}

QSize VideoExporter::detectVideoResolution(const QString &videoPath)
{
    if (videoPath.isEmpty()) {
        qWarning() << "Empty video path provided to detectVideoResolution";
        return getDefaultOverlaySize();
    }

    qDebug() << "Detecting resolution for video:" << videoPath;

    // Use FFmpeg to detect video resolution
    QString ffmpegPath = findFFmpegPath();
    if (ffmpegPath.isEmpty()) {
        qWarning() << "FFmpeg not found, cannot detect video resolution";
        return getDefaultOverlaySize();
    }

    // Approach 1: Use FFmpeg with more verbose output to stderr
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels); // Merge stdout and stderr
    
    QStringList args;
    args << "-i" << videoPath;
    
    qDebug() << "Running FFmpeg info command:" << ffmpegPath << args.join(" ");
    
    process.start(ffmpegPath, args);
    process.waitForFinished(5000);
    
    QString output = process.readAllStandardOutput();
    qDebug() << "FFmpeg info output length:" << output.length();
    qDebug() << "FFmpeg info output sample:" << output.left(200);
    
    // Look for Stream information that contains resolution
    QRegularExpression videoStreamRegex("Stream\\s+#\\d+:\\d+(?:\\[0x[0-9a-f]+\\])?[^,]*:\\s+Video[^,]*,\\s+([^,]*,\\s+)?([0-9]+x[0-9]+)");
    QRegularExpressionMatch videoStreamMatch = videoStreamRegex.match(output);
    
    if (videoStreamMatch.hasMatch()) {
        QString resString = videoStreamMatch.captured(2); // This should be something like "1920x1080"
        QRegularExpression resolutionRegex("(\\d+)x(\\d+)");
        QRegularExpressionMatch match = resolutionRegex.match(resString);
        
        if (match.hasMatch()) {
            int width = match.captured(1).toInt();
            int height = match.captured(2).toInt();
            
            if (width > 0 && height > 0) {
                qDebug() << "Detected video resolution:" << width << "x" << height;
                return QSize(width, height);
            }
        }
    }
    
    // Approach 2: Use FFprobe with different options
    QString ffprobePath = ffmpegPath;
    ffprobePath.replace("ffmpeg", "ffprobe");
    
    QStringList ffprobeArgs;
    ffprobeArgs << "-v" << "quiet" 
                << "-print_format" << "json" 
                << "-show_format" 
                << "-show_streams" 
                << videoPath;
    
    qDebug() << "Running ffprobe command:" << ffprobePath << ffprobeArgs.join(" ");
    
    process.start(ffprobePath, ffprobeArgs);
    if (!process.waitForFinished(5000)) {
        qWarning() << "ffprobe process timed out";
        process.kill();
        return getDefaultOverlaySize();
    }
    
    QString ffprobeOutput = process.readAllStandardOutput();
    qDebug() << "ffprobe output length:" << ffprobeOutput.length();
    
    if (!ffprobeOutput.isEmpty()) {
        // Look for width and height in the JSON output
        QRegularExpression widthRegex("\"width\"\\s*:\\s*(\\d+)");
        QRegularExpression heightRegex("\"height\"\\s*:\\s*(\\d+)");
        
        QRegularExpressionMatch widthMatch = widthRegex.match(ffprobeOutput);
        QRegularExpressionMatch heightMatch = heightRegex.match(ffprobeOutput);
        
        if (widthMatch.hasMatch() && heightMatch.hasMatch()) {
            int width = widthMatch.captured(1).toInt();
            int height = heightMatch.captured(1).toInt();
            
            if (width > 0 && height > 0) {
                qDebug() << "Detected video resolution (ffprobe):" << width << "x" << height;
                return QSize(width, height);
            }
        }
    }
    
    // If all else fails, use ffmpeg with simpler approach that focuses just on resolution
    QStringList simpleArgs;
    simpleArgs << "-v" << "error" 
              << "-i" << videoPath
              << "-select_streams" << "v:0" 
              << "-show_entries" << "stream=width,height" 
              << "-of" << "default=noprint_wrappers=1";
    
    qDebug() << "Running simple ffmpeg resolution command:" << ffmpegPath << simpleArgs.join(" ");
    
    process.start(ffmpegPath, simpleArgs);
    if (!process.waitForFinished(5000)) {
        qWarning() << "Simple ffmpeg process timed out";
        process.kill();
        return getDefaultOverlaySize();
    }
    
    QString simpleOutput = process.readAllStandardOutput();
    qDebug() << "Simple ffmpeg output:" << simpleOutput;
    
    QRegularExpression widthRegex("width=(\\d+)");
    QRegularExpression heightRegex("height=(\\d+)");
    
    QRegularExpressionMatch widthMatch = widthRegex.match(simpleOutput);
    QRegularExpressionMatch heightMatch = heightRegex.match(simpleOutput);
    
    if (widthMatch.hasMatch() && heightMatch.hasMatch()) {
        int width = widthMatch.captured(1).toInt();
        int height = heightMatch.captured(1).toInt();
        
        if (width > 0 && height > 0) {
            qDebug() << "Detected video resolution (simple):" << width << "x" << height;
            return QSize(width, height);
        }
    }
    
    qWarning() << "All resolution detection methods failed, using default overlay size";
    return getDefaultOverlaySize();
}

QString VideoExporter::createDefaultExportFile(DiveData* dive)
{
    if (!dive) {
        return QString();
    }
    
    // Generate a unique file name for this dive
    QString extension = getFileExtensionForCodec(m_videoCodec);
    return generateUniqueFileName(dive, extension);
}

QString VideoExporter::generateUniqueFileName(DiveData* dive, const QString &extension)
{
    QString baseName;
    
    // Use dive date and name to create the file name
    QDateTime diveTime = dive->startTime();
    if (diveTime.isValid()) {
        baseName = diveTime.toString("yyyy-MM-dd_HHmmss");
    } else {
        baseName = QDateTime::currentDateTime().toString("yyyy-MM-dd_HHmmss");
    }
    
    // Add dive name if available
    if (!dive->diveName().isEmpty()) {
        baseName += "_" + sanitizeFileName(dive->diveName());
    }
    
    // Add location if available
    if (!dive->location().isEmpty()) {
        baseName += "_" + sanitizeFileName(dive->location());
    }
    
    // Add extension
    baseName += "." + extension;
    
    // Create full path - make sure the directory exists before returning the file path
    QString dirPath = m_exportPath;
    QDir dir(dirPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    // Create and return the full path
    return QDir(dirPath).filePath(baseName);
}

QString VideoExporter::sanitizeFileName(const QString &fileName)
{
    // Replace invalid file name characters with underscores
    QString result = fileName;
    
    // Replace characters that aren't allowed in file names
    QRegularExpression regex("[\\\\/:*?\"<>|]");
    result.replace(regex, "_");
    
    // Limit length
    if (result.length() > 50) {
        result = result.left(47) + "...";
    }
    
    return result;
}