// Tests for CellData: template (.utp) JSON serialization round-trips and the
// CellType string mapping every template file depends on.

#include <QtTest>

#include <QFont>
#include <QJsonObject>

#include "include/core/cell_data.h"

using Unabara::CellData;
using Unabara::CellType;
using Unabara::ShadowType;
using Unabara::HAlign;
using Unabara::VAlign;

class CellDataTest : public QObject
{
    Q_OBJECT

private slots:
    void cellTypeStringRoundTrip()
    {
        const QList<CellType> all = {
            CellType::Depth,    CellType::Temperature, CellType::Time,
            CellType::NDL,      CellType::TTS,         CellType::Pressure,
            CellType::PO2Cell1, CellType::PO2Cell2,    CellType::PO2Cell3,
            CellType::CompositePO2, CellType::CNS,     CellType::MeanDepth,
            CellType::MaxDepth, CellType::Gas,         CellType::StopDepth,
            CellType::StopTime,
        };
        for (CellType type : all) {
            const QString str = CellData::cellTypeToString(type);
            QVERIFY2(!str.isEmpty(), "empty type string");
            QCOMPARE(CellData::cellTypeFromString(str), type);
        }
        // Unknown strings (e.g. from a newer template) degrade gracefully
        QCOMPARE(CellData::cellTypeFromString(QStringLiteral("FutureCell")),
                 CellType::Unknown);
    }

    void shadowTypeStringRoundTrip()
    {
        const QList<ShadowType> all = {
            ShadowType::Offset, ShadowType::Blurred, ShadowType::Outline,
        };
        for (ShadowType type : all) {
            const QString str = CellData::shadowTypeToString(type);
            QVERIFY2(!str.isEmpty(), "empty shadow type string");
            QCOMPARE(CellData::shadowTypeFromString(str), type);
        }
        // Unknown strings (e.g. from a newer template) degrade gracefully
        QCOMPARE(CellData::shadowTypeFromString(QStringLiteral("FutureShadow")),
                 ShadowType::Offset);
    }

    void jsonRoundTripPreservesCell()
    {
        CellData cell(QStringLiteral("tank_1"), CellType::Pressure);
        cell.setPosition(QPointF(0.25, 0.75));
        cell.setVisible(false);
        cell.setTankIndex(1);
        cell.setShowLabel(false);
        QFont font(QStringLiteral("Monospace"), 18);
        font.setBold(true);
        cell.setFont(font, true);
        // Deliberately different colors to catch a label/value swap
        cell.setLabelColor(QColor(255, 128, 0), true);
        cell.setValueColor(QColor(0, 200, 64), true);
        cell.setShadowEnabled(true);
        cell.setShadowType(ShadowType::Outline);
        cell.setShadowColor(QColor(0, 0, 255, 200));
        cell.setShadowSize(5);
        cell.setShadowOpacity(0.4);

        const CellData restored = CellData::fromJson(cell.toJson());
        QCOMPARE(restored.cellId(), QStringLiteral("tank_1"));
        QCOMPARE(restored.cellType(), CellType::Pressure);
        QCOMPARE(restored.position(), QPointF(0.25, 0.75));
        QCOMPARE(restored.visible(), false);
        QCOMPARE(restored.tankIndex(), 1);
        QCOMPARE(restored.showLabel(), false);
        QCOMPARE(restored.font().family(), QStringLiteral("Monospace"));
        QCOMPARE(restored.font().pointSize(), 18);
        QVERIFY(restored.font().bold());
        QCOMPARE(restored.labelColor(), QColor(255, 128, 0));
        QCOMPARE(restored.valueColor(), QColor(0, 200, 64));
        QVERIFY(restored.hasCustomLabelColor());
        QVERIFY(restored.hasCustomValueColor());
        QCOMPARE(restored.shadowEnabled(), true);
        QCOMPARE(restored.shadowType(), ShadowType::Outline);
        QCOMPARE(restored.shadowColor(), QColor(0, 0, 255, 200));
        QCOMPARE(restored.shadowSize(), 5);
        QCOMPARE(restored.shadowOpacity(), 0.4);
        QVERIFY(restored.hasCustomShadow());
    }

    void jsonDefaultsSurviveMinimalInput()
    {
        // A minimal cell (no custom font/color/shadow) must round-trip without
        // inventing custom styling
        CellData cell(QStringLiteral("depth"), CellType::Depth);
        const CellData restored = CellData::fromJson(cell.toJson());
        QCOMPARE(restored.cellId(), QStringLiteral("depth"));
        QCOMPARE(restored.cellType(), CellType::Depth);
        QCOMPARE(restored.visible(), true);
        QCOMPARE(restored.shadowEnabled(), false);
        QCOMPARE(restored.shadowType(), ShadowType::Offset);
        QCOMPARE(restored.shadowSize(), 2);
        QCOMPARE(restored.shadowOpacity(), 0.7);
        QVERIFY(!restored.hasCustomShadow());
        QCOMPARE(restored.labelColor(), QColor(Qt::white));
        QCOMPARE(restored.valueColor(), QColor(Qt::white));
        QVERIFY(!restored.hasCustomLabelColor());
        QVERIFY(!restored.hasCustomValueColor());
    }

