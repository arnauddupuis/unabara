#ifndef UNITS_H
#define UNITS_H

#include <QObject>
#include <QString>

class Units : public QObject
{
    Q_OBJECT
    
public:
    enum class UnitSystem {
        Metric,
        Imperial
    };
    Q_ENUM(UnitSystem)
    
    explicit Units(QObject *parent = nullptr);
    
    // Depth conversions
    static double metersToFeet(double meters);
    static double feetToMeters(double feet);
    
    // Temperature conversions  
    static double celsiusToFahrenheit(double celsius);
    static double fahrenheitToCelsius(double fahrenheit);
    
    // Pressure conversions
    static double barToPsi(double bar);
    static double psiToBar(double psi);
    
    // Format values with appropriate units and precision
    static QString formatDepth(double depthMeters, UnitSystem system);
    static QString formatTemperature(double tempCelsius, UnitSystem system);
    static QString formatPressure(double pressureBar, UnitSystem system);
    
    // Get unit labels
    static QString depthUnit(UnitSystem system);
    static QString temperatureUnit(UnitSystem system);
    static QString pressureUnit(UnitSystem system);
    
    // Convert and format in one step
    static QString formatDepthValue(double depthMeters, UnitSystem system);
    static QString formatTemperatureValue(double tempCelsius, UnitSystem system);
    static QString formatPressureValue(double pressureBar, UnitSystem system);
};

#endif // UNITS_H