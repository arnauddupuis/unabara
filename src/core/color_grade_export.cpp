#include "include/core/color_grade_export.h"

#include <QDir>
#include <QFile>

#include <cmath>

#include "include/core/cube_lut.h"
#include "include/core/dive_data.h"

namespace Unabara {

namespace {

const char* const kCubeFileName = "unabara_grade.cube";
const char* const kCmdFileName = "unabara_grade.cmd";

// 0.5 s re-command cadence: ≤1 LSB steps at realistic ascent rates, and the
// dedup below collapses constant-depth stretches.
const double kCommandCadence = 0.5;

QString num(double value)
{
    return QString::number(value, 'f', 6);
}

QString mixerOptions(const GradeOps& ops)
{
    // colorchannelmixer option names: <out><in>, rows in our matrix order.
    return QStringLiteral("rr=%1:rg=%2:rb=%3:gr=%4:gg=%5:gb=%6:br=%7:bg=%8:bb=%9")
        .arg(num(ops.matrix[0][0]), num(ops.matrix[0][1]), num(ops.matrix[0][2]),
             num(ops.matrix[1][0]), num(ops.matrix[1][1]), num(ops.matrix[1][2]),
             num(ops.matrix[2][0]), num(ops.matrix[2][1]), num(ops.matrix[2][2]));
}

// The 12 re-commanded values for one timestamp (9 matrix + 3 lutrgb
// expressions), without the timestamp — also the dedup comparison key
// ("unchanged at emitted precision" = this string is identical).
QString commandBlock(const GradeOps& ops)
{
    QStringList commands;
    static const char* const mixerNames[3][3] = {
        {"rr", "rg", "rb"}, {"gr", "gg", "gb"}, {"br", "bg", "bb"}};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            commands << QStringLiteral("colorchannelmixer@cc %1 %2")
                            .arg(QLatin1String(mixerNames[i][j]), num(ops.matrix[i][j]));
        }
    }
    static const char* const lutChannels[3] = {"r", "g", "b"};
    for (int i = 0; i < 3; ++i) {
        const QString expr = ColorGradeExport::lutrgbExpression(
            ops.black[i], ops.lift[i], ops.contrast);
        commands << QStringLiteral("lutrgb@cv %1 %2")
                        .arg(QLatin1String(lutChannels[i]),
                             ColorGradeExport::escapeSendcmdArg(expr));
    }
    return commands.join(QStringLiteral(", "));
}

bool writeFile(const QString& path, const QByteArray& bytes, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        *error = QStringLiteral("Short write to %1").arg(path);
        return false;
    }
    return true;
}

} // namespace

QString ColorGradeExport::escapeFilterOption(const QString& value)
{
    // Level 1 — option value escaping (av_get_token in the option parser):
    // backslash first, then quote and colon.
    QString s = value;
    s.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    s.replace(QLatin1Char('\''), QLatin1String("\\'"));
    s.replace(QLatin1Char(':'), QLatin1String("\\:"));
    // Level 2 — filtergraph escaping (the graph tokenizer unescapes once
    // more): backslash again first, then quote and the graph separators.
    s.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    s.replace(QLatin1Char('\''), QLatin1String("\\'"));
    s.replace(QLatin1Char(','), QLatin1String("\\,"));
    s.replace(QLatin1Char(';'), QLatin1String("\\;"));
    s.replace(QLatin1Char('['), QLatin1String("\\["));
    s.replace(QLatin1Char(']'), QLatin1String("\\]"));
    return s;
}

QString ColorGradeExport::escapeSendcmdArg(const QString& value)
{
    QString s = value;
    s.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    s.replace(QLatin1Char(','), QLatin1String("\\,"));
    s.replace(QLatin1Char(';'), QLatin1String("\\;"));
    return s;
}

QString ColorGradeExport::lutrgbExpression(double black, double lift, double contrast)
{
    // Mirrors GradeOps::apply() stages 2–4 on val ∈ [0,255]:
    //   st(0, ...)   black point, clamped
    //   st(1, ...)   midtone lift, clamped (av_expr evaluates left to
    //                right, so ld(0) inside reads the st(0) just stored)
    //   if(...)      S-curve about 0.5 on ld(1)
    // The trailing +0.5 is load-bearing: lutrgb TRUNCATES its result
    // (verified on FFmpeg 7.1.5), so without it every output would be
    // biased a full LSB low.
    const QString b = num(black);
    const QString l = num(lift);
    const QString c = num(contrast);
    return QStringLiteral(
               "(if(lt(st(1,clip(st(0,clip((val/255-%1)/(1-%1),0,1))"
               "+%2*4*ld(0)*ld(0)*(1-ld(0)),0,1)),0.5),"
               "0.5*pow(2*ld(1),%3),1-0.5*pow(2*(1-ld(1)),%3)))*255+0.5")
        .arg(b, l, c);
}

