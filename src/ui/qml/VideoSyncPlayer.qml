import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

Item {
    id: root

    property url videoSource: ""
    signal syncOffsetComputed(double offset)

    // Emitted to drive the timeline's red cursor to the dive-time of the current
    // video frame while this tab is active. main.qml routes it to currentTime.
    signal cursorTimeRequested(double diveTime)

    // True while the Video Preview tab is the active tab. Gates cursor-driving so
    // we don't move the shared timeline cursor while the user is on another tab.
    property bool tabActive: false

    property double videoCreationTime: -1  // UTC seconds since epoch; -1 if unavailable
    property var dive: null                // DiveData object (exposes startTime QDateTime)

    // Seconds to add to MediaPlayer.position to obtain dive-time. Owned by
    // the Timeline; mirrored here so the in-video overlay items can compute
    // their dive time without reaching up the tree.
    property double videoOffset: 0

    // Exposed so other UI (e.g. the Profile tab pulse timer) can gate work
    // on whether the user is actively watching the video.
    readonly property bool playing: syncPlayer.playbackState === MediaPlayer.PlayingState

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

    readonly property bool metadataAvailable: root.videoCreationTime >= 0
    readonly property bool diveAvailable: root.dive !== null

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

    // Reactive mirror of config.cameraPairingNames() — updated via onCameraPairingsChanged
    property var profileNames: []

    Component.onCompleted: {
        root.profileNames = config.cameraPairingNames()
        loadOverlayLayoutFromConfig()
    }

    onVideoSourceChanged: {
        syncPlayer.source = videoSource
        loadOverlayLayoutFromConfig()
    }

    function videoLocalPath() {
        if (videoSource == "") return ""
        return mainWindow.urlToLocalFile(videoSource.toString())
    }

    function loadOverlayLayoutFromConfig() {
        const path = videoLocalPath()
        // Even with an empty path, fetch the last-used layout as the default.
        const layout = config.videoOverlayLayout(path)
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

    function saveOverlayLayoutToConfig() {
        const path = videoLocalPath()
        if (path === "") return
        const dc = diveComputerOverlay.normalizedRect
        const dp = diveProfileOverlay.normalizedRect
        config.setVideoOverlayLayout(path, {
            "diveComputer": {
                "visible": diveComputerOverlay.placementVisible,
                "x": dc.x, "y": dc.y, "w": dc.width, "h": dc.height
            },
            "diveProfile": {
                "visible": diveProfileOverlay.placementVisible,
                "x": dp.x, "y": dp.y, "w": dp.width, "h": dp.height
            }
        })
    }

    // Force a re-fetch of both overlay images when any generator setting that
    // affects rendered output changes — needed while paused, since otherwise
    // diveTime is static and the Image source URL doesn't change.
    Connections {
        target: overlayGenerator
        function onFontChanged()              { root.refreshTick++ }
        function onTextColorChanged()         { root.refreshTick++ }
        function onTemplateChanged()          { root.refreshTick++ }
        function onShowDepthChanged()         { root.refreshTick++ }
        function onShowTemperatureChanged()   { root.refreshTick++ }
        function onShowNDLChanged()           { root.refreshTick++ }
        function onShowPressureChanged()      { root.refreshTick++ }
        function onShowTimeChanged()          { root.refreshTick++ }
        function onBackgroundOpacityChanged() { root.refreshTick++ }
        function onShowPO2Cell1Changed()      { root.refreshTick++ }
        function onShowPO2Cell2Changed()      { root.refreshTick++ }
        function onShowPO2Cell3Changed()      { root.refreshTick++ }
        function onShowCompositePO2Changed()  { root.refreshTick++ }
        function onCellsChanged()             { root.refreshTick++ }
        function onCellLayoutChanged()        { root.refreshTick++ }
    }
    Connections {
        target: profileGenerator
        function onBackgroundColorChanged()   { root.refreshTick++ }
        function onBackgroundOpacityChanged() { root.refreshTick++ }
        function onCurveColorChanged()        { root.refreshTick++ }
        function onIndicatorColorChanged()    { root.refreshTick++ }
        function onIndicatorModeChanged()     { root.refreshTick++ }
        function onIndicatorRadiusChanged()   { root.refreshTick++ }
        function onOutputWidthChanged()       { root.refreshTick++ }
        function onOutputHeightChanged()      { root.refreshTick++ }
        function onDecoZoneColorChanged()     { root.refreshTick++ }
        function onDecoZoneOpacityChanged()   { root.refreshTick++ }
        function onGridEnabledChanged()       { root.refreshTick++ }
        function onGridColorChanged()         { root.refreshTick++ }
        function onGridOpacityChanged()       { root.refreshTick++ }
    }

    function parseDiveTime(text) {
        text = text.trim()
        if (text === "") return -1
        if (/^\d+$/.test(text)) return parseInt(text, 10)
        var parts = text.split(":")
        if (parts.length === 2) {
            var m = parseInt(parts[0], 10), s = parseInt(parts[1], 10)
            if (isNaN(m) || isNaN(s) || s >= 60) return -1
            return m * 60 + s
        }
        if (parts.length === 3) {
            var h = parseInt(parts[0], 10), m2 = parseInt(parts[1], 10), s2 = parseInt(parts[2], 10)
            if (isNaN(h) || isNaN(m2) || isNaN(s2) || m2 >= 60 || s2 >= 60) return -1
            return h * 3600 + m2 * 60 + s2
        }
        return -1
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
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Video display area
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "black"

            VideoOutput {
                id: videoOut
                anchors.fill: parent
            }

            // Live preview overlays. Anchored to the video Rectangle so
            // their normalized rects map onto the full video area.
            VideoOverlayItem {
                id: diveComputerOverlay
                imageSourceBase: "image://overlay/at/"
                diveTime: root.currentDiveTime
                refreshTick: root.refreshTick
                editable: true
                visible: placementVisible && root.dive !== null && root.videoSource != ""
                onLayoutCommitted: root.saveOverlayLayoutToConfig()
                onPlacementVisibleChanged: root.saveOverlayLayoutToConfig()
            }

            VideoOverlayItem {
                id: diveProfileOverlay
                imageSourceBase: "image://profile/at/"
                diveTime: root.currentDiveTime
                refreshTick: root.refreshTick
                editable: true
                visible: placementVisible && root.dive !== null && root.videoSource != ""
                onLayoutCommitted: root.saveOverlayLayoutToConfig()
                onPlacementVisibleChanged: root.saveOverlayLayoutToConfig()
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

        // Transport bar
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 48
            color: palette.dark

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

                CheckBox {
                    text: qsTr("Dive Computer")
                    checked: diveComputerOverlay.placementVisible
                    onToggled: diveComputerOverlay.placementVisible = checked
                }

                CheckBox {
                    text: qsTr("Profile")
                    checked: diveProfileOverlay.placementVisible
                    onToggled: diveProfileOverlay.placementVisible = checked
                }
            }
        }

        // Sync workflow panel
        GroupBox {
            Layout.fillWidth: true
            title: qsTr("Sync with Dive Log")
            padding: 10

            ColumnLayout {
                anchors { left: parent.left; right: parent.right }
                spacing: 8

                // ── Section A: Use Saved Profile ──────────────────────────
                ColumnLayout {
                    visible: root.profileNames.length > 0
                             && root.metadataAvailable
                             && root.diveAvailable
                    spacing: 4

                    Label { text: qsTr("Apply Saved Camera Profile"); font.bold: true }

                    RowLayout {
                        spacing: 8

                        ComboBox {
                            id: profileComboBox
                            Layout.fillWidth: true
                            model: root.profileNames
                        }

                        Button {
                            text: qsTr("Apply Profile")
                            enabled: profileComboBox.count > 0
                            onClicked: {
                                var constant = config.cameraCalibrationConstant(
                                                   profileComboBox.currentText)
                                var diveStartSecs = root.dive.startTime.getTime() / 1000.0
                                var suggested = (root.videoCreationTime - diveStartSecs) + constant
                                root.syncOffsetComputed(suggested)
                            }
                        }
                    }

                    Label {
                        visible: profileComboBox.count > 0
                        color: palette.placeholderText
                        font.pixelSize: 11
                        text: {
                            if (profileComboBox.count === 0 || !root.metadataAvailable || !root.diveAvailable)
                                return ""
                            var constant = config.cameraCalibrationConstant(profileComboBox.currentText)
                            var diveStartSecs = root.dive.startTime.getTime() / 1000.0
                            var suggested = (root.videoCreationTime - diveStartSecs) + constant
                            return qsTr("Suggested offset: %1 s").arg(suggested.toFixed(1))
                        }
                    }

                    Connections {
                        target: config
                        function onCameraPairingsChanged() {
                            root.profileNames = config.cameraPairingNames()
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid }
                }

                // ── No-metadata notice ────────────────────────────────────
                Label {
                    visible: !root.metadataAvailable && root.videoSource != ""
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: palette.placeholderText
                    font.pixelSize: 11
                    text: qsTr("Video creation time not available — camera profiles cannot be used or saved for this file.")
                }

                // ── Graphical sync ────────────────────────────────────────
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.videoSource == ""
                          ? qsTr("No video loaded.")
                          : qsTr("Pause when your dive computer is clearly visible, then drag the video band on the timeline (or nudge below) until the overlay's data matches the display on your computer. The red cursor follows the video.")
                }

                RowLayout {
                    visible: root.videoSource != ""
                    spacing: 8

                    Label { text: qsTr("Dive time at this frame:") }

                    TextField {
                        id: diveTimeField
                        placeholderText: qsTr("MM:SS  (e.g. 12:15)")
                        implicitWidth: 120
                        // Live readout of the current frame's dive-time. Editing
                        // breaks this binding (user value wins); committing restores
                        // it. While paused exactDiveTime is static, so typing isn't
                        // overwritten.
                        text: formatTime(root.exactDiveTime)
                        onEditingFinished: {
                            // Only commit when the user actually changed the value —
                            // a bare focus-loss must not re-apply the floored readout
                            // (which would shave the sub-second fraction off the offset).
                            if (text !== formatTime(root.exactDiveTime)) {
                                var secs = parseDiveTime(text)
                                if (secs >= 0)
                                    root.syncOffsetComputed(secs - syncPlayer.position / 1000.0)
                            }
                            // Re-establish the live readout binding.
                            text = Qt.binding(function() { return formatTime(root.exactDiveTime) })
                        }
                    }

                    Label { text: qsTr("Nudge:") }

                    Button {
                        text: qsTr("-1s")
                        enabled: root.videoSource != ""
                        onClicked: root.syncOffsetComputed(root.videoOffset - 1)
                    }

                    Button {
                        text: qsTr("+1s")
                        enabled: root.videoSource != ""
                        onClicked: root.syncOffsetComputed(root.videoOffset + 1)
                    }
                }

                // ── Section B: Save Profile ───────────────────────────────
                ColumnLayout {
                    visible: root.metadataAvailable && root.diveAvailable && root.videoSource != ""
                    spacing: 4

                    Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid }

                    Label { text: qsTr("Save Camera Profile"); font.bold: true }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: palette.placeholderText
                        text: qsTr("Give this camera a name to remember its clock offset. Future dives with the same camera will sync automatically.")
                    }

                    RowLayout {
                        spacing: 8

                        TextField {
                            id: cameraNameField
                            Layout.fillWidth: true
                            placeholderText: qsTr("Camera name (e.g. GoPro Hero 12)")
                        }

                        Button {
                            text: qsTr("Save Profile")
                            enabled: cameraNameField.text.trim().length > 0
                            onClicked: {
                                var savedName = cameraNameField.text.trim()
                                var diveStartSecs = root.dive.startTime.getTime() / 1000.0
                                // The current live offset is the sync result.
                                var constant = root.videoOffset
                                               - (root.videoCreationTime - diveStartSecs)
                                // addOrUpdateCameraPairing emits cameraPairingsChanged
                                // synchronously, so root.profileNames and profileComboBox.model
                                // are already updated on the next line
                                config.addOrUpdateCameraPairing(savedName, constant)
                                cameraNameField.text = ""
                                // Select the saved profile in the ComboBox
                                var idx = root.profileNames.indexOf(savedName)
                                if (idx >= 0)
                                    profileComboBox.currentIndex = idx
                            }
                        }
                    }
                }
            }
        }
    }
}
