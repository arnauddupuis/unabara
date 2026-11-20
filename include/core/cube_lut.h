#ifndef CUBE_LUT_H
#define CUBE_LUT_H

#include <QByteArray>
#include <QString>

#include <vector>

#include "include/core/color_grade.h"

class QIODevice;

namespace Unabara {

// Adobe/Resolve .cube 3D LUT: parser, tetrahedral sampler and baker.
//
// parse() accepts 3D LUTs only (LUT_1D_SIZE is rejected with a clear
// message), honours DOMAIN_MIN/DOMAIN_MAX, TITLE, comments, blank lines and
// arbitrary whitespace, and reports short/excess/malformed data. Data is
// stored in .cube order: red index varies fastest
// (index = r + g·N + b·N²).
//
// sample() uses tetrahedral interpolation (same algorithm as FFmpeg's
// lut3d interp=tetrahedral, so a baked cube round-trips through FFmpeg
// byte-compatibly).
//
// bake() writes a .cube from GradeOps::apply() — the ground truth — with an
// optional user LUT composed in AFTER the parametric grade (the pipeline's
// documented op order). Used by the static export path and the parity test.
class CubeLut
{
public:
    CubeLut() = default;

    bool isValid() const { return m_size >= 2; }
    int size() const { return m_size; }
    double domainMin(int channel) const { return m_domainMin[channel]; }
    double domainMax(int channel) const { return m_domainMax[channel]; }
    QString title() const { return m_title; }

    // Parse .cube text. On failure the returned LUT is invalid and *error
    // (when given) holds a one-line human-readable reason.
    static CubeLut parse(QIODevice* device, QString* error = nullptr);
    static CubeLut parse(const QByteArray& bytes, QString* error = nullptr);
    // Convenience: open + parse a file (adds the path to the error text).
    static CubeLut parseFile(const QString& path, QString* error = nullptr);

    // Tetrahedral sample. Inputs are clamped to the LUT domain; outputs are
    // the raw table values (not clamped — HDR LUTs may exceed [0,1]).
    void sample(double r, double g, double b,
                double& outR, double& outG, double& outB) const;

    // Bake a .cube (text bytes) of `size`³ entries from the resolved grade
    // ops, composing `userLut` in after them when non-null. Entries are
    // clamped to [0,1] and written with 6 decimals.
    static QByteArray bake(const GradeOps& ops, const CubeLut* userLut = nullptr,
                           int size = 33);

private:
    int m_size = 0;
    double m_domainMin[3] = {0.0, 0.0, 0.0};
    double m_domainMax[3] = {1.0, 1.0, 1.0};
    QString m_title;
    std::vector<float> m_data; // size³·3 floats, red fastest
};

} // namespace Unabara

#endif // CUBE_LUT_H
