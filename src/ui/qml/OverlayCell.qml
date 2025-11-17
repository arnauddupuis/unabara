import QtQuick
import QtQuick.Controls

/**
 * Individual overlay cell component
 *
 * Displays a single data cell with:
 * - Visual selection indicator (bright green border when selected)
 * - Click handling for selection
 * - Drag behavior (will be added in Phase 3)
 * - Cell content rendering
 */
Rectangle {
    id: cellRoot

    // Properties
    property string cellId: ""
    property int cellType: 0
    property point cellPosition: Qt.point(0, 0)  // Normalized position (0.0-1.0)
    property bool cellVisible: true
    property font cellFont: Qt.font({family: "Arial", pointSize: 12})
    property color cellTextColor: "white"
    property string displayText: ""
    property size cellCalculatedSize: Qt.size(100, 40)
    property bool selected: false
    property bool hasCustomFont: false
    property bool hasCustomColor: false

    // Signals
    signal clicked()
    signal positionChanged(point newPosition)

    // Visual appearance
    visible: cellVisible
    // Size based on the actual text content, not precalculated size
    width: cellText.width + 8  // Text width + margins
    height: cellText.height + 8  // Text height + margins
    color: "transparent"
    border.color: selected ? "lime" : "transparent"
    border.width: selected ? 3 : 0

    // Cell content
    Text {
        id: cellText
        x: 4
        y: 4
        text: displayText
        font: cellFont
        color: cellTextColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignTop
        wrapMode: Text.NoWrap

        // Add slight background for better visibility
        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            color: "#80000000"  // Semi-transparent black background
            radius: 4
            z: -1
        }
    }

    // Drag and selection handler
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor

        // Enable drag
        drag.target: cellRoot
        drag.axis: Drag.XAndYAxis

        onClicked: {
            cellRoot.clicked()
        }

        onReleased: {
            if (drag.active) {
                // Calculate normalized position relative to parent container
                // Parent is the cellContainer in InteractiveOverlayPreview
                var containerWidth = cellRoot.parent.width
                var containerHeight = cellRoot.parent.height

                // Clamp position to boundaries [0, container size - cell size]
                var clampedX = Math.max(0, Math.min(cellRoot.x, containerWidth - cellRoot.width))
                var clampedY = Math.max(0, Math.min(cellRoot.y, containerHeight - cellRoot.height))

                // Convert to normalized coordinates (0.0 to 1.0)
                var normalizedX = clampedX / containerWidth
                var normalizedY = clampedY / containerHeight

                // Emit position change signal
                cellRoot.positionChanged(Qt.point(normalizedX, normalizedY))

                // Snap to clamped position for visual feedback
                cellRoot.x = clampedX
                cellRoot.y = clampedY
            }
        }
    }

    // Visual feedback on hover and drag
    states: [
        State {
            name: "hovered"
            when: mouseArea.containsMouse && !selected && !mouseArea.drag.active
            PropertyChanges {
                target: cellRoot
                border.color: "#40FFFFFF"  // Subtle white border on hover
                border.width: 2
            }
        },
        State {
            name: "dragging"
            when: mouseArea.drag.active
            PropertyChanges {
                target: cellRoot
                opacity: 0.7
                border.color: "cyan"
                border.width: 3
            }
        }
    ]

    transitions: Transition {
        PropertyAnimation {
            properties: "border.color,border.width,opacity"
            duration: 150
        }
    }
}
