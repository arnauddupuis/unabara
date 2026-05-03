#include "include/core/format_parsers/subsurface_parser.h"

#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include <algorithm>

SubsurfaceParser::SubsurfaceParser() = default;

bool SubsurfaceParser::canParse(QFile &file) const
{
    const qint64 originalPos = file.pos();
    file.seek(0);

    QXmlStreamReader xml(&file);
    bool result = false;
    while (!xml.atEnd() && !xml.hasError()) {
        if (xml.readNext() == QXmlStreamReader::StartElement) {
            result = (xml.name() == QStringLiteral("divelog"));
            break;
        }
    }

    file.seek(originalPos);
    return result;
}

void SubsurfaceParser::resetDiveState()
{
    m_initialCylinderPressures.clear();
    m_gasSwitches.clear();
    m_lastCeiling = 0.0;
    m_currentDiveHasCcrCues = false;
}

QList<DiveData *> SubsurfaceParser::parse(QFile &file, int specificDive, QString &errorOut)
{
    QList<DiveData *> result;

    file.seek(0);
    qDebug() << "Starting to parse Subsurface XML file";
    QXmlStreamReader xml(&file);

    m_diveSites.clear();

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartElement && xml.name() == QStringLiteral("divesites")) {
            qDebug() << "Found divesites element";
            parseDiveSites(xml);
        } else if (token == QXmlStreamReader::StartElement && xml.name() == QStringLiteral("dive")) {
            qDebug() << "Found dive element";

            if (specificDive != -1) {
                QXmlStreamAttributes attrs = xml.attributes();
                if (attrs.hasAttribute("number")) {
                    bool ok;
                    int number = attrs.value("number").toInt(&ok);
                    if (!ok || number != specificDive) {
                        continue;
                    }
                }
            }

            DiveData *dive = parseDiveElement(xml);
            if (dive) {
                qDebug() << "Successfully parsed dive:" << dive->diveName();
                result.append(dive);

                if (specificDive != -1) {
                    break;
                }
            }
        }
    }

    if (xml.hasError()) {
        errorOut = QStringLiteral("XML parsing error: %1").arg(xml.errorString());
        qDebug() << "XML parsing error:" << xml.errorString();
        qDeleteAll(result);
        result.clear();
        return result;
    }

    qDebug() << "Finished parsing XML file, found" << result.size() << "dives";
    return result;
}

QList<QString> SubsurfaceParser::listDives(QFile &file, QString &errorOut)
{
    QList<QString> result;

    file.seek(0);
    QXmlStreamReader xml(&file);

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartElement && xml.name() == QStringLiteral("dive")) {
            QString diveDate;
            QString diveTime;
            QString diveLocation;

            QXmlStreamAttributes attrs = xml.attributes();
            if (attrs.hasAttribute("number")) {
                QString number = attrs.value("number").toString();

                if (attrs.hasAttribute("date")) {
                    diveDate = attrs.value("date").toString();
                }
                if (attrs.hasAttribute("time")) {
                    diveTime = attrs.value("time").toString();
                }

                while (!xml.atEnd() && !(xml.tokenType() == QXmlStreamReader::EndElement && xml.name() == QStringLiteral("dive"))) {
                    if (xml.tokenType() == QXmlStreamReader::StartElement && xml.name() == QStringLiteral("location")) {
                        diveLocation = xml.readElementText();
                        break;
                    }
                    xml.readNext();
                }

                QString entry = QStringLiteral("Dive #%1").arg(number);
                if (!diveDate.isEmpty()) {
                    entry += " - " + diveDate;
                }
                if (!diveTime.isEmpty()) {
                    entry += " " + diveTime;
                }
                if (!diveLocation.isEmpty()) {
                    entry += " at " + diveLocation;
                }

                result.append(entry);
            }
        }
    }

    if (xml.hasError()) {
        errorOut = QStringLiteral("XML parsing error: %1").arg(xml.errorString());
        result.clear();
    }

    return result;
}

