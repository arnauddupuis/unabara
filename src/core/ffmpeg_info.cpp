#include "include/core/ffmpeg_info.h"

#include "include/core/config.h"
#include "include/core/ffmpeg_probe.h"

#include <QtConcurrent/QtConcurrentRun>

FfmpegInfo::FfmpegInfo(QObject* parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<Snapshot>::finished,
            this, &FfmpegInfo::onProbeFinished);

    // Push the persisted override into the probe before the first snapshot
    // (and before anything else — e.g. a clip import's ffprobe worker —
    // resolves a path), then keep it in sync. Both happen on the GUI thread;
    // setConfigOverride() is thread-safe and drops the probe's caches when
    // the value actually changed.
    FFmpegProbe::instance()->setConfigOverride(Config::instance()->ffmpegPath());
    connect(Config::instance(), &Config::ffmpegPathChanged,
            this, &FfmpegInfo::onConfigOverrideChanged);

    refresh();
}

void FfmpegInfo::onConfigOverrideChanged()
{
    FFmpegProbe::instance()->setConfigOverride(Config::instance()->ffmpegPath());
    refresh();
}

void FfmpegInfo::refresh()
{
    if (m_watcher.isRunning()) {
        // A snapshot is in flight against the old probe state; discard it
        // when it lands and run again.
        m_refreshPending = true;
        return;
    }

    m_ready = false;
    emit infoChanged();

    // The worker touches only the FFmpegProbe singleton, whose accessors are
    // mutex-guarded — never Config or this object's members.
    m_watcher.setFuture(QtConcurrent::run([]() -> Snapshot {
        FFmpegProbe* probe = FFmpegProbe::instance();
        Snapshot s;
        s.path = probe->ffmpegPath();
        if (!s.path.isEmpty()) {
            s.version = probe->version();
            s.h264 = probe->hasEncoder(QStringLiteral("libx264"));
            s.hevc = probe->hasEncoder(QStringLiteral("libx265"));
            s.prores = probe->hasEncoder(QStringLiteral("prores_ks"));
            s.vp9 = probe->hasEncoder(QStringLiteral("libvpx-vp9"));
        }
        return s;
    }));
}

void FfmpegInfo::onProbeFinished()
{
    if (m_refreshPending) {
        // The override changed while this snapshot was running — its result
        // describes the previous configuration, so drop it and re-probe.
        m_refreshPending = false;
        refresh();
        return;
    }

    m_info = m_watcher.result();
    m_ready = true;
    emit infoChanged();
}

QString FfmpegInfo::summaryText() const
{
    if (!m_ready)
        return tr("Checking for FFmpeg…");
    if (m_info.path.isEmpty())
        return tr("FFmpeg not found. Install FFmpeg or set its location below.");
    if (m_info.version.isEmpty())
        return tr("FFmpeg — %1").arg(m_info.path);
    return tr("FFmpeg %1 — %2").arg(m_info.version, m_info.path);
}

QString FfmpegInfo::capabilitiesText() const
{
    if (!m_ready || m_info.path.isEmpty())
        return QString();
    const auto mark = [](bool present) {
        return present ? QStringLiteral("✓") : QStringLiteral("✗");
    };
    return QStringLiteral("h264 %1   hevc %2   prores %3   vp9 %4")
        .arg(mark(m_info.h264), mark(m_info.hevc),
             mark(m_info.prores), mark(m_info.vp9));
}

QStringList FfmpegInfo::allCodecIds()
{
    return {QStringLiteral("vp9"), QStringLiteral("prores"),
            QStringLiteral("h264"), QStringLiteral("hevc")};
}

QStringList FfmpegInfo::availableCodecs() const
{
    if (!m_ready || m_info.path.isEmpty())
        return allCodecIds();

    QStringList codecs;
    if (m_info.vp9)
        codecs << QStringLiteral("vp9");
    if (m_info.prores)
        codecs << QStringLiteral("prores");
    if (m_info.h264)
        codecs << QStringLiteral("h264");
    if (m_info.hevc)
        codecs << QStringLiteral("hevc");

    // An FFmpeg with none of the four encoders is indistinguishable from a
    // parse failure — don't present an empty combo; the encode itself will
    // produce the real error message.
    return codecs.isEmpty() ? allCodecIds() : codecs;
}
