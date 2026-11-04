// Tests for the pure export helpers shared by ImageExporter and VideoExporter
// (include/export/export_math.h). These lock in the frame-count clamp (the
// divide-by-zero class that once existed fixed in one exporter and unfixed in
// the other), the frame naming shared by writers/cleanup/FFmpeg, path
// validation, base-name building, and the partial-frame cleanup semantics.

#include <QtTest>
#include <QTemporaryDir>
#include <QTimeZone>

#include "include/export/export_math.h"
#include "include/export/image_export.h"
#include "include/core/dive_data.h"
#include "include/generators/i_frame_generator.h"

#include <QImage>
#include <QSignalSpy>

namespace {
// Minimal frame source for the ImageExporter cleanup test.
class FlatFrameGenerator : public QObject, public IFrameGenerator
{
public:
    QImage generate(DiveData *, double) override
    {
        QImage img(8, 8, QImage::Format_ARGB32);
        img.fill(Qt::red);
        ++generated;
        return img;
    }
    void beginExport() override { ++beginCalls; }
    void endExport() override { ++endCalls; }
    int generated = 0;
    int beginCalls = 0;
    int endCalls = 0;
};

// Records every dive time it is asked to render — the pre-dive gate test
// asserts the generator is never consulted for t < 0.
class TimeRecordingGenerator : public QObject, public IFrameGenerator
{
public:
    QImage generate(DiveData *, double t) override
    {
        times.append(t);
        QImage img(8, 6, QImage::Format_ARGB32);
        img.fill(Qt::red);
        return img;
    }
    QList<double> times;
};
} // namespace

class ExportMathTest : public QObject
{
    Q_OBJECT

private slots:
    void totalFrames_data();
    void totalFrames();
    void frameTime();
    void frameLoop_noAccumulationWobble();
    void isValidExportPath_data();
    void isValidExportPath();
    void frameFileName();
    void sanitizeFileName();
    void exportBaseName();
    void removeWrittenFiles_keepsForeignFiles();
    void removeWrittenFiles_removesEmptiedDirectory();
    void removeWrittenFiles_removesOnlyListedFiles();
    void outputTailExcerpt_data();
    void outputTailExcerpt();
    void imageExporter_saveFailureRemovesWrittenFrames();
    void isPreDiveTime();
    void imageExporter_preDiveFramesAreTransparent();

private:
    static bool writeFile(const QString &path, const QByteArray &content = "x")
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(content);
        return true;
    }
};

void ExportMathTest::totalFrames_data()
{
    QTest::addColumn<double>("start");
    QTest::addColumn<double>("end");
    QTest::addColumn<double>("fps");
    QTest::addColumn<int>("expected");

    QTest::newRow("normal")            <<   0.0 <<  10.0 << 10.0 << 100;
    QTest::newRow("full dive 30fps")   <<   0.0 << 100.0 << 30.0 << 3000;
    QTest::newRow("rounds")            <<   0.0 <<   1.05 << 10.0 << 11; // qRound(10.5)
    // The degenerate cases: the export loops always write at least one frame
    // (time == startTime), so the progress divisor must never be zero.
    QTest::newRow("zero-length range") <<   5.0 <<   5.0 << 30.0 << 1;
    QTest::newRow("sub-frame range")   <<   0.0 <<   0.01 << 10.0 << 1;
    QTest::newRow("inverted range")    <<  10.0 <<   5.0 << 30.0 << 1;
    QTest::newRow("zero fps")          <<   0.0 <<  10.0 <<  0.0 << 1;
}

void ExportMathTest::totalFrames()
{
    QFETCH(double, start);
    QFETCH(double, end);
    QFETCH(double, fps);
    QFETCH(int, expected);
    QCOMPARE(ExportMath::totalFrames(start, end, fps), expected);
}

