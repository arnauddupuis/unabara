#include "include/export/video_export.h"
#include <QDateTime>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDebug>

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
    }
}

bool VideoExporter::isFFmpegAvailable()
{
    QString ffmpegPath = findFFmpegPath();
    return !ffmpegPath.isEmpty();
}

QString VideoExporter::findFFmpegPath()
{
    // First, try to find FFmpeg in the same directory as the application
    QString appDir = QCoreApplication::applicationDirPath();
    QFileInfo ffmpegFile(appDir + "/ffmpeg");
    
    if (ffmpegFile.exists() && ffmpegFile.isExecutable()) {
        return ffmpegFile.absoluteFilePath();
    }
    
    // Next, try system paths
    QString ffmpegCommand = "ffmpeg";
    
#ifdef Q_OS_WIN
    ffmpegCommand += ".exe";
#endif
    
    QString path = QStandardPaths::findExecutable(ffmpegCommand);
    if (!path.isEmpty()) {
        return path;
    }
    
    // On Windows, also check Program Files
#ifdef Q_OS_WIN
    QStringList possiblePaths = {
        "C:/Program Files/ffmpeg/bin/ffmpeg.exe",
        "C:/Program Files (x86)/ffmpeg/bin/ffmpeg.exe"
    };
    
    for (const QString &testPath : possiblePaths) {
        QFileInfo fi(testPath);
        if (fi.exists() && fi.isExecutable()) {
            return fi.absoluteFilePath();
        }
    }
#endif
    
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
    
    // First generate all the frames
    bool framesGenerated = generateFrames(dive, generator, startTime, endTime);
    if (!framesGenerated) {
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
        
        // The QTemporaryDir will automatically remove all contents when it goes out of scope
        // But we can force removal earlier if needed
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
    
    // Set up the FFmpeg command
    QString inputPattern = QString("%1/frame_%06d.png").arg(m_tempDir.path());
    
    // Prepare arguments
    QStringList args;
    args << "-y" << "-framerate" << QString::number(m_frameRate) << "-i" << inputPattern;
    
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
    
    // Start the FFmpeg process with the program and arguments list
    m_ffmpegProcess->start(ffmpegPath, args);
    
    // Wait for the process to start
    if (!m_ffmpegProcess->waitForStarted(5000)) {
        qInfo() << tr("Failed to start FFmpeg: %1").arg(m_ffmpegProcess->errorString());
        emit exportError(tr("Failed to start FFmpeg: %1").arg(m_ffmpegProcess->errorString()));
        return false;
    }
    
    // Wait for the process to finish with a timeout
    bool finished = m_ffmpegProcess->waitForFinished(300000); // 5 minute timeout
    
    if (!finished) {
        emit exportError(tr("FFmpeg process timed out"));
        m_ffmpegProcess->terminate();
        return false;
    }
    
    // Capture all error output if process failed
    if (m_ffmpegProcess->exitCode() != 0) {
        QString errorOutput = QString::fromUtf8(m_ffmpegProcess->readAllStandardError());
        QString fullError = tr("FFmpeg exited with error code: %1\nError details: %2")
                              .arg(m_ffmpegProcess->exitCode())
                              .arg(errorOutput);
        qInfo() << fullError;
        emit exportError(fullError);
        return false;
    }
    
    return true;
}

void VideoExporter::processFFmpegOutput()
{
    // Process the FFmpeg stdout and stderr
    if (m_ffmpegProcess) {
        // Read stdout
        if (m_ffmpegProcess->canReadLine()) {
            QByteArray stdoutBytes = m_ffmpegProcess->readAllStandardOutput();
            QString stdoutStr = QString::fromUtf8(stdoutBytes);
            
            qDebug() << "FFmpeg stdout:" << stdoutStr.trimmed();
            
            // Parse progress information if available
            // Example: frame= 1234 fps= 43 q=24.0 size=   12345kB time=00:01:23.45 bitrate= 1234.5kbits/s
            QRegularExpression timeRegex("time=(\\d+):(\\d+):(\\d+\\.\\d+)");
            QRegularExpressionMatch match = timeRegex.match(stdoutStr);
            
            if (match.hasMatch()) {
                // Extract hours, minutes, seconds
                int hours = match.captured(1).toInt();
                int minutes = match.captured(2).toInt();
                double seconds = match.captured(3).toDouble();
                
                // Calculate total seconds
                double totalSeconds = hours * 3600 + minutes * 60 + seconds;
                
                // Calculate progress percentage for video encoding (50-100%)
                // Assuming we know the total duration in seconds
                double totalDuration = (m_frameRate > 0) ? 
                    static_cast<double>(QDir(m_tempDir.path()).entryList(QStringList() << "*.png").count()) / m_frameRate : 0;
                
                if (totalDuration > 0) {
                    int encodeProgress = static_cast<int>((totalSeconds / totalDuration) * 50);
                    m_progress = 50 + qMin(encodeProgress, 50); // Frame generation was 50%, encoding is the other 50%
                    emit progressChanged();
                }
            }
        }
        
        // Read stderr
        if (m_ffmpegProcess->canReadLine()) {
            QByteArray stderrBytes = m_ffmpegProcess->readAllStandardError();
            QString stderrStr = QString::fromUtf8(stderrBytes);
            
            qDebug() << "FFmpeg stderr:" << stderrStr.trimmed();
            
            // Look for warnings or errors
            if (stderrStr.contains("error", Qt::CaseInsensitive)) {
                emit statusUpdate(tr("FFmpeg error: %1").arg(stderrStr.trimmed()));
            }
        }
    }
}

void VideoExporter::onFFmpegFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    bool success = false;
    
    if (exitStatus == QProcess::CrashExit) {
        emit exportError(tr("FFmpeg process crashed"));
    } else if (exitCode != 0) {
        emit exportError(tr("FFmpeg exited with error code: %1").arg(exitCode));
    } else {
        m_progress = 100;
        emit progressChanged();
        emit statusUpdate(tr("Video encoding completed successfully"));
        success = true;
    }
    
    // Clean up temp files now that encoding is done
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

QSize VideoExporter::detectVideoResolution(const QString &videoPath)
{
    if (videoPath.isEmpty()) {
        return QSize();
    }

    // Use FFmpeg to detect video resolution
    QString ffmpegPath = findFFmpegPath();
    if (ffmpegPath.isEmpty()) {
        return QSize();
    }

    QProcess process;
    QStringList args;
    args << "-i" << videoPath << "-hide_banner";
    
    process.start(ffmpegPath, args);
    process.waitForFinished();
    
    // FFmpeg outputs to stderr
    QString output = process.readAllStandardError();
    
    // Parse the output to find video stream info
    QRegularExpression resolutionRegex("Stream #\\d+:\\d+.*? (\\d+)x(\\d+)");
    QRegularExpressionMatch match = resolutionRegex.match(output);
    
    if (match.hasMatch()) {
        int width = match.captured(1).toInt();
        int height = match.captured(2).toInt();
        
        qDebug() << "Detected video resolution:" << width << "x" << height;
        return QSize(width, height);
    }
    
    return QSize();
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