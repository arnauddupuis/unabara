#include "include/core/cube_lut.h"

#include <QBuffer>
#include <QFile>
#include <QIODevice>
#include <QRegularExpression>
#include <QTextStream>

#include <cmath>

namespace Unabara {

namespace {

inline double clamp01(double x)
{
    return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
}

// Token separator for keyword and data lines. Compiled once: Qt 6 has no
// implicit pattern cache, and a 65³ LUT has ~275k lines. Matching on a const
// QRegularExpression is thread-safe (parse also runs during export planning).
const QRegularExpression& whitespaceRe()
{
    static const QRegularExpression re(QStringLiteral("\\s+"));
    return re;
}

inline void setError(QString* error, const QString& message)
{
    if (error)
        *error = message;
}

} // namespace

CubeLut CubeLut::parse(const QByteArray& bytes, QString* error)
{
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    return parse(&buffer, error);
}

CubeLut CubeLut::parseFile(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Cannot open LUT file %1: %2")
                            .arg(path, file.errorString()));
        return CubeLut();
    }
    QString innerError;
    CubeLut lut = parse(&file, &innerError);
    if (!lut.isValid())
        setError(error, QStringLiteral("%1: %2").arg(path, innerError));
    return lut;
}

CubeLut CubeLut::parse(QIODevice* device, QString* error)
{
    CubeLut lut;
    CubeLut invalid;
    QTextStream in(device);

    qsizetype dataIndex = 0;
    qsizetype expectedValues = 0; // 3·N³ once LUT_3D_SIZE is known
    int lineNumber = 0;

    while (!in.atEnd()) {
        ++lineNumber;
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        const QChar first = line.at(0);
        if (first.isLetter()) {
            // Keyword line.
            const QStringList tokens =
                line.split(whitespaceRe(), Qt::SkipEmptyParts);
            const QString keyword = tokens.first().toUpper();
            if (keyword == QLatin1String("LUT_1D_SIZE")) {
                setError(error,
                         QStringLiteral("This is a 1D LUT (LUT_1D_SIZE); Unabara only "
                                        "supports 3D .cube LUTs (LUT_3D_SIZE)"));
                return invalid;
            }
            if (keyword == QLatin1String("LUT_3D_SIZE")) {
                if (tokens.size() != 2) {
                    setError(error, QStringLiteral("Malformed LUT_3D_SIZE on line %1")
                                        .arg(lineNumber));
                    return invalid;
                }
                bool okSize = false;
                const int size = tokens.at(1).toInt(&okSize);
                if (!okSize || size < 2 || size > 256) {
                    setError(error,
                             QStringLiteral("LUT_3D_SIZE %1 out of range (2..256) on line %2")
                                 .arg(tokens.at(1)).arg(lineNumber));
                    return invalid;
                }
                if (lut.m_size != 0) {
                    // A second size line would silently zero-fill the table
                    // while keeping the old dataIndex — reject the file.
                    setError(error,
                             QStringLiteral("Duplicate LUT_3D_SIZE on line %1")
                                 .arg(lineNumber));
                    return invalid;
                }
                lut.m_size = size;
                expectedValues =
                    static_cast<qsizetype>(size) * size * size * 3;
                lut.m_data.assign(static_cast<size_t>(expectedValues), 0.0f);
                continue;
            }
            if (keyword == QLatin1String("DOMAIN_MIN")
                || keyword == QLatin1String("DOMAIN_MAX")) {
                if (tokens.size() != 4) {
                    setError(error, QStringLiteral("Malformed %1 on line %2")
                                        .arg(keyword).arg(lineNumber));
                    return invalid;
                }
                double values[3];
                for (int i = 0; i < 3; ++i) {
                    bool okValue = false;
                    values[i] = tokens.at(i + 1).toDouble(&okValue);
                    if (!okValue || !std::isfinite(values[i])) {
                        setError(error, QStringLiteral("Malformed %1 on line %2")
                                            .arg(keyword).arg(lineNumber));
                        return invalid;
                    }
                }
                double* target = keyword == QLatin1String("DOMAIN_MIN")
                                     ? lut.m_domainMin : lut.m_domainMax;
                for (int i = 0; i < 3; ++i)
                    target[i] = values[i];
                continue;
            }
            if (keyword == QLatin1String("TITLE")) {
                QString title = line.mid(5).trimmed();
                if (title.startsWith(QLatin1Char('"')) && title.endsWith(QLatin1Char('"'))
                    && title.size() >= 2)
                    title = title.mid(1, title.size() - 2);
                lut.m_title = title;
                continue;
            }
            // Unknown keyword (e.g. LUT_IN_VIDEO_RANGE): tolerated, skipped.
            continue;
        }

        // Data line: exactly three floats.
        if (lut.m_size == 0) {
            setError(error, QStringLiteral("Data before LUT_3D_SIZE on line %1")
                                .arg(lineNumber));
            return invalid;
        }
        const QStringList tokens =
            line.split(whitespaceRe(), Qt::SkipEmptyParts);
        if (tokens.size() != 3) {
            setError(error, QStringLiteral("Expected 3 values on line %1, got %2")
                                .arg(lineNumber).arg(tokens.size()));
            return invalid;
        }
        if (dataIndex + 3 > expectedValues) {
            setError(error, QStringLiteral("Too many data lines (expected %1 entries)")
                                .arg(expectedValues / 3));
            return invalid;
        }
        for (const QString& token : tokens) {
            bool okValue = false;
            const double value = token.toDouble(&okValue);
            if (!okValue || !std::isfinite(value)) {
                setError(error, QStringLiteral("Malformed value '%1' on line %2")
                                    .arg(token).arg(lineNumber));
                return invalid;
            }
            lut.m_data[static_cast<size_t>(dataIndex++)] = static_cast<float>(value);
        }
    }

    if (lut.m_size == 0) {
        setError(error, QStringLiteral("No LUT_3D_SIZE found — not a 3D .cube file"));
        return invalid;
    }
    if (dataIndex != expectedValues) {
        setError(error, QStringLiteral("Short data: got %1 of %2 expected entries")
                            .arg(dataIndex / 3).arg(expectedValues / 3));
        return invalid;
    }
    for (int i = 0; i < 3; ++i) {
        if (!(lut.m_domainMax[i] > lut.m_domainMin[i])) {
            setError(error, QStringLiteral("Invalid domain: DOMAIN_MAX must exceed "
                                           "DOMAIN_MIN per channel"));
            return invalid;
        }
    }
    return lut;
}

