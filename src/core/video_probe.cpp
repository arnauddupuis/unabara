#include "include/core/video_probe.h"
#include "include/core/ffmpeg_probe.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

#include <cmath>

namespace {

// ffprobe emits numbers as JSON strings ("duration": "120.500000") — accept
// both string and numeric forms.
double jsonToDouble(const QJsonValue& v, double fallback = -1.0)
{
    if (v.isDouble())
        return v.toDouble();
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        if (ok)
            return d;
    }
    return fallback;
}

qint64 jsonToLongLong(const QJsonValue& v, qint64 fallback = -1)
{
    if (v.isDouble())
        return static_cast<qint64>(v.toDouble());
    if (v.isString()) {
        bool ok = false;
        const qint64 n = v.toString().toLongLong(&ok);
        if (ok)
            return n;
    }
    return fallback;
}

// "30000/1001" → (30000, 1001); false when absent/degenerate ("0/0").
bool parseRational(const QString& s, int& num, int& den)
{
    const int slash = s.indexOf(QLatin1Char('/'));
    bool okNum = false;
    bool okDen = true;
    int n = 0;
    int d = 1;
    if (slash < 0) {
        n = s.toInt(&okNum);
    } else {
        n = s.left(slash).toInt(&okNum);
        d = s.mid(slash + 1).toInt(&okDen);
    }
    if (!okNum || !okDen || n <= 0 || d <= 0)
        return false;
    num = n;
    den = d;
    return true;
}

QDateTime parseCreationTime(const QString& s)
{
    QDateTime dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(s, Qt::ISODate);
    if (dt.isValid())
        dt = dt.toUTC();
    return dt;
}

int bitDepthFromStream(const QJsonObject& stream, const QString& pixFmt)
{
    const qint64 raw = jsonToLongLong(stream.value(QStringLiteral("bits_per_raw_sample")), -1);
    if (raw > 0)
        return static_cast<int>(raw);
    // Fall back to the pix_fmt name: yuv420p10le, gbrp12be, yuv444p16 …
    const QRegularExpression re(QStringLiteral("p(\\d{1,2})(?:le|be)?$"));
    const QRegularExpressionMatch m = re.match(pixFmt);
    if (m.hasMatch()) {
        const int bits = m.captured(1).toInt();
        if (bits >= 8 && bits <= 16)
            return bits;
    }
    return 8;
}

// Display rotation: Qt-era files put it in side_data_list ("rotation", often
// negative); older MP4s use the "rotate" stream tag. Normalized to 0..359.
int rotationFromStream(const QJsonObject& stream)
{
    int rotation = 0;
    const QJsonArray sideData = stream.value(QStringLiteral("side_data_list")).toArray();
    for (const QJsonValue& sdVal : sideData) {
        const QJsonObject sd = sdVal.toObject();
        if (sd.contains(QStringLiteral("rotation"))) {
            rotation = static_cast<int>(std::lround(
                jsonToDouble(sd.value(QStringLiteral("rotation")), 0.0)));
            break;
        }
    }
    if (rotation == 0) {
        const QJsonObject tags = stream.value(QStringLiteral("tags")).toObject();
        if (tags.contains(QStringLiteral("rotate")))
            rotation = static_cast<int>(std::lround(
                jsonToDouble(tags.value(QStringLiteral("rotate")), 0.0)));
    }
    rotation = ((rotation % 360) + 360) % 360;
    // Snap to the quarter turns containers actually use.
    if (rotation % 90 != 0)
        rotation = 0;
    return rotation;
}

} // namespace

double VideoProbe::parseTimecodeToSecondsOfDay(const QString& timecode)
{
    // HH:MM:SS:FF, or HH:MM:SS;FF for drop-frame. Frames are ignored (see
    // the header note). The frame field can be 3 digits: av_timecode
    // formats with %02d, which widens past 99 for 120/240 fps footage.
    const QRegularExpression re(
        QStringLiteral("^(\\d{2}):(\\d{2}):(\\d{2})[:;](\\d{2,3})$"));
    const QRegularExpressionMatch m = re.match(timecode.trimmed());
    if (!m.hasMatch())
        return -1.0;
    const int hours = m.captured(1).toInt();
    const int minutes = m.captured(2).toInt();
    const int seconds = m.captured(3).toInt();
    return hours * 3600.0 + minutes * 60.0 + seconds;
}

