import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * Bottom-anchored, auto-dismissing toast host.
 *
 * Replaces the informational modal dialogs (export completed, video
 * imported, FFmpeg missing) that used to interrupt the workflow. Errors and
 * anything that needs a decision stay modal — this is for notifications the
 * user may safely ignore.
 *
 * Fill the window's content item with this and give it a high z. It is
 * pointer-transparent everywhere except the toast box itself.
 *
 * Usage:
 *   toast.show(qsTr("Video imported"))
 *   toast.show(qsTr("Export finished"), {
 *       duration: 8000,                      // ms, default 5000
 *       actionText: qsTr("Open folder"),
 *       onAction: function() { ... }         // dismisses after invoking
 *   })
 *
 * Toasts queue: one is shown at a time, the next appears when the current
 * one times out or is dismissed with its close button.
 */
Item {
    id: root

    property int defaultDuration: 5000

    // FIFO of pending {text, duration, actionText, onAction} entries.
    property var queue: []
    property var current: null

    function show(text, options) {
        var opts = options || {}
        queue.push({
            text: text,
            duration: opts.duration !== undefined ? opts.duration : defaultDuration,
            actionText: opts.actionText !== undefined ? opts.actionText : "",
            onAction: opts.onAction !== undefined ? opts.onAction : null
        })
        if (!current)
            advance()
    }

    function advance() {
        dismissTimer.stop()
        if (queue.length === 0) {
            current = null
            return
        }
        current = queue.shift()
        // Re-arm even when a toast replaces another: assigning `current`
        // rebinds the box contents, and the timer restarts for the new one.
        dismissTimer.interval = current.duration
        dismissTimer.restart()
    }

    function dismiss() {
        advance()
    }

    Timer {
        id: dismissTimer
        repeat: false
        onTriggered: root.advance()
    }

    Rectangle {
        id: toastBox
        visible: root.current !== null
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        width: Math.min(contentRow.implicitWidth + 24, root.width - 48)
        height: contentRow.implicitHeight + 16
        radius: 6
        color: palette.toolTipBase
        border.color: palette.mid
        border.width: 1
        opacity: visible ? 0.96 : 0

        Behavior on opacity { NumberAnimation { duration: 150 } }

        RowLayout {
            id: contentRow
            anchors.centerIn: parent
            width: toastBox.width - 24
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: root.current ? root.current.text : ""
                color: palette.toolTipText
                wrapMode: Text.WordWrap
            }

            Button {
                flat: true
                visible: root.current !== null && root.current.actionText !== ""
                text: root.current ? root.current.actionText : ""
                font.bold: true
                onClicked: {
                    var action = root.current ? root.current.onAction : null
                    root.dismiss()
                    if (action)
                        action()
                }
            }

            ToolButton {
                text: "✕"
                onClicked: root.dismiss()
            }
        }
    }
}
