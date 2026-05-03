#ifndef PARSE_UTILS_H
#define PARSE_UTILS_H

#include <QDateTime>
#include <QString>
#include <QStringView>

namespace parse_utils {

inline double kelvinToCelsius(double k) { return k - 273.15; }
inline double pascalToBar(double pa)    { return pa / 1.0e5; }

// Accepts either "2.1" or "2,1" (UDDF uses comma as decimal separator in many places).
// Returns NaN on failure.
double parseLocaleDouble(QStringView s);

// ISO 8601 datetime: "2026-02-28T11:04:56", with optional "Z" or "+02:00" suffix.
// Returns an invalid QDateTime on failure.
QDateTime parseISO8601(QStringView s);

} // namespace parse_utils

#endif // PARSE_UTILS_H
