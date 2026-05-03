#include "include/core/format_parsers/uddf_parser.h"

#include <QDebug>
#include <QSet>
#include <QStringList>

#include <cmath>

#include "include/core/format_parsers/parse_utils.h"

UDDFParser::UDDFParser() = default;

bool UDDFParser::canParse(QFile &file) const
{
    const qint64 originalPos = file.pos();
    file.seek(0);

    QXmlStreamReader xml(&file);
    bool result = false;
    while (!xml.atEnd() && !xml.hasError()) {
        if (xml.readNext() == QXmlStreamReader::StartElement) {
            result = (xml.name() == QStringLiteral("uddf"));
            break;
        }
    }

    file.seek(originalPos);
    return result;
}

void UDDFParser::resetState()
{
    m_mixes.clear();
    m_diveSites.clear();
    m_mixToFirstTank.clear();
    m_currentDiveCcrSeen = false;
    m_currentDiveOcSeen = false;
}

QList<DiveData *> UDDFParser::parse(QFile &file, int specificDive, QString &errorOut)
{
    QList<DiveData *> result;

    file.seek(0);
    resetState();

    QXmlStreamReader xml(&file);
    bool foundSpecific = false;

    while (!xml.atEnd() && !xml.hasError()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }

        const auto name = xml.name();
        if (name == QStringLiteral("gasdefinitions")) {
            parseGasDefinitions(xml);
        } else if (name == QStringLiteral("divesite")) {
            parseDiveSiteContainer(xml);
        } else if (name == QStringLiteral("profiledata")) {
            parseProfileData(xml, result, specificDive, foundSpecific);
            if (specificDive != -1 && foundSpecific) {
                break;
            }
        }
    }

    if (xml.hasError()) {
        errorOut = QStringLiteral("UDDF parsing error: %1").arg(xml.errorString());
        qDebug() << "UDDF parsing error:" << xml.errorString();
        qDeleteAll(result);
        result.clear();
        return result;
    }

    qDebug() << "Finished parsing UDDF file, found" << result.size() << "dives";
    return result;
}

QList<QString> UDDFParser::listDives(QFile &file, QString &errorOut)
{
    QList<QString> result;

    file.seek(0);
    QXmlStreamReader xml(&file);

    QMap<QString, QString> siteNames; // id -> name (lightweight, only for listing)

    while (!xml.atEnd() && !xml.hasError()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }

        if (xml.name() == QStringLiteral("site")) {
            QString siteId = xml.attributes().value(QStringLiteral("id")).toString();
            QString siteName;
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.tokenType() == QXmlStreamReader::EndElement
                    && xml.name() == QStringLiteral("site")) {
                    break;
                }
                if (xml.tokenType() == QXmlStreamReader::StartElement
                    && xml.name() == QStringLiteral("name")
                    && siteName.isEmpty()) {
                    siteName = xml.readElementText();
                }
            }
            if (!siteId.isEmpty()) {
                siteNames.insert(siteId, siteName);
            }
        } else if (xml.name() == QStringLiteral("dive")) {
            QString diveNumber;
            QString datetime;
            QString siteRef;

            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.tokenType() == QXmlStreamReader::EndElement
                    && xml.name() == QStringLiteral("dive")) {
                    break;
                }
                if (xml.tokenType() == QXmlStreamReader::StartElement) {
                    if (xml.name() == QStringLiteral("divenumber")) {
                        diveNumber = xml.readElementText();
                    } else if (xml.name() == QStringLiteral("datetime")) {
                        datetime = xml.readElementText();
                    } else if (xml.name() == QStringLiteral("link") && siteRef.isEmpty()) {
                        const QString ref = xml.attributes().value(QStringLiteral("ref")).toString();
                        if (siteNames.contains(ref)) {
                            siteRef = ref;
                        }
                    } else if (xml.name() == QStringLiteral("samples")
                               || xml.name() == QStringLiteral("informationafterdive")) {
                        // We have everything we need — skip the rest of the dive cheaply.
                        xml.skipCurrentElement();
                    }
                }
            }

            QString entry;
            if (!diveNumber.isEmpty()) {
                entry = QStringLiteral("Dive #%1").arg(diveNumber);
            } else {
                entry = QStringLiteral("Dive");
            }
            if (!datetime.isEmpty()) {
                const QDateTime dt = parse_utils::parseISO8601(datetime);
                if (dt.isValid()) {
                    entry += " - " + dt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
                } else {
                    entry += " - " + datetime;
                }
            }
            if (!siteRef.isEmpty()) {
                const QString name = siteNames.value(siteRef);
                if (!name.isEmpty()) {
                    entry += " at " + name;
                }
            }
            result.append(entry);
        }
    }

    if (xml.hasError()) {
        errorOut = QStringLiteral("UDDF parsing error: %1").arg(xml.errorString());
        result.clear();
    }

    return result;
}

