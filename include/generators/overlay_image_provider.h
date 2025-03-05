#ifndef OVERLAY_IMAGE_PROVIDER_H
#define OVERLAY_IMAGE_PROVIDER_H

#include <QQuickImageProvider>
#include "include/core/dive_data.h"
#include "include/generators/overlay_gen.h"

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
    
private:
    OverlayGenerator* m_generator;
    DiveData* m_currentDive;
    double m_currentTime;
};

#endif // OVERLAY_IMAGE_PROVIDER_H