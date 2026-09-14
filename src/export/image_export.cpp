#include "include/export/image_export.h"
#include "include/core/config.h"
#include <QDir>
#include <QDateTime>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QThread>
#include <QDebug>
#include <QFileInfo>

ImageExporter::ImageExporter(QObject *parent)
    : QObject(parent)
    , m_frameRate(10.0)  // Default 10 frames per second
    , m_progress(0)
    , m_busy(false)
    , m_cancelRequested(false)
{
    // m_exportPath is the directory frames are written to. main.qml points it
    // at the per-dive sub-directory returned by createDefaultExportDir()
    // before each export; the base directory lives in Config. No directory is
    // created here — that happens when an export actually runs.
    m_exportPath = Config::instance()->lastExportPath();
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

bool ImageExporter::exportImages(DiveData* dive, QObject* generator)
{
    if (!dive || !generator) {
        emit exportError(tr("Invalid dive data or generator"));
        return false;
    }

    // Export the full dive
    return exportImageRange(dive, generator, 0, dive->durationSeconds());
}

bool ImageExporter::exportImageRange(DiveData* dive, QObject* generator,
                                     double startTime, double endTime)
{
    if (m_busy) {
        emit exportError(tr("Already exporting images"));
        return false;
    }

    IFrameGenerator* gen = dynamic_cast<IFrameGenerator*>(generator);
    if (!dive || !gen) {
        emit exportError(tr("Invalid dive data or generator"));
        return false;
    }

    m_busy = true;
    m_cancelRequested = false;
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

    // Let the generator stage any export-only state (e.g. overlay hides its
    // editor-only cell backgrounds).
    gen->beginExport();

    // Calculate the number of frames to generate
    double timeStep = 1.0 / m_frameRate;
    // The loop below always writes at least one frame (time == startTime),
    // so never let a sub-frame range divide the progress by zero
    int totalFrames = qMax(1, qRound((endTime - startTime) * m_frameRate));
    int processedFrames = 0;

    qDebug() << "Exporting images from" << startTime << "to" << endTime
             << "at" << m_frameRate << "fps (" << totalFrames << "frames)";

    // Generate and save images
    for (double time = startTime; time <= endTime; time += timeStep) {
        // Generate the frame for this time point
        QImage overlay = gen->generate(dive, time);

        if (overlay.isNull()) {
            qWarning() << "Failed to generate frame at time:" << time;
            continue;
        }

        // Create a filename with the frame number
        QString frameNumberStr = QString("%1").arg(processedFrames, 6, 10, QChar('0'));
        QString filename = QString("frame_%1.png").arg(frameNumberStr);
        QString filePath = QDir(m_exportPath).filePath(filename);

        // Save the image
        if (!overlay.save(filePath, "PNG")) {
            gen->endExport();
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

        // The Cancel button's click is delivered by the processEvents() call
        // above; cancelExport() runs there and sets the flag we poll here.
        if (m_cancelRequested) {
            gen->endExport();
            removePartialFrames(processedFrames);
            m_busy = false;
            emit busyChanged();
            emit exportCancelled();
            return false;
        }
    }

    gen->endExport();

    // Export completed successfully
    m_progress = 100;
    emit progressChanged();

    m_busy = false;
    emit busyChanged();
    emit exportFinished(true, m_exportPath);

    return true;
}

void ImageExporter::cancelExport()
{
    if (m_busy) {
        m_cancelRequested = true;
    }
}

void ImageExporter::removePartialFrames(int frameCount)
{
    // Remove exactly the frames this run wrote (they are numbered
    // sequentially from 0), then the export directory itself — but only via
    // rmdir, which fails on a non-empty directory, so a pre-existing
    // user-chosen directory holding other files is left alone.
    QDir dir(m_exportPath);
    for (int i = 0; i < frameCount; ++i) {
        dir.remove(QString("frame_%1.png").arg(i, 6, 10, QChar('0')));
    }
    const QString name = dir.dirName();
    if (dir.cdUp()) {
        dir.rmdir(name);
    }
}

QString ImageExporter::createDefaultExportDir(DiveData* dive,
                                              const QString &videoFilePath,
                                              const QString &contentType)
{
    if (!dive) {
        return QString();
    }

    // Create a unique directory for this dive under the user's configured
    // base export directory. Read from Config at call time (not m_exportPath):
    // main.qml points m_exportPath at the created sub-directory for the frame
    // writer, so using it as the base would nest every subsequent export one
    // level deeper.
    QString dirName = generateUniqueDirectoryName(dive, videoFilePath, contentType);
    QString path = QDir(Config::instance()->lastExportPath()).filePath(dirName);

    QDir dir;
    if (!dir.mkpath(path)) {
        qWarning() << "Failed to create directory:" << path;
        return QString();
    }

    return path;
}

QString ImageExporter::generateUniqueDirectoryName(DiveData* dive,
                                                   const QString &videoFilePath,
                                                   const QString &contentType)
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

    // Add video filename stem if a video is imported
    if (!videoFilePath.isEmpty()) {
        QString videoStem = QFileInfo(videoFilePath).completeBaseName();
        if (!videoStem.isEmpty()) {
            baseName += "_" + sanitizeFileName(videoStem);
        }
    }

    // Append the content-type tag (e.g. "dive_computer" / "dive_profile")
    // so exports of different overlays for the same dive don't collide.
    if (!contentType.isEmpty()) {
        baseName += "_" + sanitizeFileName(contentType);
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