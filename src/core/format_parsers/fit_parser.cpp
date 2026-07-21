#include "include/core/format_parsers/fit_parser.h"

#include <QDateTime>
#include <QDebug>
#include <QTimeZone>

#include <algorithm>

#include "include/core/units.h"

namespace {

// Seconds between the Unix epoch and the FIT epoch (1989-12-31T00:00:00 UTC).
constexpr qint64 FIT_EPOCH_OFFSET = 631065600;

// FIT global message numbers (protocol facts, dive-relevant subset).
enum FitGlobalMessage : quint16 {
    MsgSport = 12,
    MsgSession = 18,
    MsgRecord = 20,
    MsgEvent = 21,
    MsgActivity = 34,
    MsgSensorProfile = 147,
    MsgTimestampCorrelation = 162,
    MsgDiveSettings = 258,
    MsgDiveGas = 259,
    MsgDiveSummary = 268,
    MsgTankUpdate = 319,
    MsgTankSummary = 323,
};

// The timestamp field shared by all messages, and the index field used by
// per-slot messages such as dive_gas.
constexpr quint8 FieldTimestamp = 253;
constexpr quint8 FieldMessageIndex = 254;

bool isDiveSubSport(int subSport)
{
    // 53 single-gas, 54 multi-gas, 55 gauge, 56 apnea, 57 apnea hunt, 63 CCR
    return (subSport >= 53 && subSport <= 57) || subSport == 63;
}

double semicirclesToDegrees(double semicircles)
{
    return semicircles * (180.0 / 2147483648.0);
}

} // namespace

FitParser::FitParser() = default;

bool FitParser::canParse(QFile &file) const
{
    return FitDecoder::sniff(&file);
}

bool FitParser::decodeFile(QFile &file, FitDecoder &decoder, QString &errorOut) const
{
    if (!decoder.decode(&file, errorOut)) {
        // Salvage: a truncated file (e.g. a crashed watch) is still worth
        // importing if a usable number of dive samples were decoded.
        int depthSamples = 0;
        for (const FitMessage &message : decoder.messages()) {
            if (message.globalId == MsgRecord && message.has(92)) {
                depthSamples++;
            }
        }
        if (depthSamples < 2) {
            return false;
        }
        qWarning() << "FIT decode error, importing" << depthSamples
                   << "salvaged samples anyway:" << errorOut;
        errorOut.clear();
    }
    return true;
}

