#ifndef OVERLAY_IMAGE_PROVIDER_H
#define OVERLAY_IMAGE_PROVIDER_H

#include <QQuickImageProvider>
#include "include/core/dive_data.h"
#include "include/generators/overlay_gen.h"

class FrameCache;

// Forward declare the class first
class OverlayImageProvider;

// Now declare the global variable
extern OverlayImageProvider* g_imageProvider;

// Then define the class
class OverlayImageProvider : public QQuickImageProvider
{
public:
    OverlayImageProvider(OverlayGenerator* generator);
    
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
    
    void setCurrentDive(DiveData* dive);
    void setCurrentTime(double time);

    // Optional cache used for "at/<seconds>/<tick>" requests from the video
    // preview compositor. Provider does not take ownership.
    void setFrameCache(FrameCache* cache) { m_frameCache = cache; }

private:
    OverlayGenerator* m_generator;
    DiveData* m_currentDive;
    double m_currentTime;
    FrameCache* m_frameCache = nullptr;
};

#endif // OVERLAY_IMAGE_PROVIDER_H