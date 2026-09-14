#include "include/export/export_math.h"
#include "include/core/dive_data.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtGlobal>

namespace ExportMath {

int totalFrames(double startTime, double endTime, double frameRate)
{
    return qMax(1, qRound((endTime - startTime) * frameRate));
}

bool isValidExportPath(const QString &path)
{
    return !path.trimmed().isEmpty();
}

QString frameFileName(int frameIndex)
{
    return QStringLiteral("frame_%1.png").arg(frameIndex, 6, 10, QChar('0'));
}

QString framePattern()
{
    return QStringLiteral("frame_%06d.png");
}

QString sanitizeFileName(const QString &fileName)
{
    QString result = fileName;

    // Replace characters that aren't allowed in file names
    static const QRegularExpression regex(QStringLiteral("[\\\\/:*?\"<>|]"));
    result.replace(regex, QStringLiteral("_"));

    // Limit length
    if (result.length() > 50) {
        result = result.left(47) + QStringLiteral("...");
    }

    return result;
}

QString exportBaseName(DiveData *dive,
                       const QString &videoFilePath,
                       const QString &contentType)
{
    QString baseName;

    // Use dive date and name to create the base name
    const QDateTime diveTime = dive->startTime();
    if (diveTime.isValid()) {
        baseName = diveTime.toString(QStringLiteral("yyyy-MM-dd_HHmmss"));
    } else {
        baseName = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmmss"));
    }

    // Add dive name if available
    if (!dive->diveName().isEmpty()) {
        baseName += QStringLiteral("_") + sanitizeFileName(dive->diveName());
    }

    // Add location if available
    if (!dive->location().isEmpty()) {
        baseName += QStringLiteral("_") + sanitizeFileName(dive->location());
    }

    // Add video filename stem if a video is imported
    if (!videoFilePath.isEmpty()) {
        const QString videoStem = QFileInfo(videoFilePath).completeBaseName();
        if (!videoStem.isEmpty()) {
            baseName += QStringLiteral("_") + sanitizeFileName(videoStem);
        }
    }

    // Append the content-type tag (e.g. "dive_computer" / "dive_profile")
    // so exports of different overlays for the same dive don't collide.
    if (!contentType.isEmpty()) {
        baseName += QStringLiteral("_") + sanitizeFileName(contentType);
    }

    return baseName;
}

void removeWrittenFiles(const QString &dirPath, const QStringList &fileNames)
{
    QDir dir(dirPath);
    for (const QString &name : fileNames) {
        dir.remove(name);
    }
    const QString name = dir.dirName();
    if (dir.cdUp()) {
        dir.rmdir(name);
    }
}

} // namespace ExportMath
