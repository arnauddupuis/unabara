#ifndef CLIP_THUMBNAIL_H
#define CLIP_THUMBNAIL_H

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QString>
#include <QStringList>

#include <atomic>

// Pure (Core+Gui, Quick-free) half of the ClipStrip first-frame thumbnails
// (follow-up C). The Quick half — the async image provider registered as
// image://clipthumb/ — lives in include/generators/clip_thumb_image_provider.h
// and only glues these pieces to QtConcurrent; everything that can regress
// silently (id encoding, cache keying, extraction) is here and unit-tested in
// clip_thumbnail_test.
namespace ClipThumbnail {

// Provider id = "<base64url(UTF-8 path), no padding>/<mtime ms since epoch>".
// base64url's alphabet is [A-Za-z0-9_-], so arbitrary absolute paths —
// spaces, '#', '?', '%', non-ASCII, "C:/..." — survive QUrl parsing and
// percent-encoding untouched on the way to requestImageResponse() (raw paths
// lose everything after '#'/'?'; standard base64 contains '/' and '+').
// The mtime is part of the URL so an overwritten file gets a NEW url: Qt
// Quick's pixmap cache is keyed by url and would otherwise keep serving the
// stale frame without ever asking the provider again.
QString encodeId(const QString& path, qint64 mtimeMs);
// Inverse of encodeId(). Returns false (outputs untouched) for a malformed id.
bool decodeId(const QString& id, QString* path, qint64* mtimeMs);

// File mtime in ms since epoch (-1 when the file does not exist).
qint64 fileMtimeMs(const QString& path);

// Full QML source url for a clip ("image://clipthumb/<id>"), stat'ing the
// file's current mtime. Empty for an empty path.
QString imageUrl(const QString& path);

// Cache key: path AND mtime, so an overwritten file misses.
QString cacheKey(const QString& path, qint64 mtimeMs);

// Height of the extracted thumbnail (width follows the aspect ratio). The
// ClipStrip card is ~88 px tall; 120 leaves headroom for HiDPI crops.
constexpr int kThumbnailHeight = 120;
// Hard cap on one ffmpeg extraction; the process is killed past it.
constexpr int kExtractTimeoutMs = 10000;

// Blocking: runs `ffmpegExe` to decode the first video frame of `path`
// scaled to kThumbnailHeight, returning it (null on any failure: empty exe,
// missing file, ffmpeg error, timeout, undecodable output). Safe on any
// thread — owns its QProcess, touches no shared state. `error` (optional)
// receives a one-line reason on failure. `abort` (optional) is polled every
// ~100 ms; when it turns true the process is killed (app shutdown must not
// wait out a slow extraction).
QImage extractFirstFrame(const QString& ffmpegExe, const QString& path,
                         int timeoutMs = kExtractTimeoutMs,
                         QString* error = nullptr,
                         const std::atomic_bool* abort = nullptr);

// The ffmpeg argument list used by extractFirstFrame() (exposed for tests).
QStringList extractArgs(const QString& path);

// Thread-safe in-memory LRU of extracted thumbnails, keyed by cacheKey().
// Failed extractions are cached too (as a null image, see
// lookup()) so scrolled-back delegates don't relaunch ffmpeg on a clip that
// can't be decoded; a failure entry also records the ffmpeg path it was
// produced with, so pointing the app at a working FFmpeg retries.
class Cache
{
public:
    explicit Cache(int maxEntries = 128);

    // True and *image set on a hit. A cached failure is a hit with a null
    // image — unless it was recorded with a different ffmpeg path.
    bool lookup(const QString& path, qint64 mtimeMs, const QString& ffmpegExe,
                QImage* image) const;
    void insert(const QString& path, qint64 mtimeMs, const QString& ffmpegExe,
                const QImage& image);
    int size() const;

private:
    struct Entry {
        QImage image;      // null = failed extraction
        QString ffmpegExe; // meaningful for failures only
    };
    mutable QMutex m_mutex;
    QCache<QString, Entry> m_cache;
};

} // namespace ClipThumbnail

#endif // CLIP_THUMBNAIL_H
