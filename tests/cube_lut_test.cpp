// Tests for the .cube 3D LUT parser / tetrahedral sampler / baker
// (include/core/cube_lut.h): format acceptance and rejection (3D only,
// domain handling, short data), lattice-point exactness, tetrahedral
// interpolation, the bake→parse→sample round trip against the ground-truth
// GradeOps::apply(), and user-LUT composition order.

#include <QtTest>
#include <QRandomGenerator>

#include "include/core/color_grade.h"
#include "include/core/cube_lut.h"

using namespace Unabara;

namespace {

// 2³ identity LUT: tetrahedral interpolation of a linear table is exact.
const char* const kIdentityCube =
    "# a comment\n"
    "TITLE \"identity\"\n"
    "\n"
    "LUT_3D_SIZE 2\n"
    "0 0 0\n"
    "1 0 0\n"
    "0 1 0\n"
    "1 1 0\n"
    "0 0 1\n"
    "1 0 1\n"
    "0 1 1\n"
    "1 1 1\n";

// 2³ half-gain LUT.
const char* const kHalfCube =
    "LUT_3D_SIZE 2\n"
    "0 0 0\n"
    "0.5 0 0\n"
    "0 0.5 0\n"
    "0.5 0.5 0\n"
    "0 0 0.5\n"
    "0.5 0 0.5\n"
    "0 0.5 0.5\n"
    "0.5 0.5 0.5\n";

} // namespace

class CubeLutTest : public QObject
{
    Q_OBJECT

private slots:
    void parse_identity();
    void parse_whitespaceAndComments();
    void parse_domain();
    void reject_1dLut();
    void reject_shortData();
    void reject_excessData();
    void reject_malformedValue();
    void reject_dataBeforeSize();
    void reject_badSize();
    void reject_noSize();
    void reject_duplicateSize();
    void sample_latticeExact();
    void sample_tetrahedralInterpolation();
    void sample_tetrahedralCaseSplitGolden_data();
    void sample_tetrahedralCaseSplitGolden();
    void bake_roundTripMatchesApply();
    void bake_latticePointsExact();
    void bake_userLutComposedAfterGrade();
};

void CubeLutTest::parse_identity()
{
    QString error;
    const CubeLut lut = CubeLut::parse(QByteArray(kIdentityCube), &error);
    QVERIFY2(lut.isValid(), qPrintable(error));
    QCOMPARE(lut.size(), 2);
    QCOMPARE(lut.title(), QStringLiteral("identity"));

    double r, g, b;
    lut.sample(0.3, 0.7, 0.2, r, g, b);
    QVERIFY(qAbs(r - 0.3) < 1e-6);
    QVERIFY(qAbs(g - 0.7) < 1e-6);
    QVERIFY(qAbs(b - 0.2) < 1e-6);
}

void CubeLutTest::parse_whitespaceAndComments()
{
    const QByteArray messy =
        "  # leading whitespace comment\n"
        "LUT_3D_SIZE   2\n"
        "\n"
        "   0   0\t0\n"
        "0.5 0 0\n0 0.5 0\n0.5 0.5 0\n0 0 0.5\n0.5 0 0.5\n0 0.5 0.5\n"
        "  0.5\t0.5  0.5  \n";
    QString error;
    const CubeLut lut = CubeLut::parse(messy, &error);
    QVERIFY2(lut.isValid(), qPrintable(error));
}

void CubeLutTest::parse_domain()
{
    // Identity over DOMAIN [0,2]: table corners are 0 or 2.
    const QByteArray domainCube =
        "LUT_3D_SIZE 2\n"
        "DOMAIN_MIN 0 0 0\n"
        "DOMAIN_MAX 2 2 2\n"
        "0 0 0\n2 0 0\n0 2 0\n2 2 0\n0 0 2\n2 0 2\n0 2 2\n2 2 2\n";
    QString error;
    const CubeLut lut = CubeLut::parse(domainCube, &error);
    QVERIFY2(lut.isValid(), qPrintable(error));
    QCOMPARE(lut.domainMax(0), 2.0);

    double r, g, b;
    lut.sample(1.0, 0.5, 1.5, r, g, b);
    QVERIFY(qAbs(r - 1.0) < 1e-6);
    QVERIFY(qAbs(g - 0.5) < 1e-6);
    QVERIFY(qAbs(b - 1.5) < 1e-6);
    // Inputs beyond the domain clamp to it.
    lut.sample(5.0, -1.0, 0.0, r, g, b);
    QVERIFY(qAbs(r - 2.0) < 1e-6);
    QVERIFY(qAbs(g - 0.0) < 1e-6);
}

