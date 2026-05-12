#include "include/generators/profile_gen.h"

#include <QPainter>

#include <cmath>

#include "include/core/config.h"
#include "include/generators/profile_renderer.h"

ProfileGenerator::ProfileGenerator(QObject* parent)
    : QObject(parent)
{
    // Seed from Config so settings persist across sessions.
    Config* cfg = Config::instance();
    m_backgroundColor   = cfg->profileBackgroundColor();
    m_backgroundOpacity = cfg->profileBackgroundOpacity();
    m_curveColor        = cfg->profileCurveColor();
    m_indicatorColor    = cfg->profileIndicatorColor();
    m_indicatorMode     = static_cast<IndicatorMode>(cfg->profileIndicatorMode());
    m_indicatorRadius   = cfg->profileIndicatorRadius();
    m_pulsePeriodMs     = cfg->profilePulsePeriodMs();
    m_outputWidth       = cfg->profileOutputWidth();
    m_outputHeight      = cfg->profileOutputHeight();

    // Stay in sync with Config — settings changes flowing in from anywhere
    // (e.g. another window, future preferences dialog) propagate to here.
    connect(cfg, &Config::profileBackgroundColorChanged, this, [this]() {
        m_backgroundColor = Config::instance()->profileBackgroundColor();
        emit backgroundColorChanged();
    });
    connect(cfg, &Config::profileBackgroundOpacityChanged, this, [this]() {
        m_backgroundOpacity = Config::instance()->profileBackgroundOpacity();
        emit backgroundOpacityChanged();
    });
    connect(cfg, &Config::profileCurveColorChanged, this, [this]() {
        m_curveColor = Config::instance()->profileCurveColor();
        emit curveColorChanged();
    });
    connect(cfg, &Config::profileIndicatorColorChanged, this, [this]() {
        m_indicatorColor = Config::instance()->profileIndicatorColor();
        emit indicatorColorChanged();
    });
    connect(cfg, &Config::profileIndicatorModeChanged, this, [this]() {
        m_indicatorMode = static_cast<IndicatorMode>(Config::instance()->profileIndicatorMode());
        emit indicatorModeChanged();
    });
    connect(cfg, &Config::profileIndicatorRadiusChanged, this, [this]() {
        m_indicatorRadius = Config::instance()->profileIndicatorRadius();
        emit indicatorRadiusChanged();
    });
    connect(cfg, &Config::profilePulsePeriodMsChanged, this, [this]() {
        m_pulsePeriodMs = Config::instance()->profilePulsePeriodMs();
        emit pulsePeriodMsChanged();
    });
    connect(cfg, &Config::profileOutputWidthChanged, this, [this]() {
        m_outputWidth = Config::instance()->profileOutputWidth();
        emit outputWidthChanged();
    });
    connect(cfg, &Config::profileOutputHeightChanged, this, [this]() {
        m_outputHeight = Config::instance()->profileOutputHeight();
        emit outputHeightChanged();
    });
}

void ProfileGenerator::setBackgroundColor(const QColor& c)
{
    if (m_backgroundColor != c) {
        m_backgroundColor = c;
        Config::instance()->setProfileBackgroundColor(c);
        emit backgroundColorChanged();
    }
}

void ProfileGenerator::setBackgroundOpacity(double o)
{
    o = qBound(0.0, o, 1.0);
    if (qAbs(m_backgroundOpacity - o) > 0.001) {
        m_backgroundOpacity = o;
        Config::instance()->setProfileBackgroundOpacity(o);
        emit backgroundOpacityChanged();
    }
}

void ProfileGenerator::setCurveColor(const QColor& c)
{
    if (m_curveColor != c) {
        m_curveColor = c;
        Config::instance()->setProfileCurveColor(c);
        emit curveColorChanged();
    }
}

void ProfileGenerator::setIndicatorColor(const QColor& c)
{
    if (m_indicatorColor != c) {
        m_indicatorColor = c;
        Config::instance()->setProfileIndicatorColor(c);
        emit indicatorColorChanged();
    }
}

void ProfileGenerator::setIndicatorMode(IndicatorMode m)
{
    if (m_indicatorMode != m) {
        m_indicatorMode = m;
        Config::instance()->setProfileIndicatorMode(static_cast<int>(m));
        emit indicatorModeChanged();
    }
}

void ProfileGenerator::setIndicatorRadius(int r)
{
    r = qMax(1, r);
    if (m_indicatorRadius != r) {
        m_indicatorRadius = r;
        Config::instance()->setProfileIndicatorRadius(r);
        emit indicatorRadiusChanged();
    }
}

void ProfileGenerator::setPulsePeriodMs(int ms)
{
    ms = qMax(100, ms);
    if (m_pulsePeriodMs != ms) {
        m_pulsePeriodMs = ms;
        Config::instance()->setProfilePulsePeriodMs(ms);
        emit pulsePeriodMsChanged();
    }
}

void ProfileGenerator::setOutputWidth(int w)
{
    w = qMax(16, w);
    if (m_outputWidth != w) {
        m_outputWidth = w;
        Config::instance()->setProfileOutputWidth(w);
        emit outputWidthChanged();
    }
}

void ProfileGenerator::setOutputHeight(int h)
{
    h = qMax(16, h);
    if (m_outputHeight != h) {
        m_outputHeight = h;
        Config::instance()->setProfileOutputHeight(h);
        emit outputHeightChanged();
    }
}

QImage ProfileGenerator::generate(DiveData* dive, double timePoint)
{
    // Export path: pulse phase is deterministic from the dive-time so a
    // sequence of exported frames advances the pulse predictably.
    const double pulsePhase01 = (m_pulsePeriodMs > 0)
        ? std::fmod(timePoint * 1000.0, static_cast<double>(m_pulsePeriodMs)) / m_pulsePeriodMs
        : 0.0;
    return renderFrame(dive, timePoint, pulsePhase01);
}

QImage ProfileGenerator::renderFrame(DiveData* dive, double timePoint, double pulsePhase01)
{
    QImage img(m_outputWidth, m_outputHeight, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    if (!dive) {
        return img;
    }

    QPainter painter(&img);
    const QRectF rect(0, 0, m_outputWidth, m_outputHeight);

    ProfileRenderer::drawBackground(painter, rect, m_backgroundColor, m_backgroundOpacity);
    ProfileRenderer::drawDepthCurve(painter, rect, dive, m_curveColor);
    ProfileRenderer::drawIndicator(painter, rect, dive, timePoint, m_indicatorColor,
                                   static_cast<double>(m_indicatorRadius),
                                   m_indicatorMode == Pulsing, pulsePhase01);

    return img;
}
