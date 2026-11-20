#ifndef FFMPEG_PROBE_H
#define FFMPEG_PROBE_H

#include <QMutex>
#include <QSet>
#include <QString>
#include <QStringList>

// Single FFmpeg locator and capability prober for the whole app.
//
// Discovery order (see discover()): user override (Config::ffmpegPath,
// pushed in via setConfigOverride — never read from Config here, see below)
// → app directory → known platform install paths → PATH.
// The ffprobe path is derived as a *sibling file* of the found ffmpeg via
// QFileInfo directory join — never a string replace, which breaks on paths
// like "C:/Program Files/ffmpeg/bin/ffmpeg.exe" (the directory component also
// contains "ffmpeg").
//
// Capabilities (`-version`, `-encoders`, `-filters`) are probed lazily, once,
// and cached. invalidate() drops both caches; setConfigOverride() does so
// implicitly whenever the override actually changes.
//
// Thread-safe: ClipListModel's probe workers resolve ffprobePath() off the
// GUI thread, so all lazy initialization is mutex-guarded.
class FFmpegProbe
{
public:
    static FFmpegProbe* instance();

    // Absolute path to the ffmpeg executable, or empty when not found.
    QString ffmpegPath();
    // Absolute path to ffprobe: the sibling of the found ffmpeg if it exists
    // there, else a plain PATH lookup. Empty when not found.
    QString ffprobePath();

    bool isAvailable();

    // From one cached `-version` / `-encoders` / `-filters` run.
    QString version();
    bool hasEncoder(const QString& name);
    bool hasFilter(const QString& name);

    // Drop the cached path and capabilities so the next call re-discovers
    // (e.g. after the user installs FFmpeg and retries).
    void invalidate();

    // The user's FFmpeg path override (Config::ffmpegPath). Pushed IN from
    // the GUI thread (FfmpegInfo does it at startup and on every change)
    // instead of discover() pulling from Config, because discover() runs
    // under m_pathMutex on worker threads (ClipListModel's probers) where a
    // Config read would be cross-thread. Thread-safe; a changed value drops
    // both caches like invalidate(). Empty = auto-detect.
    void setConfigOverride(const QString& path);

    // Platform pieces, exposed for reuse.
    static QString ffmpegCommandName();
    static QString ffprobeCommandName();
    static QStringList knownInstallPaths();
    // Sibling join (directory of `ffmpegPath` + ffprobe executable name).
    // Returns the candidate path whether or not it exists.
    static QString siblingFfprobePath(const QString& ffmpegPath);

private:
    FFmpegProbe() = default;
    Q_DISABLE_COPY_MOVE(FFmpegProbe)

    QString discover() const;
    // Must be called with m_pathMutex held.
    void ensureResolvedLocked();
    // Takes and RELEASES m_capsMutex around the blocking QProcess runs:
    // no mutex is held while the tools execute (each can block up to 10 s
    // on a broken install), so setConfigOverride()/invalidate() on the GUI
    // thread never stall behind an in-flight probe. m_capsGeneration
    // detects a mid-probe override change and discards the stale results.
    void ensureCapabilities();
    static QString runTool(const QString& exe, const QStringList& args);

    // Two mutexes on purpose: capability state and path state are touched
    // from different threads with different costs. Lock order where both
    // are held: m_capsMutex, then m_pathMutex. Neither is ever held across
    // a subprocess run.
    QMutex m_pathMutex;
    QMutex m_capsMutex;
    bool m_pathResolved = false;
    // Guarded by m_pathMutex (discover() reads it with that mutex held).
    QString m_configOverride;
    QString m_ffmpegPath;
    QString m_ffprobePath;
    bool m_capsProbed = false;
    // Guarded by m_capsMutex; bumped by invalidate()/setConfigOverride()
    // so an in-flight (unlocked) capability probe can detect staleness.
    quint64 m_capsGeneration = 0;
    QString m_version;
    QSet<QString> m_encoders;
    QSet<QString> m_filters;
};

#endif // FFMPEG_PROBE_H
