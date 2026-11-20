#include "include/generators/frame_cache.h"
#include "include/generators/i_frame_generator.h"

#include <cmath>

FrameCache::FrameCache(IFrameGenerator* gen, double bucketSeconds, int maxEntries)
    : m_gen(gen)
    , m_bucketSeconds(bucketSeconds > 0.0 ? bucketSeconds : 0.5)
    , m_maxEntries(maxEntries > 1 ? maxEntries : 1)
    , m_epoch(1)
{
}

QImage FrameCache::frameAt(DiveData* dive, double timePoint)
{
    if (!m_gen || !dive)
        return QImage();

    const qint64 bucket = static_cast<qint64>(std::floor(timePoint / m_bucketSeconds));

    quint64 packed = 0;
    {
        QMutexLocker locker(&m_mutex);
        // Pack epoch (high 32) + bucket (low 32, biased to keep negatives
        // well-defined). Epoch read under the lock.
        packed = (m_epoch << 32) ^ static_cast<quint64>(bucket);

        for (int i = m_entries.size() - 1; i >= 0; --i) {
            if (m_entries[i].packed == packed && m_entries[i].dive == dive) {
                Entry hit = m_entries.takeAt(i);
                m_entries.append(hit); // bump to MRU
                return hit.image;      // QImage copy is COW-safe across threads
            }
        }
    }

    // Miss — render OUTSIDE the lock (see header note), then insert. If an
    // invalidate() lands meanwhile, this entry's old-epoch key simply never
    // matches again and ages out of the LRU.
    const double bucketTime = bucket * m_bucketSeconds;
    QImage img = m_gen->generate(dive, bucketTime);

    {
        QMutexLocker locker(&m_mutex);
        m_entries.append({ packed, dive, img });
        if (m_entries.size() > m_maxEntries)
            m_entries.removeFirst();
    }

    return img;
}

void FrameCache::invalidate()
{
    QMutexLocker locker(&m_mutex);
    ++m_epoch;
    m_entries.clear();
}
