#include "include/export/export_math.h"
#include "include/core/dive_data.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtGlobal>

#include <cmath>

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

qint64 sourceGridFrameLimit(qint64 nbFrames, bool vfr,
                            double videoStreamDuration, double avgFps,
                            int fpsNum, int fpsDen)
{
    if (!vfr)
        return nbFrames > 0 ? nbFrames : -1;
    if (fpsNum <= 0 || fpsDen <= 0)
        return -1;

    double pictureSeconds = videoStreamDuration;
    if (pictureSeconds <= 0.0 && nbFrames > 0 && avgFps > 0.0)
        pictureSeconds = nbFrames / avgFps;
    if (pictureSeconds <= 0.0)
        return -1;

    // ffprobe prints durations with six decimals, so an exact frame multiple
    // arrives a hair short (5.933333 s x 30 = 177.99999); the slack absorbs
    // that without ever reaching the next frame at any real frame rate.
    constexpr double kGridSlack = 1e-3;
    const qint64 frames = static_cast<qint64>(
        std::floor(pictureSeconds * fpsNum / fpsDen + kGridSlack));
    return frames > 0 ? frames : -1;
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

QString uniqueDestination(const QString &dirPath,
                          const QString &baseName,
                          const QString &suffix)
{
    constexpr int kMaxIndex = 999;
    const QDir dir(dirPath);
    for (int i = 0; i <= kMaxIndex; ++i) {
        QString name = baseName;
        if (i > 0)
            name += QStringLiteral("_%1").arg(i, 2, 10, QLatin1Char('0'));
        const QString candidate = dir.filePath(name + suffix);
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
    return dir.filePath(baseName + QLatin1Char('_')
                        + QString::number(QDateTime::currentSecsSinceEpoch())
                        + suffix);
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
