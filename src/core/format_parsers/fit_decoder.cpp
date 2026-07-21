#include "include/core/format_parsers/fit_decoder.h"

#include <QDebug>
#include <QIODevice>

#include <cstring>

namespace {

// FIT base type numbers (low 5 bits of the base type byte; bit 7 flags
// multi-byte types that honor the definition's architecture).
enum FitBaseType {
    BaseEnum = 0,
    BaseSInt8 = 1,
    BaseUInt8 = 2,
    BaseSInt16 = 3,
    BaseUInt16 = 4,
    BaseSInt32 = 5,
    BaseUInt32 = 6,
    BaseString = 7,
    BaseFloat32 = 8,
    BaseFloat64 = 9,
    BaseUInt8z = 10,
    BaseUInt16z = 11,
    BaseUInt32z = 12,
    BaseByte = 13,
    BaseSInt64 = 14,
    BaseUInt64 = 15,
    BaseUInt64z = 16,
};

int baseTypeSize(quint8 baseNum)
{
    switch (baseNum) {
        case BaseEnum:
        case BaseSInt8:
        case BaseUInt8:
        case BaseString:
        case BaseUInt8z:
        case BaseByte:
            return 1;
        case BaseSInt16:
        case BaseUInt16:
        case BaseUInt16z:
            return 2;
        case BaseSInt32:
        case BaseUInt32:
        case BaseFloat32:
        case BaseUInt32z:
            return 4;
        case BaseFloat64:
        case BaseSInt64:
        case BaseUInt64:
        case BaseUInt64z:
            return 8;
        default:
            return 0;
    }
}

quint64 readUInt(const uchar *p, int size, bool bigEndian)
{
    quint64 value = 0;
    if (bigEndian) {
        for (int i = 0; i < size; ++i) {
            value = (value << 8) | p[i];
        }
    } else {
        for (int i = size - 1; i >= 0; --i) {
            value = (value << 8) | p[i];
        }
    }
    return value;
}

// Decodes one element; returns false if the element holds the base type's
// "invalid" sentinel (field not present in this message).
bool decodeElement(const uchar *p, quint8 baseNum, bool bigEndian, double &out)
{
    const int size = baseTypeSize(baseNum);
    const quint64 raw = readUInt(p, size, bigEndian);

    switch (baseNum) {
        case BaseEnum:
        case BaseUInt8:
        case BaseByte:
            if (raw == 0xFF) return false;
            out = static_cast<double>(raw);
            return true;
        case BaseUInt16:
            if (raw == 0xFFFF) return false;
            out = static_cast<double>(raw);
            return true;
        case BaseUInt32:
            if (raw == 0xFFFFFFFFULL) return false;
            out = static_cast<double>(raw);
            return true;
        case BaseUInt64:
            if (raw == ~0ULL) return false;
            out = static_cast<double>(raw);
            return true;
        case BaseUInt8z:
        case BaseUInt16z:
        case BaseUInt32z:
        case BaseUInt64z:
            if (raw == 0) return false;
            out = static_cast<double>(raw);
            return true;
        case BaseSInt8:
            if (raw == 0x7F) return false;
            out = static_cast<double>(static_cast<qint8>(raw));
            return true;
        case BaseSInt16:
            if (raw == 0x7FFF) return false;
            out = static_cast<double>(static_cast<qint16>(raw));
            return true;
        case BaseSInt32:
            if (raw == 0x7FFFFFFFULL) return false;
            out = static_cast<double>(static_cast<qint32>(raw));
            return true;
        case BaseSInt64:
            if (raw == 0x7FFFFFFFFFFFFFFFULL) return false;
            out = static_cast<double>(static_cast<qint64>(raw));
            return true;
        case BaseFloat32: {
            if (raw == 0xFFFFFFFFULL) return false;
            float f;
            quint32 bits = static_cast<quint32>(raw);
            std::memcpy(&f, &bits, sizeof(f));
            out = static_cast<double>(f);
            return true;
        }
        case BaseFloat64: {
            if (raw == ~0ULL) return false;
            double d;
            std::memcpy(&d, &raw, sizeof(d));
            out = d;
            return true;
        }
        default:
            return false;
    }
}

} // namespace

quint16 FitDecoder::crc16(const uchar *data, qint64 length)
{
    static const quint16 table[16] = {
        0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
        0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400,
    };

    quint16 crc = 0;
    for (qint64 i = 0; i < length; ++i) {
        quint16 tmp = table[crc & 0xF];
        crc = (crc >> 4) & 0x0FFF;
        crc = crc ^ tmp ^ table[data[i] & 0xF];

        tmp = table[crc & 0xF];
        crc = (crc >> 4) & 0x0FFF;
        crc = crc ^ tmp ^ table[(data[i] >> 4) & 0xF];
    }
    return crc;
}

bool FitDecoder::sniff(QIODevice *device)
{
    const qint64 originalPos = device->pos();
    device->seek(0);

    char header[12];
    const bool ok = device->read(header, sizeof(header)) == sizeof(header)
                    && std::memcmp(header + 8, ".FIT", 4) == 0;

    device->seek(originalPos);
    return ok;
}

