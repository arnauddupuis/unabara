// ClipStrip first-frame thumbnails (follow-up C): the pure half of the
// image://clipthumb/ provider — id encoding through QUrl, path+mtime cache
// keying, failure caching — plus a real-FFmpeg extraction that QSKIPs when
// FFmpeg is not installed.

#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QtTest>

#include "include/core/clip_thumbnail.h"
#include "include/core/ffmpeg_probe.h"
#include "include/core/video_clip.h"

using namespace ClipThumbnail;

namespace {
// What QQuickPixmap hands an image provider for "image://<provider>/<id>":
// the url minus scheme and authority, leading '/' dropped (qquickpixmapcache
// imageProviderId()). Reproduced here so the round trip is tested through
// the same QUrl parsing the engine does.
QString idAsSeenByProvider(const QString& sourceUrl)
{
    const QUrl url(sourceUrl);
    return url.toString(QUrl::RemoveScheme | QUrl::RemoveAuthority).mid(1);
}
} // namespace

class ClipThumbnailTest : public QObject
{
    Q_OBJECT

private slots:
    void idRoundTripThroughUrl_data();
    void idRoundTripThroughUrl();
    void idUsesUrlSafeAlphabet();
    void querySuffixStripped();
    void malformedIdsRejected();
    void urlChangesWithMtime();
    void cacheKeysOnPathAndMtime();
    void failureCachedPerFfmpegPath();
    void videoClipThumbnailUrl();
    void extractRealFrame();
    void extractFailsGracefully();
};

void ClipThumbnailTest::idRoundTripThroughUrl_data()
{
    QTest::addColumn<QString>("path");
    QTest::newRow("plain") << QStringLiteral("/home/u/Videos/GX010001.MP4");
    QTest::newRow("spaces") << QStringLiteral("/home/u/My Videos/dive 1.mp4");
    QTest::newRow("hash") << QStringLiteral("/home/u/clips/dive#2.mp4");
    QTest::newRow("query") << QStringLiteral("/home/u/clips/what?.mp4");
    QTest::newRow("percent") << QStringLiteral("/home/u/clips/100%25 vis.mp4");
    QTest::newRow("plus-amp") << QStringLiteral("/home/u/a+b&c=d.mov");
    QTest::newRow("unicode") << QStringLiteral("/home/u/Plongée/épave ★ 深海.mp4");
    QTest::newRow("windows") << QStringLiteral("C:/Program Files/x#y/clip 1.mp4");
    QTest::newRow("backslash") << QStringLiteral("C:\\Users\\me\\Videos\\a b.mp4");
    QTest::newRow("trailing-digits") << QStringLiteral("/v/clip/123");
}

void ClipThumbnailTest::idRoundTripThroughUrl()
{
    QFETCH(QString, path);
    const qint64 mtime = 1760000000123LL;
    const QString url = QStringLiteral("image://clipthumb/") + encodeId(path, mtime);

    QString outPath;
    qint64 outMtime = 0;
    QVERIFY(decodeId(idAsSeenByProvider(url), &outPath, &outMtime));
    QCOMPARE(outPath, path);
    QCOMPARE(outMtime, mtime);
}

void ClipThumbnailTest::idUsesUrlSafeAlphabet()
{
    // Bytes chosen so standard base64 would emit '+' and '/'.
    const QString path = QString::fromUtf8("/\xef\xbf\xbe\xef\xbb\xbf?>?>~~~");
    const QString id = encodeId(path, 42);
    const QString b64 = id.section(QLatin1Char('/'), 0, 0);
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9_-]+$"));
    QVERIFY2(safe.match(b64).hasMatch(), qPrintable(id));
}

