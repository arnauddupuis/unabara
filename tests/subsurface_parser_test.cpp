// Tests for the Subsurface XML/SSRF parser and the LogParser dispatcher,
// using synthetic in-test XML so no sample files are required.

#include <QtTest>

#include "include/core/dive_data.h"
#include "include/core/log_parser.h"
#include "include/core/format_parsers/subsurface_parser.h"

namespace {

const char kTwoDives[] = R"(<divelog program='subsurface' version='3'>
<dives>
<dive number='7' date='2026-03-01' time='10:00:00'>
  <location>Test Reef</location>
  <cylinder size='12.0 l' workpressure='232.0 bar' description='D12' o2='32.0%' start='200.0 bar' end='150.0 bar' />
  <cylinder size='11.1 l' description='AL80' o2='50.0%' start='180.0 bar' end='160.0 bar' />
  <divecomputer model='Test DC'>
    <sample time='0:00 min' depth='0.0 m' temp='24.0 C' pressure0='200.0 bar' pressure1='180.0 bar' ndl='99:00 min' />
    <sample time='1:00 min' depth='18.0 m' cns='5%' />
    <event time='2:00 min' name='gaschange' cylinder='1' />
    <sample time='3:00 min' depth='20.0 m' temp='22.0 C' pressure0='170.0 bar' in_deco='1' tts='4:30 min' stopdepth='3.0 m' stoptime='2:00 min' />
    <sample time='4:00 min' depth='19.0 m' />
  </divecomputer>
</dive>
<dive number='8' date='2026-03-02' time='11:30:00'>
  <divecomputer model='Test DC'>
    <sample time='0:00 min' depth='0.0 m' temp='20.0 C' />
    <sample time='0:30 min' depth='5.0 m' />
  </divecomputer>
</dive>
</dives>
</divelog>)";

QList<DiveData *> parseXml(const QByteArray &xml, int specificDive, QString &err)
{
    QTemporaryFile tmp;
    tmp.open();
    tmp.write(xml);
    tmp.flush();
    tmp.seek(0);
    SubsurfaceParser parser;
    return parser.parse(tmp, specificDive, err);
}

} // namespace

