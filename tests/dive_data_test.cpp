// Tests for the DiveData model: interpolation semantics, gas switch
// resolution, and derived values. These encode the display contracts the
// overlay generator relies on.

#include <QtTest>

#include "include/core/dive_data.h"

namespace {

DiveDataPoint point(double t, double depth)
{
    DiveDataPoint p;
    p.timestamp = t;
    p.depth = depth;
    return p;
}

} // namespace

class DiveDataTest : public QObject
{
    Q_OBJECT

private slots:
    void interpolatesBetweenPoints()
    {
        DiveData d;
        d.addDataPoint(point(0.0, 10.0));
        d.addDataPoint(point(10.0, 20.0));

        QCOMPARE(d.dataAtTime(5.0).depth, 15.0);
        // Clamped outside the recorded range
        QCOMPARE(d.dataAtTime(-1.0).depth, 10.0);
        QCOMPARE(d.dataAtTime(99.0).depth, 20.0);
        QCOMPARE(d.durationSeconds(), 10);
    }

    void addDataPointKeepsOrder()
    {
        DiveData d;
        d.addDataPoint(point(5.0, 2.0));
        d.addDataPoint(point(0.0, 1.0));
        d.addDataPoint(point(10.0, 3.0));

        const auto &pts = d.allDataPoints();
        QCOMPARE(pts.size(), 3);
        QCOMPARE(pts[0].timestamp, 0.0);
        QCOMPARE(pts[1].timestamp, 5.0);
        QCOMPARE(pts[2].timestamp, 10.0);
    }

    void ceilingIsNotInterpolated()
    {
        // Ceiling is a state that persists until changed — blending two stop
        // depths would display a stop that never existed
        DiveData d;
        DiveDataPoint a = point(0.0, 30.0);
        a.ceiling = 6.0;
        DiveDataPoint b = point(10.0, 28.0);
        b.ceiling = 3.0;
        d.addDataPoint(a);
        d.addDataPoint(b);

        QCOMPARE(d.dataAtTime(5.0).ceiling, 6.0);
    }

    void ndlSentinelDoesNotBlend()
    {
        // -1 means "computer never reported NDL"; blending across the
        // sentinel would fabricate a deco state (ndl == 0) that never existed
        DiveData d;
        DiveDataPoint a = point(0.0, 10.0); // ndl defaults to -1
        DiveDataPoint b = point(10.0, 10.0);
        b.ndl = 40.0;
        d.addDataPoint(a);
        d.addDataPoint(b);
        QCOMPARE(d.dataAtTime(5.0).ndl, -1.0);

        DiveData e;
        DiveDataPoint c = point(0.0, 10.0);
        c.ndl = 40.0;
        DiveDataPoint f = point(10.0, 10.0);
        f.ndl = 20.0;
        e.addDataPoint(c);
        e.addDataPoint(f);
        QCOMPARE(e.dataAtTime(5.0).ndl, 30.0);
    }

    void stopTimeIsNotInterpolated()
    {
        // Stop time is a held state like ceiling — blending two required stop
        // times would display a countdown the computer never showed
        DiveData d;
        DiveDataPoint a = point(0.0, 30.0);
        a.stopTime = 3.0;
        DiveDataPoint b = point(10.0, 28.0);
        b.stopTime = 1.0;
        d.addDataPoint(a);
        d.addDataPoint(b);

        QCOMPARE(d.dataAtTime(5.0).stopTime, 3.0);
    }

    void cnsSentinelDoesNotBlend()
    {
        // -1 means "no data"; interpolating across it would fabricate values
        DiveData d;
        DiveDataPoint a = point(0.0, 10.0); // cns defaults to -1
        DiveDataPoint b = point(10.0, 10.0);
        b.cns = 10.0;
        d.addDataPoint(a);
        d.addDataPoint(b);
        QCOMPARE(d.dataAtTime(5.0).cns, -1.0);

        DiveData e;
        DiveDataPoint c = point(0.0, 10.0);
        c.cns = 10.0;
        DiveDataPoint f = point(10.0, 10.0);
        f.cns = 20.0;
        e.addDataPoint(c);
        e.addDataPoint(f);
        QCOMPARE(e.dataAtTime(5.0).cns, 15.0);
    }

