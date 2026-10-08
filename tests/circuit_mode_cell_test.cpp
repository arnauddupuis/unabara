// Tests for the Circuit Mode overlay cell (CC/BO/OC): display text in the C++
// renderer (the export ground truth CellModel must match), default-cell
// creation via setCellTypeVisible, and the template round-trip / show-flag
// sweep on load.
//
// The generator is constructed without the app's resource bundle, so no
// template image loads and it falls back to its 640x120 default canvas.

#include <QtTest>

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "include/core/dive_data.h"
#include "include/core/overlay_template.h"
#include "include/generators/overlay_gen.h"

using Unabara::CellData;
using Unabara::CellType;
using Unabara::OverlayTemplate;

namespace {

OverlayTemplate makeTemplate(const QVector<CellData> &cells)
{
    OverlayTemplate templ;
    templ.setTemplateName(QStringLiteral("CircuitMode"));
    templ.setBackgroundImagePath(QStringLiteral(":/does-not-exist.png"));
    templ.setDefaultFont(QFont(QStringLiteral("DejaVu Sans"), 12));
    for (const CellData &cell : cells)
        templ.addCell(cell);
    return templ;
}

CellData cellOf(const QString &id, CellType type, double x = 0.1, double y = 0.1)
{
    CellData cell(id, type);
    cell.setPosition(QPointF(x, y));
    cell.setVisible(true);
    return cell;
}

} // namespace

class CircuitModeCellTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_settingsDir;
    DiveData *m_ccrDive = nullptr;
    DiveData *m_ocDive = nullptr;

private slots:
    void initTestCase()
    {
        // OverlayGenerator reads Config (QSettings) in its constructor; keep
        // the test away from the developer's real settings file (see
        // overlay_geometry_test.cpp for why this only works on Linux).
#if !defined(Q_OS_LINUX)
        QSKIP("Config's NativeFormat QSettings (registry/plist) cannot be redirected off Linux");
#endif
        QVERIFY(m_settingsDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());

        // CCR dive: diluent (0), bailout OC gas (1). On the loop from the
        // start, bailed out at t=600, back on the loop at t=900.
        m_ccrDive = new DiveData(this);
        m_ccrDive->setDiveMode(DiveData::ClosedCircuit);
        CylinderInfo diluent;
        diluent.use = CylinderInfo::Diluent;
        m_ccrDive->addCylinder(diluent);
        m_ccrDive->addCylinder(CylinderInfo()); // OcGas = bailout
        m_ccrDive->addGasSwitch(600.0, 1);
        m_ccrDive->addGasSwitch(900.0, 0);
        for (int t = 0; t <= 1200; t += 10)
            m_ccrDive->addDataPoint(DiveDataPoint(t, 20.0, 22.0, 40.0));

        // Plain OC dive
        m_ocDive = new DiveData(this);
        m_ocDive->setDiveMode(DiveData::OpenCircuit);
        m_ocDive->addCylinder(CylinderInfo());
        for (int t = 0; t <= 1200; t += 10)
            m_ocDive->addDataPoint(DiveDataPoint(t, 20.0, 22.0, 40.0));
    }

    void displayTextShowsCcBoOc()
    {
        OverlayGenerator gen;

        auto text = [&gen](DiveData *dive, double t, bool showLabel = true) {
            return gen.generateCellDisplayText(CellType::CircuitMode,
                                               dive->dataAtTime(t), -1, dive,
                                               showLabel);
        };

        // CCR: CC on the loop, BO after the bailout switch (held semantics,
        // flips exactly at the switch timestamp), CC again after returning
        QCOMPARE(text(m_ccrDive, 300.0), QStringLiteral("CIRCUIT MODE\nCC"));
        QCOMPARE(text(m_ccrDive, 599.0), QStringLiteral("CIRCUIT MODE\nCC"));
        QCOMPARE(text(m_ccrDive, 600.0), QStringLiteral("CIRCUIT MODE\nBO"));
        QCOMPARE(text(m_ccrDive, 750.0), QStringLiteral("CIRCUIT MODE\nBO"));
        QCOMPARE(text(m_ccrDive, 900.0), QStringLiteral("CIRCUIT MODE\nCC"));

        // OC dive: OC throughout — always a value, never a blank line
        QCOMPARE(text(m_ocDive, 0.0), QStringLiteral("CIRCUIT MODE\nOC"));
        QCOMPARE(text(m_ocDive, 1000.0), QStringLiteral("CIRCUIT MODE\nOC"));

        // Label-less rendering keeps just the value
        QCOMPARE(text(m_ccrDive, 750.0, false), QStringLiteral("BO"));
    }

    void toggleCreatesDefaultCellAtCenter()
    {
        OverlayGenerator gen;
        gen.loadTemplate(makeTemplate({cellOf(QStringLiteral("depth"), CellType::Depth)}));
        QVERIFY(!gen.showCircuitMode());
        QVERIFY(gen.getCellData(QStringLiteral("circuit_mode")) == nullptr);

        // The canvas answers showCircuitModeChanged with this call: a missing
        // cell is created at the default (0.5, 0.5), like its siblings
        gen.setCellTypeVisible(QStringLiteral("circuit_mode"), true);
        const CellData *cell = gen.getCellData(QStringLiteral("circuit_mode"));
        QVERIFY(cell != nullptr);
        QCOMPARE(cell->cellType(), CellType::CircuitMode);
        QCOMPARE(cell->position(), QPointF(0.5, 0.5));
        QVERIFY(cell->visible());

        // Toggling off hides the existing cell instead of deleting it
        gen.setCellTypeVisible(QStringLiteral("circuit_mode"), false);
        cell = gen.getCellData(QStringLiteral("circuit_mode"));
        QVERIFY(cell != nullptr);
        QVERIFY(!cell->visible());
    }

    void templateRoundTripAndShowFlagSweep()
    {
        OverlayGenerator gen;
        QSignalSpy spy(&gen, &OverlayGenerator::showCircuitModeChanged);

        // Loading a template WITH a circuit_mode cell raises the flag and
        // emits its change signal (the checkbox sync sweep)
        gen.loadTemplate(makeTemplate({
            cellOf(QStringLiteral("depth"), CellType::Depth),
            cellOf(QStringLiteral("circuit_mode"), CellType::CircuitMode, 0.4, 0.6),
        }));
        QVERIFY(gen.showCircuitMode());
        QVERIFY(spy.count() >= 1);

        // .utp save/load round-trip preserves the cell (generic cell
        // serialization — id + "CircuitMode" type string)
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/circuit.utp");
        QVERIFY(gen.exportTemplate().saveToFile(path));

        QString error;
        const OverlayTemplate loaded = OverlayTemplate::loadFromFile(path, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        bool found = false;
        for (const CellData &cell : loaded.cells()) {
            if (cell.cellId() == QStringLiteral("circuit_mode")) {
                found = true;
                QCOMPARE(cell.cellType(), CellType::CircuitMode);
                QCOMPARE(cell.position(), QPointF(0.4, 0.6));
            }
        }
        QVERIFY2(found, "circuit_mode cell lost in .utp round-trip");

        // Loading a template WITHOUT the cell resets the flag (the "reset
        // all 17 m_show* flags before scanning" sweep)
        gen.loadTemplate(makeTemplate({cellOf(QStringLiteral("depth"), CellType::Depth)}));
        QVERIFY(!gen.showCircuitMode());
    }
};

QTEST_MAIN(CircuitModeCellTest)
#include "circuit_mode_cell_test.moc"
