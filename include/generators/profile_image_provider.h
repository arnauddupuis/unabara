#ifndef PROFILE_IMAGE_PROVIDER_H
#define PROFILE_IMAGE_PROVIDER_H

#include <QQuickImageProvider>

#include "include/core/dive_data.h"
#include "include/generators/profile_gen.h"

class ProfileImageProvider;

// Global pointer mirroring g_imageProvider so Timeline (or anything else)
// can push the current dive/time without holding a QML/context reference.
extern ProfileImageProvider* g_profileImageProvider;

class ProfileImageProvider : public QQuickImageProvider
{
public:
    explicit ProfileImageProvider(ProfileGenerator* generator);

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    void setCurrentDive(DiveData* dive);
    void setCurrentTime(double time);

private:
    ProfileGenerator* m_generator;
    DiveData* m_currentDive;
    double m_currentTime;
};

#endif // PROFILE_IMAGE_PROVIDER_H
