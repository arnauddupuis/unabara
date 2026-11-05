#ifndef VIDEO_EXPORT_H
#define VIDEO_EXPORT_H

#include <QObject>
#include <QString>
#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <QSize>
#include <functional>
#include "include/core/dive_data.h"
#include "include/generators/i_frame_generator.h"

class VideoClip;

class VideoExporter : public QObject
{
    Q_OBJECT

    // Base directory the output file lands in (createDefaultExportFile /
    // generateUniqueFileName). Bound from QML (config.lastExportPath) so the
    // exporter itself has no dependency on the settings store.
    Q_PROPERTY(QString baseDirectory READ baseDirectory WRITE setBaseDirectory NOTIFY baseDirectoryChanged)
    Q_PROPERTY(double frameRate READ frameRate WRITE setFrameRate NOTIFY frameRateChanged)
    // Quality tier id ("best"/"high"/"standard"/"small" — FFmpegArgs'
    // quality tables). Replaced the videoBitrate property in PR B3: the
    // bitrate slider never worked for h264/hevc (fixed -crf 23 always won)
    // and nothing else consumed the property.
    Q_PROPERTY(QString videoQuality READ videoQuality WRITE setVideoQuality NOTIFY videoQualityChanged)
    Q_PROPERTY(QString videoCodec READ videoCodec WRITE setVideoCodec NOTIFY videoCodecChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(QSize customResolution READ customResolution WRITE setCustomResolution NOTIFY customResolutionChanged)

public:
    explicit VideoExporter(QObject *parent = nullptr);
    ~VideoExporter();

    // Codec options
    enum VideoCodec {
        H264,      // Standard MP4 H.264, most compatible
        ProRes,    // Apple ProRes, higher quality with alpha
        VP9,       // WebM VP9, good for web
        HEVC       // H.265, better compression
    };
    Q_ENUM(VideoCodec)

    // Getters
    QString baseDirectory() const { return m_baseDirectory; }
    double frameRate() const { return m_frameRate; }
    QString videoQuality() const { return m_videoQuality; }
    QString videoCodec() const { return m_videoCodec; }
    int progress() const { return m_progress; }
    bool isBusy() const { return m_busy; }
    QSize customResolution() const { return m_customResolution; }

    // Setters
    void setBaseDirectory(const QString &path);
    void setFrameRate(double fps);
    void setVideoQuality(const QString &quality);
    void setVideoCodec(const QString &codec);
    void setCustomResolution(const QSize &size);

    // Export methods. Accepts `generator` as a QObject* — internally
    // dynamic_cast to IFrameGenerator, so both OverlayGenerator and
    // ProfileGenerator work transparently from QML.
    //
    // Raw-pipe pipeline (PR B2): FFmpeg is started first reading straight-
    // alpha RGBA8888 frames from stdin (-f rawvideo -pix_fmt rgba -i pipe:0),
    // then the render loop generates each frame and writes its pixels to the
    // pipe with backpressure — no temporary PNG files. Returning true means
    // all frames were delivered and FFmpeg is finalizing; real completion
    // arrives via exportFinished(bool, path).
    Q_INVOKABLE bool exportVideo(DiveData* dive, QObject* generator,
                                double startTime, double endTime);

    // Composited export (PR B3): the clip plays as FFmpeg input 0 (frame-
    // snapped -ss/-to window) while FrameCompositor streams one pre-placed
    // full-frame PREMULTIPLIED RGBA plane per output frame into pipe:0;
    // FFmpegArgs::compositeArgs/buildCompositeFilterGraph assemble the
    // command. The overlays and their placements come from the clip's
    // VideoOverlayLayout (hidden placements are skipped); dcGenerator /
    // profileGenerator are the usual QObject*-cast IFrameGenerators and may
    // individually be null. clipRangeStart/End are CLIP seconds, clamped to
    // the clip and snapped to its frame grid; dive time of frame i is
    // syncOffset + in + i*den/num. Output size/rate always follow the clip
    // (customResolution/frameRate do not apply here).
    //
    // overlaysOnly=true is the "overlays only, placed" mode: the SAME
    // compositor plane (converted to straight alpha at the pipe boundary)
    // through the overlay-only pipe/codecs — transparent output at clip
    // geometry for external compositing.
    //
    // Colour grade (PR C5): a composited run (never overlaysOnly — there
    // is no footage to grade) deserializes the CLIP's own stored grade
    // (ColorGrade::fromJson(clip->colorGrade())) and, when enabled, runs
    // ColorGradeExport::plan() against the snapped in/out window and
    // inserts the resulting filter snippet into the graph. Arming happens
    // HERE, per run, so the A6 batch queue gets each clip's OWN grade
    // automatically — an externally-armed one-shot hook would apply clip
    // 1's grade to every clip (or none). plan() errors abort the run via
    // exportError(); non-fatal plan notes (e.g. a missing user LUT) are
    // surfaced through exportWarning() and the export proceeds.
    //
    // Same return/complete/cancel semantics as exportVideo; both run the
    // one shared streaming loop (streamFrames/finishRun).
    Q_INVOKABLE bool exportComposite(DiveData* dive, VideoClip* clip,
                                     QObject* dcGenerator,
                                     QObject* profileGenerator,
                                     double clipRangeStart, double clipRangeEnd,
                                     bool overlaysOnly = false);

    // Track-C colour hook. Since PR C5 the normal path derives the snippet
    // from the clip's own grade inside exportComposite() (see above); this
    // remains as an explicit OVERRIDE seam for tests and advanced callers:
    // when armed, it is inserted VERBATIM between format=rgb24 and overlay
    // in the next composite run's graph and WINS over the clip's grade.
    // Still one-shot: consumed and cleared at run start, so a later export
    // can never accidentally reuse a stale override.
    Q_INVOKABLE void setColorFilterChain(const QString &snippet);
    QString colorFilterChain() const { return m_colorFilterChain; }

    // Request cancellation of a running export. FFmpeg runs for the whole
    // export (frames stream to it as they render), so this terminates the
    // process; the render loop polls the flag (its processEvents() is what
    // delivers this call) and stops writing, and onFFmpegFinished()'s cancel
    // branch completes the cancellation: partial output removed and
    // exportCancelled() emitted — not exportError().
    Q_INVOKABLE void cancelExport();

    // Helper methods
    Q_INVOKABLE bool isFFmpegAvailable();
    // `contentType` (e.g. "dive_computer", "dive_profile") is appended to the
    // output filename so exports of different overlays for the same dive land
    // in distinct files.
    Q_INVOKABLE QString createDefaultExportFile(DiveData* dive,
                                                const QString &videoFilePath = QString(),
                                                const QString &contentType = QString());
    Q_INVOKABLE QSize detectVideoResolution(const QString &videoPath);
    Q_INVOKABLE double extractVideoTimecode(const QString &videoPath);
    Q_INVOKABLE double extractVideoCreationTime(const QString &videoPath);

    // Get information about available codecs and formats
    Q_INVOKABLE QStringList getAvailableCodecs();
    Q_INVOKABLE bool codecSupportsAlpha(const QString &codec);
    Q_INVOKABLE QString getFileExtensionForCodec(const QString &codec);

    // Pre-assembled dialog text for the composite audio decision
    // (FFmpegArgs::decideAudio) — "Audio: copied (aac)" / "Audio:
    // re-encoded to AAC" / "No audio track". Strings are built in C++
    // (project rule). VALUE signature (not the clip pointer) so the QML
    // binding depends on the probe-NOTIFY'd properties and re-evaluates
    // when the async probe lands mid-dialog — the same stale-binding
    // class compositeColorTagText was designed around.
    Q_INVOKABLE QString compositeAudioText(bool hasAudio,
                                           const QString &audioCodec,
                                           const QString &codec);

    // Colour-tag advisory for the composite dialog (PR C5): empty when the
    // clip is tagged (decoded as tagged — nothing to warn about), else the
    // "No colour tags — assuming BT.709/BT.601" text matching EXACTLY what
    // FFmpegArgs::inColorMatrixFor decides for the export. Built in C++
    // (project rule). Takes the clip's colorSpace/height VALUES, not the
    // clip, so the QML binding depends on the probe-notified properties
    // and re-evaluates when the async probe lands (same reactivity pattern
    // as the HDR row — a clip-pointer signature left the row permanently
    // stale at its pre-probe evaluation).
    Q_INVOKABLE QString compositeColorTagText(const QString &colorSpace,
                                              int height);

    // Grade row for the composite dialog (PR C5): "Colour grade: applied
    // (static)" / "(depth-linked)" — the depth-path wording comes from
    // ColorGradeExport::usesDepthPath, the SAME decision plan() takes —
    // plus a missing-LUT advisory when the grade references a .cube that
    // no longer exists (cheap existence pre-check; the authoritative parse
    // failure still arrives as an exportWarning at export time). Empty
    // when the clip has no enabled grade (the dialog hides the row).
    Q_INVOKABLE QString compositeGradeText(VideoClip* clip, DiveData* dive);

signals:
    void baseDirectoryChanged();
    void frameRateChanged();
    void videoQualityChanged();
    void videoCodecChanged();
    void progressChanged();
    void busyChanged();
    void exportStarted();
    void exportFinished(bool success, const QString &path);
    void exportError(const QString &errorMessage);
    // Non-fatal advisory during export setup (PR C5: ColorGradeExport plan
    // notes, e.g. a missing user LUT — the grade still applies without it
    // and the export proceeds). QML shows these as toasts.
    void exportWarning(const QString &message);
    void exportCancelled();
    void statusUpdate(const QString &message);
    void customResolutionChanged();

private slots:
    void processFFmpegOutput();
    void onFFmpegFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QString m_baseDirectory;
    double m_frameRate;
    QString m_videoQuality;
    QString m_videoCodec;
    int m_progress;
    bool m_busy;
    bool m_cancelRequested;
    QSize m_customResolution;
    QString m_lastOutputPath;
    QString m_pendingOutputPath;
    // One-shot colour snippet for the next composite run (see
    // setColorFilterChain).
    QString m_colorFilterChain;
    // Rolling tail of FFmpeg's stderr (warnings/errors — stdout carries only
    // the -progress stream), retained so the actual error can be logged if
    // FFmpeg exits non-zero.
    QString m_ffmpegOutputTail;
    // Partial-line buffer for the machine-readable `-progress pipe:1` stream
    // on stdout; its `frame=N` lines are parsed against m_totalFrames.
    QByteArray m_progressBuffer;
    // Frame count of the current export run (ExportMath::totalFrames for
    // the overlay-only path, the snapped clip-frame count for composites),
    // the denominator for the progress percentage.
    int m_totalFrames = 0;
    // Interleaved progress counters: frames rendered by the loop vs frames
    // FFmpeg reports encoded (`frame=` lines) — both shown in the status
    // text. The percentage tracks the encoded count (it is the one that
    // reaches 100% when the file is done).
    int m_renderedFrames = 0;
    int m_encodedFrames = 0;
    // Set when the render loop has to kill a still-running FFmpeg itself
    // (a pipe write failed); onFFmpegFinished() reports it instead of the
    // generic "crashed" message. Cleared at the start of each run.
    QString m_pipeErrorMessage;
    // Export-run generation counter. m_ffmpegProcess is REUSED across runs
    // and the write loop pumps events, so a run that is still unwinding
    // after onFFmpegFinished() reset m_busy must never terminate/kill a
    // process that now belongs to a newer run (same cross-run corruption
    // class as the stale cancel kill-timer). Every exit path that touches
    // the process compares its captured serial first.
    int m_runSerial = 0;

    QProcess* m_ffmpegProcess;
    // Fallback SIGKILL escalation armed by cancelExport(); must be disarmed
    // the moment the process finishes — m_ffmpegProcess is reused across
    // export runs, so a stale timer would kill the NEXT export.
    QTimer* m_killTimer = nullptr;
    // No longer holds exported frames (they stream over FFmpeg's stdin).
    // Since PR C5 it is the colour engine's work directory: each composite
    // run's ColorGradeExport::plan() writes its baked .cube / sendcmd .cmd
    // here under FIXED names. That is safe because runs through one
    // exporter are strictly sequential (m_busy guards re-entry, and the A6
    // batch chains the next exportComposite() off exportFinished — emitted
    // only after FFmpeg exited), so an overwrite can only happen after the
    // previous process is gone; FFmpeg additionally loads both files at
    // filter-init time, never re-reading them mid-run. The dir lives as
    // long as the exporter, so the files outlive every run they feed.
    QTemporaryDir m_tempDir;

    // Raw-pipe pipeline stages (PR B2/B3)
    // Starts FFmpeg with the given argument list (always built by
    // FFmpegArgs — the ONE arg builder, unit-tested in filtergraph_test /
    // composite_export_test). Emits exportError() on failure.
    bool startFFmpegProcess(const QStringList &args);
    // Writes one 4-byte-per-pixel frame's pixels to FFmpeg's stdin. False
    // when the process is gone or a write fails.
    bool writeFramePixels(const QImage &frame);
    // Backpressure: spins while more than two frames are buffered, pumping
    // events so the UI (and a Cancel click) stays live. False when FFmpeg
    // is no longer running; returns early (true) on cancel.
    bool waitForPipeDrain(qint64 frameBytes);
    void emitInterleavedStatus();

    // THE streaming loop, shared verbatim by exportVideo and
    // exportComposite (guardrail: when two sites must agree, extract —
    // never fork the frame loop between exporters/modes again).
    // `pixelFormat` is the pipe's byte contract: straight RGBA8888 for the
    // overlay-only/placed modes, RGBA8888_Premultiplied for composites.
    // `timeOfFrame(i)` maps frame index to dive time. `firstFrame` is
    // frame 0, already rendered+converted by the caller (its size fixed
    // the pipe geometry).
    enum class StreamResult { Completed, Cancelled, PipeFailed, Superseded };
    StreamResult streamFrames(DiveData *dive, IFrameGenerator *gen,
                              int totalFrames, const QSize &frameSize,
                              QImage::Format pixelFormat,
                              const std::function<double(int)> &timeOfFrame,
                              const QImage &firstFrame, int runSerial);
    // Shared endgame: completes/aborts the run per StreamResult (cancel
    // handshake, pipe-failure kill, closeWriteChannel on success) with the
    // serial discipline. Returns exportVideo's historical return value.
    bool finishRun(StreamResult result, int runSerial);

    // Helper methods
    // Delegates to FFmpegProbe (the single app-wide locator).
    QString findFFmpegPath();
    QString generateUniqueFileName(DiveData* dive,
                                   const QString &extension,
                                   const QString &videoFilePath = QString(),
                                   const QString &contentType = QString());
    QSize getDefaultOverlaySize();
};

#endif // VIDEO_EXPORT_H
