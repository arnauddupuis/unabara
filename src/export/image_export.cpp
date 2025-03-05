#include "include/export/image_export.h"
#include <QDir>
#include <QDateTime>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QThread>
#include <QDebug>

ImageExporter::ImageExporter(QObject *parent)
    : QObject(parent)
    , m_frameRate(10.0)  // Default 10 frames per second
    , m_progress(0)
    , m_busy(false)
{
    // Set default export path to Pictures/Unabara folder
    m_exportPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/Unabara";
    QDir dir;
    if (!dir.exists(m_exportPath)) {
        dir.mkpath(m_exportPath);
    }
}

void ImageExporter::setExportPath(const QString &path)
{
    if (m_exportPath != path) {
        m_exportPath = path;
        emit exportPathChanged();
    }
}

void ImageExporter::setFrameRate(double fps)
{
    if (m_frameRate != fps) {
        m_frameRate = fps;
        emit frameRateChanged();
    }
}

bool ImageExporter::exportImages(DiveData* dive, OverlayGenerator* generator)
{
    if (!dive || !generator) {
        emit exportError(tr("Invalid dive data or overlay generator"));
        return false;
    }
    
    // Export the full dive
    return exportImageRange(dive, generator, 0, dive->durationSeconds());
}

bool ImageExporter::exportImageRange(DiveData* dive, OverlayGenerator* generator,
                                     double startTime, double endTime)
{
    if (m_busy) {
        emit exportError(tr("Already exporting images"));
        return false;
    }
    
    if (!dive || !generator) {
        emit exportError(tr("Invalid dive data or overlay generator"));
        return false;
    }
    
    m_busy = true;
    emit busyChanged();
    
    // Create the export directory if it doesn't exist
    QDir dir(m_exportPath);
    if (!dir.exists() && !dir.mkpath(".")) {
        emit exportError(tr("Failed to create export directory: %1").arg(m_exportPath));
        m_busy = false;
        emit busyChanged();
        return false;
    }
    
    // Notify that export has started
    emit exportStarted();
    
    // Calculate the number of frames to generate
    double timeStep = 1.0 / m_frameRate;
    int totalFrames = qRound((endTime - startTime) * m_frameRate);
    int processedFrames = 0;
    
    // Generate and save images
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
        QString filePath = QDir(m_exportPath).filePath(filename);
        
        // Save the image
        if (!overlay.save(filePath, "PNG")) {
            emit exportError(tr("Failed to save image: %1").arg(filePath));
            m_busy = false;
            emit busyChanged();
            return false;
        }
        
        // Update progress
        processedFrames++;
        m_progress = (processedFrames * 100) / totalFrames;
        emit progressChanged();
        
        // Process events to keep UI responsive
        QCoreApplication::processEvents();
    }
    
    // Export completed successfully
    m_progress = 100;
    emit progressChanged();
    
    m_busy = false;
    emit busyChanged();
    emit exportFinished(true, m_exportPath);
    
    return true;
}

QString ImageExporter::createDefaultExportDir(DiveData* dive)
{
    if (!dive) {
        return QString();
    }
    
    // Create a unique directory for this dive
    QString dirName = generateUniqueDirectoryName(dive);
    QString path = QDir(m_exportPath).filePath(dirName);
    
    QDir dir;
    if (!dir.mkpath(path)) {
        qWarning() << "Failed to create directory:" << path;
        return QString();
    }
    
    return path;
}

QString ImageExporter::generateUniqueDirectoryName(DiveData* dive)
{
    QString baseName;
    
    // Use dive date and name to create the directory name
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
    
    return baseName;
}

QString ImageExporter::sanitizeFileName(const QString &fileName)
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