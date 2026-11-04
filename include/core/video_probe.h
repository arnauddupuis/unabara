#ifndef VIDEO_PROBE_H
#define VIDEO_PROBE_H

#include <QByteArray>
#include <QDateTime>
#include <QString>

// One-shot video metadata extraction: probe(path) runs a single
// `ffprobe -print_format json -show_format -show_streams` call (replacing the
// three historical VideoExporter shell-outs) and all interpretation lives in
// the pure, process-free parseFfprobeJson() so tests feed it JSON directly.
//
// probe() is safe to call off the GUI thread (ClipListModel runs it through
// QtConcurrent); it holds no shared state.
class VideoProbe
{
public:
    struct Result {
        bool ok = false;      // a video stream was found and parsed
        QString error;        // set when !ok

        double durationSeconds = -1.0;
        // Nominal frame rate from r_frame_rate, kept as an exact rational —
        // 30000/1001 must never round-trip through a double.
        int fpsNum = 0;
        int fpsDen = 1;
        // avg_frame_rate as a double, for VFR detection only (-1 = unknown).
        double avgFps = -1.0;
        // True when r_frame_rate and avg_frame_rate differ meaningfully
        // (> 0.5% relative) — the variable-frame-rate phone-clip signature.
        bool vfr = false;
        qint64 nbFrames = -1;
        // Video STREAM start_time in seconds (0 when absent). Not always 0:
        // e.g. muxers that shift all streams to keep AAC priming samples
        // non-negative give the video a positive start offset. The
        // composited export folds this into its -ss/-to window so its
        // frame grid stays aligned to real frames (PR B3).
        double startTimeSeconds = 0.0;
        // Rotation-APPLIED display dimensions: a 1920x1080 stream with a
        // 90°/270° display rotation reports 1080x1920 here.
        int width = 0;
        int height = 0;
        int rotation = 0;     // normalized to 0/90/180/270
        QString codec;
        QString pixFmt;
        int bitDepth = 8;
        QString colorSpace;
        QString colorTransfer;
        QString colorPrimaries;
        QString colorRange;
        bool hasAudio = false;
        QString audioCodec;
        // Container/stream creation time, converted to UTC. Invalid when the
        // file carries no creation_time tag.
        QDateTime creationTimeUtc;
        // Embedded timecode (tmcd stream, stream tags, or format tags) as
        // seconds-of-day; -1 = none. Accepts HH:MM:SS:FF and the drop-frame
        // HH:MM:SS;FF form. The frame count is ignored (sub-second precision
        // is meaningless for dive sync — same semantics as the historical
        // extractVideoTimecode()).
        double timecodeSecondsOfDay = -1.0;

        double fps() const
        {
            return fpsDen > 0 ? static_cast<double>(fpsNum) / fpsDen : 0.0;
        }
    };

    // Runs ffprobe (located via FFmpegProbe) on the file and parses the JSON.
    // Never throws; failures come back as ok=false with an error string.
    static Result probe(const QString& videoPath);

    // Pure parser for ffprobe's -print_format json output. Malformed or empty
    // input yields ok=false, never a crash.
    static Result parseFfprobeJson(const QByteArray& json);

    // "HH:MM:SS:FF" / "HH:MM:SS;FF" → seconds of day, or -1 when unparsable.
    static double parseTimecodeToSecondsOfDay(const QString& timecode);
};

#endif // VIDEO_PROBE_H
