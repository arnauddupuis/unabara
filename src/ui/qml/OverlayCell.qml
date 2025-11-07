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
    // Width: use calculated size or default (should be set by parent based on section width)
    width: cellCalculatedSize.width > 0 ? cellCalculatedSize.width : 100
    // Height: auto-size to fit content with padding
    height: cellText.implicitHeight + 8
    color: "transparent"
    border.color: selected ? "lime" : "transparent"
    border.width: selected ? 3 : 0

    // Cell content
    Text {
        id: cellText
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 4
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

    // Selection handler
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor

        onClicked: {
            cellRoot.clicked()
        }

        // Drag behavior will be added in Phase 3
        // For now, just handle selection
    }

    // Visual feedback on hover
    states: [
        State {
            name: "hovered"
            when: mouseArea.containsMouse && !selected
            PropertyChanges {
                target: cellRoot
                border.color: "#40FFFFFF"  // Subtle white border on hover
                border.width: 2
            }
        }
    ]

    transitions: Transition {
        PropertyAnimation {
            properties: "border.color,border.width"
            duration: 150
        }
    }

    // MouseArea for hover detection
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        propagateComposedEvents: true

        onClicked: function(mouse) {
            cellRoot.clicked()
            mouse.accepted = true
        }
    }
}
