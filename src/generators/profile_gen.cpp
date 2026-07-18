#include "include/generators/profile_gen.h"

#include <QPainter>

#include <cmath>

#include "include/core/config.h"
#include "include/core/units.h"
#include "include/generators/profile_renderer.h"

ProfileGenerator::ProfileGenerator(QObject* parent)
    : QObject(parent)
{
    // Seed from Config so settings persist across sessions.
    Config* cfg = Config::instance();
    m_backgroundColor   = cfg->profileBackgroundColor();
    m_backgroundOpacity = cfg->profileBackgroundOpacity();
    m_curveColor        = cfg->profileCurveColor();
    m_curveWidth        = cfg->profileCurveWidth();
    m_indicatorColor    = cfg->profileIndicatorColor();
    m_indicatorMode     = static_cast<IndicatorMode>(cfg->profileIndicatorMode());
    m_indicatorRadius   = cfg->profileIndicatorRadius();
    m_pulsePeriodMs     = cfg->profilePulsePeriodMs();
    m_outputWidth       = cfg->profileOutputWidth();
    m_outputHeight      = cfg->profileOutputHeight();
    m_decoZoneColor     = cfg->profileDecoZoneColor();
    m_decoZoneOpacity   = cfg->profileDecoZoneOpacity();
    m_gridEnabled       = cfg->profileGridEnabled();
    m_gridDepthInterval = cfg->profileGridDepthInterval();
    m_gridTimeInterval  = cfg->profileGridTimeInterval();
    m_gridColor         = cfg->profileGridColor();
    m_gridOpacity       = cfg->profileGridOpacity();
    m_gridLineWidth     = cfg->profileGridLineWidth();
    m_gridShowLabels    = cfg->profileGridShowLabels();

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
    connect(cfg, &Config::profileCurveWidthChanged, this, [this]() {
        m_curveWidth = Config::instance()->profileCurveWidth();
        emit curveWidthChanged();
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
    connect(cfg, &Config::profileDecoZoneColorChanged, this, [this]() {
        m_decoZoneColor = Config::instance()->profileDecoZoneColor();
        emit decoZoneColorChanged();
    });
    connect(cfg, &Config::profileDecoZoneOpacityChanged, this, [this]() {
        m_decoZoneOpacity = Config::instance()->profileDecoZoneOpacity();
        emit decoZoneOpacityChanged();
    });
    connect(cfg, &Config::profileGridEnabledChanged, this, [this]() {
        m_gridEnabled = Config::instance()->profileGridEnabled();
        emit gridEnabledChanged();
    });
    connect(cfg, &Config::profileGridDepthIntervalChanged, this, [this]() {
        m_gridDepthInterval = Config::instance()->profileGridDepthInterval();
        emit gridDepthIntervalChanged();
    });
    connect(cfg, &Config::profileGridTimeIntervalChanged, this, [this]() {
        m_gridTimeInterval = Config::instance()->profileGridTimeInterval();
        emit gridTimeIntervalChanged();
    });
    connect(cfg, &Config::profileGridColorChanged, this, [this]() {
        m_gridColor = Config::instance()->profileGridColor();
        emit gridColorChanged();
    });
    connect(cfg, &Config::profileGridOpacityChanged, this, [this]() {
        m_gridOpacity = Config::instance()->profileGridOpacity();
        emit gridOpacityChanged();
    });
    connect(cfg, &Config::profileGridLineWidthChanged, this, [this]() {
        m_gridLineWidth = Config::instance()->profileGridLineWidth();
        emit gridLineWidthChanged();
    });
    connect(cfg, &Config::profileGridShowLabelsChanged, this, [this]() {
        m_gridShowLabels = Config::instance()->profileGridShowLabels();
        emit gridShowLabelsChanged();
    });

    // Repaint when the user's unit setting changes — depth-interval labels and
    // line positions are unit-dependent.
    connect(cfg, &Config::unitSystemChanged, this, [this]() {
        if (m_gridEnabled) emit gridDepthIntervalChanged();
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

void ProfileGenerator::setCurveWidth(int w)
{
    w = qMax(1, w);
    if (m_curveWidth != w) {
        m_curveWidth = w;
        Config::instance()->setProfileCurveWidth(w);
        emit curveWidthChanged();
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

void ProfileGenerator::setDecoZoneColor(const QColor& c)
{
    if (m_decoZoneColor != c) {
        m_decoZoneColor = c;
        Config::instance()->setProfileDecoZoneColor(c);
        emit decoZoneColorChanged();
    }
}

void ProfileGenerator::setDecoZoneOpacity(double o)
{
    o = qBound(0.0, o, 1.0);
    if (qAbs(m_decoZoneOpacity - o) > 0.001) {
        m_decoZoneOpacity = o;
        Config::instance()->setProfileDecoZoneOpacity(o);
        emit decoZoneOpacityChanged();
    }
}

void ProfileGenerator::setGridEnabled(bool enabled)
{
    if (m_gridEnabled != enabled) {
        m_gridEnabled = enabled;
        Config::instance()->setProfileGridEnabled(enabled);
        emit gridEnabledChanged();
    }
}

void ProfileGenerator::setGridDepthInterval(int interval)
{
    interval = qMax(1, interval);
    if (m_gridDepthInterval != interval) {
        m_gridDepthInterval = interval;
        Config::instance()->setProfileGridDepthInterval(interval);
        emit gridDepthIntervalChanged();
    }
}

void ProfileGenerator::setGridTimeInterval(int seconds)
{
    seconds = qMax(1, seconds);
    if (m_gridTimeInterval != seconds) {
        m_gridTimeInterval = seconds;
        Config::instance()->setProfileGridTimeInterval(seconds);
        emit gridTimeIntervalChanged();
    }
}

void ProfileGenerator::setGridColor(const QColor& c)
{
    if (m_gridColor != c) {
        m_gridColor = c;
        Config::instance()->setProfileGridColor(c);
        emit gridColorChanged();
    }
}

void ProfileGenerator::setGridOpacity(double o)
{
    o = qBound(0.0, o, 1.0);
    if (qAbs(m_gridOpacity - o) > 0.001) {
        m_gridOpacity = o;
        Config::instance()->setProfileGridOpacity(o);
        emit gridOpacityChanged();
    }
}

void ProfileGenerator::setGridLineWidth(int width)
{
    width = qMax(1, width);
    if (m_gridLineWidth != width) {
        m_gridLineWidth = width;
        Config::instance()->setProfileGridLineWidth(width);
        emit gridLineWidthChanged();
    }
}

void ProfileGenerator::setGridShowLabels(bool show)
{
    if (m_gridShowLabels != show) {
        m_gridShowLabels = show;
        Config::instance()->setProfileGridShowLabels(show);
        emit gridShowLabelsChanged();
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
    if (m_gridEnabled) {
        ProfileRenderer::GridOptions g;
        const Units::UnitSystem unitSystem = Config::instance()->unitSystem();
        // depth interval is entered in the user's unit; convert to meters.
        const double intervalMeters = (unitSystem == Units::UnitSystem::Imperial)
            ? Units::feetToMeters(static_cast<double>(m_gridDepthInterval))
            : static_cast<double>(m_gridDepthInterval);
        g.depthIntervalMeters = intervalMeters;
        g.timeIntervalSec = m_gridTimeInterval;
        g.color = m_gridColor;
        g.opacity = m_gridOpacity;
        g.lineWidth = static_cast<double>(m_gridLineWidth);
        g.showLabels = m_gridShowLabels;
        g.unitSystem = unitSystem;
        ProfileRenderer::drawGrid(painter, rect, dive, g);
    }
    ProfileRenderer::drawDecoZone(painter, rect, dive, m_decoZoneColor, m_decoZoneOpacity);
    ProfileRenderer::drawDepthCurve(painter, rect, dive, m_curveColor,
                                    static_cast<double>(m_curveWidth));
    ProfileRenderer::drawIndicator(painter, rect, dive, timePoint, m_indicatorColor,
                                   static_cast<double>(m_indicatorRadius),
                                   m_indicatorMode == Pulsing, pulsePhase01);

    return img;
}
