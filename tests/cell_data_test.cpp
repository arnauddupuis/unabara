// Tests for CellData: template (.utp) JSON serialization round-trips and the
// CellType string mapping every template file depends on.

#include <QtTest>

#include <QFont>
#include <QJsonObject>

#include "include/core/cell_data.h"

using Unabara::CellData;
using Unabara::CellType;
using Unabara::ShadowType;

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
            CellType::CompositePO2, CellType::CNS,
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
        cell.setTextColor(QColor(255, 128, 0), true);
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
        QCOMPARE(restored.textColor(), QColor(255, 128, 0));
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
    }
};

QTEST_MAIN(CellDataTest)
#include "cell_data_test.moc"
