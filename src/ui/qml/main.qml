import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Unabara.UI 1.0
import Unabara.Generators 1.0
import Unabara.Export 1.0

ApplicationWindow {
    id: window
    width: 1200
    height: 800
    visible: true
    title: qsTr("Unabara - Dive Telemetry Overlay")
    
    // Models and objects
    OverlayGenerator {
        id: overlayGenerator
        templatePath: "qrc:/resources/templates/default_overlay.png"
        showDepth: true
        showTemperature: true
        showTime: true
        showNDL: true
        showPressure: true
    }
    
    ImageExporter {
        id: imageExporter
        
        onExportStarted: {
            exportProgressDialog.open()
        }
        
        onProgressChanged: {
            exportProgressDialog.value = progress
        }
        
        onExportFinished: function(success, path) {
            exportProgressDialog.close()
            if (success) {
                messageDialog.title = qsTr("Export Completed")
                messageDialog.message = qsTr("Images exported successfully to:\n") + path
                messageDialog.open()
            }
        }
        
        onExportError: function(errorMessage) {
            exportProgressDialog.close()
            messageDialog.title = qsTr("Export Error")
            messageDialog.message = errorMessage
            messageDialog.open()
        }
    }
    
    // Main UI layout
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            ToolButton {
                text: qsTr("Import")
                icon.name: "document-open"
                onClicked: importMenu.open()
                
                Menu {
                    id: importMenu
                    MenuItem {
                        text: qsTr("Import Dive Log")
                        onTriggered: importDiveLogFileDialog.open()
                    }
                    MenuItem {
                        text: qsTr("Import Video")
                        enabled: mainWindow.hasActiveDive
                        onTriggered: importVideoFileDialog.open()
                    }
                }
            }
            
            ToolButton {
                text: qsTr("Export")
                icon.name: "document-save"
                enabled: mainWindow.hasActiveDive
                onClicked: {
                    exportImagesDialog.open()
                }
            }
            
            ToolButton {
                text: qsTr("Settings")
                icon.name: "configure"
                onClicked: settingsDialog.open()
            }
            
            Item { Layout.fillWidth: true }
            
            Label {
                text: mainWindow.hasActiveDive ? mainWindow.currentDive.diveName : qsTr("No dive loaded")
                elide: Text.ElideRight
                horizontalAlignment: Qt.AlignHCenter
                Layout.fillWidth: true
            }
            
            Item { Layout.fillWidth: true }
        }
    }
    
    // Split view with timeline at bottom, main area at top
    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical
        
        // Main content area - will contain the overlay preview
        Item {
            id: mainContentArea
            SplitView.fillHeight: true
            SplitView.minimumHeight: 300
            
            // Overlay preview (placeholder)
            Rectangle {
                id: overlayPreview
                anchors.centerIn: parent
                width: parent.width * 0.8
                height: parent.height * 0.8
                color: "black"
                visible: mainWindow.hasActiveDive
                
                Image {
                    id: previewImage
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectFit
                    cache: false
                    source: "image://overlay/preview/" + Math.random()
                    
                    // This would be updated when the timeline position changes
                    property var updatePreview: function() {
                        if (mainWindow.hasActiveDive && timelineView.visible) {
                            source = "image://overlay/preview/" + Math.random()
                        }
                    }
                    
                    Component.onCompleted: {
                        // Initial preview
                        updatePreview()
                    }
                }
            }
            
            // Placeholder when no dive is loaded
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.6
                height: parent.height * 0.4
                color: "#f0f0f0"
                radius: 10
                visible: !mainWindow.hasActiveDive
                
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 20
                    
                    Label {
                        text: qsTr("No Dive Data Loaded")
                        font.pixelSize: 24
                        Layout.alignment: Qt.AlignHCenter
                    }
                    
                    Button {
                        text: qsTr("Import Dive Log")
                        Layout.alignment: Qt.AlignHCenter
                        onClicked: importDiveLogFileDialog.open()
                    }
                }
            }
        }
        
        // Timeline area
        Rectangle {
            id: timelineArea
            SplitView.preferredHeight: 200
            SplitView.minimumHeight: 150
            color: "#e0e0e0"
            
            TimelineView {
                id: timelineView
                anchors.fill: parent
                visible: mainWindow.hasActiveDive
                
                onCurrentTimeChanged: {
                    if (previewImage.status === Image.Ready) {
                        previewImage.updatePreview()
                    }
                }
            }
            
            // Placeholder when no dive is loaded
            Label {
                anchors.centerIn: parent
                text: qsTr("Import a dive log to view timeline data")
                visible: !mainWindow.hasActiveDive
                font.pixelSize: 16
                color: "#606060"
            }
        }
    }
    
    // Dialogs
    FileDialog {
        id: importDiveLogFileDialog
        title: qsTr("Import Dive Log")
        nameFilters: ["Subsurface XML files (*.xml *.ssrf)", "All files (*)"]
        onAccepted: {
            console.log("Selected file path:", selectedFile.toString());
            // Use our C++ helper to convert the URL to a local file path
            let filePath = mainWindow.urlToLocalFile(selectedFile.toString());
            console.log("Converted file path:", filePath);
            
            if (logParser.importFile(filePath)) {
                // File import will be handled through the LogParser::diveImported signal
                // which is connected to MainWindow::onDiveImported
            } else {
                messageDialog.title = qsTr("Import Error");
                messageDialog.message = logParser.lastError;
                messageDialog.open();
            }
        }
    }
    
    FileDialog {
        id: importVideoFileDialog
        title: qsTr("Import Video")
        nameFilters: ["Video files (*.mp4 *.mov *.avi)", "All files (*)"]
        onAccepted: {
            let filePath = mainWindow.urlToLocalFile(selectedFile.toString());
            timelineView.setVideoPath(filePath);
        }
    }
    
    Dialog {
        id: exportImagesDialog
        title: qsTr("Export Images")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 400
        height: 300
        
        onAccepted: {
            let path = imageExporter.createDefaultExportDir(mainWindow.currentDive)
            if (path) {
                imageExporter.exportPath = path
                
                // Check if we should export only the visible range
                if (exportRangeOnly.checked) {
                    // Export only the visible range from the timeline
                    imageExporter.exportImageRange(
                        mainWindow.currentDive, 
                        overlayGenerator,
                        timelineView.visibleStartTime,  
                        timelineView.visibleEndTime
                    )
                } else {
                    // Export the full dive
                    imageExporter.exportImages(mainWindow.currentDive, overlayGenerator)
                }
            } else {
                messageDialog.title = qsTr("Export Error")
                messageDialog.message = qsTr("Failed to create export directory")
                messageDialog.open()
            }
        }
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 20
            
            Label {
                text: qsTr("Export overlay images for the current dive?")
                Layout.fillWidth: true
            }
            
            CheckBox {
                text: qsTr("Export only visible time range")
                checked: false
                id: exportRangeOnly
            }
            
            RowLayout {
                Layout.fillWidth: true
                
                Label {
                    text: qsTr("Frame rate:")
                }
                
                SpinBox {
                    id: frameRateSpinBox
                    value: 10
                    from: 1
                    to: 60
                    
                    onValueChanged: {
                        imageExporter.frameRate = value
                    }
                    
                    Component.onCompleted: {
                        imageExporter.frameRate = value
                    }
                }
            }
        }
    }
    
    Dialog {
        id: settingsDialog
        title: qsTr("Settings")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 400
        height: 300
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            
            GroupBox {
                title: qsTr("Overlay Settings")
                Layout.fillWidth: true
                
                ColumnLayout {
                    anchors.fill: parent
                    
                    CheckBox {
                        text: qsTr("Show Depth")
                        checked: overlayGenerator.showDepth
                        onCheckedChanged: {
                            overlayGenerator.showDepth = checked
                            previewImage.updatePreview()
                        }
                    }
                    
                    CheckBox {
                        text: qsTr("Show Temperature")
                        checked: overlayGenerator.showTemperature
                        onCheckedChanged: {
                            overlayGenerator.showTemperature = checked
                            previewImage.updatePreview()
                        }
                    }
                    
                    CheckBox {
                        text: qsTr("Show Time")
                        checked: overlayGenerator.showTime
                        onCheckedChanged: {
                            overlayGenerator.showTime = checked
                            previewImage.updatePreview()
                        }
                    }
                    
                    CheckBox {
                        text: qsTr("Show NDL")
                        checked: overlayGenerator.showNDL
                        onCheckedChanged: {
                            overlayGenerator.showNDL = checked
                            previewImage.updatePreview()
                        }
                    }
                    
                    CheckBox {
                        text: qsTr("Show Pressure")
                        checked: overlayGenerator.showPressure
                        onCheckedChanged: {
                            overlayGenerator.showPressure = checked
                            previewImage.updatePreview()
                        }
                    }
                }
            }
        }
    }
    
    Dialog {
        id: exportProgressDialog
        title: qsTr("Exporting Images")
        modal: true
        closePolicy: Popup.NoAutoClose
        standardButtons: Dialog.Cancel
        width: 400
        height: 200
        
        property int value: 0
        
        onRejected: {
            // TODO: Implement export cancellation
        }
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 20
            
            Label {
                text: qsTr("Generating overlay images...")
                Layout.fillWidth: true
            }
            
            ProgressBar {
                value: exportProgressDialog.value / 100
                Layout.fillWidth: true
            }
        }
    }
    
    Dialog {
        id: messageDialog
        title: ""
        property string message: ""
        modal: true
        standardButtons: Dialog.Ok
        width: 400
        height: 200
        
        ColumnLayout {
            anchors.fill: parent
            
            Label {
                text: messageDialog.message
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
        }
    }
}