void CubeLut::sample(double r, double g, double b,
                     double& outR, double& outG, double& outB) const
{
    if (!isValid()) {
        outR = r;
        outG = g;
        outB = b;
        return;
    }

    const int n = m_size;
    const double in[3] = {r, g, b};
    double scaled[3];
    for (int i = 0; i < 3; ++i) {
        const double t =
            clamp01((in[i] - m_domainMin[i]) / (m_domainMax[i] - m_domainMin[i]));
        scaled[i] = t * (n - 1);
    }

    int base[3];
    double d[3];
    for (int i = 0; i < 3; ++i) {
        base[i] = static_cast<int>(scaled[i]);
        if (base[i] > n - 2)
            base[i] = n - 2;
        d[i] = scaled[i] - base[i];
    }

    // .cube order: red fastest → flat index (r + g·N + b·N²)·3.
    const auto at = [&](int ri, int gi, int bi, int ch) -> double {
        const size_t idx =
            (static_cast<size_t>(bi) * n * n + static_cast<size_t>(gi) * n + ri) * 3
            + static_cast<size_t>(ch);
        return m_data[idx];
    };

    // Tetrahedral interpolation — same case split and barycentric weights
    // as FFmpeg's vf_lut3d interp_tetrahedral.
    const double dr = d[0], dg = d[1], db = d[2];
    const int r0 = base[0], g0 = base[1], b0 = base[2];
    const int r1 = r0 + 1, g1 = g0 + 1, b1 = b0 + 1;
    double out[3];
    for (int ch = 0; ch < 3; ++ch) {
        const double c000 = at(r0, g0, b0, ch);
        const double c111 = at(r1, g1, b1, ch);
        double value;
        if (dr > dg) {
            if (dg > db) {
                value = (1 - dr) * c000 + (dr - dg) * at(r1, g0, b0, ch)
                        + (dg - db) * at(r1, g1, b0, ch) + db * c111;
            } else if (dr > db) {
                value = (1 - dr) * c000 + (dr - db) * at(r1, g0, b0, ch)
                        + (db - dg) * at(r1, g0, b1, ch) + dg * c111;
            } else {
                value = (1 - db) * c000 + (db - dr) * at(r0, g0, b1, ch)
                        + (dr - dg) * at(r1, g0, b1, ch) + dg * c111;
            }
        } else {
            if (db > dg) {
                value = (1 - db) * c000 + (db - dg) * at(r0, g0, b1, ch)
                        + (dg - dr) * at(r0, g1, b1, ch) + dr * c111;
            } else if (db > dr) {
                value = (1 - dg) * c000 + (dg - db) * at(r0, g1, b0, ch)
                        + (db - dr) * at(r0, g1, b1, ch) + dr * c111;
            } else {
                value = (1 - dg) * c000 + (dg - dr) * at(r0, g1, b0, ch)
                        + (dr - db) * at(r1, g1, b0, ch) + db * c111;
            }
        }
        out[ch] = value;
    }
    outR = out[0];
    outG = out[1];
    outB = out[2];
}

QByteArray CubeLut::bake(const GradeOps& ops, const CubeLut* userLut, int size)
{
    QByteArray out;
    out.reserve(size * size * size * 30 + 128);
    out += "# Baked by Unabara from the parametric colour grade\n";
    out += "LUT_3D_SIZE " + QByteArray::number(size) + "\n";
    out += "DOMAIN_MIN 0.0 0.0 0.0\n";
    out += "DOMAIN_MAX 1.0 1.0 1.0\n";

    const double step = 1.0 / (size - 1);
    for (int bi = 0; bi < size; ++bi) {
        for (int gi = 0; gi < size; ++gi) {
            for (int ri = 0; ri < size; ++ri) { // red fastest per .cube spec
                double r = ri * step;
                double g = gi * step;
                double b = bi * step;
                ops.apply(r, g, b);
                if (userLut && userLut->isValid()) {
                    // User LUT composes in AFTER the parametric grade.
                    double ur, ug, ub;
                    userLut->sample(r, g, b, ur, ug, ub);
                    r = ur;
                    g = ug;
                    b = ub;
                }
                // QByteArray::number is C-locale always — snprintf("%.6f")
                // is NOT (a French system locale writes "0,5" and FFmpeg
                // rejects the cube; caught by cube_lut_test).
                out += QByteArray::number(clamp01(r), 'f', 6);
                out += ' ';
                out += QByteArray::number(clamp01(g), 'f', 6);
                out += ' ';
                out += QByteArray::number(clamp01(b), 'f', 6);
                out += '\n';
            }
        }
    }
    return out;
}

} // namespace Unabara