FitParser::Metadata FitParser::collectMetadata(const QList<FitMessage> &messages) const
{
    Metadata meta;

    QMap<int, CylinderInfo> gasesByIndex;
    QMap<int, bool> gasIsDiluent;
    QMap<quint32, QPair<double, double>> tankPressures; // sensor -> start/end bar
    QVector<double> tankVolumes;                        // liters, parallel to tankSensors
    bool sawFirstRecord = false;
    quint32 firstRecordTime = 0;

    for (const FitMessage &message : messages) {
        switch (message.globalId) {
            case MsgSport:
                if (message.has(1)) {
                    meta.subSport = static_cast<int>(message.value(1));
                }
                break;

            case MsgSession:
                if (message.has(2)) {
                    meta.hasStart = true;
                    meta.startFit = static_cast<quint32>(message.value(2));
                }
                if (message.has(3) && message.has(4)) {
                    meta.hasPosition = true;
                    meta.latitude = semicirclesToDegrees(message.value(3));
                    meta.longitude = semicirclesToDegrees(message.value(4));
                }
                if (meta.subSport < 0 && message.has(6)) {
                    meta.subSport = static_cast<int>(message.value(6));
                }
                break;

            case MsgActivity:
            case MsgTimestampCorrelation:
                // Field 5 (activity) / field 3 (timestamp correlation) hold the
                // device's local wall-clock time for the message's timestamp.
                {
                    const quint8 localField = (message.globalId == MsgActivity) ? 5 : 3;
                    if (message.has(localField) && message.has(FieldTimestamp)) {
                        meta.timeOffset = static_cast<qint64>(message.value(localField))
                                          - static_cast<qint64>(message.value(FieldTimestamp));
                    }
                }
                break;

            case MsgDiveSettings:
                meta.sawDiveData = true;
                if (message.has(23)) {
                    meta.setpointLowBar = message.value(23) / 100.0; // centibar
                }
                if (message.has(26)) {
                    meta.setpointHighBar = message.value(26) / 100.0;
                }
                break;

            case MsgDiveGas: {
                meta.sawDiveData = true;
                const int gasIndex = static_cast<int>(message.value(FieldMessageIndex, 0));
                const int status = static_cast<int>(message.value(2, 1)); // 0 disabled, 1 enabled, 2 backup
                if (status == 0) {
                    break;
                }
                CylinderInfo cylinder;
                cylinder.o2Percent = message.value(1, 21.0);
                cylinder.hePercent = message.value(0, 0.0);
                cylinder.description = Units::formatGasMix(cylinder.o2Percent, cylinder.hePercent);
                gasesByIndex.insert(gasIndex, cylinder);
                gasIsDiluent.insert(gasIndex, static_cast<int>(message.value(3, 0)) == 1);
                break;
            }

            case MsgSensorProfile: {
                // Sensor type 28 is a tank pressure pod
                if (static_cast<int>(message.value(52, -1)) != 28) {
                    break;
                }
                meta.tankSensors.append(static_cast<quint32>(message.value(0, 0)));
                double volumeLiters = 0.0;
                if (message.has(77)) {
                    const double rawVolume = message.value(77) / 10.0;
                    const int units = static_cast<int>(message.value(74, 2)); // 0 PSI, 2 Bar
                    // Volume is L*10 for Bar pods, CuFt*10 for PSI pods
                    volumeLiters = (units == 0) ? rawVolume * 28.3168 : rawVolume;
                }
                tankVolumes.append(volumeLiters);
                break;
            }

            case MsgTankSummary:
                if (message.has(0)) {
                    tankPressures.insert(static_cast<quint32>(message.value(0)),
                                         qMakePair(message.value(1, 0.0) / 100.0,
                                                   message.value(2, 0.0) / 100.0));
                }
                break;

            case MsgDiveSummary:
                meta.sawDiveData = true;
                if (message.has(10)) {
                    meta.diveNumber = static_cast<int>(message.value(10));
                }
                if (message.has(2)) {
                    meta.meanDepth = message.value(2) / 1000.0; // mm -> m
                }
                break;

            case MsgRecord:
                if (!sawFirstRecord && message.has(FieldTimestamp)) {
                    sawFirstRecord = true;
                    firstRecordTime = static_cast<quint32>(message.value(FieldTimestamp));
                }
                if (message.has(92)) {
                    meta.sawDiveData = true;
                }
                break;

            default:
                break;
        }
    }

    if (!meta.hasStart && sawFirstRecord) {
        meta.hasStart = true;
        meta.startFit = firstRecordTime;
    }

    // Cylinders in gas-slot order; the FIT gas index maps onto the cylinder index
    for (auto it = gasesByIndex.constBegin(); it != gasesByIndex.constEnd(); ++it) {
        CylinderInfo cylinder = it.value();
        cylinder.index = meta.cylinders.size();
        if (gasIsDiluent.value(it.key(), false)) {
            cylinder.description += QStringLiteral(" (diluent)");
        }
        meta.gasIndexToCylinder.insert(it.key(), cylinder.index);
        meta.cylinders.append(cylinder);
    }

    // Tank pods map onto cylinders by position (FIT has no gas <-> pod link);
    // pad with default air cylinders if there are more pods than gases.
    for (int channel = 0; channel < meta.tankSensors.size(); ++channel) {
        if (channel >= meta.cylinders.size()) {
            CylinderInfo cylinder;
            cylinder.index = meta.cylinders.size();
            cylinder.description = Units::formatGasMix(cylinder.o2Percent, cylinder.hePercent);
            meta.cylinders.append(cylinder);
        }
        if (tankVolumes.value(channel, 0.0) > 0.0) {
            meta.cylinders[channel].size = tankVolumes.at(channel);
        }
        const auto pressures = tankPressures.constFind(meta.tankSensors.at(channel));
        if (pressures != tankPressures.constEnd()) {
            meta.cylinders[channel].startPressure = pressures->first;
            meta.cylinders[channel].endPressure = pressures->second;
        }
    }

    return meta;
}

