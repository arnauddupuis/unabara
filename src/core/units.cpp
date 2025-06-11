#include "include/core/units.h"
#include <QLocale>

Units::Units(QObject *parent)
    : QObject(parent)
{
}

double Units::metersToFeet(double meters)
{
    return meters * 3.28084;
}

double Units::feetToMeters(double feet)
{
    return feet / 3.28084;
}

double Units::celsiusToFahrenheit(double celsius)
{
    return (celsius * 9.0 / 5.0) + 32.0;
}

double Units::fahrenheitToCelsius(double fahrenheit)
{
    return (fahrenheit - 32.0) * 5.0 / 9.0;
}

double Units::barToPsi(double bar)
{
    return bar * 14.5038;
}

double Units::psiToBar(double psi)
{
    return psi / 14.5038;
}

QString Units::formatDepth(double depthMeters, UnitSystem system)
{
    if (system == UnitSystem::Imperial) {
        double feet = metersToFeet(depthMeters);
        return QString::number(feet, 'f', 1);
    } else {
        return QString::number(depthMeters, 'f', 1);
    }
}

QString Units::formatTemperature(double tempCelsius, UnitSystem system)
{
    if (system == UnitSystem::Imperial) {
        double fahrenheit = celsiusToFahrenheit(tempCelsius);
        return QString::number(fahrenheit, 'f', 1);
    } else {
        return QString::number(tempCelsius, 'f', 1);
    }
}

QString Units::formatPressure(double pressureBar, UnitSystem system)
{
    if (system == UnitSystem::Imperial) {
        double psi = barToPsi(pressureBar);
        return QString::number(psi, 'f', 0);
    } else {
        return QString::number(pressureBar, 'f', 0);
    }
}

QString Units::depthUnit(UnitSystem system)
{
    return (system == UnitSystem::Imperial) ? "ft" : "m";
}

QString Units::temperatureUnit(UnitSystem system)
{
    return (system == UnitSystem::Imperial) ? "°F" : "°C";
}

QString Units::pressureUnit(UnitSystem system)
{
    return (system == UnitSystem::Imperial) ? "psi" : "bar";
}

QString Units::formatDepthValue(double depthMeters, UnitSystem system)
{
    return formatDepth(depthMeters, system) + " " + depthUnit(system);
}

QString Units::formatTemperatureValue(double tempCelsius, UnitSystem system)
{
    return formatTemperature(tempCelsius, system) + temperatureUnit(system);
}

QString Units::formatPressureValue(double pressureBar, UnitSystem system)
{
    return formatPressure(pressureBar, system) + " " + pressureUnit(system);
}