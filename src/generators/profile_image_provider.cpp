#include "include/generators/profile_image_provider.h"

#include <QDebug>

ProfileImageProvider* g_profileImageProvider = nullptr;

ProfileImageProvider::ProfileImageProvider(ProfileGenerator* generator)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_generator(generator)
    , m_currentDive(nullptr)
    , m_currentTime(0.0)
{
}

QImage ProfileImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    if (!m_generator || !m_currentDive) {
        QImage empty(m_generator ? m_generator->outputWidth() : 1920,
                     m_generator ? m_generator->outputHeight() : 400,
                     QImage::Format_ARGB32_Premultiplied);
        empty.fill(Qt::transparent);
        if (size) {
            *size = empty.size();
        }
        return empty;
    }

    QImage result;
    if (id.startsWith("preview/")) {
        // Preview path. The URL may carry an explicit pulse phase so the live
        // preview can animate the indicator on wall-clock time while the dive
        // cursor is idle. URL form: preview/phase=<0..1>/<cachebust>
        // Without a phase, fall back to timestamp-derived (matches export).
        double phase = -1.0;
        const int phaseIdx = id.indexOf(QStringLiteral("phase="));
        if (phaseIdx >= 0) {
            const int valStart = phaseIdx + 6;
            const int valEnd = id.indexOf(QLatin1Char('/'), valStart);
            const QString phaseStr = (valEnd >= 0)
                ? id.mid(valStart, valEnd - valStart)
                : id.mid(valStart);
            bool ok = false;
            const double v = phaseStr.toDouble(&ok);
            if (ok) {
                phase = v;
            }
        }
        if (phase >= 0.0) {
            result = m_generator->renderFrame(m_currentDive, m_currentTime, phase);
        } else {
            result = m_generator->generate(m_currentDive, m_currentTime);
        }
    } else {
        // Numeric id path — used for explicit timestamp requests (e.g. exports).
        bool ok = false;
        const double t = id.toDouble(&ok);
        result = m_generator->generate(m_currentDive, ok ? t : m_currentTime);
    }

    if (result.isNull()) {
        result = QImage(m_generator->outputWidth(), m_generator->outputHeight(),
                        QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::transparent);
    }

    if (size) {
        *size = result.size();
    }
    if (requestedSize.isValid() && requestedSize != result.size()) {
        result = result.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return result;
}

void ProfileImageProvider::setCurrentDive(DiveData* dive)
{
    m_currentDive = dive;
}

void ProfileImageProvider::setCurrentTime(double time)
{
    m_currentTime = time;
}
