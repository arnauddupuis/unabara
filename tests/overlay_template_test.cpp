// Tests for OverlayTemplate: the global default settings (font, color, shadow)
// must survive a toJson/fromJson round-trip — undo snapshots and .utp save/load
// both go through this path.

#include <QtTest>

#include <QFont>
#include <QJsonObject>

#include "include/core/overlay_template.h"

using Unabara::CellData;
using Unabara::CellType;
using Unabara::OverlayTemplate;
using Unabara::ShadowType;

class OverlayTemplateTest : public QObject
{
    Q_OBJECT

private slots:
    void jsonRoundTripPreservesDefaults()
    {
        OverlayTemplate templ;
        templ.setTemplateName(QStringLiteral("Test Template"));
        templ.setBackgroundImagePath(QStringLiteral(":/images/test.png"));
        templ.setBackgroundOpacity(0.5);
        templ.setDefaultFont(QFont(QStringLiteral("Monospace"), 18));
        templ.setDefaultTextColor(QColor(255, 128, 0));
        templ.setDefaultShadowEnabled(true);
        templ.setDefaultShadowType(ShadowType::Blurred);
        templ.setDefaultShadowColor(QColor(10, 20, 30, 200));
        templ.setDefaultShadowSize(7);
        templ.setDefaultShadowOpacity(0.4);
        templ.addCell(CellData(QStringLiteral("depth"), CellType::Depth));

        const OverlayTemplate restored = OverlayTemplate::fromJson(templ.toJson());
        QCOMPARE(restored.templateName(), QStringLiteral("Test Template"));
        QCOMPARE(restored.backgroundOpacity(), 0.5);
        QCOMPARE(restored.defaultFont().family(), QStringLiteral("Monospace"));
        QCOMPARE(restored.defaultTextColor(), QColor(255, 128, 0));
        QCOMPARE(restored.defaultShadowEnabled(), true);
        QCOMPARE(restored.defaultShadowType(), ShadowType::Blurred);
        QCOMPARE(restored.defaultShadowColor(), QColor(10, 20, 30, 200));
        QCOMPARE(restored.defaultShadowSize(), 7);
        QCOMPARE(restored.defaultShadowOpacity(), 0.4);
        QCOMPARE(restored.cellCount(), 1);
    }

    void shadowDefaultsSurviveMissingKeys()
    {
        // Old .utp files predate the shadow fields — loading them must yield
        // the shadow defaults (disabled, offset, size 2, opacity 0.7)
        QJsonObject json;
        json[QStringLiteral("version")] = QStringLiteral("1.0");
        json[QStringLiteral("templateName")] = QStringLiteral("Legacy");

        const OverlayTemplate restored = OverlayTemplate::fromJson(json);
        QCOMPARE(restored.defaultShadowEnabled(), false);
        QCOMPARE(restored.defaultShadowType(), ShadowType::Offset);
        QCOMPARE(restored.defaultShadowColor(), QColor(0, 0, 0));
        QCOMPARE(restored.defaultShadowSize(), 2);
        QCOMPARE(restored.defaultShadowOpacity(), 0.7);
    }
};

QTEST_MAIN(OverlayTemplateTest)
#include "overlay_template_test.moc"