bool FitDecoder::decode(QIODevice *device, QString &errorOut)
{
    m_messages.clear();
    for (LocalDef &local : m_locals) {
        local = LocalDef();
    }
    m_lastTimestamp = 0;

    device->seek(0);
    const QByteArray content = device->readAll();
    const uchar *base = reinterpret_cast<const uchar *>(content.constData());
    const qint64 fileLen = content.size();

    if (fileLen < 14) {
        errorOut = QStringLiteral("File too small to be a FIT file (%1 bytes)").arg(fileLen);
        return false;
    }

    const quint8 headerSize = base[0];
    const quint32 dataSize = static_cast<quint32>(readUInt(base + 4, 4, false));
    if (headerSize < 12 || headerSize > fileLen || std::memcmp(base + 8, ".FIT", 4) != 0) {
        errorOut = QStringLiteral("Invalid FIT file header");
        return false;
    }
    if (static_cast<qint64>(headerSize) + dataSize + 2 > fileLen) {
        // Truncated file: decode what is there, the record loop will stop at
        // the actual end of data. Warn but keep going (salvage).
        qWarning() << "FIT file shorter than header declares:" << fileLen << "bytes,"
                   << "expected" << (headerSize + dataSize + 2);
    } else {
        const quint16 expected =
            static_cast<quint16>(readUInt(base + headerSize + dataSize, 2, false));
        if (crc16(base, headerSize + dataSize) != expected) {
            qWarning() << "FIT file CRC mismatch, importing anyway";
        }
    }

    const uchar *p = base + headerSize;
    const uchar *end = base + qMin<qint64>(fileLen, static_cast<qint64>(headerSize) + dataSize);

    while (p < end) {
        const quint8 recordHeader = *p++;

        if (recordHeader & 0x80) {
            // Compressed timestamp header: 2-bit local type, 5-bit time offset
            // relative to the last full timestamp.
            const quint8 localType = (recordHeader >> 5) & 0x3;
            const quint8 offset = recordHeader & 0x1F;
            quint32 timestamp = (m_lastTimestamp & ~0x1Fu) | offset;
            if (timestamp < m_lastTimestamp) {
                timestamp += 0x20;
            }
            m_lastTimestamp = timestamp;

            const LocalDef &def = m_locals[localType];
            if (!def.valid) {
                errorOut = QStringLiteral("FIT data record for undefined local type %1 at byte %2")
                               .arg(localType)
                               .arg(p - 1 - base);
                return false;
            }
            if (!decodeData(p, end, def, errorOut)) {
                return false;
            }
            m_messages.last().fields[253] = {static_cast<double>(timestamp)};
        } else if (recordHeader & 0x40) {
            if (!decodeDefinition(p, end, recordHeader, errorOut)) {
                return false;
            }
        } else {
            const quint8 localType = recordHeader & 0xF;
            const LocalDef &def = m_locals[localType];
            if (!def.valid) {
                errorOut = QStringLiteral("FIT data record for undefined local type %1 at byte %2")
                               .arg(localType)
                               .arg(p - 1 - base);
                return false;
            }
            if (!decodeData(p, end, def, errorOut)) {
                return false;
            }
        }
    }

    return true;
}

bool FitDecoder::decodeDefinition(const uchar *&p, const uchar *end, quint8 recordHeader,
                                  QString &errorOut)
{
    if (end - p < 5) {
        errorOut = QStringLiteral("Truncated FIT definition record");
        return false;
    }

    LocalDef def;
    def.valid = true;
    // p[0] is reserved
    def.bigEndian = (p[1] != 0);
    def.globalId = static_cast<quint16>(readUInt(p + 2, 2, def.bigEndian));
    const quint8 fieldCount = p[4];
    p += 5;

    if (end - p < fieldCount * 3) {
        errorOut = QStringLiteral("Truncated FIT field definitions");
        return false;
    }
    def.fields.reserve(fieldCount);
    for (int i = 0; i < fieldCount; ++i) {
        FieldDef field;
        field.fieldNum = p[0];
        field.size = p[1];
        field.baseType = p[2];
        def.fields.append(field);
        p += 3;
    }

    if (recordHeader & 0x20) {
        // Developer field definitions: we never interpret developer data,
        // but their total size must be known to skip it in data records.
        if (end - p < 1) {
            errorOut = QStringLiteral("Truncated FIT developer field count");
            return false;
        }
        const quint8 devFieldCount = *p++;
        if (end - p < devFieldCount * 3) {
            errorOut = QStringLiteral("Truncated FIT developer field definitions");
            return false;
        }
        for (int i = 0; i < devFieldCount; ++i) {
            def.devDataBytes += p[1];
            p += 3;
        }
    }

    m_locals[recordHeader & 0xF] = def;
    return true;
}

bool FitDecoder::decodeData(const uchar *&p, const uchar *end, const LocalDef &def,
                            QString &errorOut)
{
    FitMessage message;
    message.globalId = def.globalId;

    for (const FieldDef &field : def.fields) {
        if (end - p < field.size) {
            errorOut = QStringLiteral("Truncated FIT data record (message %1)").arg(def.globalId);
            return false;
        }

        const quint8 baseNum = field.baseType & 0x1F;
        const int elementSize = baseTypeSize(baseNum);

        // Strings and malformed/unknown field encodings are skipped whole;
        // the definition's declared size keeps the stream in sync regardless.
        if (baseNum != BaseString && elementSize > 0 && field.size % elementSize == 0) {
            QVector<double> elements;
            bool anyValid = false;
            const int count = field.size / elementSize;
            for (int i = 0; i < count; ++i) {
                double value = qQNaN();
                if (decodeElement(p + i * elementSize, baseNum, def.bigEndian, value)) {
                    anyValid = true;
                } else {
                    value = qQNaN();
                }
                elements.append(value);
            }
            if (anyValid) {
                message.fields.insert(field.fieldNum, elements);
                if (field.fieldNum == 253 && !qIsNaN(elements.first())) {
                    m_lastTimestamp = static_cast<quint32>(elements.first());
                }
            }
        }

        p += field.size;
    }

    if (end - p < def.devDataBytes) {
        errorOut = QStringLiteral("Truncated FIT developer data (message %1)").arg(def.globalId);
        return false;
    }
    p += def.devDataBytes;

    m_messages.append(message);
    return true;
}
