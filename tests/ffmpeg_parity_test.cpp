// End-to-end parity between GradeOps::apply() (the pipeline's ground
// truth) and what FFmpeg actually renders from ColorGradeExport::plan()'s
// filter snippets. QSKIPs when FFmpeg is not installed (FFmpegProbe
// discovery). Two checks:
//  - static path: lut3d on the baked 33³ cube over a 64×64 gradient PNG,
//    asserted ≤ 1 LSB per pixel against apply() (the plan's claim) — run
//    from a work dir containing spaces and a comma so the escaping is
//    exercised for real, not just as strings;
//  - depth-linked path: the sendcmd + colorchannelmixer@cc + lutrgb@cv
//    chain over a 10-frame synthetic clip with a ramping dive, asserted
//    ≤ 2 LSB per frame against apply() at that frame's depth — this
//    executes the command file, the \, escaping and the lutrgb
//    expressions (including the +0.5 truncation fix) inside real FFmpeg.

#include <QtTest>
#include <QDir>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>

#include "include/core/color_grade.h"
#include "include/core/color_grade_export.h"
#include "include/core/dive_data.h"
#include "include/core/ffmpeg_probe.h"

using namespace Unabara;

namespace {

QImage makeGradient(int size = 64)
{
    QImage image(size, size, QImage::Format_RGB888);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const int r = x * 255 / (size - 1);
            const int g = y * 255 / (size - 1);
            const int b = (x + y) * 255 / (2 * (size - 1));
            image.setPixelColor(x, y, QColor(r, g, b));
        }
    }
    return image;
}

} // namespace

class FfmpegParityTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void staticPath_lut3dWithin1Lsb();
    void depthLinkedPath_sendcmdWithin2Lsb();

private:
    QString m_ffmpeg;
};

void FfmpegParityTest::initTestCase()
{
    m_ffmpeg = FFmpegProbe::instance()->ffmpegPath();
    if (m_ffmpeg.isEmpty())
        QSKIP("FFmpeg not found — parity test skipped");
    if (!FFmpegProbe::instance()->hasFilter(QStringLiteral("lut3d"))
        || !FFmpegProbe::instance()->hasFilter(QStringLiteral("sendcmd")))
        QSKIP("FFmpeg lacks lut3d/sendcmd — parity test skipped");
}

void FfmpegParityTest::staticPath_lut3dWithin1Lsb()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());
    // Work dir with a space and a comma: the escaping must survive real
    // FFmpeg filtergraph parsing, not just the string tests.
    const QString odd = QStringLiteral("work dir,odd");
    QVERIFY(QDir(base.path()).mkdir(odd));
    const QString workDir = QDir(base.path()).filePath(odd);

    ColorGrade grade;
    grade.enabled = true;
    GradeAnchor anchor;
    anchor.params.redRecovery = 0.6;
    anchor.params.waterTint = 0.35;
    anchor.params.clarity = 0.4;
    grade.anchors << anchor;

    const ColorGradeExport::Plan plan =
        ColorGradeExport::plan(grade, nullptr, 0, 0, 10, workDir);
    QVERIFY2(plan.isValid(), qPrintable(plan.error));
    QVERIFY(plan.filterSnippet.startsWith(QStringLiteral("lut3d=")));

    const QImage input = makeGradient();
    const QString inPath = QDir(base.path()).filePath(QStringLiteral("in.png"));
    const QString outPath = QDir(base.path()).filePath(QStringLiteral("out.png"));
    QVERIFY(input.save(inPath));

    QProcess ffmpeg;
    ffmpeg.start(m_ffmpeg,
                 {QStringLiteral("-y"), QStringLiteral("-hide_banner"),
                  QStringLiteral("-loglevel"), QStringLiteral("error"),
                  QStringLiteral("-i"), inPath,
                  QStringLiteral("-filter_complex"),
                  QStringLiteral("format=rgb24,") + plan.filterSnippet
                      + QStringLiteral(",format=rgb24"),
                  QStringLiteral("-frames:v"), QStringLiteral("1"), outPath});
    QVERIFY(ffmpeg.waitForFinished(30000));
    QVERIFY2(ffmpeg.exitCode() == 0,
             qPrintable(QString::fromUtf8(ffmpeg.readAllStandardError())));

    const QImage output(outPath);
    QVERIFY(!output.isNull());
    QCOMPARE(output.size(), input.size());

    const GradeOps ops = grade.resolveStatic();
    int maxDiff = 0;
    for (int y = 0; y < input.height(); ++y) {
        for (int x = 0; x < input.width(); ++x) {
            const QColor in = input.pixelColor(x, y);
            double r = in.red() / 255.0;
            double g = in.green() / 255.0;
            double b = in.blue() / 255.0;
            ops.apply(r, g, b);
            const QColor out = output.pixelColor(x, y);
            const int dr = qAbs(out.red() - qRound(r * 255.0));
            const int dg = qAbs(out.green() - qRound(g * 255.0));
            const int db = qAbs(out.blue() - qRound(b * 255.0));
            maxDiff = qMax(maxDiff, qMax(dr, qMax(dg, db)));
        }
    }
    QVERIFY2(maxDiff <= 1,
             qPrintable(QStringLiteral("static-path max diff %1 LSB (claim: <= 1)")
                            .arg(maxDiff)));
}