void CubeLutTest::reject_1dLut()
{
    QString error;
    const CubeLut lut = CubeLut::parse(QByteArray("LUT_1D_SIZE 2\n0\n1\n"), &error);
    QVERIFY(!lut.isValid());
    QVERIFY2(error.contains(QStringLiteral("1D")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("3D")), qPrintable(error));
}

void CubeLutTest::reject_shortData()
{
    QByteArray shortData(kHalfCube);
    shortData.truncate(shortData.lastIndexOf("0.5 0.5 0.5"));
    QString error;
    QVERIFY(!CubeLut::parse(shortData, &error).isValid());
    QVERIFY2(error.contains(QStringLiteral("Short data")), qPrintable(error));
}

void CubeLutTest::reject_excessData()
{
    QByteArray excess(kHalfCube);
    excess += "0.1 0.1 0.1\n";
    QString error;
    QVERIFY(!CubeLut::parse(excess, &error).isValid());
    QVERIFY2(error.contains(QStringLiteral("Too many")), qPrintable(error));
}

void CubeLutTest::reject_malformedValue()
{
    QByteArray bad(kHalfCube);
    bad.replace("0.5 0.5 0.5", "0.5 banana 0.5");
    QString error;
    QVERIFY(!CubeLut::parse(bad, &error).isValid());
    QVERIFY2(error.contains(QStringLiteral("banana")), qPrintable(error));

    // Wrong token count on a data line.
    QVERIFY(!CubeLut::parse(QByteArray("LUT_3D_SIZE 2\n0 0\n"), &error).isValid());
}

void CubeLutTest::reject_dataBeforeSize()
{
    QString error;
    QVERIFY(!CubeLut::parse(QByteArray("0 0 0\nLUT_3D_SIZE 2\n"), &error).isValid());
    QVERIFY2(error.contains(QStringLiteral("before LUT_3D_SIZE")), qPrintable(error));
}

void CubeLutTest::reject_badSize()
{
    QString error;
    QVERIFY(!CubeLut::parse(QByteArray("LUT_3D_SIZE 1\n0 0 0\n"), &error).isValid());
    QVERIFY(!CubeLut::parse(QByteArray("LUT_3D_SIZE 9999\n"), &error).isValid());
    QVERIFY(!CubeLut::parse(QByteArray("LUT_3D_SIZE banana\n"), &error).isValid());
}

void CubeLutTest::reject_noSize()
{
    QString error;
    QVERIFY(!CubeLut::parse(QByteArray("# only a comment\n"), &error).isValid());
    QVERIFY2(error.contains(QStringLiteral("LUT_3D_SIZE")), qPrintable(error));
}

void CubeLutTest::sample_latticeExact()
{
    QString error;
    const CubeLut lut = CubeLut::parse(QByteArray(kHalfCube), &error);
    QVERIFY2(lut.isValid(), qPrintable(error));
    double r, g, b;
    lut.sample(1.0, 1.0, 1.0, r, g, b);
    QCOMPARE(r, 0.5);
    QCOMPARE(g, 0.5);
    QCOMPARE(b, 0.5);
    lut.sample(0.0, 1.0, 0.0, r, g, b);
    QCOMPARE(r, 0.0);
    QCOMPARE(g, 0.5);
    QCOMPARE(b, 0.0);
}

void CubeLutTest::sample_tetrahedralInterpolation()
{
    // Half-gain is linear, so tetrahedral interpolation is exact between
    // lattice points too.
    QString error;
    const CubeLut lut = CubeLut::parse(QByteArray(kHalfCube), &error);
    QVERIFY2(lut.isValid(), qPrintable(error));
    double r, g, b;
    lut.sample(0.25, 0.5, 0.8, r, g, b);
    QVERIFY(qAbs(r - 0.125) < 1e-6);
    QVERIFY(qAbs(g - 0.25) < 1e-6);
    QVERIFY(qAbs(b - 0.4) < 1e-6);
}

