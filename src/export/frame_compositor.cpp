#include "include/export/frame_compositor.h"

#include <QPainter>

FrameCompositor::FrameCompositor(const QSize &canvasSize)
    : m_canvasSize(canvasSize)
{
}

void FrameCompositor::addLayer(IFrameGenerator *generator,
                               const OverlayPlacement &placement)
{
    m_layers.append({generator, placement});
}

QRect FrameCompositor::placementRect(const QRectF &normalizedRect,
                                     const QSize &canvas)
{
    // KEEP IN SYNC with VideoOverlayItem.qml:
    //   x: Math.round(normalizedRect.x * parent.width)   (same for y/w/h)
    // Each component rounds independently — do NOT switch to rounding
    // right/bottom edges, which would disagree with the preview by a pixel.
    return QRect(qRound(normalizedRect.x() * canvas.width()),
                 qRound(normalizedRect.y() * canvas.height()),
                 qRound(normalizedRect.width() * canvas.width()),
                 qRound(normalizedRect.height() * canvas.height()));
}

QRect FrameCompositor::fitRect(const QSize &imageSize, const QRect &box)
{
    // KEEP IN SYNC with VideoOverlayItem.qml's Image.PreserveAspectFit:
    // uniform scale = min of the two ratios, centred on both axes. The
    // preview's scene graph works in floating point; this mirror commits
    // to whole pixels (qRound) — a sub-pixel residual that is documented
    // alongside the GPU-bilinear vs SmoothTransformation resampling
    // difference in the v0.4 plan.
    if (imageSize.isEmpty() || box.width() <= 0 || box.height() <= 0)
        return QRect();

    const double scale =
        qMin(static_cast<double>(box.width()) / imageSize.width(),
             static_cast<double>(box.height()) / imageSize.height());
    const int w = qMax(1, qRound(imageSize.width() * scale));
    const int h = qMax(1, qRound(imageSize.height() * scale));
    const int x = box.x() + qRound((box.width() - w) / 2.0);
    const int y = box.y() + qRound((box.height() - h) / 2.0);
    return QRect(x, y, w, h);
}

QImage FrameCompositor::composeFrame(DiveData *dive, double timePoint) const
{
    // Premultiplied canvas on purpose (same discipline as the golden-gated
    // generators): blending happens in premultiplied space, and the raw
    // pipe ships these exact bytes under overlay=alpha=premultiplied.
    QImage canvas(m_canvasSize, QImage::Format_RGBA8888_Premultiplied);
    if (canvas.isNull())
        return canvas;
    canvas.fill(Qt::transparent);

    // Pre-dive footage (negative dive time, a clip that starts before the
    // dive): no overlay at all, matching VideoOverlayItem.qml's
    // `diveTime >= 0` source gate — DiveData::dataAtTime() would otherwise
    // clamp to sample 0 and burn a frozen t=0 overlay the preview never
    // shows. Still return the transparent canvas, NEVER a null image: a
    // null frame makes streamFrames() repeat the previous frame, and a null
    // first frame aborts the export. The post-dive end is deliberately not
    // gated (preview and export both show the clamped last sample).
    const bool preDive = timePoint < 0.0;

    QPainter painter(&canvas);
    for (const Layer &layer : m_layers) {
        if (preDive || !layer.generator || !layer.placement.visible)
            continue;

        const QImage source = layer.generator->generate(dive, timePoint);
        if (source.isNull())
            continue;

        const QRect box = placementRect(layer.placement.normalizedRect,
                                        m_canvasSize);
        const QRect target = fitRect(source.size(), box);
        if (target.isEmpty())
            continue;

        // SmoothTransformation from template resolution, exactly once per
        // layer per frame. Identity-sized sources skip the scale (the
        // compositor tests rely on byte-exact pass-through there).
        const QImage scaled =
            source.size() == target.size()
                ? source
                : source.scaled(target.size(), Qt::IgnoreAspectRatio,
                                Qt::SmoothTransformation);
        painter.drawImage(target.topLeft(), scaled);
    }
    painter.end();
    return canvas;
}

void CompositorFrameSource::beginExport()
{
    // Each distinct generator gets exactly one beginExport, however many
    // layers it backs (the exporters' begin/end contract is a symmetric
    // pair per generator, not per layer).
    QVector<IFrameGenerator *> seen;
    for (const FrameCompositor::Layer &layer : m_compositor->layers()) {
        if (layer.generator && !seen.contains(layer.generator)) {
            seen.append(layer.generator);
            layer.generator->beginExport();
        }
    }
}

void CompositorFrameSource::endExport()
{
    QVector<IFrameGenerator *> seen;
    for (const FrameCompositor::Layer &layer : m_compositor->layers()) {
        if (layer.generator && !seen.contains(layer.generator)) {
            seen.append(layer.generator);
            layer.generator->endExport();
        }
    }
}
