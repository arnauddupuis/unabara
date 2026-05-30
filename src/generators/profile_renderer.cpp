#include "include/generators/profile_renderer.h"
#include "include/core/dive_data.h"
#include "include/core/units.h"

#include <QFontMetricsF>
#include <QPainterPath>
#include <QString>
#include <QtMath>

#include <algorithm>

namespace ProfileRenderer {

namespace {

// Compute the depth-axis maximum, with a 5% headroom above the actual maxDepth
// so the curve never touches the very bottom of the rect. Returns 0 for invalid
// dives (caller should skip drawing).
double depthAxisMax(DiveData* dive)
{
    if (!dive) {
        return 0.0;
    }
    const double m = dive->maxDepth();
    if (m <= 0.0) {
        return 0.0;
    }
    return m * 1.05;
}

double timeAxisMax(DiveData* dive)
{
    if (!dive) {
        return 0.0;
    }
    return static_cast<double>(dive->durationSeconds());
}

// Map a (timestamp, depth) sample into pixel coords inside rect.
QPointF samplePoint(const QRectF& rect, double t, double depth,
                    double tMax, double depthMax)
{
    const double xFrac = (tMax > 0.0) ? (t / tMax) : 0.0;
    const double yFrac = (depthMax > 0.0) ? (depth / depthMax) : 0.0;
    return QPointF(rect.left() + xFrac * rect.width(),
                   rect.top() + yFrac * rect.height());
}

} // anonymous namespace

void drawBackground(QPainter& p, const QRectF& rect, const QColor& bg, double opacity)
{
    if (opacity <= 0.0) {
        return; // fully transparent — leave the canvas alone
    }
    QColor c = bg;
    c.setAlphaF(std::clamp(opacity, 0.0, 1.0) * (bg.alphaF() > 0 ? bg.alphaF() : 1.0));
    p.fillRect(rect, c);
}

void drawDepthCurve(QPainter& p, const QRectF& rect, DiveData* dive,
                    const QColor& color, double lineWidth)
{
    if (!dive) {
        return;
    }
    const auto& points = dive->allDataPoints();
    if (points.size() < 2) {
        return;
    }
    const double depthMax = depthAxisMax(dive);
    const double tMax = timeAxisMax(dive);
    if (depthMax <= 0.0 || tMax <= 0.0) {
        return;
    }

    QPainterPath path;
    bool first = true;
    for (const DiveDataPoint& pt : points) {
        const QPointF pix = samplePoint(rect, pt.timestamp, pt.depth, tMax, depthMax);
        if (first) {
            path.moveTo(pix);
            first = false;
        } else {
            path.lineTo(pix);
        }
    }

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color);
    pen.setWidthF(lineWidth);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.restore();
}

void drawGrid(QPainter& p, const QRectF& rect, DiveData* dive, const GridOptions& opts)
{
    if (!dive || opts.opacity <= 0.0
        || opts.depthIntervalMeters <= 0.0 || opts.timeIntervalSec <= 0) {
        return;
    }
    const double depthMax = depthAxisMax(dive);
    const double tMax = timeAxisMax(dive);
    if (depthMax <= 0.0 || tMax <= 0.0) {
        return;
    }

    QColor lineColor = opts.color;
    lineColor.setAlphaF(std::clamp(opts.opacity, 0.0, 1.0)
                        * (opts.color.alphaF() > 0 ? opts.color.alphaF() : 1.0));

    p.save();
    p.setRenderHint(QPainter::Antialiasing, false); // crisp 1-px lines

    QPen pen(lineColor);
    pen.setWidthF(opts.lineWidth);
    pen.setCapStyle(Qt::FlatCap);
    p.setPen(pen);

    // Horizontal lines (depth axis). Iterate in meters; convert label using the
    // user's unit system.
    for (double dm = opts.depthIntervalMeters; dm <= depthMax; dm += opts.depthIntervalMeters) {
        const double y = rect.top() + (dm / depthMax) * rect.height();
        p.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    }

    // Vertical lines (time axis).
    for (double t = opts.timeIntervalSec; t <= tMax; t += opts.timeIntervalSec) {
        const double x = rect.left() + (t / tMax) * rect.width();
        p.drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    }

    if (opts.showLabels) {
        // Labels share the grid color but at full opacity (so they remain
        // readable when the grid lines are faint).
        QColor labelColor = opts.color;
        labelColor.setAlphaF(1.0);
        p.setPen(labelColor);

        QFont labelFont = p.font();
        // Scale the label font with the image height so labels stay readable
        // on a 4K profile without being huge on a small one.
        const int px = std::max(10, static_cast<int>(rect.height() * 0.04));
        labelFont.setPixelSize(px);
        p.setFont(labelFont);

        const QFontMetricsF fm(labelFont);

        // Depth labels — left-anchored just inside the rect, offset above the line.
        for (double dm = opts.depthIntervalMeters; dm <= depthMax; dm += opts.depthIntervalMeters) {
            const double y = rect.top() + (dm / depthMax) * rect.height();
            const QString text = Units::formatDepthValue(dm, opts.unitSystem);
            p.drawText(QPointF(rect.left() + 4, y - 2), text);
        }

        // Time labels — top-anchored just inside the rect, mm:ss format.
        for (double t = opts.timeIntervalSec; t <= tMax; t += opts.timeIntervalSec) {
            const double x = rect.left() + (t / tMax) * rect.width();
            const int totalSec = static_cast<int>(t);
            const int mm = totalSec / 60;
            const int ss = totalSec % 60;
            const QString text = QString::asprintf("%d:%02d", mm, ss);
            p.drawText(QPointF(x + 4, rect.top() + fm.ascent() + 2), text);
        }
    }

    p.restore();
}

