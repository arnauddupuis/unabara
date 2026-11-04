// Tests for the DiveData model: interpolation semantics, gas switch
// resolution, and derived values. These encode the display contracts the
// overlay generator relies on.

#include <QtTest>
#include <QRandomGenerator>

#include <cmath>

#include "include/core/dive_data.h"

namespace {

DiveDataPoint point(double t, double depth)
{
    DiveDataPoint p;
    p.timestamp = t;
    p.depth = depth;
    return p;
}

// ORACLE: the pre-refactor linear maxDepthUntil() verbatim (scan from 0,
// strict 'timestamp > time' break, interpolated boundary segment). The
// production version (upper_bound + eager prefix-max) must agree with it
// bit-for-bit.
double bruteForceMaxDepthUntil(const QVector<DiveDataPoint> &pts, double time)
{
    double max = 0.0;
    for (int i = 0; i < pts.size(); ++i) {
        const DiveDataPoint &point = pts[i];
        if (point.timestamp > time) {
            if (i > 0) {
                const DiveDataPoint &prev = pts[i - 1];
                double dt = point.timestamp - prev.timestamp;
                if (dt > 0.0 && time > prev.timestamp) {
                    double factor = (time - prev.timestamp) / dt;
                    double depth = prev.depth + factor * (point.depth - prev.depth);
                    if (depth > max) {
                        max = depth;
                    }
                }
            }
            break;
        }
        if (point.depth > max) {
            max = point.depth;
        }
    }
    return max;
}

// ORACLE: the pre-refactor linear segment search in dataAtTime() (first
// index with timestamp >= time), followed by the unchanged depth/held-field
// formulas. Valid only for first.timestamp < time < last.timestamp.
DiveDataPoint bruteForceSegmentPoint(const QVector<DiveDataPoint> &pts, double time)
{
    int index = 0;
    while (index < pts.size() && pts[index].timestamp < time) {
        index++;
    }
    const DiveDataPoint &prev = pts[index - 1];
    const DiveDataPoint &next = pts[index];
    double factor = (time - prev.timestamp) / (next.timestamp - prev.timestamp);
    DiveDataPoint r;
    r.timestamp = time;
    r.depth = prev.depth + factor * (next.depth - prev.depth);
    r.ceiling = prev.ceiling;
    r.stopTime = prev.stopTime;
    return r;
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

    // --- Locks for the binary-search refactors (lower_bound in dataAtTime,
    // upper_bound + prefix-max in maxDepthUntil). Exact '==' on purpose:
    // QCOMPARE on doubles is fuzzy and could not prove bit-identity.

    void dataAtTimeSegmentLookupIsExact()
    {
        DiveData d;
        DiveDataPoint a = point(0.0, 10.0);  a.ceiling = 1.0; a.stopTime = 1.0;
        DiveDataPoint b = point(10.0, 20.0); b.ceiling = 2.0; b.stopTime = 2.0;
        DiveDataPoint c = point(20.0, 30.0); c.ceiling = 3.0; c.stopTime = 3.0;
        d.addDataPoint(a);
        d.addDataPoint(b);
        d.addDataPoint(c);

        // Exact interior sample timestamp: the segment is (prev=k-1, next=k)
        // with factor 1 — depth equals sample k, but HELD fields come from
        // sample k-1 (first index with timestamp >= t, not > t).
        DiveDataPoint atB = d.dataAtTime(10.0);
        QVERIFY(atB.depth == 20.0);
        QVERIFY(atB.ceiling == 1.0);
        QVERIFY(atB.stopTime == 1.0);

        // Between samples
        DiveDataPoint mid = d.dataAtTime(15.0);
        QVERIFY(mid.depth == 25.0);
        QVERIFY(mid.ceiling == 2.0);

        // Before first / after last / exactly at the ends: clamped copies
        QVERIFY(d.dataAtTime(-5.0).depth == 10.0);
        QVERIFY(d.dataAtTime(-5.0).ceiling == 1.0);
        QVERIFY(d.dataAtTime(0.0).ceiling == 1.0);
        QVERIFY(d.dataAtTime(20.0).ceiling == 3.0);
        QVERIFY(d.dataAtTime(99.0).depth == 30.0);
        QVERIFY(d.dataAtTime(99.0).ceiling == 3.0);
    }

    void dataAtTimeDuplicateTimestamps()
    {
        DiveData d;
        d.addDataPoint(point(0.0, 10.0));
        d.addDataPoint(point(10.0, 20.0));
        d.addDataPoint(point(20.0, 30.0));
        // addDataPoint's lower_bound insert puts a later-added duplicate
        // BEFORE the existing one: order is (0)(10,50)(10,20)(20)
        d.addDataPoint(point(10.0, 50.0));
        QCOMPARE(d.allDataPoints().at(1).depth, 50.0);
        QCOMPARE(d.allDataPoints().at(2).depth, 20.0);

        // At the tie, the FIRST of the duplicates is 'next'
        QVERIFY(d.dataAtTime(10.0).depth == 50.0);
        // After the tie, the LAST of the duplicates is 'prev'
        QVERIFY(d.dataAtTime(15.0).depth == 25.0);
        // Before the tie, interpolate toward the first duplicate
        QVERIFY(d.dataAtTime(5.0).depth == 30.0);
    }

    void dataAtTimeMatchesLinearOracle()
    {
        QRandomGenerator rng(0xD1FEu);
        DiveData d;
        for (int k = 0; k < 400; ++k) {
            // Integer timestamps in random insertion order → mid-vector
            // inserts and plenty of duplicate timestamps
            DiveDataPoint p = point(double(rng.bounded(300)), rng.bounded(60.0));
            p.ceiling = double(rng.bounded(10));
            p.stopTime = double(rng.bounded(5));
            d.addDataPoint(p);
        }
        const QVector<DiveDataPoint> &pts = d.allDataPoints();
        const double first = pts.first().timestamp;
        const double last = pts.last().timestamp;
        QVector<double> queries;
        for (const DiveDataPoint &p : pts) {
            queries << p.timestamp << p.timestamp + 0.5 << p.timestamp - 0.25;
        }
        for (int k = 0; k < 500; ++k) {
            queries << rng.bounded(300.0);
        }
        int checked = 0;
        for (double t : queries) {
            if (!(t > first && t < last)) {
                continue; // early-return clamps, covered elsewhere
            }
            const DiveDataPoint got = d.dataAtTime(t);
            const DiveDataPoint want = bruteForceSegmentPoint(pts, t);
            QVERIFY2(got.depth == want.depth && got.ceiling == want.ceiling
                         && got.stopTime == want.stopTime,
                     qPrintable(QStringLiteral("t=%1").arg(t, 0, 'g', 17)));
            ++checked;
        }
        QVERIFY(checked > 1000);
    }

    void maxDepthUntilBoundariesAndEqualTimestamps()
    {
        DiveData d;
        QVERIFY(d.maxDepthUntil(10.0) == 0.0); // empty dive

        d.addDataPoint(point(0.0, 5.0));
        d.addDataPoint(point(10.0, 20.0));
        d.addDataPoint(point(20.0, 8.0));
        // Equal-timestamp sample, deeper than its twin: included at t == 20
        d.addDataPoint(point(20.0, 40.0));
        d.addDataPoint(point(30.0, 2.0));

        QVERIFY(d.maxDepthUntil(-1.0) == 0.0);   // before first sample
        QVERIFY(d.maxDepthUntil(0.0) == 5.0);    // exactly at first
        QVERIFY(d.maxDepthUntil(5.0) == 12.5);   // mid-segment interpolation
        QVERIFY(d.maxDepthUntil(10.0) == 20.0);  // exactly at a sample
        // Later-added twin sorts first: order is (10,20)(20,40)(20,8), so the
        // segment ending at t=20 heads for 40 m — interpolated 30 m at t=15
        QVERIFY(d.maxDepthUntil(15.0) == 30.0);
        QVERIFY(d.maxDepthUntil(20.0) == 40.0);  // BOTH t=20 twins included
        QVERIFY(d.maxDepthUntil(99.0) == 40.0);  // after last
        QCOMPARE(d.maxDepthUntil(99.0), d.maxDepth());

        // clearData resets the prefix-max along with the samples
        d.clearData();
        QVERIFY(d.maxDepthUntil(99.0) == 0.0);
        d.addDataPoint(point(0.0, 3.0));
        QVERIFY(d.maxDepthUntil(99.0) == 3.0);
    }

    void maxDepthUntilMatchesBruteForceOracle()
    {
        QRandomGenerator rng(0xB0B5u);
        DiveData d;
        for (int k = 0; k < 500; ++k) {
            // Random insertion order (mid-vector inserts after which the
            // prefix-max suffix must change), duplicate integer timestamps,
            // occasional zero and negative depths (the oracle seeds at 0.0)
            const int roll = rng.bounded(20);
            const double depth = roll == 0 ? 0.0
                               : roll == 1 ? -rng.bounded(2.0)
                                           : rng.bounded(80.0);
            d.addDataPoint(point(double(rng.bounded(400)), depth));

            // Check incrementally too: the prefix-max must be right after
            // every single insert, not just at the end
            if (k % 25 == 0) {
                const double t = rng.bounded(400.0);
                QVERIFY(d.maxDepthUntil(t) == bruteForceMaxDepthUntil(d.allDataPoints(), t));
            }
        }
        const QVector<DiveDataPoint> &pts = d.allDataPoints();
        QVector<double> queries{-10.0, 1e9};
        for (const DiveDataPoint &p : pts) {
            queries << p.timestamp << p.timestamp + 0.5 << p.timestamp - 0.25
                    << std::nextafter(p.timestamp, 1e9);
        }
        for (int k = 0; k < 1000; ++k) {
            queries << rng.bounded(420.0) - 10.0;
        }
        for (double t : queries) {
            const double got = d.maxDepthUntil(t);
            const double want = bruteForceMaxDepthUntil(pts, t);
            QVERIFY2(got == want,
                     qPrintable(QStringLiteral("t=%1 got=%2 want=%3")
                                    .arg(t, 0, 'g', 17)
                                    .arg(got, 0, 'g', 17)
                                    .arg(want, 0, 'g', 17)));
        }
    }
};

QTEST_GUILESS_MAIN(DiveDataTest)
#include "dive_data_test.moc"