    void pressurePrefersRecordedSamples()
    {
        // With real samples on the channel, the cylinder start/end ramp must
        // not override them
        DiveData d;
        CylinderInfo cyl;
        cyl.startPressure = 200.0;
        cyl.endPressure = 100.0; // ramp midpoint would be 150
        d.addCylinder(cyl);

        DiveDataPoint a = point(0.0, 10.0);
        a.addPressure(200.0, 0);
        DiveDataPoint b = point(10.0, 10.0);
        b.addPressure(190.0, 0);
        d.addDataPoint(a);
        d.addDataPoint(b);

        QCOMPARE(d.dataAtTime(5.0).getPressure(0), 195.0);
    }

    void pressureFallsBackToCylinderRamp()
    {
        // No per-sample pressures: the start/end linear ramp is the fallback
        DiveData d;
        CylinderInfo cyl;
        cyl.startPressure = 200.0;
        cyl.endPressure = 100.0;
        d.addCylinder(cyl);

        d.addDataPoint(point(0.0, 10.0));
        d.addDataPoint(point(10.0, 10.0));

        QCOMPARE(d.dataAtTime(5.0).getPressure(0), 150.0);
    }

    void gasSwitchesResolveDeterministically()
    {
        DiveData d;
        d.addCylinder(CylinderInfo());
        d.addCylinder(CylinderInfo());
        d.addDataPoint(point(0.0, 10.0));
        d.addDataPoint(point(600.0, 10.0));

        // Two switches clamped to the same instant: the last added wins
        d.addGasSwitch(0.0, 0);
        d.addGasSwitch(0.0, 1);
        QCOMPARE(d.activeCylinderAtTime(0.0), 1);

        d.addGasSwitch(300.0, 0);
        QCOMPARE(d.activeCylinderAtTime(200.0), 1);
        QCOMPARE(d.activeCylinderAtTime(400.0), 0);
    }

    void gasSwitchRejectsInvalidCylinder()
    {
        DiveData d;
        d.addCylinder(CylinderInfo());
        d.addGasSwitch(0.0, 5); // out of range: ignored
        QCOMPARE(d.activeCylinderAtTime(10.0), 0);
    }

    void maxDepthUntilTracksRunningMaximum()
    {
        DiveData d;
        d.addDataPoint(point(0.0, 10.0));
        d.addDataPoint(point(10.0, 20.0));
        d.addDataPoint(point(20.0, 15.0));

        QCOMPARE(d.maxDepth(), 20.0);
        // Includes the interpolated depth at the query time (15 m at t=5)
        QCOMPARE(d.maxDepthUntil(5.0), 15.0);
        QCOMPARE(d.maxDepthUntil(10.0), 20.0);
        QCOMPARE(d.maxDepthUntil(20.0), 20.0);
    }

    void circuitModeOpenCircuitDiveIsAlwaysOc()
    {
        // Rule 1: anything but a CCR dive is OC for the whole dive, even
        // with role-marked cylinders and switches between them
        DiveData d;
        CylinderInfo diluent;
        diluent.use = CylinderInfo::Diluent;
        d.addCylinder(diluent);
        d.addCylinder(CylinderInfo()); // OcGas
        d.addGasSwitch(100.0, 1);

        d.setDiveMode(DiveData::OpenCircuit);
        QCOMPARE(d.circuitModeAtTime(0.0), DiveData::OnOpenCircuit);
        QCOMPARE(d.circuitModeAtTime(200.0), DiveData::OnOpenCircuit);

        // UnknownMode is not a CCR dive either
        DiveData u;
        u.addCylinder(diluent);
        QCOMPARE(u.circuitModeAtTime(0.0), DiveData::OnOpenCircuit);
    }

