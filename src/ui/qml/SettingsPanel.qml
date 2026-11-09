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

    // Asks main.qml to open the What's New dialog with the full changelog.
    signal showWhatsNew()
    // The export destination is picked with main.qml's single FolderDialog
    // (the export dialog's Destination group uses the same one) — two dialogs
    // for one setting drift apart.
    signal chooseExportDirectory()

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

                    // A click assigns `checked` imperatively, which would
                    // break a plain `checked:` binding; the explicit Binding
                    // keeps the radios tracking config if anything else
                    // ever changes the unit system.
                    RadioButton {
                        id: metricRadio
                        text: qsTr("Metric (m, °C, bar)")
                        Binding {
                            target: metricRadio
                            property: "checked"
                            value: config ? config.unitSystem === Units.Metric : true
                            restoreMode: Binding.RestoreNone
                        }
                        onClicked: {
                            if (config)
                                config.unitSystem = Units.Metric
                        }
                    }

                    RadioButton {
                        id: imperialRadio
                        text: qsTr("Imperial (ft, °F, psi)")
                        Binding {
                            target: imperialRadio
                            property: "checked"
                            value: config ? config.unitSystem === Units.Imperial : false
                            restoreMode: Binding.RestoreNone
                        }
                        onClicked: {
                            if (config)
                                config.unitSystem = Units.Imperial
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: palette.mid }

                    CheckBox {
                        text: qsTr("Check for updates at startup")
                        checked: config ? config.checkUpdatesOnStartup : true
                        onToggled: {
                            if (config)
                                config.checkUpdatesOnStartup = checked
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Label {
                            text: qsTr("Release notes for this and past versions.")
                            opacity: 0.7
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Button {
                            text: qsTr("Show What's New...")
                            onClicked: root.showWhatsNew()
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
                        text: qsTr("Saved camera clock offsets, used to suggest the video sync offset automatically. Profiles are created from the Video tab.")
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
                            onClicked: root.chooseExportDirectory()
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

                    Label { text: qsTr("Video codec:") }
                    ComboBox {
                        id: videoCodecCombo
                        Layout.fillWidth: true
                        // Probe-driven like the export dialog's combo; the
                        // dialog narrows this per export mode, this is just
                        // the default preference.
                        model: ffmpegInfo.availableCodecs
                        currentIndex: Math.max(0, model.indexOf(config ? config.videoCodec : "vp9"))
                        onModelChanged: {
                            var idx = model.indexOf(config ? config.videoCodec : "vp9")
                            currentIndex = idx >= 0 ? idx : 0
                        }
                        onActivated: {
                            if (config)
                                config.videoCodec = currentText
                        }
                    }

                    Label { text: qsTr("Video quality:") }
                    ComboBox {
                        id: videoQualityCombo
                        Layout.fillWidth: true
                        model: [qsTr("Best"), qsTr("High"), qsTr("Standard"), qsTr("Small file")]
                        readonly property var tiers: ["best", "high", "standard", "small"]
                        currentIndex: {
                            var idx = tiers.indexOf(config ? config.videoQuality : "standard")
                            return idx >= 0 ? idx : 2
                        }
                        onActivated: {
                            if (config)
                                config.videoQuality = tiers[currentIndex]
                        }
                    }

                    Label { text: qsTr("Video frame rate:") }
                    SpinBox {
                        editable: true
                        from: 1
                        to: 60
                        value: config ? Math.round(config.videoFrameRate) : 30
                        onValueModified: {
                            if (config)
                                config.videoFrameRate = value
                        }
                    }

                    Label {
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: palette.placeholderText
                        text: qsTr("The video frame rate applies to overlay-only video exports. Composited exports always use the clip's own resolution and frame rate.")
                    }
                }
            }

            GroupBox {
                title: qsTr("FFmpeg")
                Layout.fillWidth: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    // All strings come pre-assembled from the C++ bridge:
                    // ffmpegInfo probes version/encoders on a worker thread
                    // (each tool run can block for seconds on a broken
                    // install), so QML never calls into FFmpeg itself.
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: ffmpegInfo.summaryText
                    }

                    Label {
                        visible: ffmpegInfo.available
                        text: ffmpegInfo.capabilitiesText
                        font.family: "monospace"
                        color: palette.placeholderText
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: palette.mid }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: palette.placeholderText
                        text: qsTr("Leave the override empty to use the FFmpeg found automatically. Set it to use a specific FFmpeg executable instead.")
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        TextField {
                            Layout.fillWidth: true
                            text: config ? config.ffmpegPath : ""
                            placeholderText: qsTr("Auto-detect")
                            readOnly: true
                        }

                        Button {
                            text: qsTr("Browse...")
                            onClicked: ffmpegFileDialog.open()
                        }

                        Button {
                            text: qsTr("Clear")
                            enabled: config !== null && config.ffmpegPath !== ""
                            onClicked: {
                                if (config)
                                    config.ffmpegPath = ""
                            }
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: 8 }
        }
    }

    FileDialog {
        id: ffmpegFileDialog
        title: qsTr("Select FFmpeg Executable")
        onAccepted: {
            if (config) {
                // The Config setter persists eagerly; FfmpegInfo pushes the
                // override into FFmpegProbe and re-probes asynchronously.
                config.ffmpegPath = mainWindow.urlToLocalFile(selectedFile.toString())
            }
        }
    }

    FolderDialog {
        id: templateDirDialog
        title: qsTr("Select Template Directory")
        currentFolder: config ? mainWindow.localFileToUrl(config.templateDirectory) : ""
        onAccepted: {
            if (config) {
                // OverlayEditor listens to templateDirectoryChanged and
                // refreshes the generator's template list + its ComboBox.
                config.templateDirectory = mainWindow.urlToLocalFile(selectedFolder.toString())
            }
        }
    }
}
