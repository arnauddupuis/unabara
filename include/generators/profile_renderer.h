#ifndef PROFILE_RENDERER_H
#define PROFILE_RENDERER_H

#include <QColor>
#include <QPainter>
#include <QRectF>

class DiveData;

/**
 * Stateless rendering helpers for the dive profile graphic.
 *
 * All functions paint into `rect` (in the painter's coordinate space) and
 * use the dive's max depth and total duration to compute coordinate mappings.
 *
 * Depth grows downward in screen coordinates: y = rect.top + (depth / maxDepth) * rect.height.
 * Time grows left-to-right: x = rect.left + (t / duration) * rect.width.
 *
 * The "rect" is meant to be the full target image rect. There is no padding —
 * if you want padding, pass an inset rect.
 */
namespace ProfileRenderer {

// Fill `rect` with the background color at the given opacity (0..1).
// Skipped entirely when opacity == 0 so the resulting image stays transparent.
void drawBackground(QPainter& p, const QRectF& rect, const QColor& bg, double opacity);

// Stroke a depth curve across `rect`. No-op if the dive has fewer than 2 samples
// or a zero max depth.
void drawDepthCurve(QPainter& p, const QRectF& rect, DiveData* dive,
                    const QColor& color, double lineWidth = 2.0);

// Draw a filled indicator dot at the (time, depth) interpolated for currentTimeSec.
// When `pulsing` is true, the dot's radius is modulated by `pulsePhase01` (0..1
// fraction of the pulse cycle) so the same phase always produces the same visual
// — making animated previews and exported frames perfectly aligned.
void drawIndicator(QPainter& p, const QRectF& rect, DiveData* dive,
                   double currentTimeSec, const QColor& color,
                   double radius, bool pulsing, double pulsePhase01);

} // namespace ProfileRenderer

#endif // PROFILE_RENDERER_H