void ClipThumbnailTest::querySuffixStripped()
{
    // ClipStrip appends "?f=<0|1>" (FFmpeg availability) to force a new URL
    // when FFmpeg is configured mid-session; Qt Quick passes the query
    // through into the provider id, and decodeId must strip it.
    const QString path = QStringLiteral("/home/u/Videos/GX010001.MP4");
    const qint64 mtime = 1760000000123LL;
    const QString id = encodeId(path, mtime);

    QString outPath;
    qint64 outMtime = 0;
    QVERIFY(decodeId(id + QStringLiteral("?f=1"), &outPath, &outMtime));
    QCOMPARE(outPath, path);
    QCOMPARE(outMtime, mtime);

    // A query suffix can't rescue an otherwise malformed id.
    QVERIFY(!decodeId(QStringLiteral("abc?f=1"), &outPath, &outMtime));
    QVERIFY(!decodeId(QStringLiteral("L2E/notanumber?f=0"), &outPath, &outMtime));
}

void ClipThumbnailTest::malformedIdsRejected()
{
    QString path = QStringLiteral("untouched");
    qint64 mtime = 7;
    QVERIFY(!decodeId(QString(), &path, &mtime));
    QVERIFY(!decodeId(QStringLiteral("abc"), &path, &mtime));          // no mtime
    QVERIFY(!decodeId(QStringLiteral("/123"), &path, &mtime));         // no path
    QVERIFY(!decodeId(QStringLiteral("L2E/notanumber"), &path, &mtime));
    QVERIFY(!decodeId(QStringLiteral("L2E*!/123"), &path, &mtime));    // bad base64
    QCOMPARE(path, QStringLiteral("untouched"));
    QCOMPARE(mtime, qint64(7));
}

void ClipThumbnailTest::urlChangesWithMtime()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("clip #1.mp4"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    QVERIFY(f.flush()); // else close() flushes and re-stamps the mtime
    QVERIFY(f.setFileTime(QDateTime::fromMSecsSinceEpoch(1700000000000LL),
                          QFileDevice::FileModificationTime));
    f.close();

    const QString url1 = imageUrl(path);
    QVERIFY(url1.startsWith(QStringLiteral("image://clipthumb/")));
    QCOMPARE(fileMtimeMs(path), 1700000000000LL);

    // Overwrite = new mtime = new url (busts Qt Quick's per-url cache).
    QVERIFY(f.open(QIODevice::WriteOnly));
    QVERIFY(f.setFileTime(QDateTime::fromMSecsSinceEpoch(1700000005000LL),
                          QFileDevice::FileModificationTime));
    f.close();
    const QString url2 = imageUrl(path);
    QVERIFY(url1 != url2);

    QString decoded;
    qint64 mtime = 0;
    QVERIFY(decodeId(idAsSeenByProvider(url2), &decoded, &mtime));
    QCOMPARE(decoded, path);
    QCOMPARE(mtime, 1700000005000LL);

    QCOMPARE(fileMtimeMs(dir.filePath(QStringLiteral("missing.mp4"))), qint64(-1));
    QVERIFY(imageUrl(QString()).isEmpty());
}

void ClipThumbnailTest::cacheKeysOnPathAndMtime()
{
    Cache cache;
    QImage red(4, 2, QImage::Format_RGB32);
    red.fill(Qt::red);
    const QString ff = QStringLiteral("/usr/bin/ffmpeg");

    cache.insert(QStringLiteral("/v/a.mp4"), 1000, ff, red);

    QImage out;
    QVERIFY(cache.lookup(QStringLiteral("/v/a.mp4"), 1000, ff, &out));
    QCOMPARE(out, red);
    // Overwritten file (stale mtime) misses.
    QVERIFY(!cache.lookup(QStringLiteral("/v/a.mp4"), 2000, ff, &out));
    // Other path misses.
    QVERIFY(!cache.lookup(QStringLiteral("/v/b.mp4"), 1000, ff, &out));
    // A successful thumbnail is valid whatever FFmpeg is configured now.
    QVERIFY(cache.lookup(QStringLiteral("/v/a.mp4"), 1000, QString(), &out));
    // Key separation: path "x1" + mtime 23 vs path "x" + mtime 123.
    QVERIFY(cacheKey(QStringLiteral("x1"), 23) != cacheKey(QStringLiteral("x"), 123));
}