void ExportMathTest::frameTime()
{
    // Frame timestamps come from multiplication (start + i/fps), never from
    // accumulating `t += 1/fps`. The comparisons are exact on purpose: an
    // accumulated implementation produces different doubles for large i and
    // fails here.
    QCOMPARE(ExportMath::frameTime(0.0, 0, 30.0), 0.0);
    QCOMPARE(ExportMath::frameTime(5.5, 0, 30.0), 5.5);
    QCOMPARE(ExportMath::frameTime(0.0, 30, 30.0), 30 / 30.0);
    QCOMPARE(ExportMath::frameTime(100.0, 20919, 30.0), 100.0 + 20919 / 30.0);
    QCOMPARE(ExportMath::frameTime(0.0, 107892, 29.97), 107892 / 29.97);

    // Non-positive frame rates: totalFrames() clamps these ranges to one
    // frame, and that frame renders at startTime.
    QCOMPARE(ExportMath::frameTime(7.0, 0, 0.0), 7.0);
    QCOMPARE(ExportMath::frameTime(7.0, 3, -1.0), 7.0);
}

void ExportMathTest::frameLoop_noAccumulationWobble()
{
    // The old exporter loops ran `for (t = start; t <= end; t += 1/fps)`,
    // whose float drift made the frame count depend on the range's ABSOLUTE
    // position: the same 697.3 s at 30 fps exported 20919 frames from t=0
    // but 20920 from t=100 (the "wobble" the v0.4 plan calls out). First,
    // document that the accumulated loop really wobbles on these inputs...
    const auto accumulatedCount = [](double start, double end, double fps) {
        int n = 0;
        const double step = 1.0 / fps;
        for (double t = start; t <= end; t += step)
            ++n;
        return n;
    };
    QVERIFY(accumulatedCount(0.0, 697.3, 30.0)
            != accumulatedCount(100.0, 797.3, 30.0));
    QVERIFY(accumulatedCount(0.0, 60.0, 25.0)
            != accumulatedCount(33.7, 93.7, 25.0));

    // ...then that the integer frame loop both exporters now run —
    // exactly totalFrames() frames at frameTime(start, i) — cannot: equal
    // durations give equal counts, every timestamp stays inside the range,
    // and consecutive frames are one frame period apart.
    const struct { double startA, startB, duration, fps; } ranges[] = {
        {0.0, 100.0, 697.3, 30.0},
        {0.0, 33.7, 60.0, 25.0},
        {0.0, 120.0, 3600.0, 29.97},
    };
    for (const auto &r : ranges) {
        const int framesA = ExportMath::totalFrames(r.startA, r.startA + r.duration, r.fps);
        const int framesB = ExportMath::totalFrames(r.startB, r.startB + r.duration, r.fps);
        QCOMPARE(framesA, framesB);

        for (double start : {r.startA, r.startB}) {
            const double end = start + r.duration;
            double previous = start - 1.0 / r.fps;
            for (int i = 0; i < framesA; ++i) {
                const double t = ExportMath::frameTime(start, i, r.fps);
                QVERIFY(t >= start);
                // The last frame may land a hair past `end` only through the
                // qRound in totalFrames(); allow half a frame period.
                QVERIFY(t <= end + 0.5 / r.fps);
                QVERIFY(qAbs((t - previous) - 1.0 / r.fps) < 1e-9);
                previous = t;
            }
        }
    }
}

void ExportMathTest::isValidExportPath_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("valid");

    // An empty path makes QDir resolve to the working directory — cleanup
    // would then delete frames from wherever the app was launched.
    QTest::newRow("empty")            << QString() << false;
    QTest::newRow("spaces")           << QStringLiteral("   ") << false;
    QTest::newRow("whitespace mix")   << QStringLiteral(" \t\n") << false;
    QTest::newRow("absolute path")    << QStringLiteral("/tmp/export") << true;
    QTest::newRow("padded path")      << QStringLiteral("  /tmp/export  ") << true;
}

void ExportMathTest::isValidExportPath()
{
    QFETCH(QString, path);
    QFETCH(bool, valid);
    QCOMPARE(ExportMath::isValidExportPath(path), valid);
}

