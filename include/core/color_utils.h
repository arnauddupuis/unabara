#ifndef COLOR_UTILS_H
#define COLOR_UTILS_H

#include <QColor>

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

} // namespace ColorUtils
} // namespace Unabara

#endif // COLOR_UTILS_H