bool FitParser::isDive(const Metadata &meta, QString &errorOut) const
{
    if (meta.subSport >= 0 && !isDiveSubSport(meta.subSport)) {
        errorOut = QStringLiteral("FIT file does not contain a dive activity");
        return false;
    }
    if (meta.subSport < 0 && !meta.sawDiveData) {
        errorOut = QStringLiteral("FIT file does not contain a dive activity");
        return false;
    }
    if (!meta.hasStart) {
        errorOut = QStringLiteral("FIT file contains no dive samples");
        return false;
    }
    return true;
}

DiveData *FitParser::buildDive(const QList<FitMessage> &messages, const Metadata &meta) const
{
    DiveData *dive = new DiveData(nullptr);

    const bool isCcr = (meta.subSport == 63);
    dive->setDiveMode(isCcr ? DiveData::ClosedCircuit : DiveData::OpenCircuit);
    dive->setStartTime(QDateTime::fromSecsSinceEpoch(
        FIT_EPOCH_OFFSET + meta.startFit + meta.timeOffset, QTimeZone::utc()));
    dive->setDiveNumber(meta.diveNumber);
    dive->setDiveName(meta.diveNumber > 0 ? QStringLiteral("Dive #%1").arg(meta.diveNumber)
                                          : QStringLiteral("Garmin Dive"));
    if (meta.meanDepth >= 0.0) {
        dive->setMeanDepth(meta.meanDepth);
    }
    if (meta.hasPosition && (meta.latitude != 0.0 || meta.longitude != 0.0)) {
        dive->setLocation(QStringLiteral("%1, %2")
                              .arg(QString::number(meta.latitude, 'f', 6),
                                   QString::number(meta.longitude, 'f', 6)));
    }
    for (const CylinderInfo &cylinder : meta.cylinders) {
        dive->addCylinder(cylinder);
    }

    // Carry-forward sample state: dive computers only record values when
    // they change, so missing fields reuse the previous sample's value.
    double lastDepth = 0.0;
    double lastTemperature = 0.0;
    double lastNDL = 0.0;
    double lastTTS = 0.0;
    double lastCeiling = 0.0;
    double lastCNS = -1.0;
    QMap<int, double> lastPressures; // pressure channel -> bar
    QVector<quint32> tankSensors = meta.tankSensors;
    double currentO2 =
        meta.cylinders.isEmpty() ? 21.0 : meta.cylinders.first().o2Percent;
    // The initial CCR setpoint at the start of the dive is the low setpoint
    double currentSetpoint = meta.setpointLowBar;

    QVector<DiveDataPoint> points;

    for (const FitMessage &message : messages) {
        switch (message.globalId) {
            case MsgTankUpdate: {
                if (!message.has(0) || !message.has(1)) {
                    break;
                }
                const quint32 sensor = static_cast<quint32>(message.value(0));
                int channel = tankSensors.indexOf(sensor);
                if (channel < 0) {
                    // Unpaired pod seen mid-dive: give it the next free channel
                    channel = tankSensors.size();
                    tankSensors.append(sensor);
                }
                lastPressures[channel] = message.value(1) / 100.0; // bar * 100
                break;
            }

            case MsgEvent: {
                const int event = static_cast<int>(message.value(0, -1));
                const int data = static_cast<int>(message.value(3, 0));
                if (event == 57) {
                    // Gas switch; data is the FIT gas slot index
                    const int cylinderIndex = meta.gasIndexToCylinder.value(data, 0);
                    double relTime = 0.0;
                    if (message.has(FieldTimestamp)) {
                        const quint32 timestamp =
                            static_cast<quint32>(message.value(FieldTimestamp));
                        // Clamp pre-dive-start switch events to t=0 so the
                        // starting gas is reflected from the first sample
                        if (timestamp > meta.startFit) {
                            relTime = timestamp - meta.startFit;
                        }
                    }
                    dive->addGasSwitch(relTime, cylinderIndex);
                    if (cylinderIndex < meta.cylinders.size()) {
                        currentO2 = meta.cylinders.at(cylinderIndex).o2Percent;
                    }
                } else if (event == 56 && data >= 24 && data <= 27) {
                    // Automatic/manual switch to low (24/26) or high (25/27) setpoint
                    currentSetpoint =
                        (data == 24 || data == 26) ? meta.setpointLowBar : meta.setpointHighBar;
                }
                break;
            }

            case MsgRecord: {
                if (!message.has(FieldTimestamp)) {
                    break;
                }
                const quint32 timestamp = static_cast<quint32>(message.value(FieldTimestamp));
                if (timestamp < meta.startFit) {
                    break; // pre-dive surface samples
                }

                DiveDataPoint point;
                point.timestamp = static_cast<double>(timestamp - meta.startFit);

                if (message.has(92)) {
                    lastDepth = message.value(92) / 1000.0; // mm -> m
                }
                point.depth = lastDepth;

                if (message.has(13)) {
                    lastTemperature = message.value(13); // degrees C
                }
                point.temperature = lastTemperature;

                if (message.has(96)) {
                    lastNDL = message.value(96) / 60.0; // s -> min
                }
                point.ndl = lastNDL;

                if (message.has(95)) {
                    lastTTS = message.value(95) / 60.0; // s -> min
                }
                point.tts = lastTTS;

                if (message.has(93)) {
                    lastCeiling = message.value(93) / 1000.0; // next stop depth, mm -> m
                }
                point.ceiling = lastCeiling;

                if (message.has(97)) {
                    lastCNS = message.value(97); // percent
                }
                point.cns = lastCNS;

                point.o2percent = currentO2;

                for (auto it = lastPressures.constBegin(); it != lastPressures.constEnd(); ++it) {
                    point.addPressure(it.value(), it.key());
                }

                if (isCcr && currentSetpoint > 0.0) {
                    // The Descent records the active setpoint, not sensor
                    // readings; expose it through PO2 channel 0
                    point.addPO2Sensor(currentSetpoint, 0);
                }

                if (!points.isEmpty() && points.last().timestamp == point.timestamp) {
                    points.last() = point; // compressed-timestamp duplicate: last wins
                } else {
                    points.append(point);
                }
                break;
            }

            default:
                break;
        }
    }

    for (const DiveDataPoint &point : points) {
        dive->addDataPoint(point);
    }

    return dive;
}

