#ifndef COLOR_UTILS_H
#define COLOR_UTILS_H

#include <QColor>
#include <QImage>

namespace Unabara {
namespace ColorUtils {

// Fully-opaque copy of a color. Profile colors persist in Config as R/G/B
// only, so anything applied to the profile must be normalized through this
// (both when applying and when comparing) to stay stable across restarts.
inline QColor opaque(const QColor& color)
{
    return QColor(color.red(), color.green(), color.blue());
}

// Darker shade used to derive the profile grid / deco-zone colors from a
// template color scheme: subtractive per-channel shift (default 75, clamped
// at 0), always opaque. E.g. #ffffff -> #b4b4b4, #ff13f5 -> #b400aa.
inline QColor darkened(const QColor& color, int shift = 75)
{
    return QColor(qMax(0, color.red() - shift),
                  qMax(0, color.green() - shift),
                  qMax(0, color.blue() - shift));
}

// Premultiplied ARGB32 -> plain ARGB32 with Qt's integer qUnpremultiply()
// per pixel, NOT QImage::convertToFormat(). Qt's dispatched conversion
// unpremultiplies with the hardware approximate reciprocal (RCPPS), whose
// result tables are CPU-vendor-specific — exact .5 ties round differently
// on Intel vs AMD, so the same build produces different bytes per machine.
// The golden-render byte contract requires this exact path for anything
// that ends up in a PNG.
inline QImage unpremultipliedArgb32(const QImage& pm)
{
    Q_ASSERT(pm.format() == QImage::Format_ARGB32_Premultiplied);
    QImage out(pm.size(), QImage::Format_ARGB32);
    out.setDevicePixelRatio(pm.devicePixelRatio());
    for (int y = 0; y < pm.height(); ++y) {
        const QRgb* src = reinterpret_cast<const QRgb*>(pm.constScanLine(y));
        QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < pm.width(); ++x)
            dst[x] = qUnpremultiply(src[x]);
    }
    return out;
}

} // namespace ColorUtils
} // namespace Unabara

#endif // COLOR_UTILS_H