void CubeLutTest::bake_roundTripMatchesApply()
{
    // Smooth ops (no clarity: the black-point kink is the one place a 33³
    // lattice can't track apply() this tightly — it gets its own
    // lattice-exactness test below and the looser end-to-end parity test).
    GradeParams params;
    params.redRecovery = 0.8;
    params.waterTint = 0.4;
    const GradeOps ops = ColorGrade::resolve(params);

    QString error;
    const CubeLut baked = CubeLut::parse(CubeLut::bake(ops, nullptr, 33), &error);
    QVERIFY2(baked.isValid(), qPrintable(error));
    QCOMPARE(baked.size(), 33);

    QRandomGenerator rng(20261007); // deterministic
    int smoothPoints = 0;
    for (int i = 0; i < 500; ++i) {
        const double r0 = rng.generateDouble();
        const double g0 = rng.generateDouble();
        const double b0 = rng.generateDouble();
        double r = r0, g = g0, b = b0;
        ops.apply(r, g, b);
        double lr, lg, lb;
        baked.sample(r0, g0, b0, lr, lg, lb);

        // The one place the lattice can't track apply() to 1e-3 is the
        // saturation knee, where the post-matrix clamp at 0/1 puts a kink
        // inside a lattice cell. Points near that knee get a 1.5-LSB
        // bound (the end-to-end FFmpeg parity test covers them for real);
        // everywhere else the 1e-3 claim holds.
        bool nearKnee = false;
        for (int ch = 0; ch < 3; ++ch) {
            const double m = ops.matrix[ch][0] * r0 + ops.matrix[ch][1] * g0
                             + ops.matrix[ch][2] * b0;
            if (qAbs(m - 1.0) < 0.05 || qAbs(m) < 0.05)
                nearKnee = true;
        }
        const double tolerance = nearKnee ? 6e-3 : 1e-3;
        if (!nearKnee)
            ++smoothPoints;
        QVERIFY2(qAbs(lr - r) <= tolerance && qAbs(lg - g) <= tolerance
                     && qAbs(lb - b) <= tolerance,
                 qPrintable(QStringLiteral("at (%1,%2,%3): lut (%4,%5,%6) vs apply (%7,%8,%9)")
                                .arg(r0).arg(g0).arg(b0)
                                .arg(lr).arg(lg).arg(lb)
                                .arg(r).arg(g).arg(b)));
    }
    // The strict claim must have been exercised on a real share of points.
    QVERIFY(smoothPoints > 300);
}

void CubeLutTest::bake_latticePointsExact()
{
    // At lattice points the baked value IS apply()'s value (to the .cube
    // text precision of 6 decimals), full grade including clarity.
    GradeParams params;
    params.redRecovery = 0.6;
    params.waterTint = 0.35;
    params.clarity = 0.4;
    const GradeOps ops = ColorGrade::resolve(params);

    QString error;
    const CubeLut baked = CubeLut::parse(CubeLut::bake(ops, nullptr, 33), &error);
    QVERIFY2(baked.isValid(), qPrintable(error));

    for (int i = 0; i <= 32; i += 4) {
        for (int j = 0; j <= 32; j += 8) {
            const double x = i / 32.0;
            const double y = j / 32.0;
            double r = x, g = y, b = x;
            ops.apply(r, g, b);
            double lr, lg, lb;
            baked.sample(x, y, x, lr, lg, lb);
            QVERIFY(qAbs(lr - r) < 1e-5);
            QVERIFY(qAbs(lg - g) < 1e-5);
            QVERIFY(qAbs(lb - b) < 1e-5);
        }
    }
}