DiveData *SubsurfaceParser::parseDiveElement(QXmlStreamReader &xml)
{
    DiveData *dive = new DiveData();

    resetDiveState();

    QXmlStreamAttributes attrs = xml.attributes();

    if (attrs.hasAttribute("number")) {
        QString number = attrs.value("number").toString();
        bool ok;
        int diveNumber = number.toInt(&ok);
        if (ok) {
            dive->setDiveNumber(diveNumber);
        }
        dive->setDiveName(QStringLiteral("Dive #%1").arg(number));
    }

    if (attrs.hasAttribute("divesiteid")) {
        QString siteId = attrs.value("divesiteid").toString();
        dive->setDiveSiteId(siteId);

        if (m_diveSites.contains(siteId)) {
            const DiveSite &site = m_diveSites[siteId];
            dive->setDiveSiteName(site.name);
            if (dive->location().isEmpty() && !site.name.isEmpty()) {
                dive->setLocation(site.name);
            }
        }
    }

    QDateTime diveDateTime;
    if (attrs.hasAttribute("date")) {
        QString dateStr = attrs.value("date").toString();
        if (attrs.hasAttribute("time")) {
            QString timeStr = attrs.value("time").toString();
            diveDateTime = QDateTime::fromString(dateStr + " " + timeStr, "yyyy-MM-dd hh:mm:ss");
        } else {
            diveDateTime = QDateTime::fromString(dateStr, "yyyy-MM-dd");
        }
        dive->setStartTime(diveDateTime);
    }

    qDebug() << "Parsing dive element for" << dive->diveName();

    int sampleCount = 0;

    while (!xml.atEnd()) {
        xml.readNext();

        if (xml.tokenType() == QXmlStreamReader::EndElement && xml.name() == QStringLiteral("dive")) {
            break;
        }

        if (xml.tokenType() == QXmlStreamReader::StartElement) {
            QString elementName = xml.name().toString();

            if (elementName == "location") {
                dive->setLocation(xml.readElementText());
            } else if (elementName == "cylinder") {
                parseCylinderElement(xml, dive);
            } else if (elementName == "divecomputer") {
                parseDiveComputerElement(xml, dive, sampleCount);
            }
        }
    }

    if (dive->allDataPoints().isEmpty() && dive->cylinderCount() > 0) {
        DiveDataPoint initialPoint;
        initialPoint.timestamp = 0.0;

        for (int i = 0; i < dive->cylinderCount(); i++) {
            double initialPressure = m_initialCylinderPressures.value(i, 0.0);
            if (initialPressure > 0.0) {
                initialPoint.addPressure(initialPressure, i);
            }
        }

        dive->addDataPoint(initialPoint);
    }

    m_diveDuration = dive->durationSeconds();

    if (dive->diveMode() == DiveData::UnknownMode) {
        dive->setDiveMode(m_currentDiveHasCcrCues ? DiveData::ClosedCircuit : DiveData::OpenCircuit);
    }

    qDebug() << "Finished parsing dive element. Total data points:" << dive->allDataPoints().size()
             << "diveMode:" << dive->diveMode();
    return dive;
}