void ExportMathTest::frameFileName()
{
    QCOMPARE(ExportMath::frameFileName(0), QStringLiteral("frame_000000.png"));
    QCOMPARE(ExportMath::frameFileName(42), QStringLiteral("frame_000042.png"));
    QCOMPARE(ExportMath::frameFileName(999999), QStringLiteral("frame_999999.png"));
    // Beyond 6 digits the number must widen, not truncate (FFmpeg's %06d
    // pattern behaves the same way).
    QCOMPARE(ExportMath::frameFileName(1234567), QStringLiteral("frame_1234567.png"));
}

void ExportMathTest::sanitizeFileName()
{
    QCOMPARE(ExportMath::sanitizeFileName(QStringLiteral("a/b\\c:d*e?f\"g<h>i|j")),
             QStringLiteral("a_b_c_d_e_f_g_h_i_j"));
    QCOMPARE(ExportMath::sanitizeFileName(QStringLiteral("Blue Hole #2")),
             QStringLiteral("Blue Hole #2"));

    // Long names are capped at 50 characters: 47 kept + "..."
    const QString longName(60, QLatin1Char('x'));
    const QString sanitized = ExportMath::sanitizeFileName(longName);
    QCOMPARE(sanitized.length(), 50);
    QVERIFY(sanitized.endsWith(QStringLiteral("...")));
    QCOMPARE(sanitized.left(47), QString(47, QLatin1Char('x')));
}

void ExportMathTest::exportBaseName()
{
    DiveData dive;
    dive.setStartTime(QDateTime(QDate(2026, 1, 15), QTime(10, 30, 5), QTimeZone::utc()));

    // Date only
    QCOMPARE(ExportMath::exportBaseName(&dive), QStringLiteral("2026-01-15_103005"));

    // Every optional part, each sanitized
    dive.setDiveName(QStringLiteral("Wreck: Zenobia"));
    dive.setLocation(QStringLiteral("Blue/Hole"));
    QCOMPARE(ExportMath::exportBaseName(&dive,
                                        QStringLiteral("/videos/GOPR0001.MP4"),
                                        QStringLiteral("dive_computer")),
             QStringLiteral("2026-01-15_103005_Wreck_ Zenobia_Blue_Hole_GOPR0001_dive_computer"));

    // The video stem drops only the last extension (completeBaseName keeps
    // dotted names intact up to the final dot)
    QCOMPARE(ExportMath::exportBaseName(&dive, QStringLiteral("/videos/clip.v2.mp4")),
             QStringLiteral("2026-01-15_103005_Wreck_ Zenobia_Blue_Hole_clip.v2"));
}

static QStringList frameNames(int count)
{
    QStringList names;
    for (int i = 0; i < count; ++i)
        names.append(ExportMath::frameFileName(i));
    return names;
}

void ExportMathTest::removeWrittenFiles_keepsForeignFiles()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dirPath = tmp.filePath(QStringLiteral("export"));
    QVERIFY(QDir().mkpath(dirPath));

    for (const QString &name : frameNames(5))
        QVERIFY(writeFile(QDir(dirPath).filePath(name)));
    QVERIFY(writeFile(QDir(dirPath).filePath(QStringLiteral("keep.txt"))));

    ExportMath::removeWrittenFiles(dirPath, frameNames(5));

    // Frames gone, the user's file untouched, and the (non-empty) directory
    // itself preserved — rmdir must not force-remove it.
    QVERIFY(QDir(dirPath).exists());
    QVERIFY(QFile::exists(QDir(dirPath).filePath(QStringLiteral("keep.txt"))));
    for (const QString &name : frameNames(5))
        QVERIFY(!QFile::exists(QDir(dirPath).filePath(name)));
}

void ExportMathTest::removeWrittenFiles_removesEmptiedDirectory()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dirPath = tmp.filePath(QStringLiteral("export"));
    QVERIFY(QDir().mkpath(dirPath));

    for (const QString &name : frameNames(3))
        QVERIFY(writeFile(QDir(dirPath).filePath(name)));

    ExportMath::removeWrittenFiles(dirPath, frameNames(3));

    // Nothing but this run's files was inside, so the directory goes too.
    QVERIFY(!QDir(dirPath).exists());
}

