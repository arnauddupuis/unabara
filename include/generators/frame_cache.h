#ifndef FRAME_CACHE_H
#define FRAME_CACHE_H

#include <QImage>
#include <QHash>
#include <QList>

class DiveData;
class IFrameGenerator;

// Time-bucketed image cache wrapping an IFrameGenerator. Quantizes the
// requested dive-time into buckets so consecutive requests at slightly
// different times collapse into one render — the natural "low FPS" cadence
// for live preview compositing on a video player.
//
// Invalidated by bumping an epoch; the epoch is part of the cache key, so
// stale entries are simply dropped on the next lookup. Callers wire all
// generator/Config *Changed() signals to invalidate().
class FrameCache
{
public:
    FrameCache(IFrameGenerator* gen,
               double bucketSeconds = 0.5,
               int maxEntries = 64);

    // Returns the rendered frame for `dive` at `timePoint` (seconds), reusing
    // a cached result when the bucket+epoch+dive triple matches. Returns
    // QImage() if generator or dive is null.
    QImage frameAt(DiveData* dive, double timePoint);

    // Drops every cached entry — call when any config that affects the
    // generator's output changes.
    void invalidate();

private:
    struct Key { quint64 packed; DiveData* dive; };
    struct Entry { quint64 packed; DiveData* dive; QImage image; };

    IFrameGenerator* m_gen;
    double m_bucketSeconds;
    int m_maxEntries;
    quint64 m_epoch;
    QList<Entry> m_entries; // simple LRU; small N so linear scan is fine
};

#endif // FRAME_CACHE_H
