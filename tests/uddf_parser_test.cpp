// Tests for the UDDF parser using synthetic in-test XML: gas definitions,
// SI-unit conversions (Kelvin, Pascal, m³), comma decimals, CCR waypoint
// elements, and deco stops.

#include <QtTest>

#include "include/core/dive_data.h"
#include "include/core/format_parsers/uddf_parser.h"

namespace {

const char kUddfDive[] = R"(<uddf version='3.2'>
<gasdefinitions>
  <mix id='mix1'><name>EAN32</name><o2>0.32</o2><he>0.0</he></mix>
  <mix id='mix2'><name>TX18/45</name><o2>0,18</o2><he>0,45</he></mix>
</gasdefinitions>
<profiledata><repetitiongroup><dive id='d1'>
  <informationbeforedive>
    <divenumber>3</divenumber>
    <datetime>2026-04-23T09:30:00</datetime>
  </informationbeforedive>
  <tankdata>
    <link ref='mix1'/>
    <tankvolume>0.012</tankvolume>
    <tankpressurebegin>20000000</tankpressurebegin>
    <tankpressureend>15000000</tankpressureend>
  </tankdata>
  <samples>
    <waypoint>
      <divetime>0.0</divetime>
      <depth>0.0</depth>
      <temperature>297.15</temperature>
      <tankpressure ref='mix1'>20000000</tankpressure>
    </waypoint>
    <waypoint>
      <divetime>60.0</divetime>
      <depth>18.0</depth>
      <cns>12.5</cns>
      <nodecotime>1200</nodecotime>
    </waypoint>
    <waypoint>
      <divetime>120.0</divetime>
      <depth>20,5</depth>
      <decostop kind='mandatory' decodepth='3.0' duration='120'/>
      <decostop kind='mandatory' decodepth='6.0' duration='180'/>
    </waypoint>
  </samples>
  <informationafterdive><averagedepth>12.3</averagedepth></informationafterdive>
</dive></repetitiongroup></profiledata>
</uddf>)";

QList<DiveData *> parseUddf(const QByteArray &xml, QString &err)
{
    QTemporaryFile tmp;
    tmp.open();
    tmp.write(xml);
    tmp.flush();
    tmp.seek(0);
    UDDFParser parser;
    return parser.parse(tmp, -1, err);
}

} // namespace

class UddfParserTest : public QObject
{
    Q_OBJECT

private slots:
    void sniffsUddfRoot()
    {
        QTemporaryFile tmp;
        tmp.open();
        tmp.write(QByteArray(kUddfDive));
        tmp.flush();
        tmp.seek(0);
        UDDFParser parser;
        QVERIFY(parser.canParse(tmp));
        QCOMPARE(tmp.pos(), qint64(0)); // seek position restored
    }

    void metadataAndSiUnits()
    {
        QString err;
        auto dives = parseUddf(kUddfDive, err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(dives.size(), 1);
        DiveData *d = dives.first();

        QCOMPARE(d->diveNumber(), 3);
        QCOMPARE(d->startTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")),
                 QStringLiteral("2026-04-23 09:30:00"));
        QCOMPARE(d->meanDepth(), 12.3); // from <averagedepth>

        QCOMPARE(d->cylinderCount(), 1);
        const CylinderInfo &cyl = d->cylinderInfo(0);
        QCOMPARE(cyl.o2Percent, 32.0);       // mix fraction 0.32 -> percent
        QCOMPARE(cyl.size, 12.0);            // 0.012 m^3 -> liters
        QCOMPARE(cyl.startPressure, 200.0);  // 2e7 Pa -> bar
        QCOMPARE(cyl.endPressure, 150.0);
        qDeleteAll(dives);
    }

    void waypointsWithConversionsAndCarryForward()
    {
        QString err;
        auto dives = parseUddf(kUddfDive, err);
        QCOMPARE(dives.size(), 1);
        const auto &pts = dives.first()->allDataPoints();
        QCOMPARE(pts.size(), 3);

        QCOMPARE(pts[0].temperature, 24.0); // 297.15 K
        QCOMPARE(pts[0].getPressure(0), 200.0);
        QCOMPARE(pts[0].cns, -1.0);
        QCOMPARE(pts[0].ndl, -1.0); // no deco info reported yet — not "in deco"

        QCOMPARE(pts[1].depth, 18.0);
        QCOMPARE(pts[1].temperature, 24.0); // carried
        QCOMPARE(pts[1].getPressure(0), 200.0); // carried
        QCOMPARE(pts[1].cns, 12.5);
        QCOMPARE(pts[1].ndl, 20.0); // 1200 s -> min

        // Comma decimal accepted. Two mandatory decostops, shallower listed
        // first: ceiling = deepest stop, TTS = summed durations, stop time =
        // the deepest stop's own duration (order-independent)
        QCOMPARE(pts[2].depth, 20.5);
        QCOMPARE(pts[2].ceiling, 6.0);
        QCOMPARE(pts[2].tts, 5.0); // 120 s + 180 s -> min
        QCOMPARE(pts[2].stopTime, 3.0); // 180 s (deepest stop only) -> min
        QCOMPARE(pts[2].cns, 12.5); // carried
        QCOMPARE(pts[1].stopTime, 0.0); // no stop before deco
        qDeleteAll(dives);
    }

    void ccrWaypointsMapToSensors()
    {
        const QByteArray xml = QByteArrayLiteral(R"(<uddf version='3.2'>
<profiledata><repetitiongroup><dive id='d1'>
  <informationbeforedive><divenumber>1</divenumber>
    <datetime>2026-01-01T10:00:00</datetime></informationbeforedive>
  <samples>
    <waypoint><divetime>0</divetime><depth>10.0</depth>
      <divemode kind='closedcircuit'/>
      <measuredpo2 ref='s1'>122000</measuredpo2>
      <measuredpo2 ref='s2'>130000</measuredpo2>
    </waypoint>
    <waypoint><divetime>10</divetime><depth>12.0</depth>
      <measuredpo2 ref='s2'>131000</measuredpo2>
    </waypoint>
  </samples>
</dive></repetitiongroup></profiledata></uddf>)");

        QString err;
        auto dives = parseUddf(xml, err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(dives.size(), 1);
        DiveData *d = dives.first();
        QCOMPARE(d->diveMode(), DiveData::ClosedCircuit);

        const auto &pts = d->allDataPoints();
        QCOMPARE(pts[0].getPO2Sensor(0), 1.22); // Pa -> bar
        QCOMPARE(pts[0].getPO2Sensor(1), 1.30);
        // Sensor refs keep stable indices; missing s1 carries forward
        QCOMPARE(pts[1].getPO2Sensor(0), 1.22);
        QCOMPARE(pts[1].getPO2Sensor(1), 1.31);
        qDeleteAll(dives);
    }

    void malformedUddfReportsError()
    {
        QString err;
        auto dives = parseUddf(QByteArrayLiteral("<uddf><profiledata>"), err);
        QVERIFY(dives.isEmpty());
        QVERIFY(!err.isEmpty());
    }
};

QTEST_GUILESS_MAIN(UddfParserTest)
#include "uddf_parser_test.moc"
