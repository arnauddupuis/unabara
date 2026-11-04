import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Unabara.Core 1.0

// Right-side inspector for the Video tab (PR A5). Three collapsible
// sections, mirroring the tab-0/1 editor sidebars: Clip (active clip
// metadata), Sync (SyncSection — the sync workflow moved out of the
// player), and Colour (ColorPanel — track C's grading controls).
// Reads the `clipList` context property directly (same pattern as
// ClipStrip); main.qml wires the clip/dive/offset/position inputs and the
// syncOffsetComputed output.
Item {
    id: root

    // Active clip (clipList.activeClip) and current dive, bound in main.qml.
    property var videoClip: null
    property var dive: null
    // Mirror of the Timeline facade's videoOffset (dive time at clip t=0).
    property double videoOffset: 0
    // Player position in seconds (VideoSyncPlayer.videoPositionSeconds).
    property double videoPositionSeconds: 0

    // Manual sync gesture from the Sync section; main.qml routes it to the
    // Timeline facade (which attributes the write as SyncSource.Manual).
    signal syncOffsetComputed(double offset)
    // Bulk camera-profile apply summary, relayed from SyncSection for the
    // main-window toast (main.qml owns the toast).
    signal profileApplied(string profileName, int clipCount)

    implicitHeight: mainColumn.implicitHeight
    implicitWidth: 400

    readonly property bool hasClip: videoClip !== null

    // 10-bit or HDR-transfer sources render through an 8-bit SDR pipeline in
    // v0.4 (plan risk list) — surface that on the clip, not at export time.
    readonly property bool hdrSource: hasClip
                                      && (videoClip.bitDepth > 8
                                          || videoClip.colorTransfer === "smpte2084"
                                          || videoClip.colorTransfer === "arib-std-b67")

    // mm:ss — same formatting as the clip strip's duration label.
    function formatDuration(seconds) {
        var s = Math.max(0, Math.floor(seconds))
        var m = Math.floor(s / 60)
        var ss = s % 60
        return m + ":" + (ss < 10 ? "0" : "") + ss
    }

    // Embedded timecode is seconds-of-day; show it as HH:MM:SS.
    function formatTimecode(secondsOfDay) {
        if (secondsOfDay < 0)
            return qsTr("none")
        var s = Math.floor(secondsOfDay)
        var h = Math.floor(s / 3600)
        var m = Math.floor((s % 3600) / 60)
        var ss = s % 60
        function pad(v) { return (v < 10 ? "0" : "") + v }
        return pad(h) + ":" + pad(m) + ":" + pad(ss)
    }

    function tagOrUntagged(tag) {
        return tag && tag !== "" ? tag : qsTr("untagged")
    }

    ColumnLayout {
        id: mainColumn
        anchors.fill: parent
        spacing: 20

        // ── Clip: metadata of the active clip ────────────────────────────
        CollapsibleSection {
            title: qsTr("Clip")
            Layout.fillWidth: true
            // First-run defaults (item J): Clip and Sync collapsed, Colour
            // expanded. Stored ui/sections/video_* states still win.
            expanded: false
            settingsKey: "video_clip"

            Label {
                visible: !root.hasClip
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: palette.placeholderText
                text: qsTr("No clip selected — import or select a video in the strip above the player.")
            }

            ColumnLayout {
                visible: root.hasClip
                Layout.fillWidth: true
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label { text: qsTr("Name:") }

                    // Editable display name — same write path as the clip
                    // strip's inline rename. Typing breaks the live binding
                    // (user value wins); committing restores it.
                    TextField {
                        id: nameField
                        Layout.fillWidth: true
                        text: root.videoClip ? root.videoClip.displayName : ""
                        onEditingFinished: {
                            var trimmed = text.trim()
                            if (root.videoClip && trimmed.length > 0
                                    && trimmed !== root.videoClip.displayName)
                                root.videoClip.displayName = trimmed
                            // Re-establish the live binding (clip switches
                            // and strip renames must keep updating us).
                            text = Qt.binding(function() {
                                return root.videoClip ? root.videoClip.displayName : ""
                            })
                        }
                        // Escape cancels, like the clip strip's inline
                        // rename: restore the binding (discarding the
                        // edit) and drop focus so editingFinished's
                        // trimmed === displayName guard makes the commit
                        // a no-op.
                        Keys.onEscapePressed: {
                            text = Qt.binding(function() {
                                return root.videoClip ? root.videoClip.displayName : ""
                            })
                            focus = false
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 2

                    Label { text: qsTr("Path:"); color: palette.placeholderText }
                    Label {
                        Layout.fillWidth: true
                        text: root.videoClip ? root.videoClip.path : ""
                        elide: Text.ElideMiddle
                        ToolTip.visible: pathHoverArea.containsMouse
                        ToolTip.delay: 300
                        ToolTip.text: text
                        MouseArea {
                            id: pathHoverArea
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
                        }
                    }

                    Label { text: qsTr("Duration:"); color: palette.placeholderText }
                    Label {
                        text: root.videoClip && root.videoClip.durationSeconds > 0
                              ? root.formatDuration(root.videoClip.durationSeconds)
                              : "--:--"
                    }

                    Label { text: qsTr("Resolution:"); color: palette.placeholderText }
                    Label {
                        text: root.videoClip && root.videoClip.width > 0
                              ? root.videoClip.width + " × " + root.videoClip.height
                              : "—"
                    }

                    Label { text: qsTr("Frame rate:"); color: palette.placeholderText }
                    RowLayout {
                        spacing: 6

                        Label {
                            text: root.videoClip && root.videoClip.fps > 0
                                  ? qsTr("%1 fps").arg(Number(root.videoClip.fps.toFixed(3)))
                                  : "—"
                        }

                        // Plan risk list: VFR footage drifts against the
                        // constant-rate overlay math — warn on the clip.
                        Label {
                            visible: root.videoClip ? root.videoClip.vfr : false
                            text: qsTr("⚠ VFR")
                            color: "#e0a030"
                            font.pixelSize: 11
                            font.bold: true
                            ToolTip.visible: vfrHoverArea.containsMouse
                            ToolTip.delay: 300
                            ToolTip.text: qsTr("Variable frame rate — overlay sync can drift over the clip's length.")
                            MouseArea {
                                id: vfrHoverArea
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }
                        }
                    }

                    Label { text: qsTr("Codec:"); color: palette.placeholderText }
                    Label {
                        text: root.videoClip && root.videoClip.codec !== ""
                              ? root.videoClip.codec : "—"
                    }

                    Label { text: qsTr("Pixel format:"); color: palette.placeholderText }
                    Label {
                        text: root.videoClip && root.videoClip.pixFmt !== ""
                              ? qsTr("%1 (%2-bit)").arg(root.videoClip.pixFmt)
                                                   .arg(root.videoClip.bitDepth)
                              : "—"
                    }

                    Label { text: qsTr("Colour space:"); color: palette.placeholderText }
                    Label { text: root.videoClip ? root.tagOrUntagged(root.videoClip.colorSpace) : "—" }

                    Label { text: qsTr("Transfer:"); color: palette.placeholderText }
                    Label { text: root.videoClip ? root.tagOrUntagged(root.videoClip.colorTransfer) : "—" }

                    Label { text: qsTr("Primaries:"); color: palette.placeholderText }
                    Label { text: root.videoClip ? root.tagOrUntagged(root.videoClip.colorPrimaries) : "—" }

                    Label { text: qsTr("Range:"); color: palette.placeholderText }
                    Label { text: root.videoClip ? root.tagOrUntagged(root.videoClip.colorRange) : "—" }

                    Label { text: qsTr("Created:"); color: palette.placeholderText }
                    Label {
                        text: root.videoClip && root.videoClip.creationTimeSecs >= 0
                              ? Qt.formatDateTime(root.videoClip.creationTimeUtc, Locale.ShortFormat)
                              : qsTr("none")
                    }

                    Label { text: qsTr("Timecode:"); color: palette.placeholderText }
                    Label {
                        text: root.videoClip
                              ? root.formatTimecode(root.videoClip.embeddedTimecode)
                              : qsTr("none")
                    }
                }

                Label {
                    visible: root.hdrSource
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: "#e0a030"
                    font.pixelSize: 11
                    text: qsTr("10-bit/HDR source — exported as 8-bit SDR in v0.4")
                }

                Label {
                    visible: root.videoClip
                             ? root.videoClip.probeState === VideoClip.Failed : false
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: "#e05050"
                    font.pixelSize: 11
                    text: root.videoClip && root.videoClip.probeError !== ""
                          ? root.videoClip.probeError
                          : qsTr("Could not read video metadata")
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    CheckBox {
                        id: enabledCheck
                        text: qsTr("Enabled")
                        onToggled: {
                            if (root.videoClip)
                                root.videoClip.enabled = checked
                        }
                    }
                    // Clicking a CheckBox writes `checked` imperatively,
                    // which would kill a plain binding — and the strip's
                    // context menu can toggle this same clip. A Binding
                    // element re-asserts on every model change.
                    Binding {
                        target: enabledCheck
                        property: "checked"
                        value: root.videoClip ? root.videoClip.enabled : false
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: qsTr("Remove")
                        onClicked: clipList.removeClip(clipList.activeIndex)
                    }
                }
            }
        }

        // ── Sync: workflow moved out of VideoSyncPlayer (PR A5) ──────────
        CollapsibleSection {
            title: qsTr("Sync")
            Layout.fillWidth: true
            expanded: false
            settingsKey: "video_sync"

            SyncSection {
                Layout.fillWidth: true
                videoClip: root.videoClip
                dive: root.dive
                videoOffset: root.videoOffset
                videoPositionSeconds: root.videoPositionSeconds
                onSyncOffsetComputed: function(offset) {
                    root.syncOffsetComputed(offset)
                }
                onProfileApplied: function(profileName, clipCount) {
                    root.profileApplied(profileName, clipCount)
                }
            }
        }

        // ── Colour: track C grading controls (PR C2) ─────────────────────
        CollapsibleSection {
            title: qsTr("Colour")
            Layout.fillWidth: true
            expanded: true
            settingsKey: "video_colour"

            ColorPanel {
                Layout.fillWidth: true
            }
        }

        Item { Layout.fillHeight: true }
    }
}
