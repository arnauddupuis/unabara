#include "include/core/clip_thumbnail.h"

#include <QDateTime>
#include <QDeadlineTimer>
#include <QFileInfo>
#include <QMutexLocker>
#include <QProcess>

namespace ClipThumbnail {

namespace {
constexpr auto kB64 = QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals;
}

QString encodeId(const QString& path, qint64 mtimeMs)
{
    return QString::fromLatin1(path.toUtf8().toBase64(kB64))
           + QLatin1Char('/') + QString::number(mtimeMs);
}

bool decodeId(const QString& id, QString* path, qint64* mtimeMs)
{
    // Qt Quick passes a URL's query string through into the provider id.
    // ClipStrip appends "?f=<0|1>" (FFmpeg availability) so every thumbnail
    // URL changes once FFmpeg is configured mid-session — that re-request is
    // what lets the cache retry failures keyed to the old FFmpeg path. The
    // encoded path itself is base64url, so a literal '?' can only be such a
    // suffix: strip it before parsing.
    const qsizetype query = id.indexOf(QLatin1Char('?'));
    const QString base = query >= 0 ? id.left(query) : id;
    const qsizetype slash = base.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0)
        return false;
    bool ok = false;
    const qint64 mtime = base.mid(slash + 1).toLongLong(&ok);
    if (!ok)
        return false;
    const auto decoded = QByteArray::fromBase64Encoding(
        base.left(slash).toLatin1(),
        QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.isEmpty())
        return false;
    if (path)
        *path = QString::fromUtf8(decoded.decoded);
    if (mtimeMs)
        *mtimeMs = mtime;
    return true;
}

qint64 fileMtimeMs(const QString& path)
{
    const QFileInfo fi(path);
    if (!fi.exists())
        return -1;
    return fi.lastModified().toMSecsSinceEpoch(); // epoch ms: zone-independent;
}

QString imageUrl(const QString& path)
{
    if (path.isEmpty())
        return QString();
    return QStringLiteral("image://clipthumb/") + encodeId(path, fileMtimeMs(path));
}

QString cacheKey(const QString& path, qint64 mtimeMs)
{
    // '\n' cannot appear in the decimal mtime, so (path, mtime) pairs map
    // to distinct keys even for paths ending in digits.
    return path + QLatin1Char('\n') + QString::number(mtimeMs);
}

QStringList extractArgs(const QString& path)
{
    // -nostdin: never block on (or eat) the parent's stdin. First video
    // stream only, no audio/subs/data decoding. Autorotation is ffmpeg's
    // default, so portrait phone clips come out upright. PNG over a pipe:
    // no temp files to clean up.
    return {
        QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-i"), path,
        QStringLiteral("-map"), QStringLiteral("0:v:0"),
        QStringLiteral("-an"), QStringLiteral("-sn"), QStringLiteral("-dn"),
        QStringLiteral("-frames:v"), QStringLiteral("1"),
        QStringLiteral("-vf"),
        QStringLiteral("scale=-2:%1").arg(kThumbnailHeight),
        QStringLiteral("-f"), QStringLiteral("image2pipe"),
        QStringLiteral("-vcodec"), QStringLiteral("png"),
        QStringLiteral("-"),
    };
}

QImage extractFirstFrame(const QString& ffmpegExe, const QString& path,
                         int timeoutMs, QString* error,
                         const std::atomic_bool* abort)
{
    auto fail = [error](const QString& why) {
        if (error)
            *error = why;
        return QImage();
    };
    if (ffmpegExe.isEmpty())
        return fail(QStringLiteral("FFmpeg not found"));
    if (!QFileInfo(path).isFile())
        return fail(QStringLiteral("file not found"));

    // Owned by this call; no event loop needed — waitFor* drive the pipes.
    QProcess proc;
    proc.setProgram(ffmpegExe);
    proc.setArguments(extractArgs(path));
    // One deadline covers start + run, so the cap really is timeoutMs.
    const QDeadlineTimer deadline(timeoutMs);
    proc.start(QIODevice::ReadOnly);
    if (!proc.waitForStarted(int(qMax<qint64>(1, deadline.remainingTime())))) {
        // A start timeout can leave the process in Starting: kill it here so
        // ~QProcess doesn't have to (which would warn "destroyed while
        // process is still running").
        if (proc.state() != QProcess::NotRunning) {
            proc.kill();
            proc.waitForFinished(1000);
        }
        return fail(QStringLiteral("could not start FFmpeg: %1").arg(proc.errorString()));
    }

    QByteArray png;
    while (proc.state() != QProcess::NotRunning) {
        const bool aborted = abort && abort->load();
        if (aborted || deadline.hasExpired()) {
            proc.kill();
            proc.waitForFinished(1000);
            return fail(aborted ? QStringLiteral("aborted")
                                : QStringLiteral("timed out after %1 ms").arg(timeoutMs));
        }
        // Short slices keep draining stdout so a full pipe can't stall ffmpeg.
        proc.waitForFinished(100);
        png += proc.readAllStandardOutput();
    }
    png += proc.readAllStandardOutput();

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        return fail(QStringLiteral("FFmpeg exited with %1%2")
                        .arg(proc.exitCode())
                        .arg(err.isEmpty() ? QString() : QStringLiteral(": ") + err.section(QLatin1Char('\n'), 0, 0)));
    }
    QImage img = QImage::fromData(png, "PNG");
    if (img.isNull())
        return fail(QStringLiteral("FFmpeg produced no decodable frame"));
    return img;
}

Cache::Cache(int maxEntries)
    : m_cache(maxEntries)
{
}

bool Cache::lookup(const QString& path, qint64 mtimeMs, const QString& ffmpegExe,
                   QImage* image) const
{
    QMutexLocker lock(&m_mutex);
    const Entry* e = m_cache.object(cacheKey(path, mtimeMs));
    if (!e)
        return false;
    if (e->image.isNull() && e->ffmpegExe != ffmpegExe)
        return false; // failure recorded against another FFmpeg — retry
    if (image)
        *image = e->image;
    return true;
}

void Cache::insert(const QString& path, qint64 mtimeMs, const QString& ffmpegExe,
                   const QImage& image)
{
    QMutexLocker lock(&m_mutex);
    m_cache.insert(cacheKey(path, mtimeMs), new Entry{image, ffmpegExe}, 1);
}

int Cache::size() const
{
    QMutexLocker lock(&m_mutex);
    return int(m_cache.size());
}

} // namespace ClipThumbnail