void SubsurfaceParser::parseCylinderElement(QXmlStreamReader &xml, DiveData *dive)
{
    QXmlStreamAttributes attrs = xml.attributes();
    CylinderInfo cylinder;

    if (attrs.hasAttribute("size")) {
        QString sizeStr = attrs.value("size").toString();
        QRegularExpression sizeRe("(\\d+\\.?\\d*)\\s+l");
        QRegularExpressionMatch match = sizeRe.match(sizeStr);

        if (match.hasMatch()) {
            cylinder.size = match.captured(1).toDouble();
        }
    }

    if (attrs.hasAttribute("workpressure")) {
        QString pressureStr = attrs.value("workpressure").toString();
        QRegularExpression pressureRe("(\\d+\\.?\\d*)\\s+bar");
        QRegularExpressionMatch match = pressureRe.match(pressureStr);

        if (match.hasMatch()) {
            cylinder.workPressure = match.captured(1).toDouble();
        }
    }

    if (attrs.hasAttribute("description")) {
        cylinder.description = attrs.value("description").toString();
    }

    if (attrs.hasAttribute("o2")) {
        QString o2Str = attrs.value("o2").toString();
        QRegularExpression o2Re("(\\d+\\.?\\d*)\\s*%");
        QRegularExpressionMatch match = o2Re.match(o2Str);

        if (match.hasMatch()) {
            cylinder.o2Percent = match.captured(1).toDouble();
        }
    }

    if (attrs.hasAttribute("he")) {
        QString heStr = attrs.value("he").toString();
        QRegularExpression heRe("(\\d+\\.?\\d*)\\s*%");
        QRegularExpressionMatch match = heRe.match(heStr);

        if (match.hasMatch()) {
            cylinder.hePercent = match.captured(1).toDouble();
        }
    }

    if (attrs.hasAttribute("start")) {
        QString startStr = attrs.value("start").toString();
        QRegularExpression startRe("(\\d+\\.?\\d*)\\s+bar");
        QRegularExpressionMatch match = startRe.match(startStr);

        if (match.hasMatch()) {
            cylinder.startPressure = match.captured(1).toDouble();
        }
    }

    if (attrs.hasAttribute("end")) {
        QString endStr = attrs.value("end").toString();
        QRegularExpression endRe("(\\d+\\.?\\d*)\\s+bar");
        QRegularExpressionMatch match = endRe.match(endStr);

        if (match.hasMatch()) {
            cylinder.endPressure = match.captured(1).toDouble();
        }
    }

    if (attrs.hasAttribute("use")) {
        const QString useStr = attrs.value("use").toString().toLower();
        if (useStr == QLatin1String("diluent")) {
            m_currentDiveHasCcrCues = true;
        }
    }

    double initialPressure = 0.0;
    if (cylinder.startPressure > 0.0) {
        initialPressure = cylinder.startPressure;
    } else if (cylinder.workPressure > 0.0) {
        initialPressure = cylinder.workPressure;
    }

    int cylinderIndex = dive->cylinderCount();
    dive->addCylinder(cylinder);

    if (initialPressure > 0.0) {
        m_initialCylinderPressures[cylinderIndex] = initialPressure;
    }

    qDebug() << "Parsed cylinder:" << cylinder.description
             << "Index:" << cylinderIndex
             << "Size:" << cylinder.size << "l"
             << "Gas mix:" << cylinder.o2Percent << "% O2"
             << (cylinder.hePercent > 0 ? QString::number(cylinder.hePercent) + "% He" : "")
             << "Initial pressure:" << initialPressure << "bar";

    while (!(xml.tokenType() == QXmlStreamReader::EndElement && xml.name() == QStringLiteral("cylinder"))) {
        xml.readNext();
        if (xml.atEnd()) break;
    }
}

void SubsurfaceParser::parseDiveComputerElement(QXmlStreamReader &xml, DiveData *dive, int &sampleCount)
{
    qDebug() << "Parsing divecomputer element";

    double lastTemperature = 0.0;
    double lastNDL = 0.0;
    double lastTTS = 0.0;

    QMap<int, double> lastPressures;
    QMap<int, double> lastPO2Sensors;
    for (int i = 0; i < dive->cylinderCount(); i++) {
        double initialPressure = m_initialCylinderPressures.value(i, 0.0);
        if (initialPressure > 0.0) {
            lastPressures[i] = initialPressure;
        }
    }

    while (!xml.atEnd()) {
        xml.readNext();

        if (xml.tokenType() == QXmlStreamReader::EndElement && xml.name() == QStringLiteral("divecomputer")) {
            break;
        }

        if (xml.tokenType() == QXmlStreamReader::StartElement) {
            QString elementName = xml.name().toString();

            if (elementName == "sample") {
                parseSampleElement(xml, dive, lastTemperature, lastNDL, lastTTS, lastPressures, lastPO2Sensors);
                sampleCount++;

                if (sampleCount % 10 == 0) {
                    qDebug() << "Parsed" << sampleCount << "samples";
                }
            } else if (elementName == "temperature") {
                QXmlStreamAttributes attrs = xml.attributes();
                if (attrs.hasAttribute("water")) {
                    QString tempStr = attrs.value("water").toString();
                    QRegularExpression tempRe("(\\d+\\.?\\d*)\\s+C");
                    QRegularExpressionMatch match = tempRe.match(tempStr);

                    if (match.hasMatch()) {
                        lastTemperature = match.captured(1).toDouble();
                    }
                }
                while (!(xml.tokenType() == QXmlStreamReader::EndElement && xml.name() == QStringLiteral("temperature"))) {
                    xml.readNext();
                    if (xml.atEnd()) break;
                }
            } else if (elementName == "event") {
                QXmlStreamAttributes eventAttrs = xml.attributes();

                if (eventAttrs.hasAttribute("name") && eventAttrs.value("name") == QStringLiteral("gaschange")) {
                    if (eventAttrs.hasAttribute("time") && eventAttrs.hasAttribute("cylinder")) {
                        QString timeStr = eventAttrs.value("time").toString();
                        double timestamp = 0.0;

                        QRegularExpression timeRe("(\\d+):(\\d+)\\s+min");
                        QRegularExpressionMatch match = timeRe.match(timeStr);

                        if (match.hasMatch()) {
                            int minutes = match.captured(1).toInt();
                            int seconds = match.captured(2).toInt();
                            timestamp = minutes * 60 + seconds;
                        } else {
                            bool ok;
                            timestamp = timeStr.toDouble(&ok);
                            if (!ok) timestamp = 0.0;
                        }

                        int cylinderIndex = eventAttrs.value("cylinder").toInt();

                        dive->addGasSwitch(timestamp, cylinderIndex);

                        qDebug() << "Parsed gas switch at time" << timestamp
                                 << "to cylinder" << cylinderIndex;
                    }
                }

                while (!(xml.tokenType() == QXmlStreamReader::EndElement && xml.name() == QStringLiteral("event"))) {
                    xml.readNext();
                    if (xml.atEnd()) break;
                }
            }
        }
    }

    std::sort(m_gasSwitches.begin(), m_gasSwitches.end(),
              [](const GasSwitch &a, const GasSwitch &b) {
                  return a.timestamp < b.timestamp;
              });

    qDebug() << "Finished parsing divecomputer element with" << sampleCount << "samples";
}

