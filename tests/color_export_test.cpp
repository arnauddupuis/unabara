// Tests for ColorGradeExport::plan() (include/core/color_grade_export.h):
// static path (baked lut3d snippet + .cube in workDir), depth-linked path
// (sendcmd command file cadence / dedup / first-command-at-0 / monotone
// timestamps), the lutrgb +0.5 truncation fix, FFmpeg filter-option
// escaping (spaces, commas, Windows drive paths), the timeVarying flag and
// the error / fallback paths (no dive, missing user LUT, bad workDir).

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "include/core/color_grade.h"
#include "include/core/color_grade_export.h"
#include "include/core/cube_lut.h"
#include "include/core/dive_data.h"

using namespace Unabara;

namespace {

// Dive profile: linear descent 0→30 m over 0..60 s, then flat at 30 m
// until 120 s.
DiveData* makeDive(QObject* parent)
{
    auto* dive = new DiveData(parent);
    for (int t = 0; t <= 120; t += 10) {
        const double depth = t <= 60 ? t / 2.0 : 30.0;
        dive->addDataPoint(DiveDataPoint(t, depth));
    }
    return dive;
}

ColorGrade makeStaticGrade()
{
    ColorGrade grade;
    grade.enabled = true;
    GradeAnchor anchor;
    anchor.params.redRecovery = 0.6;
    anchor.params.waterTint = 0.35;
    anchor.params.clarity = 0.4;
    grade.anchors << anchor;
    return grade;
}

ColorGrade makeDepthLinkedGrade()
{
    ColorGrade grade;
    grade.enabled = true;
    grade.depthLinked = true;
    GradeAnchor shallow;
    shallow.depth = 5.0;
    shallow.params.redRecovery = 0.2;
    GradeAnchor deep;
    deep.depth = 25.0;
    deep.params.redRecovery = 1.0;
    grade.anchors << shallow << deep;
    return grade;
}

QStringList readCmdLines(const QString& workDir)
{
    QFile file(QDir(workDir).filePath(QStringLiteral("unabara_grade.cmd")));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QStringList lines;
    for (const QByteArray& raw : file.readAll().split('\n')) {
        if (!raw.trimmed().isEmpty())
            lines << QString::fromUtf8(raw);
    }
    return lines;
}

} // namespace

class ColorExportTest : public QObject
{
    Q_OBJECT

private slots:
    void disabledGrade_emptyPlan();
    void staticGrade_bakesLut3d();
    void depthLinked_commandFile();
    void depthLinked_dedupAndCadence();
    void depthLinked_constantDepthNotTimeVarying();
    void lutrgb_truncationFix();
    void escaping_filterOption();
    void escaping_sendcmdArg();
    void escaping_workDirWithSpecials();
    void noDive_fallsBackToShallowStatic();
    void missingUserLut_nonFatalNote();
    void userLut_appendedOnDepthPath();
    void badWorkDir_isError();
    void invalidRange_isError();
};

void ColorExportTest::disabledGrade_emptyPlan()
{
    QTemporaryDir dir;
    ColorGrade grade = makeStaticGrade();
    grade.enabled = false;
    ColorGradeExport::Plan plan =
        ColorGradeExport::plan(grade, nullptr, 0, 0, 10, dir.path());
    QVERIFY(plan.isValid());
    QVERIFY(plan.filterSnippet.isEmpty());
    QVERIFY(plan.tempFiles.isEmpty());
    QVERIFY(!plan.timeVarying);

    // No anchors behaves the same.
    ColorGrade empty;
    empty.enabled = true;
    plan = ColorGradeExport::plan(empty, nullptr, 0, 0, 10, dir.path());
    QVERIFY(plan.isValid());
    QVERIFY(plan.filterSnippet.isEmpty());
}

