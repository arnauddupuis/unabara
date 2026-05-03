#ifndef SUBSURFACE_PARSER_H
#define SUBSURFACE_PARSER_H

#include <QMap>
#include <QXmlStreamReader>

#include "include/core/format_parsers/idive_log_format_parser.h"

class SubsurfaceParser : public IDiveLogFormatParser
{
public:
    SubsurfaceParser();
    ~SubsurfaceParser() override = default;

    bool canParse(QFile &file) const override;
    QList<DiveData *> parse(QFile &file, int specificDive, QString &errorOut) override;
    QList<QString> listDives(QFile &file, QString &errorOut) override;
    QString formatName() const override { return QStringLiteral("Subsurface XML"); }

private:
    struct GasSwitch {
        double timestamp;  // Time in seconds when switch occurred
        int cylinderIndex; // Which cylinder was switched to
    };

    struct DiveSite {
        QString uuid;
        QString name;
        QString gps;
        QString description;
    };

    DiveData *parseDiveElement(QXmlStreamReader &xml);
    void parseDiveComputerElement(QXmlStreamReader &xml, DiveData *dive, int &sampleCount);
    void parseSampleElement(QXmlStreamReader &xml,
                            DiveData *dive,
                            double &lastTemperature,
                            double &lastNDL,
                            double &lastTTS,
                            QMap<int, double> &lastPressures,
                            QMap<int, double> &lastPO2Sensors);
    void parseCylinderElement(QXmlStreamReader &xml, DiveData *dive);
    void parseDiveSites(QXmlStreamReader &xml);

    void resetDiveState();

    QMap<int, double> m_initialCylinderPressures;
    double m_lastCeiling = 0.0;
    QList<GasSwitch> m_gasSwitches;
    double m_diveDuration = 0.0;
    QMap<QString, DiveSite> m_diveSites;
    bool m_currentDiveHasCcrCues = false;
};

#endif // SUBSURFACE_PARSER_H
