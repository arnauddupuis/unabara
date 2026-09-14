#include "include/generators/overlay_image_provider.h"
#include "include/generators/frame_cache.h"
#include <QDebug>

OverlayImageProvider::OverlayImageProvider(OverlayGenerator* generator)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_generator(generator)
    , m_currentDive(nullptr)
    , m_currentTime(0.0)
{
}

QImage OverlayImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    qDebug() << "OverlayImageProvider::requestImage called with id:" << id;
    
    if (!m_generator || !m_currentDive) {
        qWarning() << "OverlayImageProvider: Generator or dive data not set";
        QImage emptyImage(640, 120, QImage::Format_ARGB32);
        emptyImage.fill(Qt::black);
        
        if (size) {
            *size = emptyImage.size();
        }
        
        return emptyImage;
    }
    
    QImage result;

    // No generator state may be touched here: requestImage() runs on Qt
    // Quick's image-loader worker thread (renderImage is asynchronous), and
    // mutating a GUI-thread QObject — let alone emitting its signals — from
    // here is a data race. Cell backgrounds are off by default on the
    // generator (the interactive editor draws its own in QML), so the old
    // save/disable/restore dance around the render is simply gone.

    if (id.startsWith("at/")) {
        // "at/<seconds>/<tick>" — explicit dive-time request from the video
        // preview compositor. Routed through the frame cache when one is
        // attached so repeated bucket-equal requests collapse to one render.
        const QString tail = id.mid(3);
        const int slash = tail.indexOf('/');
        const QString secsStr = slash >= 0 ? tail.left(slash) : tail;
        bool ok = false;
        const double timePoint = secsStr.toDouble(&ok);
        if (ok) {
            if (m_frameCache) {
                result = m_frameCache->frameAt(m_currentDive, timePoint);
            } else {
                result = m_generator->generateOverlay(m_currentDive, timePoint);
            }
        } else {
            result = m_generator->generateOverlay(m_currentDive, m_currentTime);
        }
    } else if (id.startsWith("preview/")) {
        // For preview images, use the current time
        qDebug() << "OverlayImageProvider: Generating preview at time:" << m_currentTime;
        result = m_generator->generateOverlay(m_currentDive, m_currentTime);
    } else {
        // For specific time points
        bool ok;
        double timePoint = id.toDouble(&ok);
        if (ok) {
            result = m_generator->generateOverlay(m_currentDive, timePoint);
        } else {
            // Default to using the current time
            result = m_generator->generateOverlay(m_currentDive, m_currentTime);
        }
    }

    if (result.isNull()) {
        qWarning() << "OverlayImageProvider: Failed to generate overlay image";
        QImage emptyImage(640, 120, QImage::Format_ARGB32);
        emptyImage.fill(Qt::black);
        result = emptyImage;
    }
    
    if (size) {
        *size = result.size();
    }
    
    if (requestedSize.isValid() && requestedSize != result.size()) {
        result = result.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    
    return result;
}

void OverlayImageProvider::setCurrentDive(DiveData* dive)
{
    m_currentDive = dive;
}

void OverlayImageProvider::setCurrentTime(double time)
{
    m_currentTime = time;
}