void SubsurfaceParser::parseSampleElement(QXmlStreamReader &xml,
                                          DiveData *dive,
                                          double &lastTemperature,
                                          double &lastNDL,
                                          double &lastTTS,
                                          QMap<int, double> &lastPressures,
                                          QMap<int, double> &lastPO2Sensors)
{
    QXmlStreamAttributes attrs = xml.attributes();

    DiveDataPoint point;
    bool hasData = false;
    bool inDeco = false;

    if (attrs.hasAttribute("time")) {
        QString timeStr = attrs.value("time").toString();
        QRegularExpression timeRe("(\\d+):(\\d+)\\s+min");
        QRegularExpressionMatch match = timeRe.match(timeStr);

        if (match.hasMatch()) {
            int minutes = match.captured(1).toInt();
            int seconds = match.captured(2).toInt();
            point.timestamp = minutes * 60 + seconds;
            hasData = true;
        } else {
            bool ok;
            double time = timeStr.toDouble(&ok);
            if (ok) {
                point.timestamp = time;
                hasData = true;
            }
        }
    }

    if (attrs.hasAttribute("depth")) {
        QString depthStr = attrs.value("depth").toString();
        QRegularExpression depthRe("(\\d+\\.?\\d*)\\s+m");
        QRegularExpressionMatch match = depthRe.match(depthStr);

        if (match.hasMatch()) {
            point.depth = match.captured(1).toDouble();
            hasData = true;
        } else {
            bool ok;
            double depth = depthStr.toDouble(&ok);
            if (ok) {
                point.depth = depth;
                hasData = true;
            }
        }
    }

    if (attrs.hasAttribute("temp")) {
        QString tempStr = attrs.value("temp").toString();
        QRegularExpression tempRe("(\\d+\\.?\\d*)\\s+C");
        QRegularExpressionMatch match = tempRe.match(tempStr);

        if (match.hasMatch()) {
            point.temperature = match.captured(1).toDouble();
            lastTemperature = point.temperature;
            hasData = true;
        } else {
            bool ok;
            double temp = tempStr.toDouble(&ok);
            if (ok) {
                point.temperature = temp;
                lastTemperature = temp;
                hasData = true;
            }
        }
    } else {
        if (lastTemperature > 0.0) {
            point.temperature = lastTemperature;
        }
    }

    if (attrs.hasAttribute("pressure")) {
        QString pressureStr = attrs.value("pressure").toString();
        QRegularExpression pressureRe("(\\d+\\.?\\d*)\\s+bar");
        QRegularExpressionMatch match = pressureRe.match(pressureStr);

        if (match.hasMatch()) {
            double pressure = match.captured(1).toDouble();
            point.addPressure(pressure, 0);
            hasData = true;
        } else {
            bool ok;
            double pressure = pressureStr.toDouble(&ok);
            if (ok) {
                point.addPressure(pressure, 0);
                hasData = true;
            }
        }
    }

    for (int i = 0; i < 10; i++) {
        QString pressureAttr = QString("pressure%1").arg(i);

        if (attrs.hasAttribute(pressureAttr)) {
            QString pressureStr = attrs.value(pressureAttr).toString();
            QRegularExpression pressureRe("(\\d+\\.?\\d*)\\s+bar");
            QRegularExpressionMatch match = pressureRe.match(pressureStr);

            if (match.hasMatch()) {
                double pressure = match.captured(1).toDouble();
                point.addPressure(pressure, i);
                lastPressures[i] = pressure;
                hasData = true;
            } else {
                bool ok;
                double pressure = pressureStr.toDouble(&ok);
                if (ok) {
                    point.addPressure(pressure, i);
                    lastPressures[i] = pressure;
                    hasData = true;
                }
            }
        }
    }

    for (int i = 1; i <= 4; i++) {
        QString sensorAttr = QString("sensor%1").arg(i);

        if (attrs.hasAttribute(sensorAttr)) {
            QString sensorStr = attrs.value(sensorAttr).toString();
            QRegularExpression sensorRe("(\\d+\\.?\\d*)\\s+bar");
            QRegularExpressionMatch match = sensorRe.match(sensorStr);

            if (match.hasMatch()) {
                double sensorValue = match.captured(1).toDouble();
                point.addPO2Sensor(sensorValue, i - 1);
                lastPO2Sensors[i - 1] = sensorValue;
                m_currentDiveHasCcrCues = true;
                hasData = true;
            } else {
                bool ok;
                double sensorValue = sensorStr.toDouble(&ok);
                if (ok) {
                    point.addPO2Sensor(sensorValue, i - 1);
                    lastPO2Sensors[i - 1] = sensorValue;
                    m_currentDiveHasCcrCues = true;
                    hasData = true;
                }
            }
        }
    }

    for (auto it = lastPO2Sensors.begin(); it != lastPO2Sensors.end(); ++it) {
        int sensorIndex = it.key();
        double lastValue = it.value();

        bool sensorSet = attrs.hasAttribute(QString("sensor%1").arg(sensorIndex + 1));

        if (!sensorSet && lastValue > 0.0) {
            point.addPO2Sensor(lastValue, sensorIndex);
        }
    }

    for (int i = 0; i < dive->cylinderCount(); i++) {
        bool pressureSet = false;
        if (i == 0) {
            pressureSet = attrs.hasAttribute("pressure") || attrs.hasAttribute("pressure0");
        } else {
            pressureSet = attrs.hasAttribute(QString("pressure%1").arg(i));
        }

        if (!pressureSet && lastPressures.contains(i)) {
            point.addPressure(lastPressures[i], i);
        }
    }

    if (attrs.hasAttribute("in_deco")) {
        QString decoStr = attrs.value("in_deco").toString();
        inDeco = (decoStr == "1" || decoStr.toLower() == "true");
    }

    if (attrs.hasAttribute("tts")) {
        QString ttsStr = attrs.value("tts").toString();
        QRegularExpression ttsRe("(\\d+):(\\d+)\\s+min");
        QRegularExpressionMatch match = ttsRe.match(ttsStr);

        if (match.hasMatch()) {
            int minutes = match.captured(1).toInt();
            int seconds = match.captured(2).toInt();
            point.tts = minutes + seconds / 60.0;
            lastTTS = point.tts;
            hasData = true;
        } else {
            bool ok;
            double tts = ttsStr.toDouble(&ok);
            if (ok) {
                point.tts = tts;
                lastTTS = tts;
                hasData = true;
            }
        }
    } else {
        if (lastTTS > 0.0) {
            point.tts = lastTTS;
        }
    }

    if (attrs.hasAttribute("ndl")) {
        QString ndlStr = attrs.value("ndl").toString();
        QRegularExpression ndlRe("(\\d+):(\\d+)\\s+min");
        QRegularExpressionMatch match = ndlRe.match(ndlStr);

        if (match.hasMatch()) {
            int minutes = match.captured(1).toInt();
            int seconds = match.captured(2).toInt();
            point.ndl = minutes + seconds / 60.0;
            lastNDL = point.ndl;
            hasData = true;
        } else {
            bool ok;
            double ndl = ndlStr.toDouble(&ok);
            if (ok) {
                point.ndl = ndl;
                lastNDL = ndl;
                hasData = true;
            }
        }
    } else {
        if (lastNDL >= 0.0) {
            point.ndl = lastNDL;
        }
    }

    if (inDeco) {
        point.ndl = 0.0;
        lastNDL = 0.0;

        if (point.tts <= 0.0 && lastTTS > 0.0) {
            point.tts = lastTTS;
        } else if (point.tts <= 0.0) {
            point.tts = 1.0;
            lastTTS = 1.0;
        }
    }

    if (attrs.hasAttribute("stopdepth")) {
        QString stopDepthStr = attrs.value("stopdepth").toString();
        QRegularExpression stopDepthRe("(\\d+\\.?\\d*)\\s+m");
        QRegularExpressionMatch match = stopDepthRe.match(stopDepthStr);

        if (match.hasMatch()) {
            point.ceiling = match.captured(1).toDouble();
            m_lastCeiling = point.ceiling;
            qDebug() << "Parsed stopdepth:" << point.ceiling << "m for time:" << point.timestamp;
        } else {
            bool ok;
            double stopDepth = stopDepthStr.toDouble(&ok);
            if (ok) {
                point.ceiling = stopDepth;
                m_lastCeiling = point.ceiling;
                qDebug() << "Parsed stopdepth (numeric):" << point.ceiling << "m for time:" << point.timestamp;
            }
        }
    } else {
        point.ceiling = m_lastCeiling;
    }

    if (hasData) {
        static int sampleCount = 0;
        sampleCount++;

        if (sampleCount <= 5 || sampleCount % 20 == 0) {
            qDebug() << "Sample #" << sampleCount
                     << "time=" << point.timestamp
                     << "depth=" << point.depth
                     << "temp=" << point.temperature << "(lastTemp=" << lastTemperature << ")"
                     << "ndl=" << point.ndl << "(lastNDL=" << lastNDL << ")"
                     << "tts=" << point.tts << "(lastTTS=" << lastTTS << ")"
                     << "in_deco=" << inDeco;
            for (int i = 0; i < point.tankCount(); i++) {
                qDebug() << "  Tank" << i << "pressure=" << point.getPressure(i)
                         << "(last=" << lastPressures.value(i, 0.0) << ")";
            }
            for (int i = 0; i < point.po2SensorCount(); i++) {
                qDebug() << "  Sensor" << (i + 1) << "PO2=" << point.getPO2Sensor(i)
                         << "(last=" << lastPO2Sensors.value(i, 0.0) << ")";
            }
            if (point.po2SensorCount() > 0) {
                qDebug() << "  Composite PO2=" << point.getCompositePO2();
            }
        }

        dive->addDataPoint(point);
    }

    while (!(xml.tokenType() == QXmlStreamReader::EndElement && xml.name() == QStringLiteral("sample"))) {
        xml.readNext();
        if (xml.atEnd()) break;
    }
}

