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
        // Deliberately different colors to catch a label/value swap
        templ.setDefaultLabelColor(QColor(255, 128, 0));
        templ.setDefaultValueColor(QColor(0, 128, 255));
        templ.setDefaultShadowEnabled(true);
        templ.setDefaultShadowType(ShadowType::Blurred);
        templ.setDefaultShadowColor(QColor(10, 20, 30, 200));
        templ.setDefaultShadowSize(7);
        templ.setDefaultShadowOpacity(0.4);
        templ.setDefaultPrimaryColor(QColor(255, 179, 0));
        templ.setDefaultSecondaryColor(QColor(220, 231, 238));
        templ.addCell(CellData(QStringLiteral("depth"), CellType::Depth));

        const OverlayTemplate restored = OverlayTemplate::fromJson(templ.toJson());
        QCOMPARE(restored.templateName(), QStringLiteral("Test Template"));
        QCOMPARE(restored.backgroundOpacity(), 0.5);
        QCOMPARE(restored.defaultFont().family(), QStringLiteral("Monospace"));
        QCOMPARE(restored.defaultLabelColor(), QColor(255, 128, 0));
        QCOMPARE(restored.defaultValueColor(), QColor(0, 128, 255));
        // Downgrade-compat field mirrors the value color for older versions
        QCOMPARE(templ.toJson()[QStringLiteral("defaultTextColor")].toString(),
                 QColor(0, 128, 255).name(QColor::HexArgb));
        QCOMPARE(restored.defaultShadowEnabled(), true);
        QCOMPARE(restored.defaultShadowType(), ShadowType::Blurred);
        QCOMPARE(restored.defaultShadowColor(), QColor(10, 20, 30, 200));
        QCOMPARE(restored.defaultShadowSize(), 7);
        QCOMPARE(restored.defaultShadowOpacity(), 0.4);
        QCOMPARE(restored.defaultPrimaryColor(), QColor(255, 179, 0));
        QCOMPARE(restored.defaultSecondaryColor(), QColor(220, 231, 238));
        QVERIFY(restored.hasDefaultPrimaryColor());
        QVERIFY(restored.hasDefaultSecondaryColor());
        QCOMPARE(restored.cellCount(), 1);
    }

    void missingSchemeKeysLeaveFlagsFalse()
    {
        // Templates without the v1.1 color-scheme keys must not report a
        // scheme — the apply-to-profile prompt keys off these flags
        QJsonObject json;
        json[QStringLiteral("version")] = QStringLiteral("1.0");
        json[QStringLiteral("templateName")] = QStringLiteral("Legacy");
        json[QStringLiteral("defaultLabelColor")] = QStringLiteral("#ffffb300");
        json[QStringLiteral("defaultValueColor")] = QStringLiteral("#ffffffff");

        const OverlayTemplate restored = OverlayTemplate::fromJson(json);
        QVERIFY(!restored.hasDefaultPrimaryColor());
        QVERIFY(!restored.hasDefaultSecondaryColor());
        QVERIFY(!restored.defaultPrimaryColor().isValid());
        QVERIFY(!restored.defaultSecondaryColor().isValid());
        // ...and the absence survives a save/load round-trip
        const OverlayTemplate again = OverlayTemplate::fromJson(restored.toJson());
        QVERIFY(!again.hasDefaultPrimaryColor());
        QVERIFY(!again.hasDefaultSecondaryColor());
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
        // No color keys at all → both default colors stay white
        QCOMPARE(restored.defaultLabelColor(), QColor(Qt::white));
        QCOMPARE(restored.defaultValueColor(), QColor(Qt::white));
    }

    void bundledHudTemplatesAreValid()
    {
        // Every template of the HUD/Social family shipped in resources.qrc
        // must load, pass validation, and survive a JSON round-trip. Loaded
        // from the source tree since tests don't compile the resource bundle.
        const QDir dir(QStringLiteral(TEMPLATES_DIR));
        const QStringList families = {QStringLiteral("HUD_*.utp"),
                                      QStringLiteral("Social_*.utp"),
                                      QStringLiteral("Broadcast_*.utp")};
        const QStringList files = dir.entryList(families, QDir::Files);
        QCOMPARE(files.size(), 16);

        for (const QString& file : files) {
            QString error;
            const OverlayTemplate templ =
                OverlayTemplate::loadFromFile(dir.absoluteFilePath(file), &error);
            QVERIFY2(error.isEmpty(), qPrintable(file + ": " + error));
            QVERIFY2(templ.validate().isEmpty(),
                     qPrintable(file + ": " + templ.validate().join("; ")));
            QVERIFY2(templ.backgroundImagePath().startsWith(
                         QStringLiteral(":/images/HUD/")),
                     qPrintable(file));
            QVERIFY(!templ.cells().isEmpty());
            // The whole family ships a profile color scheme (v1.1)
            QVERIFY2(templ.hasDefaultPrimaryColor() && templ.hasDefaultSecondaryColor(),
                     qPrintable(file + ": missing color scheme keys"));

            const OverlayTemplate restored = OverlayTemplate::fromJson(templ.toJson());
            QCOMPARE(restored.cells().size(), templ.cells().size());
            for (const CellData& cell : restored.cells()) {
                QVERIFY2(cell.position().x() >= 0.0 && cell.position().x() <= 1.0
                         && cell.position().y() >= 0.0 && cell.position().y() <= 1.0,
                         qPrintable(file + ": " + cell.cellId()));
            }
        }
    }

    void legacyDefaultTextColorSeedsBoth()
    {
        // .utp files predating the label/value split only carry
        // "defaultTextColor" — it must seed both new defaults
        QJsonObject json;
        json[QStringLiteral("version")] = QStringLiteral("1.0");
        json[QStringLiteral("templateName")] = QStringLiteral("Legacy");
        json[QStringLiteral("defaultTextColor")] = QStringLiteral("#ff112233");

        const OverlayTemplate restored = OverlayTemplate::fromJson(json);
        QCOMPARE(restored.defaultLabelColor(), QColor(QStringLiteral("#ff112233")));
        QCOMPARE(restored.defaultValueColor(), QColor(QStringLiteral("#ff112233")));
    }
};

QTEST_MAIN(OverlayTemplateTest)
#include "overlay_template_test.moc"