QList<DiveData *> FitParser::parse(QFile &file, int specificDive, QString &errorOut)
{
    QList<DiveData *> result;

    FitDecoder decoder;
    if (!decodeFile(file, decoder, errorOut)) {
        return result;
    }

    const Metadata meta = collectMetadata(decoder.messages());
    if (!isDive(meta, errorOut)) {
        return result;
    }

    DiveData *dive = buildDive(decoder.messages(), meta);
    if (dive->allDataPoints().isEmpty()) {
        delete dive;
        errorOut = QStringLiteral("FIT file contains no dive samples");
        return result;
    }

    // A FIT activity file holds a single dive; honor the specific-dive
    // request by number the way the other parsers do.
    if (specificDive >= 0 && dive->diveNumber() != specificDive) {
        delete dive;
        return result;
    }

    result.append(dive);
    return result;
}

QList<QString> FitParser::listDives(QFile &file, QString &errorOut)
{
    QList<QString> result;

    FitDecoder decoder;
    if (!decodeFile(file, decoder, errorOut)) {
        return result;
    }

    const Metadata meta = collectMetadata(decoder.messages());
    if (!isDive(meta, errorOut)) {
        return result;
    }

    const QDateTime start = QDateTime::fromSecsSinceEpoch(
        FIT_EPOCH_OFFSET + meta.startFit + meta.timeOffset, QTimeZone::utc());
    QString entry = QStringLiteral("Dive #%1 - %2")
                        .arg(meta.diveNumber)
                        .arg(start.toString(QStringLiteral("yyyy-MM-dd hh:mm")));
    if (meta.hasPosition && (meta.latitude != 0.0 || meta.longitude != 0.0)) {
        entry += QStringLiteral(" at %1, %2")
                     .arg(QString::number(meta.latitude, 'f', 4),
                          QString::number(meta.longitude, 'f', 4));
    }
    result.append(entry);
    return result;
}
