#ifndef CLIP_THUMB_IMAGE_PROVIDER_H
#define CLIP_THUMB_IMAGE_PROVIDER_H

#include <QFutureWatcher>
#include <QImage>
#include <QQuickAsyncImageProvider>
#include <QThreadPool>

#include <atomic>
#include <memory>

#include "include/core/clip_thumbnail.h"

// image://clipthumb/<id> — first-frame thumbnails for the ClipStrip cards
// (follow-up C). Id format and cache keying: include/core/clip_thumbnail.h
// (VideoClip::thumbnailUrl builds the url).
//
// Async on purpose: a plain QQuickImageProvider would run every extraction
// serially on Qt Quick's single image-loader thread, so a dozen clips would
// queue behind each other (and behind every other provider's images).
// Each request instead becomes a QtConcurrent task on a PRIVATE two-thread
// pool — not the global pool, where ClipListModel's ffprobe workers run:
// slow extractions must never delay probing.
//
// THREAD RULES: requestImageResponse() runs on the image-loader thread and
// the task on a pool thread; neither touches a GUI object (no Config, no
// VideoClip). The task captures the path by value plus the shared state
// (cache + abort flag) by shared_ptr; FFmpeg is resolved through the
// thread-safe FFmpegProbe singleton (whose Config override is pushed in from
// the GUI thread, never read here).
//
// Failure (no FFmpeg, undecodable clip, 10 s timeout): the response is a
// 1×1 transparent image — never a null image/error string, which would make
// Qt print "Failed to get image from provider" — and ClipStrip treats an
// image ≤ 1 px wide as "no thumbnail", so the card looks exactly as it did
// before thumbnails. Failures are cached (one qDebug per clip, no ffmpeg
// relaunch when a delegate scrolls back in).
class ClipThumbImageProvider : public QQuickAsyncImageProvider
{
public:
    ClipThumbImageProvider();
    ~ClipThumbImageProvider() override;

    QQuickImageResponse* requestImageResponse(const QString& id,
                                              const QSize& requestedSize) override;

    // Shared with in-flight tasks so they stay valid even if a task
    // outlives a response (the pool itself is drained in the destructor).
    struct State {
        ClipThumbnail::Cache cache;
        std::atomic_bool aborting{false};
    };

private:
    std::shared_ptr<State> m_state;
    QThreadPool m_pool;
};

// One pending thumbnail. Lives on the image-loader thread; the watcher is
// its child-by-value, so a response Qt deletes early (delegate scrolled out,
// clip removed) simply drops its watcher — the task still finishes and
// fills the cache for the next request.
class ClipThumbImageResponse : public QQuickImageResponse
{
    Q_OBJECT
public:
    explicit ClipThumbImageResponse(const QFuture<QImage>& future);

    QQuickTextureFactory* textureFactory() const override;

private:
    QFutureWatcher<QImage> m_watcher;
    QImage m_image;
};

#endif // CLIP_THUMB_IMAGE_PROVIDER_H
