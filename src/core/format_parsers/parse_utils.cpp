#include "include/core/format_parsers/parse_utils.h"

#include <cmath>

namespace parse_utils {

double parseLocaleDouble(QStringView s)
{
    QString tmp = s.toString().trimmed();
    if (tmp.isEmpty()) {
        return std::nan("");
    }
    tmp.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double value = tmp.toDouble(&ok);
    return ok ? value : std::nan("");
}

QDateTime parseISO8601(QStringView s)
{
    const QString str = s.toString().trimmed();
    if (str.isEmpty()) {
        return QDateTime();
    }

    QDateTime dt = QDateTime::fromString(str, Qt::ISODateWithMs);
    if (!dt.isValid()) {
        dt = QDateTime::fromString(str, Qt::ISODate);
    }
    return dt;
}

} // namespace parse_utils