void ExportMathTest::removeWrittenFiles_removesOnlyListedFiles()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dirPath = tmp.filePath(QStringLiteral("export"));
    QVERIFY(QDir().mkpath(dirPath));

    for (const QString &name : frameNames(6))
        QVERIFY(writeFile(QDir(dirPath).filePath(name)));

    // Only frames 0..4 were recorded by "this run" — frame 5 (e.g. from an
    // earlier export into the same directory) must survive, which is the
    // point of tracking written names instead of deleting by pattern.
    ExportMath::removeWrittenFiles(dirPath, frameNames(5));

    QVERIFY(QDir(dirPath).exists());
    QVERIFY(QFile::exists(QDir(dirPath).filePath(ExportMath::frameFileName(5))));
    for (const QString &name : frameNames(5))
        QVERIFY(!QFile::exists(QDir(dirPath).filePath(name)));
}

void ExportMathTest::outputTailExcerpt_data()
{
    QTest::addColumn<QString>("tail");
    QTest::addColumn<int>("maxLines");
    QTest::addColumn<QString>("expected");

    QTest::newRow("empty") << QString() << 3 << QString();
    QTest::newRow("whitespace only") << QStringLiteral("  \n\t\n\r\n") << 3 << QString();
    QTest::newRow("single line, trimmed")
        << QStringLiteral("  Conversion failed!  \n") << 3 << QStringLiteral("Conversion failed!");
    QTest::newRow("last three non-empty of many")
        << QStringLiteral("a\nb\n\nc\n   \nd\ne\n\n") << 3 << QStringLiteral("c\nd\ne");
    QTest::newRow("fewer than max")
        << QStringLiteral("x\n\ny") << 3 << QStringLiteral("x\ny");
    QTest::newRow("CRLF and bare CR")
        << QStringLiteral("one\r\ntwo\rthree\r\nfour\r\n") << 2 << QStringLiteral("three\nfour");
    QTest::newRow("maxLines 0") << QStringLiteral("a\nb") << 0 << QString();
    // Per-line cap at 300 chars: a single huge FFmpeg filter-graph dump
    // line must not flood the fixed-size message dialog.
    QTest::newRow("overlong line elided")
        << (QStringLiteral("short\n") + QString(500, QLatin1Char('x')))
        << 3
        << (QStringLiteral("short\n") + QString(299, QLatin1Char('x'))
            + QChar(0x2026));
}

void ExportMathTest::outputTailExcerpt()
{
    QFETCH(QString, tail);
    QFETCH(int, maxLines);
    QFETCH(QString, expected);
    QCOMPARE(ExportMath::outputTailExcerpt(tail, maxLines), expected);
}

void ExportMathTest::imageExporter_saveFailureRemovesWrittenFrames()
{
    // A save failure mid-run must leave no partial sequence behind — the
    // same recorded-names cleanup as cancellation, never a name pattern.
    // Frame 1 is made unwritable by a pre-existing DIRECTORY at its path
    // (QImage::save onto a directory fails on every platform, no permission
    // games); frame 0 is written, then must be removed.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dirPath = tmp.filePath(QStringLiteral("export"));
    const QString blocker = QDir(dirPath).filePath(ExportMath::frameFileName(1));
    QVERIFY(QDir().mkpath(blocker));

    DiveData dive;
    FlatFrameGenerator gen;
    ImageExporter exporter;
    exporter.setExportPath(dirPath);
    exporter.setFrameRate(10.0);

    QSignalSpy errorSpy(&exporter, &ImageExporter::exportError);
    QSignalSpy finishedSpy(&exporter, &ImageExporter::exportFinished);
    QVERIFY(!exporter.exportImageRange(&dive, &gen, 0.0, 0.5)); // 5 frames requested

    QCOMPARE(errorSpy.count(), 1);
    QVERIFY2(errorSpy.first().first().toString().contains(QLatin1String("Failed to save image")),
             qPrintable(errorSpy.first().first().toString()));
    QVERIFY(finishedSpy.isEmpty());
    QVERIFY(!exporter.isBusy());
    QCOMPARE(gen.generated, 2);
    QCOMPARE(gen.beginCalls, 1);
    QCOMPARE(gen.endCalls, 1);

    // This run's written frame is gone; the foreign blocker (not recorded
    // by the run) survives, and so does the directory holding it.
    QVERIFY(!QFile::exists(QDir(dirPath).filePath(ExportMath::frameFileName(0))));
    QVERIFY(QFileInfo(blocker).isDir());
    QVERIFY(QDir(dirPath).exists());
}