void ColorExportTest::staticGrade_bakesLut3d()
{
    QTemporaryDir dir;
    const ColorGrade grade = makeStaticGrade();
    const ColorGradeExport::Plan plan =
        ColorGradeExport::plan(grade, nullptr, 0, 0, 10, dir.path());
    QVERIFY2(plan.isValid(), qPrintable(plan.error));
    QVERIFY(plan.filterSnippet.startsWith(QStringLiteral("lut3d=file=")));
    QVERIFY(plan.filterSnippet.endsWith(QStringLiteral(":interp=tetrahedral")));
    QVERIFY(!plan.timeVarying);
    QCOMPARE(plan.tempFiles.size(), 1);
    QVERIFY(QFile::exists(plan.tempFiles.first()));

    // The baked cube is a valid 33³ LUT that matches the ground truth at a
    // lattice point (0.5 = 16/32).
    QString error;
    const CubeLut baked = CubeLut::parseFile(plan.tempFiles.first(), &error);
    QVERIFY2(baked.isValid(), qPrintable(error));
    QCOMPARE(baked.size(), 33);
    const GradeOps ops = grade.resolveStatic();
    double r = 0.5, g = 0.5, b = 0.5;
    ops.apply(r, g, b);
    double lr, lg, lb;
    baked.sample(0.5, 0.5, 0.5, lr, lg, lb);
    QVERIFY(qAbs(lr - r) < 1e-5);
    QVERIFY(qAbs(lg - g) < 1e-5);
    QVERIFY(qAbs(lb - b) < 1e-5);
}

void ColorExportTest::depthLinked_commandFile()
{
    QTemporaryDir dir;
    DiveData* dive = makeDive(this);
    const ColorGrade grade = makeDepthLinkedGrade();
    const ColorGradeExport::Plan plan =
        ColorGradeExport::plan(grade, dive, 0.0, 0.0, 120.0, dir.path());
    QVERIFY2(plan.isValid(), qPrintable(plan.error));
    QVERIFY(plan.timeVarying);
    QVERIFY(plan.filterSnippet.contains(QStringLiteral("sendcmd=f=")));
    QVERIFY(plan.filterSnippet.contains(QStringLiteral("colorchannelmixer@cc=")));
    QVERIFY(plan.filterSnippet.contains(QStringLiteral("lutrgb@cv=r='")));
    QCOMPARE(plan.tempFiles.size(), 1);

    const QStringList lines = readCmdLines(dir.path());
    QVERIFY(!lines.isEmpty());
    // First command at t = 0 (frame pts start at 0 at the in-point).
    QVERIFY2(lines.first().startsWith(QStringLiteral("0.000 ")),
             qPrintable(lines.first()));
    // Monotone, strictly increasing timestamps on the 0.5 s grid; every
    // interval re-commands the mixer and all three lutrgb components and
    // ends with ';'.
    double prev = -1.0;
    for (const QString& line : lines) {
        const double ts = line.section(QLatin1Char(' '), 0, 0).toDouble();
        QVERIFY2(ts > prev, qPrintable(line));
        const double onGrid = ts / 0.5;
        QVERIFY(qAbs(onGrid - qRound(onGrid)) < 1e-9);
        QVERIFY(line.endsWith(QLatin1Char(';')));
        QVERIFY(line.contains(QStringLiteral("colorchannelmixer@cc rr ")));
        QVERIFY(line.contains(QStringLiteral("lutrgb@cv r ")));
        QVERIFY(line.contains(QStringLiteral("lutrgb@cv g ")));
        QVERIFY(line.contains(QStringLiteral("lutrgb@cv b ")));
        prev = ts;
    }
}

void ColorExportTest::depthLinked_dedupAndCadence()
{
    QTemporaryDir dir;
    DiveData* dive = makeDive(this);
    const ColorGrade grade = makeDepthLinkedGrade();
    const ColorGradeExport::Plan plan =
        ColorGradeExport::plan(grade, dive, 0.0, 0.0, 120.0, dir.path());
    QVERIFY2(plan.isValid(), qPrintable(plan.error));

    const QStringList lines = readCmdLines(dir.path());
    // 120 s at 0.5 s cadence would be 241 raw timestamps; the dive sits at
    // a clamped-constant depth from 50 s (deep anchor at 25 m) onward, so
    // dedup must drop that entire tail.
    QVERIFY2(lines.size() < 120, qPrintable(QString::number(lines.size())));
    const double last =
        lines.last().section(QLatin1Char(' '), 0, 0).toDouble();
    QVERIFY2(last <= 51.0, qPrintable(QString::number(last)));
    // But the varying stretch is properly sampled: more than a handful.
    QVERIFY(lines.size() > 50);
}

