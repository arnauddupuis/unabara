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

double frameTime(double startTime, int frameIndex, double frameRate)
{
    if (frameRate <= 0.0)
        return startTime;
    return startTime + frameIndex / frameRate;
}

bool isPreDiveTime(double diveTime)
{
    // Strict: t == 0 is the first dive sample and renders normally. The
    // post-dive end is deliberately not gated (the clamped last sample is
    // shown, as in the preview).
    return diveTime < 0.0;
}

bool isValidExportPath(const QString &path)
{
    return !path.trimmed().isEmpty();
}

QString frameFileName(int frameIndex)
{
    return QStringLiteral("frame_%1.png").arg(frameIndex, 6, 10, QChar('0'));
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

QString outputTailExcerpt(const QString &tail, int maxLines)
{
    if (maxLines <= 0)
        return QString();
    QString normalized = tail;
    normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    // Per-line length cap: the excerpt lands in a fixed-size message
    // dialog, and a single FFmpeg filter-graph dump line can approach the
    // whole 4 KB stderr tail.
    constexpr int kMaxLineChars = 300;
    QStringList kept;
    const QStringList lines = normalized.split(QLatin1Char('\n'));
    for (auto it = lines.crbegin(); it != lines.crend() && kept.size() < maxLines; ++it) {
        QString line = it->trimmed();
        if (line.isEmpty())
            continue;
        if (line.size() > kMaxLineChars)
            line = line.left(kMaxLineChars - 1) + QChar(0x2026); // …
        kept.prepend(line);
    }
    return kept.join(QLatin1Char('\n'));
}

} // namespace ExportMath