void ExportMathTest::isPreDiveTime()
{
    QVERIFY(ExportMath::isPreDiveTime(-0.001));
    QVERIFY(ExportMath::isPreDiveTime(-120.0));
    QVERIFY(!ExportMath::isPreDiveTime(0.0));   // first sample renders
    QVERIFY(!ExportMath::isPreDiveTime(-0.0));
    QVERIFY(!ExportMath::isPreDiveTime(0.001));
    QVERIFY(!ExportMath::isPreDiveTime(99999.0)); // post-dive not gated
}

void ExportMathTest::imageExporter_preDiveFramesAreTransparent()
{
    // An "imported video range" export whose clip starts before the dive:
    // range [-0.5, 0.5) at 10 fps = 10 frames, the first 5 pre-dive. Those
    // must be fully transparent PNGs of the run's size WITHOUT consulting
    // the generator (dataAtTime would clamp to sample 0 and burn a frozen
    // t=0 overlay); the frame count is the full range, never clamped.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dirPath = tmp.filePath(QStringLiteral("export"));

    DiveData dive;
    TimeRecordingGenerator gen;
    ImageExporter exporter;
    exporter.setExportPath(dirPath);
    exporter.setFrameRate(10.0);

    QSignalSpy finishedSpy(&exporter, &ImageExporter::exportFinished);
    QVERIFY(exporter.exportImageRange(&dive, &gen, -0.5, 0.5));
    QCOMPARE(finishedSpy.count(), 1);

    // (a) never asked for a negative time; asked for every t >= 0 frame
    // (plus at most one t = 0 sizing render for the transparent frames).
    for (double t : std::as_const(gen.times))
        QVERIFY2(t >= 0.0, qPrintable(QString::number(t)));
    for (int i = 5; i < 10; ++i) {
        const double t = ExportMath::frameTime(-0.5, i, 10.0);
        QVERIFY2(gen.times.contains(t), qPrintable(QString::number(t)));
    }
    QVERIFY(gen.times.size() <= 6);

    // (c) + (d) full frame count, standard contiguous naming, nothing else.
    const QStringList files =
        QDir(dirPath).entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    QCOMPARE(files.size(), 10);
    for (int i = 0; i < 10; ++i)
        QCOMPARE(files.at(i), ExportMath::frameFileName(i));

    // (b) pre-dive PNGs: generator size, every pixel alpha 0; dive frames
    // carry the generator's content.
    for (int i = 0; i < 10; ++i) {
        const QImage img(QDir(dirPath).filePath(files.at(i)));
        QVERIFY(!img.isNull());
        QCOMPARE(img.size(), QSize(8, 6));
        const QImage argb = img.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < argb.height(); ++y) {
            for (int x = 0; x < argb.width(); ++x) {
                const QRgb px = argb.pixel(x, y);
                if (i < 5)
                    QVERIFY2(qAlpha(px) == 0,
                             qPrintable(QStringLiteral("frame %1 not transparent").arg(i)));
                else
                    QCOMPARE(px, qRgb(255, 0, 0));
            }
        }
    }
}

QTEST_GUILESS_MAIN(ExportMathTest)
#include "export_math_test.moc"