void UDDFParser::parseGasDefinitions(QXmlStreamReader &xml)
{
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::EndElement
            && xml.name() == QStringLiteral("gasdefinitions")) {
            break;
        }
        if (xml.tokenType() != QXmlStreamReader::StartElement) {
            continue;
        }
        if (xml.name() != QStringLiteral("mix")) {
            continue;
        }

        const QString mixId = xml.attributes().value(QStringLiteral("id")).toString();
        GasMix mix;

        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.tokenType() == QXmlStreamReader::EndElement
                && xml.name() == QStringLiteral("mix")) {
                break;
            }
            if (xml.tokenType() != QXmlStreamReader::StartElement) {
                continue;
            }
            if (xml.name() == QStringLiteral("name")) {
                mix.name = xml.readElementText();
            } else if (xml.name() == QStringLiteral("o2")) {
                const double frac = parse_utils::parseLocaleDouble(xml.readElementText());
                if (!std::isnan(frac)) {
                    mix.o2Percent = frac * 100.0;
                }
            } else if (xml.name() == QStringLiteral("he")) {
                const double frac = parse_utils::parseLocaleDouble(xml.readElementText());
                if (!std::isnan(frac)) {
                    mix.hePercent = frac * 100.0;
                }
            }
        }

        if (!mixId.isEmpty()) {
            m_mixes.insert(mixId, mix);
            qDebug() << "Parsed UDDF mix" << mixId
                     << "O2:" << mix.o2Percent << "% He:" << mix.hePercent << "%";
        }
    }
}

void UDDFParser::parseDiveSiteContainer(QXmlStreamReader &xml)
{
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::EndElement
            && xml.name() == QStringLiteral("divesite")) {
            break;
        }
        if (xml.tokenType() == QXmlStreamReader::StartElement
            && xml.name() == QStringLiteral("site")) {
            parseDiveSiteEntry(xml);
        }
    }
}

void UDDFParser::parseDiveSiteEntry(QXmlStreamReader &xml)
{
    DiveSite site;
    site.id = xml.attributes().value(QStringLiteral("id")).toString();

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::EndElement
            && xml.name() == QStringLiteral("site")) {
            break;
        }
        if (xml.tokenType() != QXmlStreamReader::StartElement) {
            continue;
        }
        if (xml.name() == QStringLiteral("name")) {
            site.name = xml.readElementText();
        } else if (xml.name() == QStringLiteral("geography")) {
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.tokenType() == QXmlStreamReader::EndElement
                    && xml.name() == QStringLiteral("geography")) {
                    break;
                }
                if (xml.tokenType() == QXmlStreamReader::StartElement
                    && xml.name() == QStringLiteral("location")) {
                    site.location = xml.readElementText();
                }
            }
        }
    }

    if (!site.id.isEmpty()) {
        m_diveSites.insert(site.id, site);
        qDebug() << "Parsed UDDF dive site" << site.id << ":" << site.name;
    }
}

void UDDFParser::parseProfileData(QXmlStreamReader &xml,
                                  QList<DiveData *> &out,
                                  int specificDive,
                                  bool &foundSpecific)
{
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::EndElement
            && xml.name() == QStringLiteral("profiledata")) {
            break;
        }
        if (xml.tokenType() != QXmlStreamReader::StartElement) {
            continue;
        }

        if (xml.name() == QStringLiteral("repetitiongroup")) {
            // Walk into the repetition group.
            continue;
        }

        if (xml.name() == QStringLiteral("dive")) {
            DiveData *dive = parseDiveElement(xml);
            if (!dive) {
                continue;
            }

            if (specificDive != -1 && dive->diveNumber() != specificDive) {
                delete dive;
                continue;
            }

            out.append(dive);

            if (specificDive != -1) {
                foundSpecific = true;
                return;
            }
        }
    }
}

