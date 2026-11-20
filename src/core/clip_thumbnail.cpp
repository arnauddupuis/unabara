#include "include/core/clip_thumbnail.h"

#include <QDateTime>
#include <QDeadlineTimer>
#include <QFileInfo>
#include <QMutexLocker>
#include <QProcess>
#include <QStringList>

#include "include/core/color_grade.h"

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

namespace {

// The 16 ops values in their fixed encode/decode order.
constexpr int kOpsValueCount = 16;

void flattenOps(const Unabara::GradeOps& ops, double out[kOpsValueCount])
{
    int i = 0;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out[i++] = ops.matrix[r][c];
    for (int c = 0; c < 3; ++c)
        out[i++] = ops.black[c];
    for (int c = 0; c < 3; ++c)
        out[i++] = ops.lift[c];
    out[i++] = ops.contrast;
}

} // namespace

QString encodeOps(const Unabara::GradeOps& ops)
{
    double values[kOpsValueCount];
    double identity[kOpsValueCount];
    flattenOps(ops, values);
    flattenOps(Unabara::GradeOps{}, identity);

    bool isIdentity = true;
    for (int i = 0; i < kOpsValueCount && isIdentity; ++i)
        isIdentity = values[i] == identity[i];
    if (isIdentity)
        return QString();

    QStringList parts;
    parts.reserve(kOpsValueCount);
    for (int i = 0; i < kOpsValueCount; ++i)
        parts << QString::number(values[i], 'g', 17); // C locale, exact round-trip
    return parts.join(QLatin1Char(','));
}

bool decodeOpsQuery(const QString& id, Unabara::GradeOps* ops)
{
    const qsizetype query = id.indexOf(QLatin1Char('?'));
    if (query < 0)
        return false;
    const QStringList params =
        id.mid(query + 1).split(QLatin1Char('&'), Qt::SkipEmptyParts);
    for (const QString& param : params) {
        if (!param.startsWith(QLatin1String("g=")))
            continue;
        const QStringList nums = param.mid(2).split(QLatin1Char(','));
        if (nums.size() != kOpsValueCount)
            return false;
        double values[kOpsValueCount];
        for (int i = 0; i < kOpsValueCount; ++i) {
            bool ok = false;
            values[i] = nums.at(i).toDouble(&ok); // C locale, matches encode
            if (!ok)
                return false;
        }
        if (ops) {
            int i = 0;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    ops->matrix[r][c] = values[i++];
            for (int c = 0; c < 3; ++c)
                ops->black[c] = values[i++];
            for (int c = 0; c < 3; ++c)
                ops->lift[c] = values[i++];
            ops->contrast = values[i++];
        }
        return true;
    }
    return false;
}

QImage applyOps(const Unabara::GradeOps& ops, QImage image)
{
    if (image.isNull())
        return image;
    // Opaque video frame, not golden-gated: plain convertToFormat is fine
    // here (the CLAUDE.md warning is about unpremultiply in golden paths).
    image = std::move(image).convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            double r = qRed(line[x]) / 255.0;
            double g = qGreen(line[x]) / 255.0;
            double b = qBlue(line[x]) / 255.0;
            ops.apply(r, g, b);
            line[x] = qRgb(qBound(0, qRound(r * 255.0), 255),
                           qBound(0, qRound(g * 255.0), 255),
                           qBound(0, qRound(b * 255.0), 255));
        }
    }
    return image;
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
