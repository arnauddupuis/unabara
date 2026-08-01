// Unit tests for the dive log parsers, centered on the Garmin FIT importer.
//
// Fixtures:
//  - SYNTH_FIT_PATH: deterministic file from make_synth_fit.py (generated at
//    build time), covering big-endian records, compressed timestamps,
//    developer fields, tank pods, and a gas switch.
//  - TEST_DATA_DIR: local sample logs (untracked). Tests that need them
//    QSKIP when the files are absent, so the suite is CI-safe.

#include <QtTest>

#include "include/core/dive_data.h"
#include "include/core/format_parsers/fit_decoder.h"
#include "include/core/format_parsers/fit_parser.h"
#include "include/core/format_parsers/subsurface_parser.h"
#include "include/core/format_parsers/uddf_parser.h"

namespace {

// The synthetic dive's ground truth (see make_synth_fit.py)
constexpr qint64 kSynthStartEpoch = 631065600 + 1100000000; // 2024-11-08T11:33:20Z
constexpr int kSynthUtcOffset = 3600;                       // ACTIVITY says UTC+1

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QList<DiveData *> parseBytes(const QByteArray &bytes, QString &err)
{
    QTemporaryFile tmp;
    tmp.open();
    tmp.write(bytes);
    tmp.flush();
    tmp.seek(0);
    FitParser parser;
    return parser.parse(tmp, -1, err);
}

// Minimal in-memory FIT builder for malformed/edge-case streams the Python
// generator does not produce. Little-endian only.
class FitBuilder
{
public:
    // fields: {fieldNum, size, baseType}
    void definition(quint8 local, quint16 globalId,
                    const QVector<std::array<quint8, 3>> &fields)
    {
        m_records.append(char(0x40 | local));
        m_records.append('\0');
        m_records.append('\0'); // little-endian
        m_records.append(char(globalId & 0xFF));
        m_records.append(char(globalId >> 8));
        m_records.append(char(fields.size()));
        for (const auto &f : fields) {
            m_records.append(char(f[0]));
            m_records.append(char(f[1]));
            m_records.append(char(f[2]));
        }
    }

    void dataRecord(quint8 local, const QByteArray &payload)
    {
        m_records.append(char(local));
        m_records.append(payload);
    }

    static QByteArray u32(quint32 v)
    {
        QByteArray out;
        for (int i = 0; i < 4; ++i) {
            out.append(char((v >> (8 * i)) & 0xFF));
        }
        return out;
    }

    QByteArray build() const
    {
        QByteArray header(12, '\0');
        header[0] = 12; // 12-byte header, no header CRC
        header[1] = 0x20;
        const quint32 size = static_cast<quint32>(m_records.size());
        for (int i = 0; i < 4; ++i) {
            header[4 + i] = char((size >> (8 * i)) & 0xFF);
        }
        header.replace(8, 4, ".FIT");
        QByteArray payload = header + m_records;
        const quint16 crc = FitDecoder::crc16(
            reinterpret_cast<const uchar *>(payload.constData()), payload.size());
        payload.append(char(crc & 0xFF));
        payload.append(char(crc >> 8));
        return payload;
    }

private:
    QByteArray m_records;
};

} // namespace

class FitParserTest : public QObject
{
    Q_OBJECT

private:
    QByteArray m_synthBytes;

    QList<DiveData *> parseSynth(QString &err)
    {
        return parseBytes(m_synthBytes, err);
    }

private slots:
    void initTestCase()
    {
        m_synthBytes = readAll(QStringLiteral(SYNTH_FIT_PATH));
        QVERIFY2(!m_synthBytes.isEmpty(), "synthetic fixture missing — build error");
    }

    // ---- Wire-level ----

    void sniffAcceptsFit()
    {
        QTemporaryFile tmp;
        tmp.open();
        tmp.write(m_synthBytes);
        tmp.flush();
        tmp.seek(3); // sniff must work from any position and restore it
        QVERIFY(FitDecoder::sniff(&tmp));
        QCOMPARE(tmp.pos(), qint64(3));
    }