    void legacyTextColorSeedsBothColors()
    {
        // Pre-split cell JSON only carries "textColor"; it must seed both the
        // label and the value color
        QJsonObject json;
        json[QStringLiteral("cellId")] = QStringLiteral("depth");
        json[QStringLiteral("cellType")] = QStringLiteral("Depth");
        json[QStringLiteral("textColor")] = QStringLiteral("#ffff8000");
        json[QStringLiteral("hasCustomColor")] = true;

        const CellData restored = CellData::fromJson(json);
        QCOMPARE(restored.labelColor(), QColor(QStringLiteral("#ffff8000")));
        QCOMPARE(restored.valueColor(), QColor(QStringLiteral("#ffff8000")));
        QVERIFY(restored.hasCustomLabelColor());
        QVERIFY(restored.hasCustomValueColor());

        // Legacy cell without any color keys stays at inherited defaults
        QJsonObject plain;
        plain[QStringLiteral("cellId")] = QStringLiteral("time");
        plain[QStringLiteral("cellType")] = QStringLiteral("Time");
        const CellData inherited = CellData::fromJson(plain);
        QCOMPARE(inherited.labelColor(), QColor(Qt::white));
        QCOMPARE(inherited.valueColor(), QColor(Qt::white));
        QVERIFY(!inherited.hasCustomLabelColor());
        QVERIFY(!inherited.hasCustomValueColor());
    }

    void alignStringRoundTrip()
    {
        const QList<HAlign> hAll = {HAlign::Left, HAlign::Center, HAlign::Right};
        for (HAlign a : hAll) {
            QCOMPARE(CellData::hAlignFromString(CellData::hAlignToString(a)), a);
        }
        const QList<VAlign> vAll = {VAlign::Top, VAlign::Middle, VAlign::Bottom};
        for (VAlign a : vAll) {
            QCOMPARE(CellData::vAlignFromString(CellData::vAlignToString(a)), a);
        }
        // The wire strings are a file-format contract: build_templates.py
        // writes them by hand for all 16 bundled templates, so a rename here
        // would silently drop every bundled template to top-left anchoring
        // while the round-trip above still passed.
        QCOMPARE(CellData::hAlignToString(HAlign::Left), QStringLiteral("left"));
        QCOMPARE(CellData::hAlignToString(HAlign::Center), QStringLiteral("center"));
        QCOMPARE(CellData::hAlignToString(HAlign::Right), QStringLiteral("right"));
        QCOMPARE(CellData::vAlignToString(VAlign::Top), QStringLiteral("top"));
        QCOMPARE(CellData::vAlignToString(VAlign::Middle), QStringLiteral("middle"));
        QCOMPARE(CellData::vAlignToString(VAlign::Bottom), QStringLiteral("bottom"));

        // Unknown or absent strings degrade to the legacy anchor
        QCOMPARE(CellData::hAlignFromString(QStringLiteral("diagonal")), HAlign::Left);
        QCOMPARE(CellData::hAlignFromString(QString()), HAlign::Left);
        QCOMPARE(CellData::vAlignFromString(QString()), VAlign::Top);
    }

    void geometryRoundTripPreservesAlignmentAndSize()
    {
        CellData cell(QStringLiteral("depth"), CellType::Depth);
        cell.setHAlign(HAlign::Right);
        cell.setVAlign(VAlign::Bottom);
        cell.setFixedSize(QSizeF(0.25, 0.1));

        const CellData restored = CellData::fromJson(cell.toJson());
        QCOMPARE(restored.hAlign(), HAlign::Right);
        QCOMPARE(restored.vAlign(), VAlign::Bottom);
        QVERIFY(restored.hasFixedSize());
        QCOMPARE(restored.fixedSize(), QSizeF(0.25, 0.1));
    }

    void geometryDefaultsSurviveMissingKeys()
    {
        // Pre-1.2 cell JSON has no geometry keys: it must load with the legacy
        // top-left anchor and auto-size, and clearing a fixed size must make
        // the "size" key disappear again on save
        QJsonObject json;
        json[QStringLiteral("cellId")] = QStringLiteral("depth");
        json[QStringLiteral("cellType")] = QStringLiteral("Depth");

        const CellData restored = CellData::fromJson(json);
        QCOMPARE(restored.hAlign(), HAlign::Left);
        QCOMPARE(restored.vAlign(), VAlign::Top);
        QVERIFY(!restored.hasFixedSize());
        QVERIFY(!restored.toJson().contains(QStringLiteral("size")));

        CellData cell(QStringLiteral("time"), CellType::Time);
        cell.setFixedSize(QSizeF(0.2, 0.2));
        cell.clearFixedSize();
        QVERIFY(!cell.hasFixedSize());
        QVERIFY(!cell.toJson().contains(QStringLiteral("size")));

        // A degenerate stored size (zero/negative) is treated as auto-size
        QJsonObject degenerate = json;
        QJsonObject sizeJson;
        sizeJson[QStringLiteral("width")] = 0.0;
        sizeJson[QStringLiteral("height")] = 0.3;
        degenerate[QStringLiteral("size")] = sizeJson;
        QVERIFY(!CellData::fromJson(degenerate).hasFixedSize());
        sizeJson[QStringLiteral("width")] = 0.2;
        sizeJson[QStringLiteral("height")] = -0.1;
        degenerate[QStringLiteral("size")] = sizeJson;
        QVERIFY(!CellData::fromJson(degenerate).hasFixedSize());
    }

    void downgradeCompatFieldsWritten()
    {
        // New-format JSON keeps the legacy "textColor"/"hasCustomColor" keys
        // (mirroring the value color) so older app versions can read it
        CellData cell(QStringLiteral("depth"), CellType::Depth);
        cell.setValueColor(QColor(10, 20, 30), true);

        const QJsonObject json = cell.toJson();
        QCOMPARE(json[QStringLiteral("textColor")].toString(),
                 QColor(10, 20, 30).name(QColor::HexArgb));
        QCOMPARE(json[QStringLiteral("hasCustomColor")].toBool(), true);
        QVERIFY(!json.contains(QStringLiteral("labelColor")));
        QCOMPARE(json[QStringLiteral("hasCustomLabelColor")].toBool(true), false);
    }
};

QTEST_MAIN(CellDataTest)
#include "cell_data_test.moc"
