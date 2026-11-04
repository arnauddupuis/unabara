#include "include/core/ffmpeg_probe.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

FFmpegProbe* FFmpegProbe::instance()
{
    static FFmpegProbe probe;
    return &probe;
}

QString FFmpegProbe::ffmpegCommandName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("ffmpeg.exe");
#else
    return QStringLiteral("ffmpeg");
#endif
}

QString FFmpegProbe::ffprobeCommandName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("ffprobe.exe");
#else
    return QStringLiteral("ffprobe");
#endif
}

QStringList FFmpegProbe::knownInstallPaths()
{
    // Standard user install locations tried before the generic PATH lookup
    // (brew destination dirs, /usr/local for manual builds, the default
    // Windows unzip targets). Same set VideoExporter historically used.
#ifdef Q_OS_WIN
    return {
        QStringLiteral("C:/Program Files/ffmpeg/bin/"),
        QStringLiteral("C:/Program Files (x86)/ffmpeg/bin/")
    };
#elif defined(Q_OS_APPLE)
    return {
        QStringLiteral("/opt/homebrew/bin/"),
        QStringLiteral("/usr/local/bin/"),
    };
#else
    return {
        QStringLiteral("/usr/local/bin/"),
    };
#endif
}

QString FFmpegProbe::siblingFfprobePath(const QString& ffmpegPath)
{
    if (ffmpegPath.isEmpty())
        return QString();
    // Directory join, NOT a "ffmpeg"→"ffprobe" string replace: on
    // "C:/Program Files/ffmpeg/bin/ffmpeg.exe" a replace would also rewrite
    // the directory component and produce a path that doesn't exist.
    return QFileInfo(ffmpegPath).dir().filePath(ffprobeCommandName());
}

QString FFmpegProbe::discover() const
{
    const QString command = ffmpegCommandName();

    // User override first (Config::ffmpegPath, pushed in through
    // setConfigOverride — discover() runs under m_pathMutex on worker
    // threads, so it must never reach into Config itself). An override that
    // no longer points at an executable file is reported once per discovery
    // and falls through to auto-detect, so a stale path doesn't brick export.
    if (!m_configOverride.isEmpty()) {
        const QFileInfo fi(m_configOverride);
        if (fi.exists() && fi.isExecutable())
            return fi.absoluteFilePath();
        qWarning() << "FFmpegProbe: configured FFmpeg path" << m_configOverride
                   << "is not an executable file — falling back to auto-detection";
    }

    // App directory (bundled FFmpeg — PR B4 ships it there).
    const QString appDir = QCoreApplication::applicationDirPath();
    const QFileInfo bundled(appDir + QLatin1Char('/') + command);
    if (bundled.exists() && bundled.isExecutable())
        return bundled.absoluteFilePath();

    // Known platform install paths.
    QString path = QStandardPaths::findExecutable(command, knownInstallPaths());
    if (!path.isEmpty())
        return path;

    // Finally, PATH.
    path = QStandardPaths::findExecutable(command);
    if (!path.isEmpty())
        return path;

    return QString();
}

void FFmpegProbe::ensureResolvedLocked()
{
    if (m_pathResolved)
        return;
    m_ffmpegPath = discover();
    // Only latch a *successful* discovery. A user who hits the "FFmpeg not
    // found" dialog, installs FFmpeg and retries must not need an app
    // restart (the old per-call rescan allowed that); a miss re-scans on
    // the next call, which is cheap (a few findExecutable/stat calls).
    m_pathResolved = !m_ffmpegPath.isEmpty();

    m_ffprobePath.clear();
    if (!m_ffmpegPath.isEmpty()) {
        const QString sibling = siblingFfprobePath(m_ffmpegPath);
        const QFileInfo fi(sibling);
        if (fi.exists() && fi.isExecutable())
            m_ffprobePath = fi.absoluteFilePath();
    }
    if (m_ffprobePath.isEmpty()) {
        // No sibling (unusual split install): fall back to a PATH lookup so
        // probing still works when ffprobe lives elsewhere.
        m_ffprobePath = QStandardPaths::findExecutable(ffprobeCommandName());
    }
}

QString FFmpegProbe::ffmpegPath()
{
    QMutexLocker locker(&m_pathMutex);
    ensureResolvedLocked();
    return m_ffmpegPath;
}

QString FFmpegProbe::ffprobePath()
{
    QMutexLocker locker(&m_pathMutex);
    ensureResolvedLocked();
    return m_ffprobePath;
}

bool FFmpegProbe::isAvailable()
{
    return !ffmpegPath().isEmpty();
}