    void circuitModeFollowsCylinderRoles()
    {
        // CCR dive: oxygen (0), diluent (1), bailout (2, no role).
        // CC -> BO at the bailout switch -> back to CC on return to diluent.
        DiveData d;
        d.setDiveMode(DiveData::ClosedCircuit);
        CylinderInfo oxygen;
        oxygen.use = CylinderInfo::Oxygen;
        CylinderInfo diluent;
        diluent.use = CylinderInfo::Diluent;
        CylinderInfo bailout; // OcGas
        d.addCylinder(oxygen);
        d.addCylinder(diluent);
        d.addCylinder(bailout);

        d.addGasSwitch(10.0, 1);    // diluent
        d.addGasSwitch(600.0, 2);   // bailout!
        d.addGasSwitch(900.0, 1);   // back on the loop

        // Before any switch the default cylinder 0 (oxygen) is active
        QCOMPARE(d.circuitModeAtTime(0.0), DiveData::OnLoop);
        QCOMPARE(d.circuitModeAtTime(300.0), DiveData::OnLoop);
        // Held semantics: the state flips exactly at the switch timestamp
        QCOMPARE(d.circuitModeAtTime(599.0), DiveData::OnLoop);
        QCOMPARE(d.circuitModeAtTime(600.0), DiveData::BailedOut);
        QCOMPARE(d.circuitModeAtTime(750.0), DiveData::BailedOut);
        QCOMPARE(d.circuitModeAtTime(899.0), DiveData::BailedOut);
        QCOMPARE(d.circuitModeAtTime(900.0), DiveData::OnLoop);
        QCOMPARE(d.circuitModeAtTime(9999.0), DiveData::OnLoop);
    }

    void circuitModeWithoutRoleInfoStaysOnLoop()
    {
        // CCR dive whose log carries no diluent/oxygen role: never fabricate
        // a bailout from missing data — on the loop for the whole dive
        DiveData d;
        d.setDiveMode(DiveData::ClosedCircuit);
        d.addCylinder(CylinderInfo()); // OcGas
        d.addCylinder(CylinderInfo()); // OcGas
        d.addGasSwitch(300.0, 1);

        QCOMPARE(d.circuitModeAtTime(0.0), DiveData::OnLoop);
        QCOMPARE(d.circuitModeAtTime(400.0), DiveData::OnLoop);
    }

    void circuitModeOverrideBeatsCylinderDerivation()
    {
        // Explicit modechange events win from their timestamp onward
        // (constructed directly — no fixture logs modechange yet)
        DiveData d;
        d.setDiveMode(DiveData::ClosedCircuit);
        CylinderInfo diluent;
        diluent.use = CylinderInfo::Diluent;
        d.addCylinder(diluent);

        // Still on the diluent cylinder, but the computer says OC (bailout)
        d.addCircuitModeOverride(300.0, DiveData::BailedOut);
        d.addCircuitModeOverride(500.0, DiveData::OnLoop);

        QCOMPARE(d.circuitModeAtTime(0.0), DiveData::OnLoop);       // pre-override: cylinder rule
        QCOMPARE(d.circuitModeAtTime(299.0), DiveData::OnLoop);
        QCOMPARE(d.circuitModeAtTime(300.0), DiveData::BailedOut);  // held from its timestamp
        QCOMPARE(d.circuitModeAtTime(499.0), DiveData::BailedOut);
        QCOMPARE(d.circuitModeAtTime(500.0), DiveData::OnLoop);

        // Overrides apply even without cylinder role info...
        DiveData n;
        n.setDiveMode(DiveData::ClosedCircuit);
        n.addCylinder(CylinderInfo());
        n.addCircuitModeOverride(100.0, DiveData::BailedOut);
        QCOMPARE(n.circuitModeAtTime(50.0), DiveData::OnLoop);
        QCOMPARE(n.circuitModeAtTime(100.0), DiveData::BailedOut);

        // ...but never on a non-CCR dive
        DiveData o;
        o.setDiveMode(DiveData::OpenCircuit);
        o.addCircuitModeOverride(100.0, DiveData::BailedOut);
        QCOMPARE(o.circuitModeAtTime(200.0), DiveData::OnOpenCircuit);
    }

    void meanDepthExplicitBeatsDerived()
    {
        DiveData d;
        d.addDataPoint(point(0.0, 0.0));
        d.addDataPoint(point(10.0, 10.0));

        // Unset: time-weighted average of the profile
        QCOMPARE(d.meanDepth(), 5.0);
        // A log-reported average (e.g. FIT DIVE_SUMMARY) takes precedence
        d.setMeanDepth(12.5);
        QCOMPARE(d.meanDepth(), 12.5);
    }
};

QTEST_GUILESS_MAIN(DiveDataTest)
#include "dive_data_test.moc"
