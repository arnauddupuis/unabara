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
