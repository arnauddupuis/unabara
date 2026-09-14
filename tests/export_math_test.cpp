// Tests for the pure export helpers shared by ImageExporter and VideoExporter
// (include/export/export_math.h). These lock in the frame-count clamp (the
// divide-by-zero class that once existed fixed in one exporter and unfixed in
// the other), the frame naming shared by writers/cleanup/FFmpeg, path
// validation, base-name building, and the partial-frame cleanup semantics.

#include <QtTest>
#include <QTemporaryDir>
#include <QTimeZone>

#include "include/export/export_math.h"
#include "include/core/dive_data.h"

class ExportMathTest : public QObject
{
    Q_OBJECT

private slots:
    void totalFrames_data();
    void totalFrames();
    void isValidExportPath_data();
    void isValidExportPath();
    void frameFileName();
    void framePatternMatchesFileName();
    void sanitizeFileName();
    void exportBaseName();
    void removeWrittenFiles_keepsForeignFiles();
    void removeWrittenFiles_removesEmptiedDirectory();
    void removeWrittenFiles_removesOnlyListedFiles();

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

void ExportMathTest::framePatternMatchesFileName()
{
    // The FFmpeg input pattern and the writer/cleanup naming must agree, or
    // the encoder silently finds no input frames.
    const QString fromPattern =
        QString::asprintf(ExportMath::framePattern().toUtf8().constData(), 42);
    QCOMPARE(fromPattern, ExportMath::frameFileName(42));
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

QTEST_GUILESS_MAIN(ExportMathTest)
#include "export_math_test.moc"
