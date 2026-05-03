#ifndef IDIVE_LOG_FORMAT_PARSER_H
#define IDIVE_LOG_FORMAT_PARSER_H

#include <QFile>
#include <QList>
#include <QString>
#include "include/core/dive_data.h"

class IDiveLogFormatParser
{
public:
    virtual ~IDiveLogFormatParser() = default;

    // Cheap content sniff: peek at the file's first StartElement.
    // Implementations must restore the seek position on exit.
    virtual bool canParse(QFile &file) const = 0;

    // Parse all dives, or one if specificDive >= 0. errorOut is populated on failure.
    // Caller takes ownership of returned DiveData* objects.
    virtual QList<DiveData *> parse(QFile &file, int specificDive, QString &errorOut) = 0;

    // Light-weight dive listing for the import-dialog picker.
    virtual QList<QString> listDives(QFile &file, QString &errorOut) = 0;

    // Human-readable name, used in error messages and logs.
    virtual QString formatName() const = 0;
};

#endif // IDIVE_LOG_FORMAT_PARSER_H
