#ifndef FRAME_COMPOSITOR_H
#define FRAME_COMPOSITOR_H

#include <QImage>
#include <QRect>
#include <QSize>
#include <QVector>

#include "include/core/video_overlay_layout.h"
#include "include/generators/i_frame_generator.h"

class DiveData;

// Places generator-rendered overlays onto a full-frame RGBA plane at their
// video-preview positions (PR B3). The canvas is the clip's rotation-applied
// size; each layer is a frame generator plus its normalized OverlayPlacement
// from VideoOverlayLayout — exactly what the QML preview feeds its
// VideoOverlayItems, so the composited export reproduces the preview
// placement.
//
// Output format: Format_RGBA8888_Premultiplied, because the composite
// filter graph declares the pipe as overlay=alpha=premultiplied (the plan's
// choice: compositing math stays exact and SmoothTransformation scaling has
// no fringing). The "overlays only, placed" mode reuses the same plane and
// converts to straight alpha at the pipe boundary (VideoExporter does the
// convertToFormat).
//
// Pure and GUI-thread-only by construction: it owns nothing, holds plain
// pointers to generators it never mutates beyond generate()/begin/endExport
// (OverlayGenerator is GUI-thread-only — the exporters already run their
// render loops on the GUI thread), and has no QObject machinery, so the
// geometry math is unit-testable (composite_export_test).
class FrameCompositor
{
public:
    struct Layer {
        IFrameGenerator *generator = nullptr;
        OverlayPlacement placement;
    };

    explicit FrameCompositor(const QSize &canvasSize);

    // Layers render in insertion order (later = on top), matching the QML
    // sibling order of the two VideoOverlayItems. Hidden placements and
    // null generators are skipped at compose time.
    void addLayer(IFrameGenerator *generator, const OverlayPlacement &placement);
    int layerCount() const { return m_layers.size(); }
    const QVector<Layer> &layers() const { return m_layers; }
    QSize canvasSize() const { return m_canvasSize; }

    // One pre-placed full-frame RGBA8888_Premultiplied plane: transparent
    // canvas, each layer generated at its own (template) resolution, scaled
    // with QImage::scaled(SmoothTransformation) into fitRect() and drawn.
    QImage composeFrame(DiveData *dive, double timePoint) const;

    // Pixel rect of a normalized placement on the canvas.
    // KEEP IN SYNC with VideoOverlayItem.qml's pixel placement: each of
    // x/y/w/h is Math.round()ed INDEPENDENTLY against the content size
    // (JS Math.round == qRound for the non-negative values a clamped
    // normalized rect produces). This duplication is documented on both
    // sides — change the QML, change this.
    static QRect placementRect(const QRectF &normalizedRect, const QSize &canvas);

    // Where an image of `imageSize` lands inside `box` — the mirror of the
    // QML preview's Image.PreserveAspectFit: scaled by min(wRatio, hRatio),
    // centred on both axes. Size and centring offsets are rounded to whole
    // pixels (the scene graph renders the preview subpixel; that sub-pixel
    // difference plus GPU-bilinear-vs-SmoothTransformation resampling are
    // the documented preview↔export residuals). KEEP IN SYNC with
    // VideoOverlayItem.qml's Image fillMode.
    static QRect fitRect(const QSize &imageSize, const QRect &box);

private:
    QSize m_canvasSize;
    QVector<Layer> m_layers;
};

// IFrameGenerator adapter so the compositor plugs into VideoExporter's ONE
// streaming loop (the same loop drives overlay-only and composited exports
// — guardrail: fix the pattern, not the instance). begin/endExport fan out
// to every distinct layer generator, keeping the exporters' symmetric
// begin/end contract intact for the real generators underneath.
class CompositorFrameSource : public IFrameGenerator
{
public:
    explicit CompositorFrameSource(const FrameCompositor *compositor)
        : m_compositor(compositor) {}

    QImage generate(DiveData *dive, double timePoint) override
    {
        return m_compositor->composeFrame(dive, timePoint);
    }

    void beginExport() override;
    void endExport() override;

private:
    const FrameCompositor *m_compositor;
};

#endif // FRAME_COMPOSITOR_H
