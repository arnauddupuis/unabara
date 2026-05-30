#ifndef PROFILE_RENDERER_H
#define PROFILE_RENDERER_H

#include <QColor>
#include <QPainter>
#include <QRectF>

#include "include/core/units.h"

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

// Grid styling. `depthIntervalMeters` is in meters regardless of unitSystem;
// the UI is responsible for converting the user-entered value (which may be in
// feet) into meters before populating this struct. Labels are formatted using
// `unitSystem`.
struct GridOptions {
    double depthIntervalMeters;
    int timeIntervalSec;
    QColor color;
    double opacity;
    double lineWidth;
    bool showLabels;
    Units::UnitSystem unitSystem;
};

// Draw a reference grid over `rect`: horizontal lines at multiples of
// `depthIntervalMeters` (labeled in the user's unit) and vertical lines at
// multiples of `timeIntervalSec` (labeled as mm:ss). No-op if intervals are
// non-positive or the dive is empty.
void drawGrid(QPainter& p, const QRectF& rect, DiveData* dive, const GridOptions& opts);

// Fill the decompression-ceiling region: for each consecutive run of samples
// where ceiling > 0, fill a polygon from the surface (y=0) down to the ceiling
// depth at each timestamp. No-op when opacity is 0 or no samples have a
// non-zero ceiling.
void drawDecoZone(QPainter& p, const QRectF& rect, DiveData* dive,
                  const QColor& color, double opacity);

// Draw a filled indicator dot at the (time, depth) interpolated for currentTimeSec.
// When `pulsing` is true, the dot's radius is modulated by `pulsePhase01` (0..1
// fraction of the pulse cycle) so the same phase always produces the same visual
// — making animated previews and exported frames perfectly aligned.
void drawIndicator(QPainter& p, const QRectF& rect, DiveData* dive,
                   double currentTimeSec, const QColor& color,
                   double radius, bool pulsing, double pulsePhase01);

} // namespace ProfileRenderer

#endif // PROFILE_RENDERER_H
