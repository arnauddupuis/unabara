// Timeline facade tests (PR A2): videoPath/videoOffset/videoDuration keep
// their historical names and NOTIFY signals but read/write through the clip
// model's active clip. Locks in: no-clip defaults and dropped writes,
// read-through, write-through with Manual sync attribution, and NOTIFY
// re-emission both on clip-level changes and on active-clip switches.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "include/ui/timeline.h"
#include "include/core/clip_list_model.h"
#include "include/core/video_clip.h"

// timeline.cpp pokes these globals (null-guarded) to push the current time
// into the Quick image providers. The real definitions live in main.cpp /
// profile_image_provider.cpp, which are deliberately not linked here — stub
// the symbols so the facade can be tested without the Quick scene machinery.
#include "include/generators/overlay_image_provider.h"
#include "include/generators/profile_image_provider.h"
OverlayImageProvider* g_imageProvider = nullptr;
ProfileImageProvider* g_profileImageProvider = nullptr;
void OverlayImageProvider::setCurrentTime(double) {}
void ProfileImageProvider::setCurrentTime(double) {}

namespace {

// Deterministic prober: no ffprobe, no file I/O. Runs on a worker thread,
// so it must be self-contained (same contract as setProberForTesting).
VideoProbe::Result probedResult(double duration)
{
    VideoProbe::Result r;
    r.ok = true;
    r.durationSeconds = duration;
    r.fpsNum = 30;
    r.fpsDen = 1;
    r.width = 1920;
    r.height = 1080;
    return r;
}

VideoProbe::Result failedResult()
{
    VideoProbe::Result r;
    r.ok = false;
    r.error = QStringLiteral("no ffprobe in tests");
    return r;
}

} // namespace

class TimelineFacadeTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString makeClipFile(const QString& name)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.write("not a real video");
        return path;
    }

    // Adds a clip and waits for its async probe to settle, so later signal
    // counting isn't polluted by a probe finishing mid-test.
    VideoClip* addSettledClip(ClipListModel& model, const QString& path)
    {
        QSignalSpy probed(&model, &ClipListModel::clipProbed);
        VideoClip* clip = model.addClip(path);
        if (!clip)
            return nullptr;
        if (clip->probeState() == VideoClip::Probing && !probed.wait(5000))
            return nullptr;
        return clip;
    }

