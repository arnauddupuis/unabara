// Tests for CellData: template (.utp) JSON serialization round-trips and the
// CellType string mapping every template file depends on.

#include <QtTest>

#include <QFont>
#include <QJsonObject>

#include "include/core/cell_data.h"

using Unabara::CellData;
using Unabara::CellType;

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
    }

    void jsonDefaultsSurviveMinimalInput()
    {
        // A minimal cell (no custom font/color) must round-trip without
        // inventing custom styling
        CellData cell(QStringLiteral("depth"), CellType::Depth);
        const CellData restored = CellData::fromJson(cell.toJson());
        QCOMPARE(restored.cellId(), QStringLiteral("depth"));
        QCOMPARE(restored.cellType(), CellType::Depth);
        QCOMPARE(restored.visible(), true);
    }
};

QTEST_MAIN(CellDataTest)
#include "cell_data_test.moc"
