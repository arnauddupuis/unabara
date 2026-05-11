#include "include/generators/profile_renderer.h"
#include "include/core/dive_data.h"

#include <QPainterPath>
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
