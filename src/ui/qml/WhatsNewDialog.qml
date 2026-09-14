import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * Release-notes dialog fed by whatsnew.json (via the whatsNew context
 * property). Two entry points:
 *   - startup: main.qml opens it with whatsNew.pendingReleases(...) when the
 *     app runs a newer version than the user last saw;
 *   - manual: the Settings tab opens it with whatsNew.allReleases().
 *
 * "Show me" buttons emit showMeRequested(target); main.qml owns the
 * navigation mapping (and, later, the zone tours).
 */
Dialog {
    id: root
    title: qsTr("What's New in Unabara")
    modal: true
    width: 560
    anchors.centerIn: parent
    padding: 10

    // List of release maps: { version, highlights: [{title, description,
    // showMe}], changes: [string] } — see WhatsNew::loadFromJson().
    property var releases: []

    signal showMeRequested(string target)

    implicitHeight: Math.min(680, contentColumn.implicitHeight + 120)

    standardButtons: Dialog.Close

    contentItem: ScrollView {
        id: scroll
        clip: true
        contentWidth: availableWidth
        contentHeight: contentColumn.implicitHeight

        ColumnLayout {
            id: contentColumn
            width: scroll.availableWidth
            spacing: 8

            Repeater {
                model: root.releases

                delegate: ColumnLayout {
                    id: releaseItem
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        text: qsTr("Version %1").arg(releaseItem.modelData.version)
                        font.bold: true
                        font.pointSize: 13
                    }

                    Repeater {
                        model: releaseItem.modelData.highlights

                        delegate: ColumnLayout {
                            id: highlightItem
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 2

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Label {
                                    text: highlightItem.modelData.title
                                    font.bold: true
                                    wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }

                                Button {
                                    text: qsTr("Show me")
                                    visible: highlightItem.modelData.showMe !== ""
                                    onClicked: root.showMeRequested(highlightItem.modelData.showMe)
                                }
                            }

                            Label {
                                text: highlightItem.modelData.description
                                wrapMode: Text.WordWrap
                                opacity: 0.85
                                Layout.fillWidth: true
                            }
                        }
                    }

                    Label {
                        text: qsTr("Also in this release")
                        font.bold: true
                        visible: releaseItem.modelData.changes.length > 0
                        Layout.topMargin: 6
                    }

                    Repeater {
                        model: releaseItem.modelData.changes

                        delegate: Label {
                            id: changeItem
                            required property var modelData
                            text: "•  " + changeItem.modelData
                            wrapMode: Text.WordWrap
                            opacity: 0.85
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }
    }
}