void drawDecoZone(QPainter& p, const QRectF& rect, DiveData* dive,
                  const QColor& color, double opacity)
{
    if (!dive || opacity <= 0.0) {
        return;
    }
    const auto& points = dive->allDataPoints();
    if (points.size() < 2) {
        return;
    }
    const double depthMax = depthAxisMax(dive);
    const double tMax = timeAxisMax(dive);
    if (depthMax <= 0.0 || tMax <= 0.0) {
        return;
    }

    QColor fill = color;
    fill.setAlphaF(std::clamp(opacity, 0.0, 1.0) * (color.alphaF() > 0 ? color.alphaF() : 1.0));

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);

    // Walk the samples and emit one filled polygon per consecutive run where
    // ceiling > 0. Polygon shape: top edge along y=0 (surface), bottom edge
    // follows the ceiling depth.
    QPainterPath path;
    bool inRun = false;
    QList<QPointF> ceilingPts;   // bottom edge of the current polygon
    double runStartT = 0.0;
    double runEndT = 0.0;

    auto flushRun = [&]() {
        if (ceilingPts.size() < 1) {
            return;
        }
        QPainterPath poly;
        const QPointF topLeft = samplePoint(rect, runStartT, 0.0, tMax, depthMax);
        poly.moveTo(topLeft);
        for (const QPointF& pt : ceilingPts) {
            poly.lineTo(pt);
        }
        const QPointF topRight = samplePoint(rect, runEndT, 0.0, tMax, depthMax);
        poly.lineTo(topRight);
        poly.closeSubpath();
        path.addPath(poly);
    };

    for (const DiveDataPoint& pt : points) {
        if (pt.ceiling > 0.0) {
            if (!inRun) {
                inRun = true;
                ceilingPts.clear();
                runStartT = pt.timestamp;
            }
            ceilingPts.append(samplePoint(rect, pt.timestamp, pt.ceiling, tMax, depthMax));
            runEndT = pt.timestamp;
        } else if (inRun) {
            flushRun();
            inRun = false;
            ceilingPts.clear();
        }
    }
    if (inRun) {
        flushRun();
    }

    if (!path.isEmpty()) {
        p.drawPath(path);
    }
    p.restore();
}

void drawIndicator(QPainter& p, const QRectF& rect, DiveData* dive,
                   double currentTimeSec, const QColor& color,
                   double radius, bool pulsing, double pulsePhase01)
{
    if (!dive || radius <= 0.0) {
        return;
    }
    const double depthMax = depthAxisMax(dive);
    const double tMax = timeAxisMax(dive);
    if (depthMax <= 0.0 || tMax <= 0.0) {
        return;
    }

    const DiveDataPoint sample = dive->dataAtTime(currentTimeSec);
    const QPointF center = samplePoint(rect, currentTimeSec, sample.depth, tMax, depthMax);

    // Pulse envelope: cosine ramp giving a deterministic 0..1..0 amplitude
    // over the period. We use this both for the dot size and for a translucent
    // "halo" ring drawn around it, so the pulse is visible even when the
    // radius setting is small.
    double envelope = 0.0;
    double scale = 1.0;
    if (pulsing) {
        const double phase = std::clamp(pulsePhase01, 0.0, 1.0);
        envelope = 0.5 - 0.5 * std::cos(phase * 2.0 * M_PI); // 0..1..0
        scale = 1.0 + 0.6 * envelope;                         // 1.0 → 1.6 → 1.0
    }

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // Halo: expanding translucent ring while pulsing (more visible than
    // size modulation alone, especially with small indicator radii).
    if (pulsing && envelope > 0.01) {
        const double haloRadius = radius * (1.0 + 1.6 * envelope);
        QColor halo = color;
        halo.setAlphaF(std::clamp(color.alphaF() * (1.0 - envelope) * 0.6, 0.0, 1.0));
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(center, haloRadius, haloRadius);
    }

    // Core dot — subtle dark outline so it stays visible on any background.
    QColor outline = Qt::black;
    outline.setAlphaF(0.6);
    QPen pen(outline);
    pen.setWidthF(1.5);
    p.setPen(pen);
    p.setBrush(color);

    const double r = radius * scale;
    p.drawEllipse(center, r, r);

    p.restore();
}

} // namespace ProfileRenderer
