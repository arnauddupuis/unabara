#ifndef FIT_DECODER_H
#define FIT_DECODER_H

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QVector>

class QIODevice;

// One decoded FIT data message. Fields are keyed by the field definition
// number from the wire format; each holds one or more decoded elements
// (arrays keep their invalid elements as NaN, invalid scalars are omitted).
struct FitMessage {
    quint16 globalId = 0;
    QMap<quint8, QVector<double>> fields;

    bool has(quint8 fieldNum) const { return fields.contains(fieldNum); }
    double value(quint8 fieldNum, double fallback = 0.0) const
    {
        auto it = fields.constFind(fieldNum);
        return (it != fields.constEnd() && !it->isEmpty()) ? it->first() : fallback;
    }
};

// Wire-level decoder for the FIT container format (clean-room implementation).
// Turns the byte stream into a flat list of FitMessage; knows nothing about
// dive semantics — that lives in FitParser.
class FitDecoder
{
public:
    // Decodes the device's full contents (from position 0). Returns false on a
    // fatal structural error; messages() still holds everything decoded before
    // the failure so callers can salvage truncated files.
    bool decode(QIODevice *device, QString &errorOut);

    const QList<FitMessage> &messages() const { return m_messages; }

    // Cheap format sniff: bytes 8-11 are the ASCII string ".FIT".
    // Restores the device's seek position.
    static bool sniff(QIODevice *device);

    static quint16 crc16(const uchar *data, qint64 length);

private:
    struct FieldDef {
        quint8 fieldNum = 0;
        quint8 size = 0;
        quint8 baseType = 0;
    };

    struct LocalDef {
        bool valid = false;
        quint16 globalId = 0;
        bool bigEndian = false;
        QVector<FieldDef> fields;
        int devDataBytes = 0; // developer fields: decoded for size, skipped as data
    };

    bool decodeDefinition(const uchar *&p, const uchar *end, quint8 recordHeader,
                          QString &errorOut);
    bool decodeData(const uchar *&p, const uchar *end, const LocalDef &def,
                    QString &errorOut);

    LocalDef m_locals[16];
    quint32 m_lastTimestamp = 0; // for compressed-timestamp record headers
    QList<FitMessage> m_messages;
};

#endif // FIT_DECODER_H
