#ifndef LUT_STRIP_IMAGE_PROVIDER_H
#define LUT_STRIP_IMAGE_PROVIDER_H

#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

// image://lutstrip/<revision> — serves the active colour grade's user LUT
// as a Hald-style 2D strip for the colorgrade.frag sampler (binding 2).
//
// Strip layout (must match sampleLutStrip() in
// resources/shaders/colorgrade.frag): N slices of N×N laid side by side —
// the image is N²×N (1089×33 for N=33) and pixel (b·N + r, g) holds the
// LUT output for input (r, g, b): red fastest horizontally inside a slice,
// green down, blue = slice index.
//
// C2 ships the identity default so the shader path is complete; C4's LUT
// import replaces the strip via setStrip() and bumps the controller's
// lutRevision so the QML Image re-requests (the <revision> id exists only
// to bust Qt's per-URL image cache — requestImage() ignores it).
//
// THREAD RULE: requestImage() runs on the QML image-provider thread. The
// provider owns an immutable copy of the strip, swapped in from the GUI
// thread under a mutex and only ever read under the same mutex
// (ProfileGenerator's mutex discipline) — it never reaches into live
// objects.
class LutStripImageProvider : public QQuickImageProvider
{
public:
    // Matches the .cube default edge size; ColorGradeController::lutSize
    // reports the strip's N to the shader.
    static constexpr int kDefaultSize = 33;

    LutStripImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    // GUI thread only: swap in a new strip (C4's LUT import), or drop back
    // to the identity strip. An invalid image resets to identity.
    void setStrip(const QImage& strip);
    void resetToIdentity();

    // Identity Hald strip of edge size n (strip is n²×n). 8-bit channels:
    // the ≤1-LSB quantisation this costs under an active LUT is part of
    // the documented preview-vs-export residual.
    static QImage identityStrip(int n = kDefaultSize);

private:
    mutable QMutex m_mutex;
    QImage m_strip;
};

#endif // LUT_STRIP_IMAGE_PROVIDER_H
