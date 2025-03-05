#include "include/generators/overlay_image_provider.h"
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
    
    if (id.startsWith("preview/")) {
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