private slots:
    void noClipDefaults()
    {
        Timeline t;

        // No model wired at all.
        QCOMPARE(t.videoPath(), QString());
        QCOMPARE(t.videoOffset(), 0.0);
        QCOMPARE(t.videoDuration(), 0.0);

        // Model wired but empty: same defaults, writes dropped (qWarning).
        ClipListModel model;
        t.setClipModel(&model);
        QCOMPARE(t.videoPath(), QString());
        QCOMPARE(t.videoOffset(), 0.0);
        QCOMPARE(t.videoDuration(), 0.0);

        t.setVideoOffset(42.0);
        t.setVideoDuration(90.0);
        t.setVideoPath(QStringLiteral("/nope.mp4"));
        QCOMPARE(t.videoOffset(), 0.0);
        QCOMPARE(t.videoDuration(), 0.0);
        QCOMPARE(t.videoPath(), QString());

        QCOMPARE(t.getVideoStartTime(), 0.0);
        QCOMPARE(t.getVideoEndTime(), 0.0);
        QVERIFY(!t.isTimeInVideo(1.0));
    }

    void readThrough()
    {
        ClipListModel model;
        model.setProberForTesting([](const QString&) { return probedResult(90.0); });

        Timeline t;
        t.setClipModel(&model);

        VideoClip* clip = addSettledClip(model, makeClipFile("a.mp4"));
        QVERIFY(clip);
        QCOMPARE(t.activeClip(), clip);
        QCOMPARE(t.videoPath(), clip->path());
        QCOMPARE(t.videoDuration(), 90.0);
        QCOMPARE(t.videoOffset(), 0.0);

        // A clip-level change (as the timecode auto-sync or camera-profile
        // apply performs it) must surface through the facade NOTIFY.
        QSignalSpy offsetSpy(&t, &Timeline::videoOffsetChanged);
        clip->setSyncOffset(12.5);
        QCOMPARE(offsetSpy.count(), 1);
        QCOMPARE(t.videoOffset(), 12.5);
        QCOMPARE(t.getVideoStartTime(), 12.5);
        QCOMPARE(t.getVideoEndTime(), 102.5);
        QVERIFY(t.isTimeInVideo(50.0));
        QVERIFY(!t.isTimeInVideo(110.0));

        // Duration updates (probe refresh / player fallback) re-emit too.
        QSignalSpy durationSpy(&t, &Timeline::videoDurationChanged);
        clip->setDurationSeconds(120.0);
        QCOMPARE(durationSpy.count(), 1);
        QCOMPARE(t.videoDuration(), 120.0);
    }

    void writeThroughMarksManual()
    {
        ClipListModel model;
        model.setProberForTesting([](const QString&) { return probedResult(60.0); });

        Timeline t;
        t.setClipModel(&model);
        VideoClip* clip = addSettledClip(model, makeClipFile("b.mp4"));
        QVERIFY(clip);
        QCOMPARE(clip->syncSource(), VideoClip::Unsynced);

        // The facade write path is the manual sync gesture (zone drag /
        // SpinBox / nudge): value lands on the clip, source becomes Manual.
        QSignalSpy offsetSpy(&t, &Timeline::videoOffsetChanged);
        t.setVideoOffset(42.0);
        QCOMPARE(clip->syncOffset(), 42.0);
        QCOMPARE(clip->syncSource(), VideoClip::Manual);
        QCOMPARE(t.videoOffset(), 42.0);
        QCOMPARE(offsetSpy.count(), 1);

        // A same-value write is a no-op and must NOT re-attribute the sync
        // (e.g. the SpinBox echoing a camera-profile offset back).
        clip->setSyncSource(VideoClip::CameraProfile);
        t.setVideoOffset(42.0);
        QCOMPARE(clip->syncSource(), VideoClip::CameraProfile);
        QCOMPARE(offsetSpy.count(), 1);

        // Duration write-through. Non-positive writes are dropped: they
        // must neither clobber a known duration nor turn the -1 "never
        // probed" sentinel into a known 0 (A3 persists durationSeconds,
        // where the two differ on disk).
        QCOMPARE(clip->durationSeconds(), 60.0);
        t.setVideoDuration(-5.0);
        QCOMPARE(clip->durationSeconds(), 60.0);
        QCOMPARE(t.videoDuration(), 60.0);
        t.setVideoDuration(75.0);
        QCOMPARE(t.videoDuration(), 75.0);

        // Paths are immutable through the facade: a redundant write of the
        // current value is tolerated, anything else is dropped.
        const QString before = t.videoPath();
        t.setVideoPath(before);
        t.setVideoPath(QStringLiteral("/other.mp4"));
        QCOMPARE(t.videoPath(), before);
    }

    // The timeline's Video Offset SpinBox shows Math.round(videoOffset) but
    // the stored offset is a double (timecode auto-sync / the dive-time
    // field produce fractions). Locks the two facts its QML write guard
    // relies on: the facade never rounds in either direction, and a write
    // of the displayed rounding is a REAL change (so the SpinBox must only
    // write a typed value that differs from the displayed rounding — an
    // echo/focus-out write-back would shave the fraction and re-attribute
    // the sync to Manual).
    void offsetsPassThroughUnrounded()
    {
        ClipListModel model;
        model.setProberForTesting([](const QString&) { return probedResult(60.0); });

        Timeline t;
        t.setClipModel(&model);
        VideoClip* clip = addSettledClip(model, makeClipFile("frac.mp4"));
        QVERIFY(clip);

        // Programmatic sync (auto-sync / camera profile set the clip
        // directly): fraction surfaces intact, attribution untouched.
        QSignalSpy offsetSpy(&t, &Timeline::videoOffsetChanged);
        clip->setSyncOffset(590.353);
        clip->setSyncSource(VideoClip::Timecode);
        QCOMPARE(offsetSpy.count(), 1);
        QCOMPARE(t.videoOffset(), 590.353);
        QCOMPARE(clip->syncSource(), VideoClip::Timecode);

        // Writing the displayed rounding is not a no-op: it truncates the
        // stored offset and marks Manual.
        t.setVideoOffset(590.0);
        QCOMPARE(clip->syncOffset(), 590.0);
        QCOMPARE(clip->syncSource(), VideoClip::Manual);

        // Large negative offsets (clip starting long before the dive
        // start) round-trip exactly — the −3723 typed-input case.
        t.setVideoOffset(-3723.0);
        QCOMPARE(clip->syncOffset(), -3723.0);
        QCOMPARE(t.videoOffset(), -3723.0);
        t.setVideoOffset(-3723.25);
        QCOMPARE(t.videoOffset(), -3723.25);
    }

    void unknownDurationReadsAsZero()
    {
        ClipListModel model;
        model.setProberForTesting([](const QString&) { return failedResult(); });

        Timeline t;
        t.setClipModel(&model);
        VideoClip* clip = addSettledClip(model, makeClipFile("c.mp4"));
        QVERIFY(clip);
        QCOMPARE(clip->probeState(), VideoClip::Failed);

        // VideoClip keeps -1 for "unknown"; the facade's historical contract
        // is 0 (what the zone paint and export dialog gate on).
        QCOMPARE(clip->durationSeconds(), -1.0);
        QCOMPARE(t.videoDuration(), 0.0);
        QCOMPARE(t.getVideoEndTime(), t.getVideoStartTime());
    }

    void activeClipSwitchReemitsEverything()
    {
        ClipListModel model;
        model.setProberForTesting([](const QString&) { return probedResult(30.0); });

        Timeline t;
        t.setClipModel(&model);

        VideoClip* first = addSettledClip(model, makeClipFile("d.mp4"));
        QVERIFY(first);
        VideoClip* second = addSettledClip(model, makeClipFile("e.mp4"));
        QVERIFY(second);
        first->setSyncOffset(10.0);
        second->setSyncOffset(200.0);
        second->setDurationSeconds(45.0);

        // addClip activates the newest clip.
        QCOMPARE(model.activeClip(), second);
        QCOMPARE(t.videoOffset(), 200.0);

        QSignalSpy pathSpy(&t, &Timeline::videoPathChanged);
        QSignalSpy offsetSpy(&t, &Timeline::videoOffsetChanged);
        QSignalSpy durationSpy(&t, &Timeline::videoDurationChanged);
        QSignalSpy clipSpy(&t, &Timeline::activeClipChanged);

        model.setActiveIndex(0);
        QVERIFY(pathSpy.count() >= 1);
        QVERIFY(offsetSpy.count() >= 1);
        QVERIFY(durationSpy.count() >= 1);
        QCOMPARE(clipSpy.count(), 1);
        QCOMPARE(t.activeClip(), first);
        QCOMPARE(t.videoPath(), first->path());
        QCOMPARE(t.videoOffset(), 10.0);
        QCOMPARE(t.videoDuration(), 30.0);

        // The facade connections must have moved with the switch: a change
        // on the now-active clip notifies, one on the old clip must not.
        const int offsetsAfterSwitch = offsetSpy.count();
        second->setSyncOffset(201.0);
        QCOMPARE(offsetSpy.count(), offsetsAfterSwitch);
        first->setSyncOffset(11.0);
        QCOMPARE(offsetSpy.count(), offsetsAfterSwitch + 1);
        QCOMPARE(t.videoOffset(), 11.0);

        // Removing the last clip empties the facade and notifies again.
        model.removeClip(1);
        model.removeClip(0);
        QCOMPARE(t.activeClip(), static_cast<VideoClip*>(nullptr));
        QCOMPARE(t.videoPath(), QString());
        QCOMPARE(t.videoOffset(), 0.0);
        QCOMPARE(t.videoDuration(), 0.0);
    }
};

QTEST_GUILESS_MAIN(TimelineFacadeTest)
#include "timeline_facade_test.moc"