void ColorExportTest::depthLinked_constantDepthNotTimeVarying()
{
    QTemporaryDir dir;
    auto* dive = new DiveData(this);
    dive->addDataPoint(DiveDataPoint(0.0, 10.0));
    dive->addDataPoint(DiveDataPoint(120.0, 10.0));
    const ColorGrade grade = makeDepthLinkedGrade();
    const ColorGradeExport::Plan plan =
        ColorGradeExport::plan(grade, dive, 0.0, 0.0, 120.0, dir.path());
    QVERIFY2(plan.isValid(), qPrintable(plan.error));
    // Depth never changes → one command block at t=0, not time-varying.
    QVERIFY(!plan.timeVarying);
    const QStringList lines = readCmdLines(dir.path());
    QCOMPARE(lines.size(), 1);
    QVERIFY(lines.first().startsWith(QStringLiteral("0.000 ")));
}

void ColorExportTest::lutrgb_truncationFix()
{
    // lutrgb TRUNCATES its result (verified on FFmpeg 7.1.5): without the
    // trailing +0.5 every output value is biased a full LSB low. The
    // expression must carry the fix...
    const QString expr = ColorGradeExport::lutrgbExpression(0.12, 0.35, 1.6);
    QVERIFY2(expr.endsWith(QStringLiteral("*255+0.5")), qPrintable(expr));
    // ...and so must every lutrgb expression in an emitted plan (both the
    // filter init args and the sendcmd re-commands).
    QTemporaryDir dir;
    DiveData* dive = makeDive(this);
    const ColorGradeExport::Plan plan = ColorGradeExport::plan(
        makeDepthLinkedGrade(), dive, 0.0, 0.0, 60.0, dir.path());
    QVERIFY(plan.filterSnippet.contains(QStringLiteral("*255+0.5")));
    const QStringList lines = readCmdLines(dir.path());
    for (const QString& line : lines)
        QVERIFY2(line.count(QStringLiteral("*255+0.5")) == 3, qPrintable(line));
}

void ColorExportTest::escaping_filterOption()
{
    const QString bs(1, QLatin1Char('\\'));

    // Unix path with spaces: nothing to escape (QProcess passes the whole
    // filtergraph as one argv, no shell).
    QCOMPARE(ColorGradeExport::escapeFilterOption(QStringLiteral("/a b/x.cube")),
             QStringLiteral("/a b/x.cube"));

    // Comma: one graph-level escape.
    QCOMPARE(ColorGradeExport::escapeFilterOption(QStringLiteral("/a,b/x.cube")),
             QStringLiteral("/a") + bs + QStringLiteral(",b/x.cube"));

    // Windows drive path: the documented two-level escaping. 'C:\Users\x'
    // → option level 'C\:\\Users\\x' → graph level doubles every
    // backslash: 'C\\:\\\\Users\\\\x' (2 before the colon, 4 per original
    // backslash). Verified against FFmpeg 7.1.5's filtergraph parser.
    const QString windowsPath = QStringLiteral("C:") + bs
                                + QStringLiteral("Users") + bs
                                + QStringLiteral("dive video") + bs
                                + QStringLiteral("grade.cube");
    const QString expected = QStringLiteral("C") + bs.repeated(2)
                             + QStringLiteral(":") + bs.repeated(4)
                             + QStringLiteral("Users") + bs.repeated(4)
                             + QStringLiteral("dive video") + bs.repeated(4)
                             + QStringLiteral("grade.cube");
    QCOMPARE(ColorGradeExport::escapeFilterOption(windowsPath), expected);

    // Single quote: option level '\'' then graph level '\\\'' (3
    // backslashes + quote — FFmpeg's own documented double-escape example).
    QCOMPARE(ColorGradeExport::escapeFilterOption(QStringLiteral("a'b")),
             QStringLiteral("a") + bs.repeated(3) + QStringLiteral("'b"));
}

void ColorExportTest::escaping_sendcmdArg()
{
    const QString bs(1, QLatin1Char('\\'));
    QCOMPARE(ColorGradeExport::escapeSendcmdArg(QStringLiteral("clip(val,0,1)")),
             QStringLiteral("clip(val") + bs + QStringLiteral(",0") + bs
                 + QStringLiteral(",1)"));
    QCOMPARE(ColorGradeExport::escapeSendcmdArg(QStringLiteral("a;b")),
             QStringLiteral("a") + bs + QStringLiteral(";b"));
    QCOMPARE(ColorGradeExport::escapeSendcmdArg(bs),
             bs.repeated(2));
}

