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

    // Optional persistence: give the section a stable settingsKey (e.g.
    // "overlay_template", no '/') and its expand/collapse state is restored
    // from Config on creation and saved on every toggle. The declared
    // `expanded` value acts as the first-run default.
    property string settingsKey: ""

    default property alias contentData: contentColumn.data

    spacing: 0

    // Persistence must stay off until the restore has run: applying the
    // instantiating document's initial `expanded:` value fires
    // onExpandedChanged during object creation (with settingsKey already
    // set), which would overwrite the stored state with the default.
    property bool _stateRestored: false

    Component.onCompleted: {
        if (settingsKey !== "")
            expanded = config.sectionExpanded(settingsKey, expanded)
        _stateRestored = true
    }

    onExpandedChanged: {
        if (_stateRestored && settingsKey !== "")
            config.setSectionExpanded(settingsKey, expanded)
    }

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
        // Zero-height clipped items keep their children in the tab-focus
        // chain — without this, keyboard navigation walks blindly through
        // the controls of every collapsed section. Height > 0 keeps the
        // content visible while the collapse animation is still running.
        visible: section.expanded || height > 0

        Behavior on height {
            // Off until the persisted state has been applied: a stored
            // state that differs from the declared default would otherwise
            // play a visible slide on every launch.
            enabled: section._stateRestored
            NumberAnimation { duration: 150; easing.type: Easing.InOutQuad }
        }

        ColumnLayout {
            id: contentColumn
            y: 8
            width: contentWrapper.width
        }
    }
}
