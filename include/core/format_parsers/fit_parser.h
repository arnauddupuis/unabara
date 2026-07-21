#ifndef FIT_PARSER_H
#define FIT_PARSER_H

#include <QMap>
#include <QString>
#include <QVector>

#include "include/core/format_parsers/idive_log_format_parser.h"
#include "include/core/format_parsers/fit_decoder.h"

// Parser for Garmin FIT activity files (Descent dive computers).
// Clean-room implementation: message and field numbers are protocol facts
// taken from public format documentation and independent implementations —
// no Garmin SDK code is used.
class FitParser : public IDiveLogFormatParser
{
public:
    FitParser();
    ~FitParser() override = default;

    bool canParse(QFile &file) const override;
    QList<DiveData *> parse(QFile &file, int specificDive, QString &errorOut) override;
    QList<QString> listDives(QFile &file, QString &errorOut) override;
    QString formatName() const override { return QStringLiteral("Garmin FIT"); }

private:
    // Per-file metadata gathered before building samples. FIT files put
    // summary messages (session, dive summary) after the sample records,
    // so everything is decoded first and read in two passes.
    struct Metadata {
        int subSport = -1;              // 53-57, 63 are dive activities
        bool hasStart = false;
        quint32 startFit = 0;           // dive start in FIT epoch seconds
        qint64 timeOffset = 0;          // local wall-clock offset in seconds
        bool hasPosition = false;
        double latitude = 0.0;          // degrees
        double longitude = 0.0;         // degrees
        double setpointLowBar = 0.0;    // CCR setpoints
        double setpointHighBar = 0.0;
        QVector<CylinderInfo> cylinders;
        QMap<int, int> gasIndexToCylinder; // FIT gas message index -> cylinder index
        QVector<quint32> tankSensors;   // tank pod sensor IDs, order = pressure channel
        int diveNumber = 0;
        double meanDepth = -1.0;        // meters, < 0 when not reported
        bool sawDiveData = false;       // any depth sample or dive-specific message
    };

    bool decodeFile(QFile &file, FitDecoder &decoder, QString &errorOut) const;
    Metadata collectMetadata(const QList<FitMessage> &messages) const;
    bool isDive(const Metadata &meta, QString &errorOut) const;
    DiveData *buildDive(const QList<FitMessage> &messages, const Metadata &meta) const;
};

#endif // FIT_PARSER_H