class SubsurfaceParserTest : public QObject
{
    Q_OBJECT

private slots:
    void parsesDiveMetadataAndCylinders()
    {
        QString err;
        auto dives = parseXml(kTwoDives, 7, err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(dives.size(), 1);
        DiveData *d = dives.first();

        QCOMPARE(d->diveNumber(), 7);
        QCOMPARE(d->location(), QStringLiteral("Test Reef"));
        QCOMPARE(d->startTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")),
                 QStringLiteral("2026-03-01 10:00:00"));

        QCOMPARE(d->cylinderCount(), 2);
        const CylinderInfo &main = d->cylinderInfo(0);
        QCOMPARE(main.size, 12.0);
        QCOMPARE(main.workPressure, 232.0);
        QCOMPARE(main.o2Percent, 32.0);
        QCOMPARE(main.startPressure, 200.0);
        QCOMPARE(main.endPressure, 150.0);
        QCOMPARE(main.description, QStringLiteral("D12"));
        QCOMPARE(d->cylinderInfo(1).o2Percent, 50.0);
        qDeleteAll(dives);
    }

    void samplesWithCarryForward()
    {
        QString err;
        auto dives = parseXml(kTwoDives, 7, err);
        QCOMPARE(dives.size(), 1);
        const auto &pts = dives.first()->allDataPoints();
        QCOMPARE(pts.size(), 4);

        QCOMPARE(pts[0].timestamp, 0.0);
        QCOMPARE(pts[0].temperature, 24.0);
        QCOMPARE(pts[0].ndl, 99.0);
        QCOMPARE(pts[0].getPressure(0), 200.0);
        QCOMPARE(pts[0].getPressure(1), 180.0);
        QCOMPARE(pts[0].cns, -1.0); // no CNS data yet

        // Sample 2 omits temp/pressures: previous values carry forward
        QCOMPARE(pts[1].timestamp, 60.0);
        QCOMPARE(pts[1].depth, 18.0);
        QCOMPARE(pts[1].temperature, 24.0);
        QCOMPARE(pts[1].getPressure(0), 200.0);
        QCOMPARE(pts[1].getPressure(1), 180.0);
        QCOMPARE(pts[1].cns, 5.0);

        // in_deco='1' forces NDL to 0; tts/stoptime parsed as M:SS,
        // stopdepth as metres
        QCOMPARE(pts[2].timestamp, 180.0);
        QCOMPARE(pts[2].ndl, 0.0);
        QCOMPARE(pts[2].tts, 4.5);
        QCOMPARE(pts[2].getPressure(0), 170.0);
        QCOMPARE(pts[2].cns, 5.0); // carried
        QCOMPARE(pts[2].ceiling, 3.0);
        QCOMPARE(pts[2].stopTime, 2.0);

        // Delta encoding: a sample omitting stopdepth/stoptime carries both
        QCOMPARE(pts[3].timestamp, 240.0);
        QCOMPARE(pts[3].ceiling, 3.0);
        QCOMPARE(pts[3].stopTime, 2.0);
        qDeleteAll(dives);
    }

    void gasSwitchEventApplies()
    {
        QString err;
        auto dives = parseXml(kTwoDives, 7, err);
        QCOMPARE(dives.size(), 1);
        DiveData *d = dives.first();
        QCOMPARE(d->activeCylinderAtTime(60.0), 0);
        QCOMPARE(d->activeCylinderAtTime(150.0), 1);
        qDeleteAll(dives);
    }

    void parsesAllDivesAndSelectsByNumber()
    {
        QString err;
        auto all = parseXml(kTwoDives, -1, err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(all.size(), 2);
        qDeleteAll(all);

        auto only8 = parseXml(kTwoDives, 8, err);
        QCOMPARE(only8.size(), 1);
        QCOMPARE(only8.first()->diveNumber(), 8);
        qDeleteAll(only8);

        // Absent number: empty result, but not an error (LogParser reports it)
        auto none = parseXml(kTwoDives, 99, err);
        QVERIFY(none.isEmpty());
        QVERIFY(err.isEmpty());
    }

    void listDivesEntries()
    {
        QTemporaryFile tmp;
        tmp.open();
        tmp.write(QByteArray(kTwoDives));
        tmp.flush();
        SubsurfaceParser parser;
        QString err;
        const auto entries = parser.listDives(tmp, err);
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries[0],
                 QStringLiteral("Dive #7 - 2026-03-01 10:00:00 at Test Reef"));
    }

    void malformedXmlReportsError()
    {
        QString err;
        auto dives = parseXml(QByteArrayLiteral("<divelog><dives><dive number='1'"), -1, err);
        QVERIFY(dives.isEmpty());
        QVERIFY(!err.isEmpty());
    }
};

// ---- LogParser dispatch (content sniffing, signals) ----

class LogParserDispatchTest : public QObject
{
    Q_OBJECT

private slots:
    void dispatchIgnoresFileExtension()
    {
        // SSRF content in a file named .fit: the sniff must win
        QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/dispatch_XXXXXX.fit"));
        tmp.open();
        tmp.write(QByteArray(kTwoDives));
        tmp.flush();

        LogParser lp;
        QList<DiveData *> received;
        int importSignals = 0;
        connect(&lp, &LogParser::multipleImported, this,
                [&](const QList<DiveData *> &dives) {
                    received = dives;
                    ++importSignals;
                });
        QSignalSpy errors(&lp, &LogParser::errorOccurred);
        QVERIFY(lp.importFile(tmp.fileName()));
        QCOMPARE(importSignals, 1);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(received.size(), 2);
        qDeleteAll(received);
    }

    void unsupportedContentReportsError()
    {
        QTemporaryFile tmp;
        tmp.open();
        tmp.write(QByteArrayLiteral("not a dive log at all"));
        tmp.flush();

        LogParser lp;
        QSignalSpy errors(&lp, &LogParser::errorOccurred);
        QVERIFY(!lp.importFile(tmp.fileName()));
        QCOMPARE(errors.count(), 1);
        QVERIFY(!lp.lastError().isEmpty());
    }
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    int status = 0;
    {
        SubsurfaceParserTest t1;
        status |= QTest::qExec(&t1, argc, argv);
    }
    {
        LogParserDispatchTest t2;
        status |= QTest::qExec(&t2, argc, argv);
    }
    return status;
}

#include "subsurface_parser_test.moc"