DiveData *UDDFParser::parseDiveElement(QXmlStreamReader &xml)
{
    DiveData *dive = new DiveData();

    // Per-dive state reset.
    m_mixToFirstTank.clear();
    m_currentDiveCcrSeen = false;
    m_currentDiveOcSeen = false;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::EndElement
            && xml.name() == QStringLiteral("dive")) {
            break;
        }
        if (xml.tokenType() != QXmlStreamReader::StartElement) {
            continue;
        }

        const auto name = xml.name();
        if (name == QStringLiteral("informationbeforedive")) {
            parseInformationBeforeDive(xml, dive);
        } else if (name == QStringLiteral("tankdata")) {
            parseTankData(xml, dive);
        } else if (name == QStringLiteral("samples")) {
            parseSamples(xml, dive);
        } else {
            // Skip everything else — informationafterdive, applieddivedurationformula, etc.
            xml.skipCurrentElement();
        }
    }

    if (dive->diveName().isEmpty() && dive->diveNumber() > 0) {
        dive->setDiveName(QStringLiteral("Dive #%1").arg(dive->diveNumber()));
    } else if (dive->diveName().isEmpty()) {
        dive->setDiveName(QStringLiteral("Dive"));
    }

    // Empty-dive fallback: if we have cylinders but no waypoints (e.g. a
    // header-only UDDF), seed a single point at t=0 with each cylinder's
    // initial pressure so downstream rendering doesn't see a totally empty
    // profile. Mirrors the SSRF parser's identical safeguard.
    if (dive->allDataPoints().isEmpty() && dive->cylinderCount() > 0) {
        DiveDataPoint initialPoint;
        initialPoint.timestamp = 0.0;

        for (int i = 0; i < dive->cylinderCount(); i++) {
            const CylinderInfo &cyl = dive->cylinderInfo(i);
            double initialPressure = 0.0;
            if (cyl.startPressure > 0.0) {
                initialPressure = cyl.startPressure;
            } else if (cyl.workPressure > 0.0) {
                initialPressure = cyl.workPressure;
            }
            if (initialPressure > 0.0) {
                initialPoint.addPressure(initialPressure, i);
            }
        }

        dive->addDataPoint(initialPoint);
    }

    if (dive->diveMode() == DiveData::UnknownMode) {
        if (m_currentDiveCcrSeen) {
            dive->setDiveMode(DiveData::ClosedCircuit);
        } else if (m_currentDiveOcSeen) {
            dive->setDiveMode(DiveData::OpenCircuit);
        }
    }

    qDebug() << "Finished parsing UDDF dive" << dive->diveName()
             << "data points:" << dive->allDataPoints().size()
             << "cylinders:" << dive->cylinderCount()
             << "diveMode:" << dive->diveMode();
    return dive;
}

void UDDFParser::parseInformationBeforeDive(QXmlStreamReader &xml, DiveData *dive)
{
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::EndElement
            && xml.name() == QStringLiteral("informationbeforedive")) {
            break;
        }
        if (xml.tokenType() != QXmlStreamReader::StartElement) {
            continue;
        }

        const auto name = xml.name();
        if (name == QStringLiteral("divenumber")) {
            bool ok = false;
            const int n = xml.readElementText().toInt(&ok);
            if (ok) {
                dive->setDiveNumber(n);
            }
        } else if (name == QStringLiteral("datetime")) {
            const QDateTime dt = parse_utils::parseISO8601(xml.readElementText());
            if (dt.isValid()) {
                dive->setStartTime(dt);
            }
        } else if (name == QStringLiteral("link")) {
            const QString ref = xml.attributes().value(QStringLiteral("ref")).toString();
            if (m_diveSites.contains(ref)) {
                const DiveSite &site = m_diveSites.value(ref);
                dive->setDiveSiteId(ref);
                if (!site.name.isEmpty()) {
                    dive->setDiveSiteName(site.name);
                    if (dive->location().isEmpty()) {
                        dive->setLocation(site.name);
                    }
                }
            }
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }
}