void CubeLutTest::bake_userLutComposedAfterGrade()
{
    // The user LUT composes in AFTER the parametric grade: baked(x) =
    // user(apply(x)). With a half-gain user LUT that is apply(x)/2 — which
    // differs measurably from apply(x/2) because the grade is non-linear
    // (clarity black point), so a wrong composition order fails this.
    GradeParams params;
    params.redRecovery = 0.5;
    params.clarity = 0.6;
    const GradeOps ops = ColorGrade::resolve(params);

    QString error;
    const CubeLut user = CubeLut::parse(QByteArray(kHalfCube), &error);
    QVERIFY2(user.isValid(), qPrintable(error));
    const CubeLut baked = CubeLut::parse(CubeLut::bake(ops, &user, 33), &error);
    QVERIFY2(baked.isValid(), qPrintable(error));

    // Check at lattice points (exact apart from text precision).
    for (int i = 0; i <= 32; i += 2) {
        const double x = i / 32.0;
        double r = x, g = x, b = x;
        ops.apply(r, g, b);
        double lr, lg, lb;
        baked.sample(x, x, x, lr, lg, lb);
        QVERIFY(qAbs(lr - r * 0.5) < 1e-5);
        QVERIFY(qAbs(lg - g * 0.5) < 1e-5);
        QVERIFY(qAbs(lb - b * 0.5) < 1e-5);
    }
}

// 4³ deliberately NON-LINEAR LUT (deterministic scrambled k/64 values,
// exact in float32). On a linear table every tetrahedron agrees, so only
// a non-linear one can catch a wrong case split in sample() — a mutated
// branch condition was measured at 231 LSB error against FFmpeg's
// lut3d=interp=tetrahedral, while the shipped split matches within 1 LSB.
static const char* const kTetraGoldenCube =
    "LUT_3D_SIZE 4\n"
    "0.796875 0.218750 0.937500\n"
    "0.312500 0.359375 0.031250\n"
    "0.328125 0.812500 0.015625\n"
    "0.453125 0.578125 0.015625\n"
    "0.984375 0.921875 0.312500\n"
    "0.500000 0.890625 0.328125\n"
    "0.750000 0.906250 0.640625\n"
    "0.921875 0.218750 0.953125\n"
    "0.953125 0.718750 0.953125\n"
    "0.781250 0.843750 0.984375\n"
    "0.031250 0.781250 0.093750\n"
    "0.312500 0.593750 0.265625\n"
    "0.046875 0.921875 0.203125\n"
    "0.125000 0.812500 0.015625\n"
    "0.921875 0.671875 0.109375\n"
    "0.718750 0.531250 0.546875\n"
    "0.765625 0.046875 0.015625\n"
    "0.078125 0.828125 0.046875\n"
    "0.828125 0.968750 0.265625\n"
    "0.671875 0.515625 0.953125\n"
    "0.203125 0.734375 0.218750\n"
    "0.953125 0.609375 0.812500\n"
    "0.359375 0.390625 0.921875\n"
    "0.625000 0.437500 0.218750\n"
    "0.687500 1.000000 0.125000\n"
    "0.000000 0.109375 0.968750\n"
    "0.156250 0.109375 0.531250\n"
    "0.531250 0.500000 0.062500\n"
    "0.625000 0.421875 0.093750\n"
    "0.171875 0.515625 0.500000\n"
    "0.734375 0.343750 0.953125\n"
    "0.562500 0.671875 0.531250\n"
    "1.000000 0.718750 0.031250\n"
    "0.000000 0.062500 0.203125\n"
    "0.406250 0.125000 0.218750\n"
    "0.640625 0.781250 0.968750\n"
    "0.796875 0.046875 0.343750\n"
    "0.218750 0.656250 0.437500\n"
    "0.546875 0.187500 0.484375\n"
    "0.906250 0.421875 0.640625\n"
    "0.687500 0.953125 0.875000\n"
    "0.078125 0.421875 0.421875\n"
    "0.671875 0.453125 0.953125\n"
    "0.953125 0.000000 0.406250\n"
    "0.953125 0.031250 0.406250\n"
    "0.125000 0.953125 0.562500\n"
    "0.781250 0.671875 0.359375\n"
    "0.906250 0.484375 0.796875\n"
    "0.953125 0.890625 0.796875\n"
    "0.171875 0.593750 0.015625\n"
    "0.031250 0.859375 0.906250\n"
    "0.015625 0.015625 0.828125\n"
    "0.000000 0.281250 0.015625\n"
    "0.812500 0.671875 0.484375\n"
    "0.484375 0.843750 0.859375\n"
    "0.250000 0.578125 0.359375\n"
    "0.156250 0.234375 0.906250\n"
    "0.031250 0.296875 0.906250\n"
    "0.546875 0.281250 0.281250\n"
    "0.296875 0.796875 0.500000\n"
    "0.609375 0.593750 0.000000\n"
    "0.156250 0.875000 0.765625\n"
    "0.343750 0.468750 0.640625\n"
    "0.093750 0.234375 0.921875\n";

