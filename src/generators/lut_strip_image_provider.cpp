#include "include/generators/lut_strip_image_provider.h"

#include <QMutexLocker>

#include <cmath>

LutStripImageProvider::LutStripImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_strip(identityStrip())
{
}

QImage LutStripImageProvider::requestImage(const QString& id, QSize* size,
                                           const QSize& requestedSize)
{
    Q_UNUSED(id)            // revision token: cache-buster only
    Q_UNUSED(requestedSize) // the strip is always served at its exact size

    QMutexLocker locker(&m_mutex);
    // QImage's implicit sharing makes this copy a thread-safe atomic-refcount
    // snapshot; the GUI thread's setStrip() detaches, never mutates in place.
    const QImage strip = m_strip;
    locker.unlock();

    if (size)
        *size = strip.size();
    return strip;
}

void LutStripImageProvider::setStrip(const QImage& strip)
{
    QMutexLocker locker(&m_mutex);
    m_strip = strip.isNull() ? identityStrip() : strip;
}

void LutStripImageProvider::resetToIdentity()
{
    QMutexLocker locker(&m_mutex);
    m_strip = identityStrip();
}

QImage LutStripImageProvider::identityStrip(int n)
{
    QImage img(n * n, n, QImage::Format_RGB32);
    const double scale = 255.0 / (n - 1);
    for (int b = 0; b < n; ++b) {
        const int xBase = b * n;
        const int blue = static_cast<int>(std::lround(b * scale));
        for (int g = 0; g < n; ++g) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(g)) + xBase;
            const int green = static_cast<int>(std::lround(g * scale));
            for (int r = 0; r < n; ++r)
                line[r] = qRgb(static_cast<int>(std::lround(r * scale)), green, blue);
        }
    }
    return img;
}