void UDDFParser::parseTankData(QXmlStreamReader &xml, DiveData *dive)
{
    CylinderInfo cylinder;
    QString mixRef;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::EndElement
            && xml.name() == QStringLiteral("tankdata")) {
            break;
        }
        if (xml.tokenType() != QXmlStreamReader::StartElement) {
            continue;
        }

        const auto name = xml.name();
        if (name == QStringLiteral("link")) {
            mixRef = xml.attributes().value(QStringLiteral("ref")).toString();
            xml.skipCurrentElement();
        } else if (name == QStringLiteral("tankvolume")) {
            // UDDF: cubic meters at 1 atm — convert to liters.
            const double m3 = parse_utils::parseLocaleDouble(xml.readElementText());
            if (!std::isnan(m3)) {
                cylinder.size = m3 * 1000.0;
            }
        } else if (name == QStringLiteral("tankpressurebegin")) {
            const double pa = parse_utils::parseLocaleDouble(xml.readElementText());
            if (!std::isnan(pa)) {
                cylinder.startPressure = parse_utils::pascalToBar(pa);
            }
        } else if (name == QStringLiteral("tankpressureend")) {
            const double pa = parse_utils::parseLocaleDouble(xml.readElementText());
            if (!std::isnan(pa)) {
                cylinder.endPressure = parse_utils::pascalToBar(pa);
            }
        } else {
            xml.skipCurrentElement();
        }
    }

    if (!mixRef.isEmpty() && m_mixes.contains(mixRef)) {
        const GasMix &mix = m_mixes.value(mixRef);
        cylinder.o2Percent = mix.o2Percent;
        cylinder.hePercent = mix.hePercent;
        if (cylinder.description.isEmpty() && !mix.name.isEmpty()) {
            cylinder.description = mix.name;
        }
    }

    const int newIndex = dive->cylinderCount();
    dive->addCylinder(cylinder);

    if (!mixRef.isEmpty() && !m_mixToFirstTank.contains(mixRef)) {
        m_mixToFirstTank.insert(mixRef, newIndex);
    }

    qDebug() << "Parsed UDDF tank index" << newIndex
             << "mix:" << mixRef
             << "size:" << cylinder.size << "L"
             << "start:" << cylinder.startPressure << "bar"
             << "end:" << cylinder.endPressure << "bar";
}

void UDDFParser::parseSamples(QXmlStreamReader &xml, DiveData *dive)
{
    // UDDF waypoints record changes only — values not present in a waypoint
    // should be inherited from the previous one. We carry forward temperature,
    // NDL, TTS, ceiling, per-tank pressure, and per-sensor PO2 across
    // waypoints, mirroring the SSRF parser's logic.
    double lastTemperature = 0.0;
    double lastNDL = 0.0;
    double lastTTS = 0.0;
    double lastCeiling = 0.0;
    QMap<int, double> lastPressures;
    QMap<int, double> lastPO2Sensors;
    // Maps a <measuredpo2 ref="..."> identifier to its stable sensor index.
    // First time a ref is seen it is assigned the next free index; subsequent
    // waypoints reuse the same index even if sensors appear in a different
    // order or some are omitted.
    QMap<QString, int> po2SensorRefToIndex;

    // Pre-seed lastPressures from each cylinder's start (or work) pressure,
    // so the first waypoints carry sensible values even before any
    // <tankpressure> shows up in the stream. Mirrors the SSRF parser's
    // m_initialCylinderPressures behaviour.
    for (int i = 0; i < dive->cylinderCount(); i++) {
        const CylinderInfo &cyl = dive->cylinderInfo(i);
        double initialPressure = 0.0;
        if (cyl.startPressure > 0.0) {
            initialPressure = cyl.startPressure;
        } else if (cyl.workPressure > 0.0) {
            initialPressure = cyl.workPressure;
        }
        if (initialPressure > 0.0) {
            lastPressures[i] = initialPressure;
        }
    }

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::EndElement
            && xml.name() == QStringLiteral("samples")) {
            break;
        }
        if (xml.tokenType() == QXmlStreamReader::StartElement
            && xml.name() == QStringLiteral("waypoint")) {
            parseWaypoint(xml, dive, lastTemperature, lastNDL, lastTTS, lastCeiling,
                          lastPressures, lastPO2Sensors, po2SensorRefToIndex);
        }
    }
}

