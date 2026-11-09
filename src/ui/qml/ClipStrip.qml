import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Unabara.Core 1.0

// Horizontal strip of imported video clips (PR A4), shown above the video
// player on the Video tab. One card per clip in list (import)
// order, plus a trailing "+" card that opens the import dialog. Clicking a
// card activates its clip; the context menu offers rename (inline edit),
// enable/disable, remove and reveal-in-file-manager.
//
// Reads the `clipList` context property directly (same pattern as the rest
// of the UI); main.qml only wires addRequested to the import FileDialog.
Rectangle {
    id: root

    // Compact by design: the strip must not steal vertical space from the
    // player below it.
    implicitHeight: 100
    color: palette.window

    signal addRequested()

    // FFmpeg availability feeds the thumbnail URLs: when the user configures
    // FFmpeg in Settings mid-session, ffmpegInfo.available flips and every
    // card's URL changes, so Qt Quick re-requests it and the provider's
    // cached failures (keyed to the old FFmpeg path) are retried. The typeof
    // guard keeps the component instantiable without the context property.
    readonly property bool ffmpegReady:
        typeof ffmpegInfo !== "undefined" && ffmpegInfo ? ffmpegInfo.available : false

    // mm:ss, matching the timeline's and the player's time labels. Clip
    // duration is plain time, not a unit-system value, so QML formatting
    // is fine here (the Units rule covers metric/imperial values).
    function formatDuration(seconds) {
        var s = Math.max(0, Math.floor(seconds))
        var m = Math.floor(s / 60)
        var ss = s % 60
        return m + ":" + (ss < 10 ? "0" : "") + ss
    }

    function syncSourceLabel(source) {
        switch (source) {
        case VideoClip.Timecode:      return qsTr("Synced from embedded timecode")
        case VideoClip.CameraProfile: return qsTr("Synced from camera profile")
        case VideoClip.Manual:        return qsTr("Synced manually")
        case VideoClip.Restored:      return qsTr("Sync restored from last session")
        default:                      return qsTr("Not synced")
        }
    }

    ListView {
        id: clipListView
        anchors.fill: parent
        anchors.margins: 6
        orientation: ListView.Horizontal
        spacing: 6
        clip: true
        model: clipList

        // The scrollbar gets its own lane below the cards once they
        // overflow: attached to the ListView it paints over the bottom
        // edge, which used to sit right on the cards' badge row. Cards and
        // the "+" card shrink by the lane height instead of the strip
        // growing, so adding the clip that starts the overflow doesn't make
        // the player below jump.
        ScrollBar.horizontal: ScrollBar { id: hScrollBar }
        readonly property real scrollLane:
            contentWidth > width ? hScrollBar.implicitHeight + 2 : 0

        // Mouse-wheel scrolling (follow-up D): a horizontal-only Flickable
        // ignores the vertical wheel, so map it onto contentX ourselves.
        // orientation: Vertical means this handler (while inactive) only
        // accepts events with a vertical component; pure horizontal wheel/
        // tilt events keep going to the ListView's own (animated) wheel
        // scrolling. Once a vertical event activates the handler it grabs
        // horizontal events too for ~100 ms (Qt's activeTimeout), but onWheel
        // scrolls them by the same dominant-axis rule in the direction the
        // ListView would, so the user sees no difference. For a mixed
        // trackpad event the dominant axis wins. Wheel events don't touch
        // press/click delivery, so the cards' MouseAreas keep working.
        WheelHandler {
            target: null
            orientation: Qt.Vertical
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: function(event) {
                var view = clipListView
                var dx, dy
                if (event.pixelDelta.x !== 0 || event.pixelDelta.y !== 0) {
                    // Trackpads report precise pixel deltas.
                    dx = event.pixelDelta.x
                    dy = event.pixelDelta.y
                } else {
                    // Classic wheel: 120 units per notch → 60 px per notch.
                    dx = event.angleDelta.x / 2
                    dy = event.angleDelta.y / 2
                }
                var delta = Math.abs(dy) >= Math.abs(dx) ? dy : dx
                var maxX = view.originX + Math.max(0, view.contentWidth - view.width)
                if (delta === 0 || view.contentWidth <= view.width)
                    return
                view.cancelFlick()
                // Wheel up / swipe left → toward the start of the list.
                view.contentX = Math.max(view.originX,
                                         Math.min(maxX, view.contentX - delta))
            }
        }

        // The "+" card scrolls with the cards so it always sits after the
        // last clip.
        footer: Item {
            width: 64
            height: clipListView.height - clipListView.scrollLane

            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: clipListView.count > 0 ? 6 : 0
                radius: 4
                color: addCardArea.containsMouse ? palette.midlight : palette.base
                border.color: palette.mid
                border.width: 1

                Label {
                    anchors.centerIn: parent
                    text: "+"
                    font.pixelSize: 28
                    color: palette.windowText
                }

                MouseArea {
                    id: addCardArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.addRequested()
                }

                ToolTip.visible: addCardArea.containsMouse
                ToolTip.delay: 500
                ToolTip.text: qsTr("Import videos")
            }
        }

        delegate: Item {
            id: card

            // Role names only, through `model`: the "enabled" and "active"
            // roles would otherwise collide with the Item properties of the
            // same names.
            required property var model
            required property int index

            // Live VideoClip for what the roles don't carry (probeError,
            // writable displayName/enabled). Row order never changes while
            // a clip exists (list order = import order), so index is stable.
            readonly property var videoClip: clipList.clipAt(index)

            // First-frame thumbnail (follow-up C), served asynchronously by
            // image://clipthumb/ (ClipThumbImageProvider). The provider
            // answers a 1x1 transparent image when FFmpeg is missing or the
            // clip can't be decoded, so "has a thumbnail" = Ready and wider
            // than 1 px; without one the card is exactly the plain card.
            // Not gated on probing: the image simply appears when ready.
            readonly property bool hasThumbnail: thumbnail.status === Image.Ready
                                                 && thumbnail.implicitWidth > 1

            width: 160
            height: clipListView.height - clipListView.scrollLane

            Rectangle {
                id: cardFrame
                anchors.fill: parent
                radius: 4
                color: palette.base
                border.color: card.model.active ? "#ff8c00" : palette.mid
                border.width: card.model.active ? 2 : 1
                // Disabled clips are dimmed, matching their grey timeline band.
                opacity: card.model.enabled ? 1.0 : 0.4

                // Inset by the border width: a Rectangle paints its border
                // UNDER its children, so a full-bleed image would hide the
                // orange active-clip border. The extra pixel keeps the
                // image's square corners off the radius-4 border arc.
                Item {
                    anchors.fill: parent
                    anchors.margins: cardFrame.border.width + 1
                    visible: card.hasThumbnail
                    clip: true

                    Image {
                        id: thumbnail
                        anchors.fill: parent
                        // Re-evaluated when ffmpegReady flips; the url
                        // carries the file's mtime, so an overwritten clip
                        // re-extracts on delegate recreation. The "?f=" query
                        // is stripped by the provider's id decoder.
                        source: {
                            if (!card.videoClip)
                                return ""
                            var u = card.videoClip.thumbnailUrl()
                            return u === "" ? ""
                                            : u + "?f=" + (root.ffmpegReady ? "1" : "0")
                        }
                        asynchronous: true
                        fillMode: Image.PreserveAspectCrop
                        smooth: true
                        opacity: card.hasThumbnail ? 1.0 : 0.0
                        Behavior on opacity { NumberAnimation { duration: 200 } }
                    }

                    // Legibility veil: the theme's base colour over the frame
                    // keeps palette.text readable in light AND dark themes
                    // (a black veil would bury dark text in the light theme).
                    Rectangle {
                        anchors.fill: parent
                        color: palette.base
                        opacity: 0.6
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 2

                    Label {
                        id: nameLabel
                        Layout.fillWidth: true
                        visible: !renameField.visible
                        text: card.model.name
                        elide: Text.ElideMiddle
                        font.bold: card.model.active
                        color: palette.text
                    }

                    // Inline rename editor, opened from the context menu.
                    TextField {
                        id: renameField
                        Layout.fillWidth: true
                        visible: false
                        onAccepted: commit()
                        onEditingFinished: commit()
                        Keys.onEscapePressed: visible = false

                        function startEditing() {
                            text = card.model.name
                            visible = true
                            forceActiveFocus()
                            selectAll()
                        }
                        function commit() {
                            if (!visible)
                                return
                            visible = false
                            var trimmed = text.trim()
                            if (trimmed.length > 0 && card.videoClip
                                    && trimmed !== card.model.name)
                                card.videoClip.displayName = trimmed
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        // Hidden while renaming: the TextField is ~15 px
                        // taller than the name label, and at the reduced
                        // overflow card height (scrollbar lane) the column
                        // would otherwise push the badge row off the card.
                        visible: !renameField.visible
                        text: card.model.duration > 0
                              ? root.formatDuration(card.model.duration)
                              : "--:--"
                        font.pixelSize: 11
                        // placeholderText is too faint over a busy frame.
                        color: card.hasThumbnail ? palette.text : palette.placeholderText
                    }

                    Item { Layout.fillHeight: true }

                    // Status badge row: probing spinner / failed icon /
                    // synced checkmark.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        BusyIndicator {
                            visible: card.model.probeState === VideoClip.Probing
                            running: visible
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                        }

                        Label {
                            id: failedBadge
                            visible: card.model.probeState === VideoClip.Failed
                            text: "⚠"
                            color: "#e05050"
                            font.pixelSize: 13
                            ToolTip.visible: failedBadgeArea.containsMouse
                            ToolTip.delay: 300
                            ToolTip.text: card.videoClip && card.videoClip.probeError !== ""
                                          ? card.videoClip.probeError
                                          : qsTr("Could not read video metadata")
                            MouseArea {
                                id: failedBadgeArea
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }
                        }

                        Label {
                            id: syncedBadge
                            visible: card.model.synced
                            text: "✓"
                            color: "#3cb371"
                            font.bold: true
                            font.pixelSize: 13
                            ToolTip.visible: syncedBadgeArea.containsMouse
                            ToolTip.delay: 300
                            ToolTip.text: root.syncSourceLabel(card.model.syncSource)
                            MouseArea {
                                id: syncedBadgeArea
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }
                        }

                        Label {
                            visible: !card.model.enabled
                            text: qsTr("disabled")
                            font.pixelSize: 10
                            font.italic: true
                            color: card.hasThumbnail ? palette.text : palette.placeholderText
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                // Below the badge hover areas and the rename field in z so
                // their tooltips/editing keep working.
                z: -1
                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton)
                        cardMenu.popup()
                    else
                        clipList.activeIndex = card.index
                }
            }

            Menu {
                id: cardMenu

                MenuItem {
                    text: qsTr("Rename")
                    onTriggered: renameField.startEditing()
                }
                // Not a checkable item: MenuItem toggles its own `checked`
                // on trigger, which would break a `checked:` binding to the
                // model (the imperative-write-kills-binding trap).
                MenuItem {
                    text: card.model.enabled ? qsTr("Disable") : qsTr("Enable")
                    onTriggered: {
                        if (card.videoClip)
                            card.videoClip.enabled = !card.model.enabled
                    }
                }
                MenuItem {
                    text: qsTr("Reveal in File Manager")
                    onTriggered: mainWindow.revealInFileManager(card.model.path)
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Remove")
                    onTriggered: clipList.removeClip(card.index)
                }
            }
        }

        Label {
            anchors.centerIn: parent
            visible: clipListView.count === 0
            text: qsTr("No videos imported — click + or drop video files anywhere in the window.")
            color: palette.placeholderText
            font.pixelSize: 12
        }
    }
}