bool ColorGradeExport::usesDepthPath(const ColorGrade& grade, const DiveData* dive)
{
    return grade.enabled && !grade.anchors.isEmpty() && grade.depthLinked
           && dive && grade.hasDepthRamp();
}

ColorGradeExport::Plan ColorGradeExport::plan(const ColorGrade& grade,
                                              const DiveData* dive,
                                              double clipOffset, double in,
                                              double out, const QString& workDir)
{
    Plan result;

    // Disabled or empty grade: nothing to insert (B treats an empty
    // snippet as "no colour pass").
    if (!grade.enabled || grade.anchors.isEmpty())
        return result;

    // User LUT: missing/invalid is non-fatal by design — the parametric
    // grade still applies, the note becomes a warning row in the UI.
    CubeLut userLut;
    const CubeLut* userLutPtr = nullptr;
    if (!grade.lutPath.isEmpty()) {
        QString lutError;
        userLut = CubeLut::parseFile(grade.lutPath, &lutError);
        if (userLut.isValid()) {
            userLutPtr = &userLut;
        } else {
            result.notes << QStringLiteral(
                                "User LUT could not be loaded (%1) — the grade is "
                                "exported without it").arg(lutError);
        }
    }

    const bool depthPath = usesDepthPath(grade, dive);

    if (!depthPath) {
        // Static path. A depth-linked grade without a dive log (or without
        // a usable ramp) clamps to the shallow anchor, like dataAtTime
        // clamps outside the log.
        GradeOps ops;
        if (grade.depthLinked) {
            ops = grade.resolveAtDepth(-1.0e9); // weight clamps to the shallow anchor
            if (!dive && grade.hasDepthRamp())
                result.notes << QStringLiteral(
                    "Depth link has no dive log — exporting the shallow "
                    "anchor as a static grade");
        } else {
            ops = grade.resolveStatic();
        }

        const QString cubePath = QDir(workDir).filePath(QLatin1String(kCubeFileName));
        const QByteArray cube = CubeLut::bake(ops, userLutPtr, 33);
        if (!writeFile(cubePath, cube, &result.error))
            return result;
        result.tempFiles << cubePath;
        result.filterSnippet = QStringLiteral("lut3d=file=%1:interp=tetrahedral")
                                   .arg(escapeFilterOption(cubePath));
        return result;
    }

    // Depth-linked path: sendcmd + colorchannelmixer@cc + lutrgb@cv.
    if (!(out > in)) {
        result.error = QStringLiteral("Invalid export range (out <= in)");
        return result;
    }

    const double duration = out - in;
    QStringList lines;
    QString lastBlock;
    GradeOps initialOps;
    int emitted = 0;
    // Frame pts entering the snippet start at 0 at the in-point, so the
    // command timestamps are output-local and the dive time of command
    // time t is clipOffset + in + t.
    for (double t = 0.0; t <= duration + 1e-9; t += kCommandCadence) {
        const double diveTime = clipOffset + in + t;
        const double depth = dive->dataAtTime(diveTime).depth;
        const GradeOps ops = grade.resolveAtDepth(depth);
        const QString block = commandBlock(ops);
        if (emitted == 0)
            initialOps = ops;
        else if (block == lastBlock)
            continue; // dedup: everything unchanged at emitted precision
        lines << QStringLiteral("%1 %2;").arg(QString::number(t, 'f', 3), block);
        lastBlock = block;
        ++emitted;
    }

    const QString cmdPath = QDir(workDir).filePath(QLatin1String(kCmdFileName));
    if (!writeFile(cmdPath, lines.join(QLatin1Char('\n')).toUtf8() + '\n', &result.error))
        return result;
    result.tempFiles << cmdPath;

    QString snippet = QStringLiteral("sendcmd=f=%1,colorchannelmixer@cc=%2,"
                                     "lutrgb@cv=r='%3':g='%4':b='%5'")
                          .arg(escapeFilterOption(cmdPath), mixerOptions(initialOps),
                               lutrgbExpression(initialOps.black[0], initialOps.lift[0],
                                                initialOps.contrast),
                               lutrgbExpression(initialOps.black[1], initialOps.lift[1],
                                                initialOps.contrast),
                               lutrgbExpression(initialOps.black[2], initialOps.lift[2],
                                                initialOps.contrast));
    if (userLutPtr) {
        snippet += QStringLiteral(",lut3d=file=%1:interp=tetrahedral")
                       .arg(escapeFilterOption(grade.lutPath));
    }
    result.filterSnippet = snippet;
    result.timeVarying = emitted > 1;
    return result;
}

} // namespace Unabara