    void sniffRejectsOthers()
    {
        QTemporaryFile tmp;
        tmp.open();
        tmp.write(QByteArrayLiteral("<?xml version=\"1.0\"?><divelog></divelog>"));
        tmp.flush();
        tmp.seek(0);
        QVERIFY(!FitDecoder::sniff(&tmp));

        QTemporaryFile small;
        small.open();
        small.write(QByteArrayLiteral("short"));
        small.flush();
        small.seek(0);
        QVERIFY(!FitDecoder::sniff(&small));
    }

    // ---- Synthetic dive: full pipeline ----

    void synthMetadata()
    {
        QString err;
        auto dives = parseSynth(err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(dives.size(), 1);
        DiveData *d = dives.first();

        QCOMPARE(d->diveNumber(), 42);
        QCOMPARE(d->diveMode(), DiveData::OpenCircuit);
        QCOMPARE(d->durationSeconds(), 9);
        QCOMPARE(d->maxDepth(), 20.0);
        QCOMPARE(d->minTemperature(), 18.0);
        QCOMPARE(d->meanDepth(), 10.5); // from DIVE_SUMMARY avg_depth
        QCOMPARE(d->location(), QStringLiteral("46.500000, 6.500000"));
        qDeleteAll(dives);
    }

    void synthStartTime()
    {
        QString err;
        auto dives = parseSynth(err);
        QCOMPARE(dives.size(), 1);
        const QDateTime start = dives.first()->startTime();

        // The epoch must be the true UTC instant (video sync depends on it)...
        QCOMPARE(start.toSecsSinceEpoch(), kSynthStartEpoch);
        // ...while the displayed wall clock matches the watch (UTC+1)
        QCOMPARE(start.offsetFromUtc(), kSynthUtcOffset);
        QCOMPARE(start.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")),
                 QStringLiteral("2024-11-08 12:33:20"));
        qDeleteAll(dives);
    }

    void synthCylinders()
    {
        QString err;
        auto dives = parseSynth(err);
        QCOMPARE(dives.size(), 1);
        DiveData *d = dives.first();

        QCOMPARE(d->cylinderCount(), 2);
        const CylinderInfo &air = d->cylinderInfo(0);
        QCOMPARE(air.o2Percent, 21.0);
        QCOMPARE(air.size, 12.0);
        QCOMPARE(air.startPressure, 200.5);
        QCOMPARE(air.endPressure, 185.0);

        const CylinderInfo &ean = d->cylinderInfo(1);
        QCOMPARE(ean.o2Percent, 50.0);
        QCOMPARE(ean.size, 10.0);
        QCOMPARE(ean.startPressure, 180.0);
        QCOMPARE(ean.endPressure, 175.0);
        QVERIFY(ean.description.contains(QStringLiteral("50")));
        qDeleteAll(dives);
    }

    void synthSamples()
    {
        QString err;
        auto dives = parseSynth(err);
        QCOMPARE(dives.size(), 1);
        const auto &points = dives.first()->allDataPoints();
        QCOMPARE(points.size(), 7);

        // t=0: surface, no tank data yet
        QCOMPARE(points[0].timestamp, 0.0);
        QCOMPARE(points[0].depth, 1.0);
        QCOMPARE(points[0].temperature, 19.0);
        QCOMPARE(points[0].ndl, 50.0);
        QCOMPARE(points[0].cns, 0.0);
        QCOMPARE(points[0].tankCount(), 0);

        // t=2 came in via a compressed-timestamp record
        QCOMPARE(points[2].timestamp, 2.0);
        QCOMPARE(points[2].depth, 10.0);

        // t=3: big-endian record; pressures carried from the t=1 tank updates
        QCOMPARE(points[3].timestamp, 3.0);
        QCOMPARE(points[3].depth, 15.0);
        QVERIFY(qFuzzyCompare(points[3].ndl, 2000.0 / 60.0));
        QCOMPARE(points[3].cns, 1.0);
        QCOMPARE(points[3].getPressure(0), 200.5);
        QCOMPARE(points[3].getPressure(1), 180.0);

        // t=9: after the gas switch to EAN50
        QCOMPARE(points[6].timestamp, 9.0);
        QCOMPARE(points[6].depth, 2.0);
        QCOMPARE(points[6].cns, 3.0);
        QCOMPARE(points[6].o2percent, 50.0);
        QCOMPARE(points[6].getPressure(0), 190.0);
        qDeleteAll(dives);
    }

    void synthGasSwitches()
    {
        QString err;
        auto dives = parseSynth(err);
        QCOMPARE(dives.size(), 1);
        DiveData *d = dives.first();

        // Pre-start switch clamps to t=0 on gas 0; switch to EAN50 at t=5
        QCOMPARE(d->activeCylinderAtTime(0.0), 0);
        QCOMPARE(d->activeCylinderAtTime(4.0), 0);
        QCOMPARE(d->activeCylinderAtTime(5.0), 1);
        QCOMPARE(d->activeCylinderAtTime(9.0), 1);
        qDeleteAll(dives);
    }

    void synthListDives()
    {
        QTemporaryFile tmp;
        tmp.open();
        tmp.write(m_synthBytes);
        tmp.flush();
        FitParser parser;
        QString err;
        const QList<QString> entries = parser.listDives(tmp, err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first(),
                 QStringLiteral("Dive #42 - 2024-11-08 12:33:20 at 46.500000, 6.500000"));
    }

    void pressureInterpolationPrefersSamples()
    {
        QString err;
        auto dives = parseSynth(err);
        QCOMPARE(dives.size(), 1);
        DiveData *d = dives.first();

        // Between the t=1 (200.5) and t=5 (190.0) tank updates the value must
        // follow the recorded samples, not the TANK_SUMMARY 200.5->185 ramp
        QCOMPARE(d->dataAtTime(4.0).getPressure(0), 195.25);
        // After the last update the recorded value holds; the summary ramp
        // would drift toward 185
        QCOMPARE(d->dataAtTime(6.0).getPressure(0), 190.0);
        QCOMPARE(d->dataAtTime(8.0).getPressure(0), 190.0);
        // Pod 2 never updates after t=1: constant samples, constant display
        QCOMPARE(d->dataAtTime(6.0).getPressure(1), 180.0);
        qDeleteAll(dives);
    }

    // ---- Robustness ----

    void truncatedFileSalvages()
    {
        // Drop the tail (summary/session/activity + some records); enough
        // depth records must remain for the salvage rule to accept it
        QString err;
        auto dives = parseBytes(m_synthBytes.left(m_synthBytes.size() * 2 / 3), err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(dives.size(), 1);
        QVERIFY(dives.first()->allDataPoints().size() >= 2);
        qDeleteAll(dives);
    }

    void zeroDataSizeSalvages()
    {
        // Crash-recovered files: header written before data, dataSize never
        // backfilled. The decoder must fall back to end-of-file.
        QByteArray bytes = m_synthBytes;
        bytes[4] = bytes[5] = bytes[6] = bytes[7] = '\0';
        QString err;
        auto dives = parseBytes(bytes, err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(dives.size(), 1);
        QCOMPARE(dives.first()->allDataPoints().size(), 7);
        qDeleteAll(dives);
    }

    void garbageWithMagicFailsCleanly()
    {
        QByteArray bytes(200, '\x5A');
        bytes[0] = 12;
        bytes.replace(8, 4, ".FIT");
        QString err;
        auto dives = parseBytes(bytes, err);
        QVERIFY(dives.isEmpty());
        QVERIFY(!err.isEmpty());
    }

    void nonDiveFitRejected()
    {
        // A running activity: sport message + heart-rate-only records
        FitBuilder b;
        b.definition(0, 12, {{{1, 1, 0x00}}});         // SPORT: sub_sport enum
        b.dataRecord(0, QByteArrayLiteral("\x01"));    // 1 = running
        b.definition(1, 20, {{{253, 4, 0x86}, {3, 1, 0x02}}}); // RECORD: ts, heart_rate
        b.dataRecord(1, FitBuilder::u32(1100000000) + QByteArrayLiteral("\x78"));
        b.dataRecord(1, FitBuilder::u32(1100000001) + QByteArrayLiteral("\x79"));

        QString err;
        auto dives = parseBytes(b.build(), err);
        QVERIFY(dives.isEmpty());
        QVERIFY2(err.contains(QStringLiteral("dive")), qPrintable(err));
    }

    void nanArrayElementDoesNotEscape()
    {
        // Depth declared as a 2-element u32 array whose first element is the
        // invalid sentinel: value() must return the valid element, not NaN
        FitBuilder b;
        b.definition(0, 12, {{{1, 1, 0x00}}});
        b.dataRecord(0, QByteArrayLiteral("\x36")); // 54 = multi-gas dive
        b.definition(1, 20, {{{253, 4, 0x86}, {92, 8, 0x86}}}); // ts + depth[2]
        b.dataRecord(1, FitBuilder::u32(1100000000) + FitBuilder::u32(0xFFFFFFFF)
                            + FitBuilder::u32(5000));
        b.dataRecord(1, FitBuilder::u32(1100000001) + FitBuilder::u32(0xFFFFFFFF)
                            + FitBuilder::u32(6000));

        QString err;
        auto dives = parseBytes(b.build(), err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(dives.size(), 1);
        const auto &points = dives.first()->allDataPoints();
        QCOMPARE(points.size(), 2);
        QCOMPARE(points[0].depth, 5.0);
        QCOMPARE(points[1].depth, 6.0);
        qDeleteAll(dives);
    }

    // ---- Local-only tier (skipped when sample logs are absent) ----

    void realFitFiles()
    {
        const QDir dir(QStringLiteral(TEST_DATA_DIR));
        const QStringList files = dir.entryList({QStringLiteral("*.fit")}, QDir::Files);
        if (files.isEmpty()) {
            QSKIP("no real .fit samples in tests/data");
        }
        for (const QString &name : files) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::ReadOnly));
            FitParser parser;
            QVERIFY2(parser.canParse(f), qPrintable(name));
            QString err;
            auto dives = parser.parse(f, -1, err);
            QVERIFY2(err.isEmpty(), qPrintable(name + ": " + err));
            QCOMPARE(dives.size(), 1);
            DiveData *d = dives.first();
            QVERIFY2(d->allDataPoints().size() > 100, qPrintable(name));
            QVERIFY2(d->maxDepth() > 1.0, qPrintable(name));
            QVERIFY2(d->startTime().isValid(), qPrintable(name));
            QVERIFY2(qAbs(d->startTime().offsetFromUtc()) <= 14 * 3600, qPrintable(name));
            qDeleteAll(dives);
        }
    }

    void crossFormatDispatch()
    {
        const QDir dir(QStringLiteral(TEST_DATA_DIR));
        const QString ssrf = dir.filePath(QStringLiteral("blue_hole.ssrf"));
        if (!QFile::exists(ssrf)) {
            QSKIP("no .ssrf samples in tests/data");
        }
        QFile f(ssrf);
        QVERIFY(f.open(QIODevice::ReadOnly));

        // The FIT parser must not claim XML logs, and the XML parser must
        // still parse them with the binary-safe (non-Text) open mode
        FitParser fit;
        QVERIFY(!fit.canParse(f));
        SubsurfaceParser subsurface;
        QVERIFY(subsurface.canParse(f));
        QString err;
        auto dives = subsurface.parse(f, -1, err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(!dives.isEmpty());
        QVERIFY(dives.first()->allDataPoints().size() > 10);
        qDeleteAll(dives);
    }

    void uddfRegression()
    {
        const QDir dir(QStringLiteral(TEST_DATA_DIR));
        const QString uddf = dir.filePath(QStringLiteral("Garmin.uddf"));
        if (!QFile::exists(uddf)) {
            QSKIP("no .uddf samples in tests/data");
        }
        QFile f(uddf);
        QVERIFY(f.open(QIODevice::ReadOnly));
        UDDFParser parser;
        QVERIFY(parser.canParse(f));
        QString err;
        auto dives = parser.parse(f, -1, err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(!dives.isEmpty());
        qDeleteAll(dives);
    }
};

QTEST_GUILESS_MAIN(FitParserTest)
#include "fit_parser_test.moc"
