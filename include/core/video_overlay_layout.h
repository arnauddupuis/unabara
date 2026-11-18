#ifndef VIDEO_OVERLAY_LAYOUT_H
#define VIDEO_OVERLAY_LAYOUT_H

#include <QMetaType>
#include <QRectF>
#include <QVariantMap>

// Placement of one overlay on top of the video frame. Coordinates are
// normalized to [0..1] of the video frame so the same layout survives
// resolution changes and is reusable for both the live preview and any
// future burn-in compositing pass.
struct OverlayPlacement {
    bool visible = true;
    QRectF normalizedRect; // x, y, w, h ∈ [0..1]

    // Smallest usable overlay rect, as a fraction of the frame. KEEP IN
    // SYNC with VideoOverlayItem.qml's minNormSize (the live-drag clamp —
    // same semantics, applied during the gesture before anything commits).
    static constexpr double kMinNormSize = 0.05;

    QVariantMap toVariantMap() const {
        QVariantMap m;
        m.insert(QStringLiteral("visible"), visible);
        m.insert(QStringLiteral("x"), normalizedRect.x());
        m.insert(QStringLiteral("y"), normalizedRect.y());
        m.insert(QStringLiteral("w"), normalizedRect.width());
        m.insert(QStringLiteral("h"), normalizedRect.height());
        return m;
    }

    // Parse-time clamp: w/h into [kMinNormSize, 1] first, then x/y into
    // what remains. THE single enforcement of rect sanity — every commit
    // and restore path parses through here (commitOverlayLayout, Config's
    // blob load, VideoClip::fromVariantMap), so a stale or hand-edited
    // stored rect is healed at the boundary instead of being re-clamped in
    // each UI site.
    static OverlayPlacement fromVariantMap(const QVariantMap& m) {
        OverlayPlacement p;
        p.visible = m.value(QStringLiteral("visible"), true).toBool();
        const double w = qBound(kMinNormSize,
                                m.value(QStringLiteral("w"), 0.3).toDouble(), 1.0);
        const double h = qBound(kMinNormSize,
                                m.value(QStringLiteral("h"), 0.3).toDouble(), 1.0);
        const double x = qBound(0.0, m.value(QStringLiteral("x"), 0.0).toDouble(),
                                1.0 - w);
        const double y = qBound(0.0, m.value(QStringLiteral("y"), 0.0).toDouble(),
                                1.0 - h);
        p.normalizedRect = QRectF(x, y, w, h);
        return p;
    }

    bool operator==(const OverlayPlacement& o) const
    {
        return visible == o.visible && normalizedRect == o.normalizedRect;
    }
    bool operator!=(const OverlayPlacement& o) const { return !(*this == o); }
};

struct VideoOverlayLayout {
    OverlayPlacement diveComputer{ true, QRectF(0.02, 0.55, 0.30, 0.40) };
    OverlayPlacement diveProfile { true, QRectF(0.30, 0.78, 0.68, 0.20) };

    QVariantMap toVariantMap() const {
        QVariantMap m;
        m.insert(QStringLiteral("diveComputer"), diveComputer.toVariantMap());
        m.insert(QStringLiteral("diveProfile"),  diveProfile.toVariantMap());
        return m;
    }

    static VideoOverlayLayout fromVariantMap(const QVariantMap& m) {
        VideoOverlayLayout l;
        if (m.contains(QStringLiteral("diveComputer")))
            l.diveComputer = OverlayPlacement::fromVariantMap(
                m.value(QStringLiteral("diveComputer")).toMap());
        if (m.contains(QStringLiteral("diveProfile")))
            l.diveProfile = OverlayPlacement::fromVariantMap(
                m.value(QStringLiteral("diveProfile")).toMap());
        return l;
    }

    bool operator==(const VideoOverlayLayout& o) const
    {
        return diveComputer == o.diveComputer && diveProfile == o.diveProfile;
    }
    bool operator!=(const VideoOverlayLayout& o) const { return !(*this == o); }
};

Q_DECLARE_METATYPE(OverlayPlacement)
Q_DECLARE_METATYPE(VideoOverlayLayout)

#endif // VIDEO_OVERLAY_LAYOUT_H
