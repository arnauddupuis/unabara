#ifndef FFMPEG_INFO_H
#define FFMPEG_INFO_H

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QStringList>

// GUI-thread bridge between FFmpegProbe and QML (context property
// `ffmpegInfo`): feeds the Settings tab's FFmpeg row and the export dialog's
// probe-driven codec list.
//
// FFmpegProbe::version()/hasEncoder() shell out (up to 10 s per tool run on
// a broken install), so they must NEVER be called from QML bindings on the
// GUI thread. refresh() snapshots the probe on a QtConcurrent worker (same
// pattern as ClipListModel's probers) and emits infoChanged() when the
// snapshot lands; until then ready() is false and the properties hold the
// previous (or fallback) values.
//
// Also owns the Config → FFmpegProbe override wiring: the constructor and
// every Config::ffmpegPathChanged push Config::ffmpegPath() into
// FFmpegProbe::setConfigOverride() (the probe must not read Config itself —
// its discovery runs on worker threads under a mutex), then re-probe.
class FfmpegInfo : public QObject
{
    Q_OBJECT

    // False while a probe snapshot is being (re)computed.
    Q_PROPERTY(bool ready READ ready NOTIFY infoChanged)
    Q_PROPERTY(bool available READ available NOTIFY infoChanged)
    // All display strings are assembled in C++ (project rule), e.g.
    // "FFmpeg 7.1.5 — /usr/bin/ffmpeg" and "h264 ✓   hevc ✓   ...".
    Q_PROPERTY(QString summaryText READ summaryText NOTIFY infoChanged)
    Q_PROPERTY(QString capabilitiesText READ capabilitiesText NOTIFY infoChanged)
    // Codec ids (same ids/order as VideoExporter::getAvailableCodecs())
    // whose encoder the found FFmpeg actually has. Falls back to all four
    // while probing or when FFmpeg is missing — the export button is
    // already gated on isFFmpegAvailable.
    Q_PROPERTY(QStringList availableCodecs READ availableCodecs NOTIFY infoChanged)

public:
    explicit FfmpegInfo(QObject* parent = nullptr);

    bool ready() const { return m_ready; }
    bool available() const { return m_ready && !m_info.path.isEmpty(); }
    QString summaryText() const;
    QString capabilitiesText() const;
    QStringList availableCodecs() const;

    // Re-snapshot the probe asynchronously. Safe to call while a snapshot
    // is in flight: the in-flight (now stale) result is discarded and a new
    // run starts when it finishes.
    Q_INVOKABLE void refresh();

    // The canonical codec-id list (ids and order) — the single source
    // availableCodecs() filters and VideoExporter::getAvailableCodecs()
    // returns, so the two can never drift.
    static QStringList allCodecIds();

signals:
    void infoChanged();

private:
    struct Snapshot {
        QString path;
        QString version;
        bool h264 = false;
        bool hevc = false;
        bool prores = false;
        bool vp9 = false;
    };

    void onProbeFinished();
    void onConfigOverrideChanged();

    bool m_ready = false;
    bool m_refreshPending = false;
    Snapshot m_info;
    QFutureWatcher<Snapshot> m_watcher;
};

#endif // FFMPEG_INFO_H
