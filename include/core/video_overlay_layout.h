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

    QVariantMap toVariantMap() const {
        QVariantMap m;
        m.insert(QStringLiteral("visible"), visible);
        m.insert(QStringLiteral("x"), normalizedRect.x());
        m.insert(QStringLiteral("y"), normalizedRect.y());
        m.insert(QStringLiteral("w"), normalizedRect.width());
        m.insert(QStringLiteral("h"), normalizedRect.height());
        return m;
    }

    static OverlayPlacement fromVariantMap(const QVariantMap& m) {
        OverlayPlacement p;
        p.visible = m.value(QStringLiteral("visible"), true).toBool();
        p.normalizedRect = QRectF(
            m.value(QStringLiteral("x"), 0.0).toDouble(),
            m.value(QStringLiteral("y"), 0.0).toDouble(),
            m.value(QStringLiteral("w"), 0.3).toDouble(),
            m.value(QStringLiteral("h"), 0.3).toDouble());
        return p;
    }
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
};

Q_DECLARE_METATYPE(OverlayPlacement)
Q_DECLARE_METATYPE(VideoOverlayLayout)

#endif // VIDEO_OVERLAY_LAYOUT_H
