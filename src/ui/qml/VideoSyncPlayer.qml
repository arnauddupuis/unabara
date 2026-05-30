import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

Item {
    id: root

    property url videoSource: ""
    signal syncOffsetComputed(double offset)

    property double videoCreationTime: -1  // UTC seconds since epoch; -1 if unavailable
    property var dive: null                // DiveData object (exposes startTime QDateTime)

    property double capturedVideoPosition: -1
    property bool syncPointCaptured: capturedVideoPosition >= 0
    property double parsedDiveSeconds: -1
    property bool inputValid: parsedDiveSeconds >= 0
    property double previewOffset: (syncPointCaptured && inputValid)
                                   ? parsedDiveSeconds - capturedVideoPosition
                                   : 0
    property bool syncApplied: false

    readonly property bool metadataAvailable: root.videoCreationTime >= 0
    readonly property bool diveAvailable: root.dive !== null

    // Reactive mirror of config.cameraPairingNames() — updated via onCameraPairingsChanged
    property var profileNames: []

    Component.onCompleted: {
        root.profileNames = config.cameraPairingNames()
    }

    onVideoSourceChanged: {
        syncPlayer.source = videoSource
        root.capturedVideoPosition = -1
        diveTimeField.text = ""
        root.syncApplied = false
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
                                root.syncApplied = true
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

                // ── Manual sync workflow ──────────────────────────────────
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: {
                        if (root.videoSource == "")
                            return qsTr("No video loaded.")
                        if (!root.syncPointCaptured)
                            return qsTr("Play the video and pause when you can clearly see the dive computer display, then press 'Set Sync Point'.")
                        if (!root.inputValid)
                            return qsTr("Video captured at %1. Now enter the dive time shown on your dive computer display.").arg(formatTime(root.capturedVideoPosition))
                        return qsTr("Ready to apply. The video will be aligned to start %1 seconds into the dive.").arg(root.previewOffset.toFixed(1))
                    }
                }

                RowLayout {
                    spacing: 8

                    Button {
                        text: root.syncPointCaptured ? qsTr("Reset Sync Point") : qsTr("Set Sync Point")
                        enabled: root.videoSource != ""
                        onClicked: {
                            syncPlayer.pause()
                            root.capturedVideoPosition = syncPlayer.position / 1000.0
                            diveTimeField.text = ""
                            diveTimeField.forceActiveFocus()
                        }
                    }

                    Label {
                        visible: root.syncPointCaptured
                        text: qsTr("Captured at %1").arg(formatTime(root.capturedVideoPosition))
                    }
                }

                RowLayout {
                    visible: root.syncPointCaptured
                    spacing: 8

                    Label { text: qsTr("Dive time shown on computer:") }

                    TextField {
                        id: diveTimeField
                        placeholderText: qsTr("MM:SS  (e.g. 12:15)")
                        implicitWidth: 160
                        onTextChanged: root.parsedDiveSeconds = parseDiveTime(text)
                    }

                    Button {
                        text: qsTr("Apply")
                        enabled: root.syncPointCaptured && root.inputValid
                        onClicked: {
                            root.syncOffsetComputed(root.previewOffset)
                            root.syncApplied = true
                        }
                    }
                }

                // ── Section B: Save Profile ───────────────────────────────
                ColumnLayout {
                    visible: root.syncApplied && root.metadataAvailable && root.diveAvailable
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
                                var constant = root.previewOffset
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
