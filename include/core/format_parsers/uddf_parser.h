#ifndef UDDF_PARSER_H
#define UDDF_PARSER_H

#include <QMap>
#include <QString>
#include <QXmlStreamReader>

#include "include/core/format_parsers/idive_log_format_parser.h"

class UDDFParser : public IDiveLogFormatParser
{
public:
    UDDFParser();
    ~UDDFParser() override = default;

    bool canParse(QFile &file) const override;
    QList<DiveData *> parse(QFile &file, int specificDive, QString &errorOut) override;
    QList<QString> listDives(QFile &file, QString &errorOut) override;
    QString formatName() const override { return QStringLiteral("UDDF"); }

private:
    struct GasMix {
        QString name;
        double o2Percent = 21.0;
        double hePercent = 0.0;
    };

    struct DiveSite {
        QString id;
        QString name;
        QString location;
    };

    void resetState();
    void parseGasDefinitions(QXmlStreamReader &xml);
    void parseDiveSiteContainer(QXmlStreamReader &xml);
    void parseDiveSiteEntry(QXmlStreamReader &xml);
    void parseProfileData(QXmlStreamReader &xml,
                          QList<DiveData *> &out,
                          int specificDive,
                          bool &foundSpecific);

    DiveData *parseDiveElement(QXmlStreamReader &xml);
    void parseInformationBeforeDive(QXmlStreamReader &xml, DiveData *dive);
    void parseTankData(QXmlStreamReader &xml, DiveData *dive);
    void parseSamples(QXmlStreamReader &xml, DiveData *dive);
    void parseWaypoint(QXmlStreamReader &xml,
                       DiveData *dive,
                       double &lastTemperature,
                       double &lastNDL,
                       double &lastTTS,
                       double &lastCeiling,
                       QMap<int, double> &lastPressures,
                       QMap<int, double> &lastPO2Sensors,
                       QMap<QString, int> &po2SensorRefToIndex);

    int firstTankIndexForMix(const QString &mixId) const;

    QMap<QString, GasMix> m_mixes;
    QMap<QString, DiveSite> m_diveSites;
    QMap<QString, int> m_mixToFirstTank;
    bool m_currentDiveCcrSeen = false;
    bool m_currentDiveOcSeen = false;
};

#endif // UDDF_PARSER_H