void ColorExportTest::escaping_workDirWithSpecials()
{
    QTemporaryDir base;
    const QString odd = QStringLiteral("work dir,odd");
    QVERIFY(QDir(base.path()).mkdir(odd));
    const QString workDir = QDir(base.path()).filePath(odd);

    const ColorGradeExport::Plan plan =
        ColorGradeExport::plan(makeStaticGrade(), nullptr, 0, 0, 10, workDir);
    QVERIFY2(plan.isValid(), qPrintable(plan.error));
    // The emitted snippet must carry the escaped form of the comma...
    QVERIFY(plan.filterSnippet.contains(QStringLiteral("work dir\\,odd")));
    // ...and never the raw separator-ambiguous form.
    QVERIFY(!plan.filterSnippet.contains(QStringLiteral("work dir,odd")));
}

void ColorExportTest::noDive_fallsBackToShallowStatic()
{
    QTemporaryDir dir;
    const ColorGrade grade = makeDepthLinkedGrade();
    const ColorGradeExport::Plan plan =
        ColorGradeExport::plan(grade, nullptr, 0.0, 0.0, 60.0, dir.path());
    QVERIFY2(plan.isValid(), qPrintable(plan.error));
    QVERIFY(plan.filterSnippet.startsWith(QStringLiteral("lut3d=file=")));
    QVERIFY(!plan.timeVarying);
    QVERIFY(!plan.notes.isEmpty());

    // The baked cube is exactly the SHALLOW anchor's static grade.
    QFile file(plan.tempFiles.first());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray expected =
        CubeLut::bake(grade.resolveAtDepth(-1.0e9), nullptr, 33);
    QCOMPARE(file.readAll(), expected);
}

void ColorExportTest::missingUserLut_nonFatalNote()
{
    QTemporaryDir dir;
    ColorGrade grade = makeStaticGrade();
    grade.lutPath = QStringLiteral("/nonexistent/look.cube");
    const ColorGradeExport::Plan plan =
        ColorGradeExport::plan(grade, nullptr, 0, 0, 10, dir.path());
    // Plan's rule: missing LUT → warning row, the grade still applies.
    QVERIFY2(plan.isValid(), qPrintable(plan.error));
    QVERIFY(plan.filterSnippet.startsWith(QStringLiteral("lut3d=file=")));
    QCOMPARE(plan.notes.size(), 1);
    QVERIFY(!plan.filterSnippet.contains(QStringLiteral("nonexistent")));
}

void ColorExportTest::userLut_appendedOnDepthPath()
{
    QTemporaryDir dir;
    // A real user LUT next to the work dir.
    const QString lutPath = QDir(dir.path()).filePath(QStringLiteral("user.cube"));
    {
        QFile lutFile(lutPath);
        QVERIFY(lutFile.open(QIODevice::WriteOnly));
        lutFile.write("LUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n"
                      "0 0 1\n1 0 1\n0 1 1\n1 1 1\n");
    }
    DiveData* dive = makeDive(this);
    ColorGrade grade = makeDepthLinkedGrade();
    grade.lutPath = lutPath;
    const ColorGradeExport::Plan plan =
        ColorGradeExport::plan(grade, dive, 0.0, 0.0, 60.0, dir.path());
    QVERIFY2(plan.isValid(), qPrintable(plan.error));
    // The user LUT rides AFTER the parametric chain as its own lut3d.
    QVERIFY(plan.filterSnippet.contains(QStringLiteral(",lut3d=file=")));
    QVERIFY(plan.filterSnippet.endsWith(QStringLiteral(":interp=tetrahedral")));
    QVERIFY(plan.notes.isEmpty());
}

void ColorExportTest::badWorkDir_isError()
{
    const ColorGradeExport::Plan plan = ColorGradeExport::plan(
        makeStaticGrade(), nullptr, 0, 0, 10,
        QStringLiteral("/nonexistent-unabara-workdir/sub"));
    QVERIFY(!plan.isValid());
    QVERIFY(!plan.error.isEmpty());
    QVERIFY(plan.filterSnippet.isEmpty());
}

void ColorExportTest::invalidRange_isError()
{
    QTemporaryDir dir;
    DiveData* dive = makeDive(this);
    const ColorGradeExport::Plan plan = ColorGradeExport::plan(
        makeDepthLinkedGrade(), dive, 0.0, 10.0, 10.0, dir.path());
    QVERIFY(!plan.isValid());
    QVERIFY(plan.error.contains(QStringLiteral("range")));
}

QTEST_GUILESS_MAIN(ColorExportTest)
#include "color_export_test.moc"