VideoProbe::Result VideoProbe::parseFfprobeJson(const QByteArray& json)
{
    Result r;

    if (json.trimmed().isEmpty()) {
        r.error = QStringLiteral("empty ffprobe output");
        return r;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        r.error = QStringLiteral("ffprobe JSON parse error: %1")
                      .arg(parseError.errorString());
        return r;
    }

    const QJsonObject root = doc.object();
    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();

    // Pick the real video stream: skip attached pictures (cover art shows up
    // as a video stream with disposition.attached_pic = 1).
    QJsonObject video;
    bool haveVideo = false;
    QString timecodeStr;
    for (const QJsonValue& sVal : streams) {
        const QJsonObject s = sVal.toObject();
        const QString type = s.value(QStringLiteral("codec_type")).toString();
        const QJsonObject tags = s.value(QStringLiteral("tags")).toObject();

        if (!haveVideo && type == QLatin1String("video")) {
            const QJsonObject disposition =
                s.value(QStringLiteral("disposition")).toObject();
            if (disposition.value(QStringLiteral("attached_pic")).toInt() != 1) {
                video = s;
                haveVideo = true;
            }
        } else if (!r.hasAudio && type == QLatin1String("audio")) {
            r.hasAudio = true;
            r.audioCodec = s.value(QStringLiteral("codec_name")).toString();
        }

        // Timecode: a tmcd/data stream wins (GoPro, pro cameras)…
        if (timecodeStr.isEmpty()
            && (s.value(QStringLiteral("codec_name")).toString() == QLatin1String("tmcd")
                || type == QLatin1String("data"))
            && tags.contains(QStringLiteral("timecode"))) {
            timecodeStr = tags.value(QStringLiteral("timecode")).toString();
        }
    }

    if (!haveVideo) {
        r.error = QStringLiteral("no video stream");
        return r;
    }

    const QJsonObject videoTags = video.value(QStringLiteral("tags")).toObject();
    const QJsonObject formatTags = format.value(QStringLiteral("tags")).toObject();

    // …then the video stream's own timecode tag, then the container's.
    if (timecodeStr.isEmpty())
        timecodeStr = videoTags.value(QStringLiteral("timecode")).toString();
    if (timecodeStr.isEmpty())
        timecodeStr = formatTags.value(QStringLiteral("timecode")).toString();
    if (!timecodeStr.isEmpty())
        r.timecodeSecondsOfDay = parseTimecodeToSecondsOfDay(timecodeStr);

    // Geometry, rotation-applied.
    const int codedWidth = static_cast<int>(
        jsonToLongLong(video.value(QStringLiteral("width")), 0));
    const int codedHeight = static_cast<int>(
        jsonToLongLong(video.value(QStringLiteral("height")), 0));
    r.rotation = rotationFromStream(video);
    if (r.rotation == 90 || r.rotation == 270) {
        r.width = codedHeight;
        r.height = codedWidth;
    } else {
        r.width = codedWidth;
        r.height = codedHeight;
    }

    // Frame rate: r_frame_rate as an exact rational; avg_frame_rate only as
    // a double for the VFR comparison.
    parseRational(video.value(QStringLiteral("r_frame_rate")).toString(),
                  r.fpsNum, r.fpsDen);
    int avgNum = 0;
    int avgDen = 1;
    if (parseRational(video.value(QStringLiteral("avg_frame_rate")).toString(),
                      avgNum, avgDen))
        r.avgFps = static_cast<double>(avgNum) / avgDen;
    if (r.fpsNum > 0 && r.avgFps > 0.0) {
        const double nominal = r.fps();
        r.vfr = std::fabs(nominal - r.avgFps) / nominal > 0.005;
    }

    r.nbFrames = jsonToLongLong(video.value(QStringLiteral("nb_frames")), -1);
    // Video stream start offset (0 when ffprobe reports none). Kept signed
    // and un-clamped: a negative start_time is passed through as-is and the
    // composite seek clamps at 0.
    r.startTimeSeconds =
        jsonToDouble(video.value(QStringLiteral("start_time")), 0.0);

    r.durationSeconds = jsonToDouble(format.value(QStringLiteral("duration")), -1.0);
    if (r.durationSeconds <= 0.0)
        r.durationSeconds = jsonToDouble(video.value(QStringLiteral("duration")), -1.0);

    r.codec = video.value(QStringLiteral("codec_name")).toString();
    r.pixFmt = video.value(QStringLiteral("pix_fmt")).toString();
    r.bitDepth = bitDepthFromStream(video, r.pixFmt);
    r.colorSpace = video.value(QStringLiteral("color_space")).toString();
    r.colorTransfer = video.value(QStringLiteral("color_transfer")).toString();
    r.colorPrimaries = video.value(QStringLiteral("color_primaries")).toString();
    r.colorRange = video.value(QStringLiteral("color_range")).toString();

    // Creation time: container tag preferred, stream tag as fallback.
    QString creation = formatTags.value(QStringLiteral("creation_time")).toString();
    if (creation.isEmpty())
        creation = videoTags.value(QStringLiteral("creation_time")).toString();
    if (!creation.isEmpty())
        r.creationTimeUtc = parseCreationTime(creation);

    r.ok = true;
    return r;
}

VideoProbe::Result VideoProbe::probe(const QString& videoPath)
{
    Result r;
    if (videoPath.isEmpty()) {
        r.error = QStringLiteral("empty video path");
        return r;
    }

    const QString ffprobePath = FFmpegProbe::instance()->ffprobePath();
    if (ffprobePath.isEmpty()) {
        r.error = QStringLiteral("ffprobe not found");
        return r;
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    const QStringList args{
        QStringLiteral("-v"), QStringLiteral("quiet"),
        QStringLiteral("-print_format"), QStringLiteral("json"),
        QStringLiteral("-show_format"),
        QStringLiteral("-show_streams"),
        videoPath};

    process.start(ffprobePath, args);
    if (!process.waitForFinished(10000)) {
        qWarning() << "VideoProbe: ffprobe timed out for" << videoPath;
        process.kill();
        process.waitForFinished(1000);
        r.error = QStringLiteral("ffprobe timed out");
        return r;
    }

    return parseFfprobeJson(process.readAllStandardOutput());
}
