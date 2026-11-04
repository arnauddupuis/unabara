import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import Unabara.Core 1.0

Item {
    id: root

    // The active VideoClip (clipList.activeClip, bound in main.qml); null
    // when no video is imported. Single source of truth for the source URL,
    // creation time and (via the probe) duration.
    // Named videoClip, not clip: a plain `clip` property on an Item root
    // shadows QQuickItem::clip (same landmine class ClipStrip's delegate hit).
    property var videoClip: null

    readonly property url videoSource: videoClip ? videoClip.url : ""

    // Emitted to drive the timeline's red cursor to the dive-time of the current
    // video frame while this tab is active. main.qml routes it to currentTime.
    signal cursorTimeRequested(double diveTime)

    // True while the Video tab is the active tab. Gates cursor-driving so
    // we don't move the shared timeline cursor while the user is on another tab.
    property bool tabActive: false

    property var dive: null                // DiveData object (exposes startTime QDateTime)

    // Seconds to add to MediaPlayer.position to obtain dive-time. Owned by
    // the Timeline; mirrored here so the in-video overlay items can compute
    // their dive time without reaching up the tree.
    property double videoOffset: 0

    // Exposed so other UI (e.g. the Profile tab pulse timer) can gate work
    // on whether the user is actively watching the video.
    readonly property bool playing: syncPlayer.playbackState === MediaPlayer.PlayingState

    // Stop playback from outside (exports pause the player: the generators
    // are not thread-safe, and playback keeps the image provider rendering
    // on its worker thread while the exporter renders on the GUI thread).
    function pause() {
        if (syncPlayer.playbackState === MediaPlayer.PlayingState)
            syncPlayer.pause()
    }

    // Quantized to the same bucket as FrameCache so the Image source URL
    // doesn't change faster than the cache regenerates frames.
    readonly property double bucketSeconds: 0.5
    readonly property double currentDiveTime: {
        const raw = (syncPlayer.position / 1000.0) + root.videoOffset
        return Math.floor(raw / bucketSeconds) * bucketSeconds
    }

    // Bumped whenever a generator setting that affects rendered output
    // changes, so the Image re-requests with a new URL token while paused.
    property int refreshTick: 0

    // Precise (un-bucketed) dive-time of the current video frame. Drives the red
    // cursor and the editable "Dive time at this frame" field. The overlay images
    // use the bucketed currentDiveTime instead so the cache isn't thrashed.
    readonly property double exactDiveTime: (syncPlayer.position / 1000.0) + root.videoOffset

    // C-track hooks (v0.4 plan): the player position in seconds for the
    // inspector's dive-time field and C3's depth readout, and the
    // VideoOutput item for C2's ShaderEffectSource.
    readonly property double videoPositionSeconds: syncPlayer.position / 1000.0
    property alias videoOutput: videoOut

    // C3 depth-linked grading: push the current frame's dive-time (plus
    // the raw player position, for anchor capture) into the controller.
    // Gated like cursor-driving above so hidden tabs never pay for the
    // per-tick depth resolve; the gate's own flip pushes once, so enabling
    // the link / switching to this tab while paused still lands a fresh
    // sample immediately.
    readonly property bool depthLinkFeedActive: tabActive && root.dive !== null
                                                && colorGrade.hasClip
                                                && colorGrade.enabled
                                                && colorGrade.depthLinked
    onDepthLinkFeedActiveChanged: pushDepthSample()
    onExactDiveTimeChanged: pushDepthSample()
    // Clip/dive switches reset the controller's sample without necessarily
    // flipping the gate or moving exactDiveTime — re-push on its request.
    Connections {
        target: colorGrade
        function onPlaybackSampleInvalidated() { root.pushDepthSample() }
    }
    function pushDepthSample() {
        if (depthLinkFeedActive)
            colorGrade.setCurrentDiveTime(exactDiveTime, videoPositionSeconds)
    }

    // "Go to" on a depth-anchor row seeks the player to the anchor's
    // bound video time.
    Connections {
        target: colorGrade
        function onSeekRequested(videoTimeSeconds) {
            syncPlayer.setPosition(Math.round(videoTimeSeconds * 1000))
        }
    }

    // Push the red timeline cursor to the current frame's dive-time. No-op unless
    // this tab is active and a video + dive are loaded.
    function updateCursor() {
        if (tabActive && videoSource != "" && root.dive !== null)
            root.cursorTimeRequested(exactDiveTime)
    }

    // Re-drive the cursor whenever the frame's dive-time can change: the offset is
    // adjusted, or the tab becomes active. (Playback position is handled in the
    // syncPlayer Connections below.)
    onVideoOffsetChanged: updateCursor()
    onTabActiveChanged: updateCursor()

    Component.onCompleted: applyClipOverlayLayout()

    onVideoSourceChanged: {
        // Stop before switching sources: assigning a new source to a playing
        // MediaPlayer can keep decoding the old one on some backends.
        syncPlayer.stop()
        syncPlayer.source = videoSource
    }

    // Follow-up F: the clip model owns layout resolution (dive x resolution
    // bucket → path entry → last-used → defaults) and re-broadcasts edits
    // to same-bucket siblings, so the preview items simply MIRROR the
    // active clip's overlayLayoutMap: on clip switch, and whenever the
    // model changes it (a probe landing re-joins the clip's bucket, or a
    // sibling broadcast). This side never reads Config.
    onVideoClipChanged: applyClipOverlayLayout()
    Connections {
        target: root.videoClip
        ignoreUnknownSignals: true
        function onOverlayLayoutChanged() { root.applyClipOverlayLayout() }
    }

    function applyClipOverlayLayout() {
        if (!videoClip)
            return
        const layout = videoClip.overlayLayoutMap
        if (layout.diveComputer) {
            const dc = layout.diveComputer
            diveComputerOverlay.normalizedRect = Qt.rect(dc.x, dc.y, dc.w, dc.h)
            diveComputerOverlay.placementVisible = dc.visible
        }
        if (layout.diveProfile) {
            const dp = layout.diveProfile
            diveProfileOverlay.normalizedRect = Qt.rect(dp.x, dp.y, dp.w, dp.h)
            diveProfileOverlay.placementVisible = dp.visible
        }
    }

    // Commit channel for USER gestures only — drag/resize release and the
    // two visibility checkboxes. Deliberately NOT wired to
    // placementVisibleChanged: applyClipOverlayLayout() writes
    // placementVisible imperatively, and committing from there would
    // persist (and broadcast to the whole bucket) a half-applied layout —
    // one overlay already switched to the new clip, the other still the
    // previous clip's.
    function commitOverlayLayout() {
        if (!videoClip)
            return
        const dc = diveComputerOverlay.normalizedRect
        const dp = diveProfileOverlay.normalizedRect
        const layout = {
            "diveComputer": {
                "visible": diveComputerOverlay.placementVisible,
                "x": dc.x, "y": dc.y, "w": dc.width, "h": dc.height
            },
            "diveProfile": {
                "visible": diveProfileOverlay.placementVisible,
                "x": dp.x, "y": dp.y, "w": dp.width, "h": dp.height
            }
        }
        clipList.commitOverlayLayout(videoClip, layout)
    }

    // Force a re-fetch of both overlay images when any generator setting that
    // affects rendered output changes — needed while paused, since otherwise
    // diveTime is static and the Image source URL doesn't change.
    //
    // The bump is deferred one event-loop turn (Qt.callLater): QML signal
    // handlers run BEFORE plain QObject::connect slots on the same signal, so
    // a synchronous bump posts the async image request before main.cpp's
    // FrameCache::invalidate() has run, and the loader thread can serve the
    // stale cached frame (observed: ~1 in 3 unit flips stayed stale).
    function bumpRefreshTick() { root.refreshTick++ }
    function requestRefresh()  { Qt.callLater(root.bumpRefreshTick) }
    Connections {
        target: overlayGenerator
        function onFontChanged()              { root.requestRefresh() }
        function onLabelColorChanged()        { root.requestRefresh() }
        function onValueColorChanged()        { root.requestRefresh() }
        function onTemplateChanged()          { root.requestRefresh() }
        function onShowDepthChanged()         { root.requestRefresh() }
        function onShowTemperatureChanged()   { root.requestRefresh() }
        function onShowNDLChanged()           { root.requestRefresh() }
        function onShowPressureChanged()      { root.requestRefresh() }
        function onShowTimeChanged()          { root.requestRefresh() }
        function onBackgroundOpacityChanged() { root.requestRefresh() }
        function onShowPO2Cell1Changed()      { root.requestRefresh() }
        function onShowPO2Cell2Changed()      { root.requestRefresh() }
        function onShowPO2Cell3Changed()      { root.requestRefresh() }
        function onShowCompositePO2Changed()  { root.requestRefresh() }
        function onCellsChanged()             { root.requestRefresh() }
        function onCellLayoutChanged()        { root.requestRefresh() }
    }
    Connections {
        target: profileGenerator
        function onBackgroundColorChanged()   { root.requestRefresh() }
        function onBackgroundOpacityChanged() { root.requestRefresh() }
        function onCurveColorChanged()        { root.requestRefresh() }
        function onCurveWidthChanged()        { root.requestRefresh() }
        function onIndicatorColorChanged()    { root.requestRefresh() }
        function onIndicatorModeChanged()     { root.requestRefresh() }
        function onIndicatorRadiusChanged()   { root.requestRefresh() }
        function onOutputWidthChanged()       { root.requestRefresh() }
        function onOutputHeightChanged()      { root.requestRefresh() }
        function onDecoZoneColorChanged()     { root.requestRefresh() }
        function onDecoZoneOpacityChanged()   { root.requestRefresh() }
        function onGridEnabledChanged()       { root.requestRefresh() }
        function onGridColorChanged()         { root.requestRefresh() }
        function onGridOpacityChanged()       { root.requestRefresh() }
        function onGridDepthIntervalChanged() { root.requestRefresh() }
        function onGridTimeIntervalChanged()  { root.requestRefresh() }
        function onGridLineWidthChanged()     { root.requestRefresh() }
        function onGridShowLabelsChanged()    { root.requestRefresh() }
        // Pulse phase derives from the timestamp and the period, so a period
        // change alters the indicator even at a fixed paused time.
        function onPulsePeriodMsChanged()     { root.requestRefresh() }
    }
    // Unit flips re-format every value both generators draw; main.cpp already
    // invalidates both FrameCaches, but a paused video still needs a new URL.
    Connections {
        target: config
        function onUnitSystemChanged()        { root.requestRefresh() }
    }

    function formatTime(totalSeconds) {
        var s = Math.max(0, Math.floor(totalSeconds))
        var m = Math.floor(s / 60)
        var ss = s % 60
        return m + ":" + (ss < 10 ? "0" : "") + ss
    }

    MediaPlayer {
        id: syncPlayer
        videoOutput: videoOut
    }

    Connections {
        target: syncPlayer
        function onPositionChanged() {
            if (!seekSlider.pressed)
                seekSlider.value = syncPlayer.position
            root.updateCursor()
        }
        // Duration fallback: when ffprobe is unavailable (probe failed or
        // still pending) the clip has no duration, so feed the real player's
        // once it is known. A successful probe later overwrites it with the
        // container-exact value.
        function onDurationChanged() {
            if (root.videoClip && syncPlayer.duration > 0
                    && (root.videoClip.probeState !== VideoClip.Probed
                        || root.videoClip.durationSeconds <= 0)) {
                root.videoClip.setDurationSeconds(syncPlayer.duration / 1000.0)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Video display area
        Rectangle {
            id: videoSurface
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "black"

            VideoOutput {
                id: videoOut
                anchors.fill: parent
            }

            // ── Colour-grade preview chain (v0.4 track C, PR C2) ─────────
            //
            // Activity gating (the plan's "inactive = zero cost" rule):
            //  - gradeChainAlive keeps the ShaderEffectSource attached while
            //    the grade is merely bypassed (hold-to-compare), so a press
            //    doesn't tear down and rebuild the FBO; with the effect
            //    invisible nothing pulls the texture, so a held press costs
            //    nothing either.
            //  - grade disabled / no clip → sourceItem: null releases the
            //    FBO entirely and the effect is visible:false +
            //    hideSource:false → plain VideoOutput, zero shader cost.
            //  - bypass shows the ORIGINAL by deactivating the effect
            //    (visible:false + hideSource:false), not via a shader
            //    uniform — cheaper, and exactly original by construction.
            readonly property bool gradeChainAlive: colorGrade.enabled
                                                    && colorGrade.hasClip
                                                    && root.videoSource != ""
            readonly property bool gradeActive: colorGrade.previewActive
                                                && root.videoSource != ""

            ShaderEffectSource {
                id: gradeSource
                visible: false
                live: true
                sourceItem: videoSurface.gradeChainAlive ? videoOut : null
                // contentRect is in videoOut's own coordinates — exactly
                // what sourceRect expects. FBO at display size, per plan.
                sourceRect: videoOut.contentRect
                hideSource: gradeEffect.visible
            }

            // Hidden sampler feed for the shader's LUT strip (binding 2).
            // Identity until C4's LUT import; lutRevision busts Qt's
            // per-URL image cache when the strip changes. smooth (linear
            // filtering) is REQUIRED: the shader's in-slice red/green
            // interpolation is the sampler's own bilinear filter.
            Image {
                id: lutStripImage
                visible: false
                smooth: true
                mipmap: false
                cache: false
                source: "image://lutstrip/" + colorGrade.lutRevision
            }

            // Live preview overlays, parented to the actual video content
            // area (videoOut.contentRect) rather than the full black
            // Rectangle — with a letterboxed video the bars would otherwise
            // be inside the normalized overlay space, so the preview
            // placement could never match a composited export, which has no
            // bars. contentRect is in videoOut's coordinates; videoOut
            // fills this Rectangle, so the mapping is direct. The overlay
            // items derive their geometry from this parent via bindings
            // (they only ever write their own normalizedRect), so drag /
            // resize restore math follows the content area automatically.
            Item {
                id: videoContentArea
                x: videoOut.contentRect.x
                y: videoOut.contentRect.y
                width: videoOut.contentRect.width
                height: videoOut.contentRect.height

                // Graded video, drawn exactly over the content area. FIRST
                // child on purpose: the VideoOverlayItems below are later
                // siblings, so they stack above the effect and stay
                // ungraded (their FrameCache images are never routed
                // through the shader — correct per the plan; the export
                // path grades video before compositing overlays too).
                ShaderEffect {
                    id: gradeEffect
                    anchors.fill: parent
                    visible: videoSurface.gradeActive
                    fragmentShader: "qrc:/shaders/colorgrade.frag.qsb"

                    // Sampler + uniform properties: names must match the
                    // std140 block members in colorgrade.frag exactly.
                    property variant source: gradeSource
                    property variant lutStrip: lutStripImage
                    property vector4d uMatR: colorGrade.matrixR
                    property vector4d uMatG: colorGrade.matrixG
                    property vector4d uMatB: colorGrade.matrixB
                    property vector4d uBlack: colorGrade.blackPoint
                    property vector4d uLift: colorGrade.lift
                    property real uContrast: colorGrade.contrast
                    property real uSplitPos: colorGrade.splitPos
                    property real uLutEnabled: colorGrade.lutEnabled ? 1.0 : 0.0
                    property real uLutSize: colorGrade.lutSize
                }

                VideoOverlayItem {
                    id: diveComputerOverlay
                    imageSourceBase: "image://overlay/at/"
                    diveTime: root.currentDiveTime
                    refreshTick: root.refreshTick
                    editable: true
                    visible: placementVisible && root.dive !== null && root.videoSource != ""
                    onLayoutCommitted: root.commitOverlayLayout()
                }

                VideoOverlayItem {
                    id: diveProfileOverlay
                    imageSourceBase: "image://profile/at/"
                    diveTime: root.currentDiveTime
                    refreshTick: root.refreshTick
                    editable: true
                    visible: placementVisible && root.dive !== null && root.videoSource != ""
                    onLayoutCommitted: root.commitOverlayLayout()
                }

                // ── Eyedropper capture (C4) ──────────────────────────
                // Armed by ColorPanel's "Pick neutral" toggle through
                // colorGrade.pickingActive. Fills the video CONTENT area,
                // so x/width,y/height is the click normalized across the
                // actual frame — exactly what pickNeutral() maps onto the
                // native-resolution QVideoSink frame. z above the overlay
                // items: a pick click must sample, never start a drag. A
                // successful pick disarms the mode in the controller; a
                // failed one (unmappable playing frame) toasts and stays
                // armed so the user can pause and click again.
                MouseArea {
                    anchors.fill: parent
                    z: 50
                    visible: colorGrade.pickingActive && root.videoSource != ""
                    enabled: visible
                    cursorShape: Qt.CrossCursor
                    onClicked: function(mouse) {
                        colorGrade.pickNeutral(videoOut.videoSink,
                                               mouse.x / Math.max(1, width),
                                               mouse.y / Math.max(1, height))
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: root.videoSource == ""
                text: qsTr("No video loaded.\nImport a video using the Import menu.")
                horizontalAlignment: Text.AlignHCenter
                color: "#888888"
                font.pixelSize: 16
            }
        }

        // Transport bar. Must stay on a theme-neutral surface: standard
        // controls (checkboxes, labels) draw theme-colored text/indicators,
        // which are unreadable on palette.dark in light mode.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 48
            color: palette.window

            RowLayout {
                anchors {
                    fill: parent
                    margins: 8
                }
                spacing: 8

                Button {
                    text: syncPlayer.playbackState === MediaPlayer.PlayingState
                          ? qsTr("Pause") : qsTr("Play")
                    enabled: root.videoSource != ""
                    onClicked: {
                        if (syncPlayer.playbackState === MediaPlayer.PlayingState)
                            syncPlayer.pause()
                        else
                            syncPlayer.play()
                    }
                }

                Slider {
                    id: seekSlider
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, syncPlayer.duration)
                    enabled: root.videoSource != "" && syncPlayer.duration > 0
                    // onMoved fires only on user interaction, so no binding-loop guard needed
                    onMoved: syncPlayer.setPosition(Math.round(value))
                }

                Label {
                    text: formatTime(syncPlayer.position / 1000) +
                          " / " + formatTime(syncPlayer.duration / 1000)
                    color: palette.windowText
                    font.family: "monospace"
                }

                // Visibility toggles are user gestures → commit (shared by
                // the clip's whole dive x resolution bucket). `checked` is
                // driven by a Binding element, not a plain binding: the
                // user's click writes `checked`, and a plain binding would
                // then never follow a clip switch again.
                CheckBox {
                    id: dcVisibleBox
                    text: qsTr("Dive Computer")
                    onToggled: {
                        diveComputerOverlay.placementVisible = checked
                        root.commitOverlayLayout()
                    }
                    Binding {
                        target: dcVisibleBox
                        property: "checked"
                        value: diveComputerOverlay.placementVisible
                    }
                }

                CheckBox {
                    id: profileVisibleBox
                    text: qsTr("Profile")
                    onToggled: {
                        diveProfileOverlay.placementVisible = checked
                        root.commitOverlayLayout()
                    }
                    Binding {
                        target: profileVisibleBox
                        property: "checked"
                        value: diveProfileOverlay.placementVisible
                    }
                }
            }
        }

        // The sync workflow ("Sync with Dive Log" GroupBox) moved to the
        // video inspector's Sync section (SyncSection.qml, PR A5).
    }
}
