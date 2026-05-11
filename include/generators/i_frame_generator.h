#ifndef I_FRAME_GENERATOR_H
#define I_FRAME_GENERATOR_H

#include <QImage>

class DiveData;

/**
 * Minimal abstract interface for anything that can render a single overlay frame
 * for a dive at a given timestamp.
 *
 * Both OverlayGenerator (dive computer cells) and ProfileGenerator (depth curve
 * with animated indicator) implement this interface so the export pipeline
 * (ImageExporter / VideoExporter) can drive either renderer without caring which.
 */
class IFrameGenerator
{
public:
    virtual ~IFrameGenerator() = default;

    // Render the overlay for the given dive at the given dive-time (seconds).
    // Returns a transparent-background QImage sized for compositing.
    virtual QImage generate(DiveData* dive, double timePoint) = 0;

    // Optional hooks for generators that need to stage/restore state for
    // export passes (e.g. hide editor-only chrome). Default no-op.
    // Exporters MUST call beginExport() before the first generate() and
    // endExport() after the last, in a symmetric pair.
    virtual void beginExport() {}
    virtual void endExport() {}
};

#endif // I_FRAME_GENERATOR_H
