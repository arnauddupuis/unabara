import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Unabara.Core 1.0

/**
 * Application settings tab.
 *
 * First-class home for the persisted options that used to hide inside the
 * editors (units, template directory, color-scheme policy) plus settings
 * that previously had no UI at all (update check, camera profile
 * management, default export frame rate). Everything writes straight to the
 * Config singleton, which persists on change or at exit.
 */
Item {
    id: root

    // Reactive mirror of config.cameraPairingNames()
    property var cameraProfileNames: config ? config.cameraPairingNames() : []

    Connections {
        target: config
        enabled: config !== null
        function onCameraPairingsChanged() {
            root.cameraProfileNames = config.cameraPairingNames()
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            // Readable measure: keep the settings column from stretching
            // across the whole window on wide screens.
            width: Math.min(640, parent.width - 40)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 16

            Item { Layout.preferredHeight: 4 }

            GroupBox {
                title: qsTr("General")
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Label {
                        text: qsTr("Units")
                        font.bold: true
                    }

                    RadioButton {
                        text: qsTr("Metric (m, °C, bar)")
                        checked: config ? config.unitSystem === Units.Metric : true
                        onCheckedChanged: {
                            if (checked && config && config.unitSystem !== Units.Metric) {
                                config.unitSystem = Units.Metric
                            }
                        }
                    }

                    RadioButton {
                        text: qsTr("Imperial (ft, °F, psi)")
                        checked: config ? config.unitSystem === Units.Imperial : false
                        onCheckedChanged: {
                            if (checked && config && config.unitSystem !== Units.Imperial) {
                                config.unitSystem = Units.Imperial
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: palette.mid }

                    CheckBox {
                        text: qsTr("Check for updates at startup")
                        checked: config ? config.checkUpdatesOnStartup : true
                        onToggled: {
                            if (config)
                                config.checkUpdatesOnStartup = checked
                        }
                    }
                }
            }

            GroupBox {
                title: qsTr("Templates")
                Layout.fillWidth: true

                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 8

                    Label { text: qsTr("Template directory:") }
                    RowLayout {
                        Layout.fillWidth: true

                        TextField {
                            Layout.fillWidth: true
                            text: config ? config.templateDirectory : ""
                            readOnly: true
                        }

                        Button {
                            text: qsTr("Browse...")
                            onClicked: templateDirDialog.open()
                        }
                    }

                    Label { text: qsTr("When a template carries profile colors:") }
                    ComboBox {
                        id: colorSchemePolicyCombo
                        Layout.fillWidth: true
                        model: [qsTr("Ask"), qsTr("Always apply"), qsTr("Never apply")]
                        property var policies: ["ask", "always", "never"]
                        currentIndex: {
                            var idx = policies.indexOf(config ? config.profileColorSchemePolicy : "ask")
                            return idx >= 0 ? idx : 0
                        }
                        onActivated: {
                            if (config)
                                config.profileColorSchemePolicy = policies[currentIndex]
                        }
                    }
                }
            }

            GroupBox {
                title: qsTr("Camera Profiles")
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: palette.placeholderText
                        text: qsTr("Saved camera clock offsets, used to suggest the video sync offset automatically. Profiles are created from the Video Preview tab.")
                    }

                    Label {
                        visible: root.cameraProfileNames.length === 0
                        text: qsTr("No camera profiles saved yet.")
                        font.italic: true
                        color: palette.placeholderText
                    }

                    Repeater {
                        model: root.cameraProfileNames
                        delegate: RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Label {
                                text: modelData
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Label {
                                text: qsTr("offset constant: %1 s")
                                      .arg(config.cameraCalibrationConstant(modelData).toFixed(1))
                                color: palette.placeholderText
                                font.family: "monospace"
                            }

                            Button {
                                text: qsTr("Delete")
                                onClicked: {
                                    if (config)
                                        config.removeCameraPairing(modelData)
                                }
                            }
                        }
                    }
                }
            }

            GroupBox {
                title: qsTr("Export Defaults")
                Layout.fillWidth: true

                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 8

                    Label { text: qsTr("Export directory:") }
                    RowLayout {
                        Layout.fillWidth: true

                        TextField {
                            Layout.fillWidth: true
                            text: config ? config.lastExportPath : ""
                            readOnly: true
                        }

                        Button {
                            text: qsTr("Browse...")
                            onClicked: exportDirDialog.open()
                        }
                    }

                    Label {
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: palette.placeholderText
                        text: qsTr("Each export creates an automatically named sub-folder or file inside this directory. It can also be changed from the export dialog.")
                    }

                    Label { text: qsTr("Image sequence frame rate:") }
                    SpinBox {
                        editable: true
                        from: 1
                        to: 60
                        value: config ? Math.round(config.frameRate) : 10
                        onValueModified: {
                            if (config)
                                config.frameRate = value
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: 8 }
        }
    }

    FolderDialog {
        id: exportDirDialog
        title: qsTr("Select Export Directory")
        currentFolder: config && config.lastExportPath !== ""
                       ? "file://" + config.lastExportPath : ""
        onAccepted: {
            if (config)
                config.lastExportPath = mainWindow.urlToLocalFile(selectedFolder.toString())
        }
    }

    FolderDialog {
        id: templateDirDialog
        title: qsTr("Select Template Directory")
        onAccepted: {
            if (config) {
                // OverlayEditor listens to templateDirectoryChanged and
                // refreshes the generator's template list + its ComboBox.
                config.templateDirectory = mainWindow.urlToLocalFile(selectedFolder.toString())
            }
        }
    }
}