void UDDFParser::parseWaypoint(QXmlStreamReader &xml,
                               DiveData *dive,
                               double &lastTemperature,
                               double &lastNDL,
                               double &lastTTS,
                               double &lastCeiling,
                               QMap<int, double> &lastPressures,
                               QMap<int, double> &lastPO2Sensors,
                               QMap<QString, int> &po2SensorRefToIndex)
{
    DiveDataPoint point;
    bool hasTimestamp = false;

    // Inherit the last known temperature; <temperature> below overrides it
    // when the waypoint actually carries a fresh reading.
    if (lastTemperature > 0.0) {
        point.temperature = lastTemperature;
    }

    // Track which tanks / sensors were explicitly populated by this waypoint
    // so we know which ones to fill from the carry-forward maps at the end.
    QSet<int> tanksSetThisWaypoint;
    QSet<int> sensorsSetThisWaypoint;

    // Defer <switchmix> until we have the final timestamp — the schema does
    // not pin element order within a waypoint.
    QStringList pendingSwitchMixRefs;

    // <nodecotime> and <decostop> deco state. <decostop> can appear multiple
    // times per waypoint to describe the full ascent profile, so we accumulate
    // across the parse loop and only apply the result at the end.
    bool ndlSetThisWaypoint = false;
    double waypointNDL = 0.0;
    int mandatoryDecostopCount = 0;
    double waypointDecoCeiling = 0.0;        // metres, deepest mandatory stop
    double waypointTotalDecoDuration = 0.0;  // seconds, summed across mandatory stops

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::EndElement
            && xml.name() == QStringLiteral("waypoint")) {
            break;
        }
        if (xml.tokenType() != QXmlStreamReader::StartElement) {
            continue;
        }

        const auto name = xml.name();
        if (name == QStringLiteral("divetime")) {
            const double t = parse_utils::parseLocaleDouble(xml.readElementText());
            if (!std::isnan(t)) {
                point.timestamp = t;
                hasTimestamp = true;
            }
        } else if (name == QStringLiteral("depth")) {
            const double d = parse_utils::parseLocaleDouble(xml.readElementText());
            if (!std::isnan(d)) {
                point.depth = d;
            }
        } else if (name == QStringLiteral("temperature")) {
            const double k = parse_utils::parseLocaleDouble(xml.readElementText());
            if (!std::isnan(k)) {
                point.temperature = parse_utils::kelvinToCelsius(k);
                lastTemperature = point.temperature;
            }
        } else if (name == QStringLiteral("switchmix")) {
            pendingSwitchMixRefs.append(xml.attributes().value(QStringLiteral("ref")).toString());
            xml.skipCurrentElement();
        } else if (name == QStringLiteral("tankpressure")) {
            const QString ref = xml.attributes().value(QStringLiteral("ref")).toString();
            int tankIdx = ref.isEmpty() ? 0 : firstTankIndexForMix(ref);
            if (tankIdx < 0) {
                tankIdx = 0;
            }
            const double pa = parse_utils::parseLocaleDouble(xml.readElementText());
            if (!std::isnan(pa)) {
                const double bar = parse_utils::pascalToBar(pa);
                point.addPressure(bar, tankIdx);
                lastPressures[tankIdx] = bar;
                tanksSetThisWaypoint.insert(tankIdx);
            }
        } else if (name == QStringLiteral("measuredpo2")) {
            // <measuredpo2 ref="o2sensor_1">1.22e5</measuredpo2> — value is in
            // Pascal (1.22e5 Pa = 1.22 bar). The `ref` attribute identifies
            // which physical O2 sensor produced the reading; we map each
            // distinct ref to a stable sensor index that survives across
            // waypoints regardless of element ordering or omissions.
            const QString ref = xml.attributes().value(QStringLiteral("ref")).toString();
            const double pa = parse_utils::parseLocaleDouble(xml.readElementText());
            if (!std::isnan(pa)) {
                int sensorIndex;
                if (ref.isEmpty()) {
                    // Spec violation: ref is supposed to be mandatory. Fall
                    // back to a positional index so we don't drop the reading.
                    sensorIndex = point.po2SensorCount();
                } else if (po2SensorRefToIndex.contains(ref)) {
                    sensorIndex = po2SensorRefToIndex.value(ref);
                } else {
                    sensorIndex = po2SensorRefToIndex.size();
                    po2SensorRefToIndex.insert(ref, sensorIndex);
                }
                const double bar = parse_utils::pascalToBar(pa);
                point.addPO2Sensor(bar, sensorIndex);
                lastPO2Sensors[sensorIndex] = bar;
                sensorsSetThisWaypoint.insert(sensorIndex);
                m_currentDiveCcrSeen = true;
            }
        } else if (name == QStringLiteral("nodecotime")) {
            // <nodecotime> is in seconds; DiveDataPoint::ndl is in minutes.
            const double seconds = parse_utils::parseLocaleDouble(xml.readElementText());
            if (!std::isnan(seconds) && seconds >= 0.0) {
                waypointNDL = seconds / 60.0;
                ndlSetThisWaypoint = true;
            }
        } else if (name == QStringLiteral("decostop")) {
            // Schema: <decostop kind="mandatory|safety" decodepth="..." duration="..."/>.
            // Multiple decostops can appear per waypoint to describe the full
            // ascent: deepest decodepth = current ceiling, sum of durations = TTS.
            // Only mandatory stops contribute to deco state — safety stops are
            // voluntary and don't pin a ceiling.
            const auto attrs = xml.attributes();
            const QString kind = attrs.value(QStringLiteral("kind")).toString().toLower();
            if (kind.isEmpty() || kind == QLatin1String("mandatory")) {
                const double decodepth = parse_utils::parseLocaleDouble(
                    attrs.value(QStringLiteral("decodepth")));
                const double duration = parse_utils::parseLocaleDouble(
                    attrs.value(QStringLiteral("duration")));
                if (!std::isnan(decodepth) && decodepth > waypointDecoCeiling) {
                    waypointDecoCeiling = decodepth;
                }
                if (!std::isnan(duration) && duration > 0.0) {
                    waypointTotalDecoDuration += duration;
                }
                mandatoryDecostopCount++;
            }
            xml.skipCurrentElement();
        } else if (name == QStringLiteral("divemode")) {
            const QString kind = xml.attributes().value(QStringLiteral("kind")).toString().toLower();
            if (kind == QLatin1String("closedcircuit")) {
                if (dive->diveMode() == DiveData::UnknownMode) {
                    dive->setDiveMode(DiveData::ClosedCircuit);
                }
                m_currentDiveCcrSeen = true;
            } else if (kind == QLatin1String("opencircuit")) {
                if (dive->diveMode() == DiveData::UnknownMode) {
                    dive->setDiveMode(DiveData::OpenCircuit);
                }
                m_currentDiveOcSeen = true;
            }
            xml.skipCurrentElement();
        } else {
            xml.skipCurrentElement();
        }
    }

    // Resolve NDL / TTS / ceiling, mirroring the SSRF parser's behaviour:
    //  - Mandatory <decostop>s present → in deco. NDL = 0; ceiling = deepest
    //    decodepth seen; TTS = sum of durations (or fallback to lastTTS, or
    //    1.0 minimum, matching SSRF).
    //  - Else if <nodecotime> present → NDL = nodecotime; carry TTS / ceiling
    //    forward from previous waypoints.
    //  - Else → carry NDL, TTS, ceiling forward.
    if (mandatoryDecostopCount > 0) {
        point.ndl = 0.0;
        lastNDL = 0.0;

        if (waypointTotalDecoDuration > 0.0) {
            point.tts = waypointTotalDecoDuration / 60.0;
            lastTTS = point.tts;
        } else if (lastTTS > 0.0) {
            point.tts = lastTTS;
        } else {
            point.tts = 1.0;
            lastTTS = 1.0;
        }

        point.ceiling = waypointDecoCeiling;
        lastCeiling = point.ceiling;
    } else if (ndlSetThisWaypoint) {
        point.ndl = waypointNDL;
        lastNDL = point.ndl;
        if (lastTTS > 0.0) {
            point.tts = lastTTS;
        }
        point.ceiling = lastCeiling;
    } else {
        if (lastNDL >= 0.0) {
            point.ndl = lastNDL;
        }
        if (lastTTS > 0.0) {
            point.tts = lastTTS;
        }
        point.ceiling = lastCeiling;
    }

    // Apply pending gas switches now that the timestamp is final.
    for (const QString &ref : pendingSwitchMixRefs) {
        const int tankIdx = firstTankIndexForMix(ref);
        if (tankIdx >= 0) {
            dive->addGasSwitch(point.timestamp, tankIdx);
        } else {
            qDebug() << "UDDF switchmix references unknown mix:" << ref;
        }
    }

    // Carry forward any PO2 sensor values that this waypoint did not override.
    for (auto it = lastPO2Sensors.cbegin(); it != lastPO2Sensors.cend(); ++it) {
        const int sensorIndex = it.key();
        const double lastValue = it.value();
        if (!sensorsSetThisWaypoint.contains(sensorIndex) && lastValue > 0.0) {
            point.addPO2Sensor(lastValue, sensorIndex);
        }
    }

    // Apply last known pressures for ALL declared cylinders, so every data
    // point has a pressure value for every cylinder — same invariant as SSRF.
    for (int i = 0; i < dive->cylinderCount(); i++) {
        if (!tanksSetThisWaypoint.contains(i) && lastPressures.contains(i)) {
            point.addPressure(lastPressures.value(i), i);
        }
    }

    if (hasTimestamp) {
        dive->addDataPoint(point);
    }
}

int UDDFParser::firstTankIndexForMix(const QString &mixId) const
{
    if (mixId.isEmpty()) {
        return -1;
    }
    return m_mixToFirstTank.value(mixId, -1);
}