QString FFmpegProbe::runTool(const QString& exe, const QStringList& args)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(exe, args);
    if (!process.waitForFinished(10000)) {
        qWarning() << "FFmpegProbe: timed out running" << exe << args;
        process.kill();
        process.waitForFinished(1000);
        return QString();
    }
    return QString::fromUtf8(process.readAllStandardOutput());
}

void FFmpegProbe::ensureCapabilities()
{
    quint64 generation;
    {
        QMutexLocker locker(&m_capsMutex);
        if (m_capsProbed)
            return;
        generation = m_capsGeneration;
    }

    // Resolve the path via the public accessor (takes m_pathMutex briefly).
    const QString ffmpeg = ffmpegPath();
    if (ffmpeg.isEmpty())
        return; // Don't latch: capabilities re-probe once FFmpeg appears.

    // Run the tools with NO mutex held: each runTool can block up to 10 s
    // on a broken install, and setConfigOverride()/invalidate() run on the
    // GUI thread and take m_capsMutex — holding it here froze the Settings
    // UI when the user changed the override mid-probe. Results land in
    // locals; the generation counter below discards them if the override
    // changed while we probed. Two threads may occasionally probe
    // concurrently (duplicate subprocess work) — rare and harmless.

    // -version: first line is "ffmpeg version <ver> Copyright ...".
    QString version;
    const QString versionOut = runTool(ffmpeg, {QStringLiteral("-version")});
    const QRegularExpression versionRe(QStringLiteral("^ffmpeg version (\\S+)"));
    const QRegularExpressionMatch vm = versionRe.match(versionOut);
    if (vm.hasMatch())
        version = vm.captured(1);

    // -encoders: entries follow the "------" separator line, name in the
    // second column ("V....D libx264   ...").
    QSet<QString> encoders;
    const QString encodersOut = runTool(
        ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-encoders")});
    bool pastSeparator = false;
    const QStringList encoderLines = encodersOut.split(QLatin1Char('\n'));
    for (const QString& line : encoderLines) {
        if (!pastSeparator) {
            if (line.contains(QLatin1String("------")))
                pastSeparator = true;
            continue;
        }
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 2)
            encoders.insert(parts.at(1));
    }

    // -filters: no separator line; entries have an "A->V"-style IO column
    // after the name, which the legend lines don't.
    QSet<QString> filters;
    const QString filtersOut = runTool(
        ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-filters")});
    const QRegularExpression filterRe(
        QStringLiteral("^\\s*[TSC.]{3}\\s+(\\S+)\\s+\\S*->"));
    const QStringList filterLines = filtersOut.split(QLatin1Char('\n'));
    for (const QString& line : filterLines) {
        const QRegularExpressionMatch m = filterRe.match(line);
        if (m.hasMatch())
            filters.insert(m.captured(1));
    }

    QMutexLocker locker(&m_capsMutex);
    if (m_capsGeneration != generation)
        return; // Override changed / invalidated mid-probe: results stale.
    m_capsProbed = true;
    m_version = version;
    m_encoders = encoders;
    m_filters = filters;
}

QString FFmpegProbe::version()
{
    ensureCapabilities();
    QMutexLocker locker(&m_capsMutex);
    return m_version;
}

bool FFmpegProbe::hasEncoder(const QString& name)
{
    ensureCapabilities();
    QMutexLocker locker(&m_capsMutex);
    return m_encoders.contains(name);
}

bool FFmpegProbe::hasFilter(const QString& name)
{
    ensureCapabilities();
    QMutexLocker locker(&m_capsMutex);
    return m_filters.contains(name);
}

void FFmpegProbe::invalidate()
{
    // Lock order caps -> path (the only places both are held are here and
    // setConfigOverride; ensureCapabilities takes them one at a time).
    QMutexLocker capsLocker(&m_capsMutex);
    QMutexLocker pathLocker(&m_pathMutex);
    ++m_capsGeneration; // Any in-flight capability probe becomes stale.
    m_pathResolved = false;
    m_ffmpegPath.clear();
    m_ffprobePath.clear();
    m_capsProbed = false;
    m_version.clear();
    m_encoders.clear();
    m_filters.clear();
}

void FFmpegProbe::setConfigOverride(const QString& path)
{
    // Same lock order as invalidate(); the caches are dropped in the same
    // critical section so no thread can observe the new override with the
    // old discovery result.
    QMutexLocker capsLocker(&m_capsMutex);
    QMutexLocker pathLocker(&m_pathMutex);
    if (m_configOverride == path)
        return;
    m_configOverride = path;
    ++m_capsGeneration; // Any in-flight capability probe becomes stale.
    m_pathResolved = false;
    m_ffmpegPath.clear();
    m_ffprobePath.clear();
    m_capsProbed = false;
    m_version.clear();
    m_encoders.clear();
    m_filters.clear();
}
