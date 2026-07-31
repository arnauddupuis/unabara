// Tests for parse_utils (shared parsing helpers) and Units (display
// formatting and unit conversions).

#include <QtTest>

#include <cmath>

#include "include/core/format_parsers/parse_utils.h"
#include "include/core/units.h"

class CoreUtilsTest : public QObject
{
    Q_OBJECT

private slots:
    // ---- parse_utils ----

    void parseLocaleDoubleAcceptsBothSeparators()
    {
        QCOMPARE(parse_utils::parseLocaleDouble(u"2.1"), 2.1);
        QCOMPARE(parse_utils::parseLocaleDouble(u"2,1"), 2.1);
        QCOMPARE(parse_utils::parseLocaleDouble(u"-3,5"), -3.5);
        QCOMPARE(parse_utils::parseLocaleDouble(u"120"), 120.0);
        QVERIFY(std::isnan(parse_utils::parseLocaleDouble(u"abc")));
        QVERIFY(std::isnan(parse_utils::parseLocaleDouble(u"")));
    }

    void parseISO8601Variants()
    {
        const QDateTime plain = parse_utils::parseISO8601(u"2026-02-28T11:04:56");
        QVERIFY(plain.isValid());
        QCOMPARE(plain.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")),
                 QStringLiteral("2026-02-28 11:04:56"));

        const QDateTime zulu = parse_utils::parseISO8601(u"2026-02-28T11:04:56Z");
        QVERIFY(zulu.isValid());
        const QDateTime offset = parse_utils::parseISO8601(u"2026-02-28T13:04:56+02:00");
        QVERIFY(offset.isValid());
        // Same instant expressed two ways
        QCOMPARE(offset.toUTC(), zulu.toUTC());

        QVERIFY(!parse_utils::parseISO8601(u"not a date").isValid());
    }

    void unitConversionHelpers()
    {
        QCOMPARE(parse_utils::kelvinToCelsius(273.15), 0.0);
        QCOMPARE(parse_utils::kelvinToCelsius(297.15), 24.0);
        QCOMPARE(parse_utils::pascalToBar(100000.0), 1.0);
        QCOMPARE(parse_utils::pascalToBar(20050000.0), 200.5);
    }

    // ---- Units ----

    void gasMixNames()
    {
        QCOMPARE(Units::formatGasMix(21.0, 0.0), QStringLiteral("Air"));
        QCOMPARE(Units::formatGasMix(20.9, 0.0), QStringLiteral("Air")); // rounds
        QCOMPARE(Units::formatGasMix(32.0, 0.0), QStringLiteral("EAN32"));
        QCOMPARE(Units::formatGasMix(100.0, 0.0), QStringLiteral("O2"));
        QCOMPARE(Units::formatGasMix(18.0, 45.0), QStringLiteral("18/45"));
    }

    void metricImperialConversions()
    {
        QVERIFY(qAbs(Units::metersToFeet(10.0) - 32.8084) < 0.001);
        QVERIFY(qAbs(Units::feetToMeters(Units::metersToFeet(12.3)) - 12.3) < 1e-9);
        QCOMPARE(Units::celsiusToFahrenheit(0.0), 32.0);
        QCOMPARE(Units::fahrenheitToCelsius(212.0), 100.0);
        QVERIFY(qAbs(Units::barToPsi(1.0) - 14.5038) < 0.001);
        QVERIFY(qAbs(Units::psiToBar(Units::barToPsi(200.0)) - 200.0) < 1e-9);
    }

    void formattedValues()
    {
        QCOMPARE(Units::formatDepth(18.04, Units::UnitSystem::Metric),
                 QStringLiteral("18.0"));
        QCOMPARE(Units::formatDepth(10.0, Units::UnitSystem::Imperial),
                 QStringLiteral("32.8"));
    }
};

QTEST_GUILESS_MAIN(CoreUtilsTest)
#include "core_utils_test.moc"