void SubsurfaceParser::parseDiveSites(QXmlStreamReader &xml)
{
    qDebug() << "Parsing divesites element";

    while (!xml.atEnd()) {
        xml.readNext();

        if (xml.tokenType() == QXmlStreamReader::EndElement && xml.name() == QStringLiteral("divesites")) {
            break;
        }

        if (xml.tokenType() == QXmlStreamReader::StartElement && xml.name() == QStringLiteral("site")) {
            QXmlStreamAttributes attrs = xml.attributes();
            DiveSite site;

            if (attrs.hasAttribute("uuid")) {
                site.uuid = attrs.value("uuid").toString();
            }

            if (attrs.hasAttribute("name")) {
                site.name = attrs.value("name").toString();
            }

            if (attrs.hasAttribute("gps")) {
                site.gps = attrs.value("gps").toString();
            }

            if (attrs.hasAttribute("description")) {
                site.description = attrs.value("description").toString();
            }

            if (!site.uuid.isEmpty()) {
                m_diveSites[site.uuid] = site;
                qDebug() << "Parsed dive site:" << site.name << "UUID:" << site.uuid;
            }

            while (!(xml.tokenType() == QXmlStreamReader::EndElement && xml.name() == QStringLiteral("site"))) {
                xml.readNext();
                if (xml.atEnd()) break;
            }
        }
    }

    qDebug() << "Finished parsing divesites, found" << m_diveSites.size() << "sites";
}