void CubeLutTest::sample_tetrahedralCaseSplitGolden_data()
{
    QTest::addColumn<double>("inR");
    QTest::addColumn<double>("inG");
    QTest::addColumn<double>("inB");
    QTest::addColumn<double>("expR");
    QTest::addColumn<double>("expG");
    QTest::addColumn<double>("expB");

    // Golden values generated from the shipped algorithm after it was
    // verified term-by-term and empirically (random non-linear LUT, 64x64
    // gradient) against FFmpeg 7.1.5 lut3d tetrahedral output. One row per
    // tetrahedron of the 6-case split, named by its branch condition, plus
    // the all-equal diagonal and a lattice point.
    const auto addRow = [](const char* name, double r, double g, double b,
                           double er, double eg, double eb) {
        QTest::newRow(name) << r << g << b << er << eg << eb;
    };
    addRow("dr>dg>db", 0.566666666667, 0.133333333333, 0.733333333333, 0.328125000000, 0.262500000000, 0.395312500000);
    addRow("dr>dg, dr>db>=dg", 0.233333333333, 0.400000000000, 0.466666666667, 0.406250000000, 0.618750000000, 0.481250000000);
    addRow("dr>dg, db>=dr", 0.833333333333, 0.066666666667, 0.233333333333, 0.590625000000, 0.679687500000, 0.387500000000);
    addRow("dg>=dr, db>dg", 0.066666666667, 0.800000000000, 0.566666666667, 0.628125000000, 0.782812500000, 0.493750000000);
    addRow("dg>=dr, dg>=db>dr", 0.400000000000, 0.566666666667, 0.133333333333, 0.415625000000, 0.564062500000, 0.693750000000);
    addRow("dg>=dr>=db", 0.766666666667, 0.900000000000, 0.733333333333, 0.623437500000, 0.500000000000, 0.693750000000);
    addRow("diagonal dr=dg=db", 0.500000000000, 0.833333333333, 0.166666666667, 0.757812500000, 0.593750000000, 0.968750000000);
    addRow("lattice point", 0.666666666667, 0.333333333333, 0.333333333333, 0.359375000000, 0.390625000000, 0.921875000000);
}

void CubeLutTest::sample_tetrahedralCaseSplitGolden()
{
    QFETCH(double, inR);
    QFETCH(double, inG);
    QFETCH(double, inB);
    QFETCH(double, expR);
    QFETCH(double, expG);
    QFETCH(double, expB);

    QString error;
    const CubeLut lut = CubeLut::parse(QByteArray(kTetraGoldenCube), &error);
    QVERIFY2(lut.isValid(), qPrintable(error));

    double r, g, b;
    lut.sample(inR, inG, inB, r, g, b);
    // 1e-6: far above double/float32 literal rounding (~1e-12), far below
    // the ~0.9 error a wrong tetrahedron produces on this table.
    QVERIFY2(qAbs(r - expR) < 1e-6, qPrintable(QStringLiteral("r=%1 exp=%2").arg(r, 0, 'f', 9).arg(expR, 0, 'f', 9)));
    QVERIFY2(qAbs(g - expG) < 1e-6, qPrintable(QStringLiteral("g=%1 exp=%2").arg(g, 0, 'f', 9).arg(expG, 0, 'f', 9)));
    QVERIFY2(qAbs(b - expB) < 1e-6, qPrintable(QStringLiteral("b=%1 exp=%2").arg(b, 0, 'f', 9).arg(expB, 0, 'f', 9)));
}

void CubeLutTest::reject_duplicateSize()
{
    QString error;
    const QByteArray dup =
        "LUT_3D_SIZE 2\n"
        "0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n"
        "LUT_3D_SIZE 2\n"
        "0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n";
    QVERIFY(!CubeLut::parse(dup, &error).isValid());
    QVERIFY2(error.contains(QStringLiteral("Duplicate LUT_3D_SIZE")), qPrintable(error));
}

QTEST_GUILESS_MAIN(CubeLutTest)
#include "cube_lut_test.moc"