void ClipThumbnailTest::failureCachedPerFfmpegPath()
{
    Cache cache;
    // Recorded with no FFmpeg available.
    cache.insert(QStringLiteral("/v/a.mp4"), 1000, QString(), QImage());

    QImage out(1, 1, QImage::Format_RGB32);
    QVERIFY(cache.lookup(QStringLiteral("/v/a.mp4"), 1000, QString(), &out));
    QVERIFY(out.isNull()); // cached failure: no relaunch, no repeated log
    // The user then configures FFmpeg → the failure must not stick.
    QVERIFY(!cache.lookup(QStringLiteral("/v/a.mp4"), 1000,
                          QStringLiteral("/opt/ffmpeg/bin/ffmpeg"), &out));
}

void ClipThumbnailTest::videoClipThumbnailUrl()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("a b#c.mp4"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();

    VideoClip clip(path);
    QString decoded;
    QVERIFY(decodeId(idAsSeenByProvider(clip.thumbnailUrl()), &decoded, nullptr));
    QCOMPARE(decoded, path);
}

void ClipThumbnailTest::extractRealFrame()
{
    const QString ffmpeg = FFmpegProbe::instance()->ffmpegPath();
    if (ffmpeg.isEmpty())
        QSKIP("FFmpeg not installed");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Space and '#' in the name: the path goes to FFmpeg as one argv entry.
    const QString clip = dir.filePath(QStringLiteral("red clip #1.mp4"));
    QProcess gen;
    gen.start(ffmpeg, {QStringLiteral("-nostdin"), QStringLiteral("-v"), QStringLiteral("error"),
                       QStringLiteral("-f"), QStringLiteral("lavfi"),
                       QStringLiteral("-i"), QStringLiteral("color=c=red:s=640x360:d=1:r=25"),
                       QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), clip});
    QVERIFY(gen.waitForFinished(30000));
    QCOMPARE(gen.exitCode(), 0);

    QString error;
    const QImage img = extractFirstFrame(ffmpeg, clip, kExtractTimeoutMs, &error);
    QVERIFY2(!img.isNull(), qPrintable(error));
    QCOMPARE(img.height(), kThumbnailHeight);
    QCOMPARE(img.width(), 214); // scale=-2:120 keeps 16:9, rounded to even
    const QColor c = img.pixelColor(img.width() / 2, img.height() / 2);
    QVERIFY2(c.red() > 200 && c.green() < 60 && c.blue() < 60,
             qPrintable(c.name()));
}

void ClipThumbnailTest::extractFailsGracefully()
{
    QString error;
    QVERIFY(extractFirstFrame(QString(), QStringLiteral("/nope.mp4"), 1000, &error).isNull());
    QVERIFY(!error.isEmpty());

    error.clear();
    QVERIFY(extractFirstFrame(QStringLiteral("/usr/bin/ffmpeg"),
                              QStringLiteral("/definitely/missing.mp4"), 1000, &error).isNull());
    QVERIFY(!error.isEmpty());

    const QString ffmpeg = FFmpegProbe::instance()->ffmpegPath();
    if (ffmpeg.isEmpty())
        return;
    // A non-video file: FFmpeg runs and fails → null image, no crash/hang.
    QTemporaryDir dir;
    const QString junk = dir.filePath(QStringLiteral("junk.mp4"));
    QFile f(junk);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(4096, 'z'));
    f.close();
    error.clear();
    QVERIFY(extractFirstFrame(ffmpeg, junk, kExtractTimeoutMs, &error).isNull());
    QVERIFY(!error.isEmpty());

    // Abort flag kills the process promptly.
    std::atomic_bool abort{true};
    error.clear();
    QElapsedTimer t;
    t.start();
    QVERIFY(extractFirstFrame(ffmpeg, junk, kExtractTimeoutMs, &error, &abort).isNull());
    QVERIFY(t.elapsed() < 5000);
}

QTEST_GUILESS_MAIN(ClipThumbnailTest)
#include "clip_thumbnail_test.moc"
