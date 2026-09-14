#include "include/export/image_export.h"
#include "include/export/export_math.h"
#include <QDir>
#include <QCoreApplication>
#include <QDebug>

ImageExporter::ImageExporter(QObject *parent)
    : QObject(parent)
    , m_frameRate(10.0)  // Default 10 frames per second
    , m_progress(0)
    , m_busy(false)
    , m_cancelRequested(false)
{
    // m_exportPath is the directory frames are written to: main.qml points it
    // at the per-dive sub-directory returned by createDefaultExportDir()
    // before each export. m_baseDirectory (what that subfolder is created
    // under) is bound from QML — the exporter never touches the settings
    // store itself. No directory is created here.
}

void ImageExporter::setExportPath(const QString &path)
{
    if (m_exportPath != path) {
        m_exportPath = path;
        emit exportPathChanged();
    }
}

void ImageExporter::setBaseDirectory(const QString &path)
{
    if (m_baseDirectory != path) {
        m_baseDirectory = path;
        emit baseDirectoryChanged();
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

    // An empty path would make QDir resolve to the working directory: frames
    // would land there and a cancellation's cleanup would delete files from —
    // and try to rmdir — whatever directory the app was launched in.
    if (!ExportMath::isValidExportPath(m_exportPath)) {
        emit exportError(tr("No export directory is set"));
        return false;
    }

    m_busy = true;
    m_cancelRequested = false;
    emit busyChanged();

    // Capture the destination for the whole run: the exportPath property is
    // writable from QML and the loop below pumps the event loop, so a
    // mid-run write must not redirect later frames — or the cancellation
    // cleanup — to a different directory.
    const QString exportDir = m_exportPath;

    // Create the export directory if it doesn't exist
    QDir dir(exportDir);
    if (!dir.exists() && !dir.mkpath(".")) {
        emit exportError(tr("Failed to create export directory: %1").arg(exportDir));
        m_busy = false;
        emit busyChanged();
        return false;
    }

    // Notify that export has started
    emit exportStarted();

    // Let the generator stage any export-only state (IFrameGenerator
    // contract; currently a no-op for both generators).
    gen->beginExport();

    // Calculate the number of frames to generate
    double timeStep = 1.0 / m_frameRate;
    // The loop below always writes at least one frame (time == startTime),
    // so never let a sub-frame range divide the progress by zero
    int totalFrames = ExportMath::totalFrames(startTime, endTime, m_frameRate);
    int processedFrames = 0;
    // Exactly the files this run writes — cancellation cleanup removes these
    // and nothing else (never a name pattern, which could hit files from an
    // earlier export into the same directory).
    QStringList framesWritten;

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
        const QString fileName = ExportMath::frameFileName(processedFrames);
        QString filePath = QDir(exportDir).filePath(fileName);

        // Save the image
        if (!overlay.save(filePath, "PNG")) {
            gen->endExport();
            emit exportError(tr("Failed to save image: %1").arg(filePath));
            m_busy = false;
            emit busyChanged();
            return false;
        }
        framesWritten.append(fileName);

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
            ExportMath::removeWrittenFiles(exportDir, framesWritten);
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
    emit exportFinished(true, exportDir);

    return true;
}

void ImageExporter::cancelExport()
{
    if (m_busy) {
        m_cancelRequested = true;
    }
}

QString ImageExporter::createDefaultExportDir(DiveData* dive,
                                              const QString &videoFilePath,
                                              const QString &contentType)
{
    if (!dive) {
        return QString();
    }

    // Create a unique directory for this dive under the QML-bound base
    // directory (not m_exportPath: main.qml points that at the created
    // sub-directory for the frame writer, so using it as the base would nest
    // every subsequent export one level deeper).
    if (!ExportMath::isValidExportPath(m_baseDirectory)) {
        qWarning() << "createDefaultExportDir: no base directory set";
        return QString();
    }
    QString dirName = ExportMath::exportBaseName(dive, videoFilePath, contentType);
    QString path = QDir(m_baseDirectory).filePath(dirName);

    QDir dir;
    if (!dir.mkpath(path)) {
        qWarning() << "Failed to create directory:" << path;
        return QString();
    }

    return path;
}