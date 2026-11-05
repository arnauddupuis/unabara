#include "include/generators/clip_thumb_image_provider.h"

#include <QDebug>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrentRun>

#include "include/core/ffmpeg_probe.h"

namespace {

QImage transparentPlaceholder()
{
    QImage img(1, 1, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    return img;
}

} // namespace

ClipThumbImageProvider::ClipThumbImageProvider()
    : m_state(std::make_shared<State>())
{
    // Two concurrent ffmpeg decodes: enough to fill a strip quickly without
    // saturating the machine while the player is also decoding.
    m_pool.setMaxThreadCount(2);
}

ClipThumbImageProvider::~ClipThumbImageProvider()
{
    // Lifecycle boundary: drop queued tasks, kill running extractions
    // (polled every ~100 ms) and wait for them — no pool thread may outlive
    // the provider.
    m_state->aborting.store(true);
    m_pool.clear();
    m_pool.waitForDone();
}

QQuickImageResponse* ClipThumbImageProvider::requestImageResponse(const QString& id,
                                                                  const QSize& /*requestedSize*/)
{
    QString path;
    if (!ClipThumbnail::decodeId(id, &path, nullptr)) {
        qDebug() << "ClipThumbImageProvider: malformed id" << id;
        return new ClipThumbImageResponse(QtFuture::makeReadyValueFuture(transparentPlaceholder()));
    }

    std::shared_ptr<State> state = m_state;
    QFuture<QImage> future = QtConcurrent::run(&m_pool, [state, path]() -> QImage {
        if (state->aborting.load())
            return transparentPlaceholder();
        // Re-stat here: the cache keys on the file's REAL mtime, not the
        // one baked into the url (which may predate an overwrite).
        const qint64 mtime = ClipThumbnail::fileMtimeMs(path);
        const QString ffmpeg = FFmpegProbe::instance()->ffmpegPath();

        QImage img;
        if (!state->cache.lookup(path, mtime, ffmpeg, &img)) {
            QString error;
            img = ClipThumbnail::extractFirstFrame(ffmpeg, path,
                                                   ClipThumbnail::kExtractTimeoutMs,
                                                   &error, &state->aborting);
            if (state->aborting.load())
                return transparentPlaceholder(); // don't cache a shutdown kill
            if (img.isNull())
                qDebug().noquote() << "ClipThumbImageProvider: no thumbnail for"
                                   << QFileInfo(path).fileName() << "-" << error;
            state->cache.insert(path, mtime, ffmpeg, img);
        }
        return img.isNull() ? transparentPlaceholder() : img;
    });
    return new ClipThumbImageResponse(future);
}

ClipThumbImageResponse::ClipThumbImageResponse(const QFuture<QImage>& future)
{
    // Connected before setFuture(): an already-finished future (cache-free
    // ready value above) reports through the event loop, i.e. after Qt has
    // connected to our finished() signal.
    connect(&m_watcher, &QFutureWatcher<QImage>::finished, this, [this]() {
        m_image = m_watcher.result();
        emit finished();
    });
    m_watcher.setFuture(future);
}

QQuickTextureFactory* ClipThumbImageResponse::textureFactory() const
{
    return QQuickTextureFactory::textureFactoryForImage(m_image);
}
