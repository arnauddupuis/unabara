import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * A titled inspector section with a clickable header that collapses/expands
 * its content. Children declared inside the section land in the content
 * column via the default property.
 *
 * Usage:
 *   CollapsibleSection {
 *       Layout.fillWidth: true
 *       title: qsTr("Text")
 *       GridLayout { ... }
 *   }
 */
Column {
    id: section

    property string title: ""
    property bool expanded: true

    default property alias contentData: contentColumn.data

    spacing: 0

    Rectangle {
        id: header
        width: section.width
        height: 32
        radius: 4
        color: headerMouse.containsMouse ? palette.midlight : palette.mid

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 6

            Label {
                text: section.expanded ? "▾" : "▸"  // ▾ / ▸
                font.bold: true
            }

            Label {
                text: section.title
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        MouseArea {
            id: headerMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: section.expanded = !section.expanded
        }
    }

    // Clipped wrapper so the content slides away cleanly on collapse.
    Item {
        id: contentWrapper
        width: section.width
        clip: true
        height: section.expanded ? contentColumn.implicitHeight + 8 : 0

        Behavior on height {
            NumberAnimation { duration: 150; easing.type: Easing.InOutQuad }
        }

        ColumnLayout {
            id: contentColumn
            y: 8
            width: contentWrapper.width
        }
    }
}
