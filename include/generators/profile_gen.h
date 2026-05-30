#ifndef PROFILE_GEN_H
#define PROFILE_GEN_H

#include <QColor>
#include <QImage>
#include <QObject>

#include "include/core/dive_data.h"
#include "include/generators/i_frame_generator.h"

/**
 * Renders the dive depth-profile graphic: a curve of depth-over-time with an
 * indicator dot positioned at the current dive-time. Mirrors OverlayGenerator's
 * shape so both can flow through the same export pipeline (via IFrameGenerator).
 *
 * All settings are mirrored to/from Config so they persist across sessions.
 */
class ProfileGenerator : public QObject, public IFrameGenerator
{
    Q_OBJECT

    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY backgroundColorChanged)
    Q_PROPERTY(double backgroundOpacity READ backgroundOpacity WRITE setBackgroundOpacity NOTIFY backgroundOpacityChanged)
    Q_PROPERTY(QColor curveColor READ curveColor WRITE setCurveColor NOTIFY curveColorChanged)
    Q_PROPERTY(QColor indicatorColor READ indicatorColor WRITE setIndicatorColor NOTIFY indicatorColorChanged)
    Q_PROPERTY(IndicatorMode indicatorMode READ indicatorMode WRITE setIndicatorMode NOTIFY indicatorModeChanged)
    Q_PROPERTY(int indicatorRadius READ indicatorRadius WRITE setIndicatorRadius NOTIFY indicatorRadiusChanged)
    Q_PROPERTY(int pulsePeriodMs READ pulsePeriodMs WRITE setPulsePeriodMs NOTIFY pulsePeriodMsChanged)
    Q_PROPERTY(int outputWidth READ outputWidth WRITE setOutputWidth NOTIFY outputWidthChanged)
    Q_PROPERTY(int outputHeight READ outputHeight WRITE setOutputHeight NOTIFY outputHeightChanged)
    Q_PROPERTY(QColor decoZoneColor READ decoZoneColor WRITE setDecoZoneColor NOTIFY decoZoneColorChanged)
    Q_PROPERTY(double decoZoneOpacity READ decoZoneOpacity WRITE setDecoZoneOpacity NOTIFY decoZoneOpacityChanged)
    Q_PROPERTY(bool gridEnabled READ gridEnabled WRITE setGridEnabled NOTIFY gridEnabledChanged)
    Q_PROPERTY(int gridDepthInterval READ gridDepthInterval WRITE setGridDepthInterval NOTIFY gridDepthIntervalChanged)
    Q_PROPERTY(int gridTimeInterval READ gridTimeInterval WRITE setGridTimeInterval NOTIFY gridTimeIntervalChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY gridColorChanged)
    Q_PROPERTY(double gridOpacity READ gridOpacity WRITE setGridOpacity NOTIFY gridOpacityChanged)
    Q_PROPERTY(int gridLineWidth READ gridLineWidth WRITE setGridLineWidth NOTIFY gridLineWidthChanged)
    Q_PROPERTY(bool gridShowLabels READ gridShowLabels WRITE setGridShowLabels NOTIFY gridShowLabelsChanged)

public:
    enum IndicatorMode {
        Static = 0,
        Pulsing = 1
    };
    Q_ENUM(IndicatorMode)

    explicit ProfileGenerator(QObject* parent = nullptr);

    QColor backgroundColor() const { return m_backgroundColor; }
    double backgroundOpacity() const { return m_backgroundOpacity; }
    QColor curveColor() const { return m_curveColor; }
    QColor indicatorColor() const { return m_indicatorColor; }
    IndicatorMode indicatorMode() const { return m_indicatorMode; }
    int indicatorRadius() const { return m_indicatorRadius; }
    int pulsePeriodMs() const { return m_pulsePeriodMs; }
    int outputWidth() const { return m_outputWidth; }
    int outputHeight() const { return m_outputHeight; }
    QColor decoZoneColor() const { return m_decoZoneColor; }
    double decoZoneOpacity() const { return m_decoZoneOpacity; }
    bool gridEnabled() const { return m_gridEnabled; }
    int gridDepthInterval() const { return m_gridDepthInterval; }
    int gridTimeInterval() const { return m_gridTimeInterval; }
    QColor gridColor() const { return m_gridColor; }
    double gridOpacity() const { return m_gridOpacity; }
    int gridLineWidth() const { return m_gridLineWidth; }
    bool gridShowLabels() const { return m_gridShowLabels; }

    void setBackgroundColor(const QColor& c);
    void setBackgroundOpacity(double o);
    void setCurveColor(const QColor& c);
    void setIndicatorColor(const QColor& c);
    void setIndicatorMode(IndicatorMode m);
    void setIndicatorRadius(int r);
    void setPulsePeriodMs(int ms);
    void setOutputWidth(int w);
    void setOutputHeight(int h);
    void setDecoZoneColor(const QColor& c);
    void setDecoZoneOpacity(double o);
    void setGridEnabled(bool enabled);
    void setGridDepthInterval(int interval);
    void setGridTimeInterval(int seconds);
    void setGridColor(const QColor& c);
    void setGridOpacity(double o);
    void setGridLineWidth(int width);
    void setGridShowLabels(bool show);

    // IFrameGenerator — for exports, pulse phase is timestamp-derived so each
    // frame in a sequence has a deterministic pulse state.
    Q_INVOKABLE QImage generate(DiveData* dive, double timePoint) override;

    // Like generate() but with an explicit pulse phase (0..1). Used by the live
    // preview to animate the indicator while the user is idle on the timeline.
    QImage renderFrame(DiveData* dive, double timePoint, double pulsePhase01);

signals:
    void backgroundColorChanged();
    void backgroundOpacityChanged();
    void curveColorChanged();
    void indicatorColorChanged();
    void indicatorModeChanged();
    void indicatorRadiusChanged();
    void pulsePeriodMsChanged();
    void outputWidthChanged();
    void outputHeightChanged();
    void decoZoneColorChanged();
    void decoZoneOpacityChanged();
    void gridEnabledChanged();
    void gridDepthIntervalChanged();
    void gridTimeIntervalChanged();
    void gridColorChanged();
    void gridOpacityChanged();
    void gridLineWidthChanged();
    void gridShowLabelsChanged();

private:
    QColor m_backgroundColor;
    double m_backgroundOpacity;
    QColor m_curveColor;
    QColor m_indicatorColor;
    IndicatorMode m_indicatorMode;
    int m_indicatorRadius;
    int m_pulsePeriodMs;
    int m_outputWidth;
    int m_outputHeight;
    QColor m_decoZoneColor;
    double m_decoZoneOpacity;
    bool m_gridEnabled;
    int m_gridDepthInterval;
    int m_gridTimeInterval;
    QColor m_gridColor;
    double m_gridOpacity;
    int m_gridLineWidth;
    bool m_gridShowLabels;
};

#endif // PROFILE_GEN_H