void FfmpegParityTest::depthLinkedPath_sendcmdWithin2Lsb()
{
    QTemporaryDir base;
    QVERIFY(base.isValid());

    // Dive: linear 0→30 m over 10 s.
    DiveData dive;
    dive.addDataPoint(DiveDataPoint(0.0, 0.0));
    dive.addDataPoint(DiveDataPoint(10.0, 30.0));

    ColorGrade grade;
    grade.enabled = true;
    grade.depthLinked = true;
    GradeAnchor shallow;
    shallow.depth = 0.0;
    shallow.params.redRecovery = 0.1;
    shallow.params.clarity = 0.2;
    GradeAnchor deep;
    deep.depth = 30.0;
    deep.params.redRecovery = 1.0;
    deep.params.waterTint = -0.6;
    deep.params.clarity = 0.6;
    grade.anchors << shallow << deep;

    const ColorGradeExport::Plan plan =
        ColorGradeExport::plan(grade, &dive, 0.0, 0.0, 10.0, base.path());
    QVERIFY2(plan.isValid(), qPrintable(plan.error));
    QVERIFY(plan.timeVarying);

    // The gradient PNG looped at 1 fps for 10 frames: an RGB-exact source
    // (lavfi `color` negotiates through YUV and lands ±2 off per channel),
    // so the reference input values are the PNG's own bytes. Frame k has
    // pts k, so the command block for t=k applies to it.
    const QImage input = makeGradient(16);
    const QString inPath = QDir(base.path()).filePath(QStringLiteral("in.png"));
    QVERIFY(input.save(inPath));

    QProcess ffmpeg;
    ffmpeg.start(m_ffmpeg,
                 {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                  QStringLiteral("error"), QStringLiteral("-loop"),
                  QStringLiteral("1"), QStringLiteral("-framerate"),
                  QStringLiteral("1"), QStringLiteral("-i"), inPath,
                  QStringLiteral("-vf"),
                  QStringLiteral("format=rgb24,") + plan.filterSnippet,
                  QStringLiteral("-frames:v"), QStringLiteral("10"),
                  QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
                  QStringLiteral("-f"), QStringLiteral("rawvideo"),
                  QStringLiteral("-")});
    QVERIFY(ffmpeg.waitForFinished(30000));
    QVERIFY2(ffmpeg.exitCode() == 0,
             qPrintable(QString::fromUtf8(ffmpeg.readAllStandardError())));
    const QByteArray raw = ffmpeg.readAllStandardOutput();
    const int w = input.width();
    const int h = input.height();
    const int frameBytes = w * h * 3;
    QCOMPARE(raw.size(), qsizetype(frameBytes) * 10);

    int maxDiff = 0;
    for (int frame = 0; frame < 10; ++frame) {
        const double depth = dive.dataAtTime(frame).depth;
        const GradeOps ops = grade.resolveAtDepth(depth);
        const uchar* px =
            reinterpret_cast<const uchar*>(raw.constData()) + frame * frameBytes;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const QColor in = input.pixelColor(x, y);
                double r = in.red() / 255.0;
                double g = in.green() / 255.0;
                double b = in.blue() / 255.0;
                ops.apply(r, g, b);
                const uchar* p = px + (y * w + x) * 3;
                const int dr = qAbs(int(p[0]) - qRound(r * 255.0));
                const int dg = qAbs(int(p[1]) - qRound(g * 255.0));
                const int db = qAbs(int(p[2]) - qRound(b * 255.0));
                const int diff = qMax(dr, qMax(dg, db));
                if (diff > maxDiff)
                    maxDiff = diff;
                QVERIFY2(diff <= 2,
                         qPrintable(QStringLiteral(
                                        "frame %1 (depth %2 m) pixel %3,%4: got "
                                        "(%5,%6,%7), expected (%8,%9,%10)")
                                        .arg(frame).arg(depth).arg(x).arg(y)
                                        .arg(p[0]).arg(p[1]).arg(p[2])
                                        .arg(qRound(r * 255.0))
                                        .arg(qRound(g * 255.0))
                                        .arg(qRound(b * 255.0))));
            }
        }
    }
    qInfo("depth-linked parity max diff: %d LSB (tolerance 2)", maxDiff);
}

// QTEST_MAIN (not GUILESS): QImage PNG I/O wants the Gui application's
// plugin machinery; the suite runs offscreen via QT_QPA_PLATFORM.
QTEST_MAIN(FfmpegParityTest)
#include "ffmpeg_parity_test.moc"
