import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Unabara.UI 1.0
import Unabara.Generators 1.0
import Unabara.Export 1.0
import QtMultimedia

ApplicationWindow {
    id: window
    width: 1200
    height: 800
    visible: true
    title: qsTr("Unabara - Dive Telemetry Overlay")
    
    // Models and objects 
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
    
    // Video exporter
    VideoExporter {
        id: videoExporter
        
        onExportStarted: {
            videoExportProgressDialog.open()
        }
        
        onProgressChanged: {
            videoExportProgressDialog.value = progress
        }
        
        onStatusUpdate: function(message) {
            videoExportProgressDialog.statusText = message
        }
        
        onExportFinished: function(success, path) {
            videoExportProgressDialog.close()
            if (success) {
                messageDialog.title = qsTr("Export Completed")
                messageDialog.message = qsTr("Video exported successfully to:\n") + path
                messageDialog.open()
            }
        }
        
        onExportError: function(errorMessage) {
            videoExportProgressDialog.close()
            messageDialog.title = qsTr("Export Error")
            messageDialog.message = errorMessage
            messageDialog.open()
        }
    }

    Connections {
        target: videoExporter
        function onProgressChanged() {
            // Debug progress updates
            console.log("Progress update from C++:", videoExporter.progress);
            
            // Set timer to ensure UI updates
            videoExportProgressDialog.value = videoExporter.progress;
            
            // Force immediate refresh
            if (videoExportProgressDialog.visible) {
                progressBarContainer.Layout.preferredHeight += 0.001;
                progressBarContainer.Layout.preferredHeight -= 0.001;
            }
        }
    }

    Timer {
        id: progressUpdateTimer
        interval: 250 // Check every 250ms
        repeat: true
        running: videoExportProgressDialog.visible
        
        onTriggered: {
            if (videoExportProgressDialog.visible) {
                // Force update from latest value
                videoExportProgressDialog.value = videoExporter.progress;
                console.log("Timer checking progress:", videoExporter.progress, "%");
            }
        }
    }
    
    // Hidden MediaPlayer to get video metadata
    MediaPlayer {
        id: metadataPlayer
        source: ""
        
        // onSourceChanged: function() {
        //     if (source != "") {
        //         console.log("Loading video metadata from:", source)
        //     }
        // }
        
        // onPlaybackStateChanged: function() {
        //     if (playbackState === MediaPlayer.PlayingState) {
        //         // Immediately pause once it starts playing
        //         pause()
        //     }
        // }
        
        // onDurationChanged: function() {
        //     if (duration > 0 && hasVideo) {
        //         var durationInSeconds = duration/1000;
        //         console.log("Video metadata loaded: Duration =", durationInSeconds, "seconds")
        //         timelineView.setVideoDuration(durationInSeconds) // Convert from milliseconds to seconds
                
        //         // Default to positioning video at the beginning of the dive
        //         timelineView.timeline.videoOffset = 0.0
        //     }
        // }

        // Add media-specific properties
        videoOutput: VideoOutput { visible: false } // Create an invisible VideoOutput
        
        onSourceChanged: function() {
            if (source != "") {
                console.log("Loading video metadata from:", source)
                // Force the player to load the metadata
                metadataLoaded = false
                metadataPlayer.play()
            }
        }
        
        // Add property to track metadata loading
        property bool metadataLoaded: false
        
        onPlaybackStateChanged: function() {
            console.log("Metadata player state changed:", playbackState)
            if (playbackState === MediaPlayer.PlayingState) {
                // Immediately pause once it starts playing
                pause()
            }
        }
        
        onDurationChanged: function() {
            console.log("Duration changed:", duration)
            if (duration > 0) {
                metadataLoaded = true
                var durationInSeconds = duration/1000;
                console.log("Video metadata loaded: Duration =", durationInSeconds, "seconds")
                
                // Get video resolution
                console.log("Video resolution:", metadataPlayer.metaData.resolution)
                
                timelineView.setVideoDuration(durationInSeconds) // Convert from milliseconds to seconds
                // Default to positioning video at the beginning of the dive
                timelineView.timeline.videoOffset = 0.0
            }
        }
        
        onErrorStringChanged: function() {
            if (errorString) {
                console.error("MediaPlayer error:", errorString)
            }
        }
        
        onMetaDataChanged: function() {
            console.log("Metadata received:", JSON.stringify(metaData))
            
            // Access specific metadata properties
            if (metaData && metaData.resolution) {
                console.log("Video resolution from metadata:", metaData.resolution)
            }
        }
    }
    
    Component.onCompleted: {
        // Check if FFmpeg is available and show a notification if not
        if (!videoExporter.isFFmpegAvailable()) {
            messageDialog.title = qsTr("FFmpeg Not Found")
            messageDialog.message = qsTr("FFmpeg was not found on your system. The video export feature will be disabled.\n\n" +
                               "To enable video export, please install FFmpeg and restart the application.")
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
                    asynchronous: true
                    
                    // Add a timer to handle the update with proper delay
                    Timer {
                        id: updateTimer
                        interval: 100  // Short delay to ensure property changes are processed
                        repeat: false
                        onTriggered: {
                            // Force complete source refresh with two-step approach
                            previewImage.source = ""
                            Qt.callLater(function() {
                                previewImage.source = "image://overlay/preview/" + Date.now() // Use current time for unique URL
                                console.log("Preview source updated: " + previewImage.source)
                            })
                        }
                    }
                    
                    // This would be updated when the timeline position changes or settings change
                    property var updatePreview: function() {
                        if (mainWindow.hasActiveDive && timelineView.visible) {
                            // Use timer to delay update slightly
                            updateTimer.restart()
                        }
                    }
                    
                    Component.onCompleted: {
                        // Set initial source with a short delay
                        Qt.callLater(function() {
                            source = "image://overlay/preview/" + Date.now()
                        })
                    }
                    
                    // Monitor changes to overlay generator properties
                    Connections {
                        target: overlayGenerator
                        
                        function onShowDepthChanged() { previewImage.updatePreview() }
                        function onShowTemperatureChanged() { previewImage.updatePreview() }
                        function onShowTimeChanged() { previewImage.updatePreview() }
                        function onShowNDLChanged() { previewImage.updatePreview() }
                        function onShowPressureChanged() { previewImage.updatePreview() }
                        function onTemplateChanged() { previewImage.updatePreview() }
                        function onFontChanged() { previewImage.updatePreview() }
                        function onTextColorChanged() { previewImage.updatePreview() }
                    }
                    
                    // Add status changes monitoring
                    onStatusChanged: {
                        if (status === Image.Error) {
                            console.error("Error loading preview image")
                        } else if (status === Image.Ready) {
                            console.log("Preview image loaded successfully")
                        }
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
                
                // Function to set video duration
                function setVideoDuration(duration) {
                    timeline.videoDuration = duration
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
            console.log("Importing video from:", filePath);
            
            // First set the video path in timeline
            timelineView.setVideoPath(filePath);
            
            // Setup a fallback timer in case metadata loading fails
            let metadataTimer = Qt.createQmlObject(
                'import QtQuick; Timer { interval: 3000; repeat: false; }',
                importVideoFileDialog
            );
            
            metadataTimer.triggered.connect(function() {
                // If we get here, metadata loading probably failed
                console.log("Metadata loading timed out, checking duration");
                
                // If duration hasn't been set, use a default
                if (timelineView.timeline.videoDuration <= 0) {
                    console.log("Setting default video duration");
                    // Get the actual duration from metadata player if possible
                    if (metadataPlayer.duration > 0) {
                        timelineView.setVideoDuration(metadataPlayer.duration / 1000);
                    } else {
                        console.log("Using fallback 60 second duration");
                        timelineView.setVideoDuration(60); // Default to 1 minute
                    }
                    timelineView.timeline.videoOffset = 0.0; // Position at beginning of dive
                } else {
                    console.log("Duration already set to:", timelineView.timeline.videoDuration);
                }
            });
            
            // Then use MediaPlayer to determine video duration
            metadataPlayer.source = selectedFile;
            // metadataPlayer.play();  // Start playback to initialize metadata
            metadataTimer.start();
            
            // Show message about successful import
            messageDialog.title = qsTr("Video Imported");
            messageDialog.message = qsTr("Video imported successfully. You can now adjust its position on the timeline by dragging the orange rectangle.");
            messageDialog.open();
        }
    }
    
    Dialog {
        id: exportImagesDialog
        title: qsTr("Export Overlay")
        modal: true
        width: 500

        // Use implicitHeight instead of fixed height to adapt to content
        implicitHeight: contentColumn.implicitHeight + 140 // Add padding for dialog margins
        
        // Move standard buttons to the footer
        footer: DialogButtonBox {
            standardButtons: Dialog.Ok | Dialog.Cancel
            // Define a function to handle video export
            function handleExport() {
                console.log("Export dialog accepted - this should only print once");
                if (exportTypeImages.checked) {
                    // Images export mode
                    let path = imageExporter.createDefaultExportDir(mainWindow.currentDive);
                    if (path) {
                        imageExporter.exportPath = path;
                        
                        // Check if we have a video and should export only the video portion
                        if (exportVideoRangeOnly.checked && timelineView.videoPath !== "") {
                            // Export only the video range - use the methods
                            let videoStart = timelineView.timeline.getVideoStartTime();
                            let videoEnd = timelineView.timeline.getVideoEndTime();
                            
                            console.log("Exporting video range from", videoStart, "to", videoEnd);
                            
                            imageExporter.exportImageRange(
                                mainWindow.currentDive, 
                                overlayGenerator,
                                videoStart,  
                                videoEnd
                            );
                        }
                        // Check if we should export only the visible range
                        else if (exportRangeOnly.checked) {
                            // Export only the visible range from the timeline
                            imageExporter.exportImageRange(
                                mainWindow.currentDive, 
                                overlayGenerator,
                                timelineView.visibleStartTime,  
                                timelineView.visibleEndTime
                            );
                        } else {
                            // Export the full dive
                            imageExporter.exportImages(mainWindow.currentDive, overlayGenerator);
                        }
                    } else {
                        messageDialog.title = qsTr("Export Error");
                        messageDialog.message = qsTr("Failed to create export directory");
                        messageDialog.open();
                    }
                } else {
                    // Video export mode
                    let outputFile = videoExporter.createDefaultExportFile(mainWindow.currentDive);
                    if (outputFile) {
                        // Just pass the file path directly, don't manipulate it further
                        videoExporter.frameRate = videoFrameRateSpinBox.value;
                        videoExporter.videoBitrate = bitrateSlider.value;
                        videoExporter.videoCodec = codecComboBox.currentText;
                        
                        // Handle resolution matching
                        if (matchVideoResolutionCheckbox.checked && timelineView.videoPath !== "") {
                            // Detect and use the video's resolution
                            let videoRes = videoExporter.detectVideoResolution(timelineView.videoPath);
                            if (videoRes.width > 0 && videoRes.height > 0) {
                                videoExporter.customResolution = videoRes;
                            }
                        } else {
                            // Use default resolution from the template
                            videoExporter.customResolution = Qt.size(0, 0);
                        }
                        
                        // Determine the time range to export
                        let startTime, endTime;
                        
                        if (exportVideoRangeOnly.checked && timelineView.videoPath !== "") {
                            // Export the imported video's time range
                            startTime = timelineView.timeline.getVideoStartTime();
                            endTime = timelineView.timeline.getVideoEndTime();
                        } else if (exportRangeOnly.checked) {
                            // Export the visible range from the timeline
                            startTime = timelineView.visibleStartTime;
                            endTime = timelineView.visibleEndTime;
                        } else {
                            // Export the full dive
                            startTime = 0;
                            endTime = mainWindow.currentDive.durationSeconds;
                        }
                        
                        console.log("Exporting video from", startTime, "to", endTime);
                        videoExporter.exportVideo(mainWindow.currentDive, overlayGenerator, startTime, endTime);
                    } else {
                        messageDialog.title = qsTr("Export Error");
                        messageDialog.message = qsTr("Failed to create export file");
                        messageDialog.open();
                    }
                }
            }
            onAccepted: {
                handleExport();
                exportImagesDialog.accept();
            }
            onRejected: exportImagesDialog.reject()
            padding: 10
        }
        
        // Update height when export type changes
        onImplicitHeightChanged: {
            // Ensure dialog is properly sized after content changes
            height = implicitHeight
        }
        
        // Add connections to resize the dialog when export type changes
        Connections {
            target: exportTypeImages
            function onCheckedChanged() {
                // Force dialog height update after a short delay
                resizeTimer.start()
            }
        }
        
        Connections {
            target: exportTypeVideo
            function onCheckedChanged() {
                // Force dialog height update after a short delay
                resizeTimer.start()
            }
        }
        
        // Timer to handle resize after animation completes
        Timer {
            id: resizeTimer
            interval: 10
            onTriggered: {
                exportImagesDialog.height = contentColumn.implicitHeight + 140
            }
        }
        
        ColumnLayout {
            id: contentColumn
            anchors.fill: parent
            anchors.margins: 20
            spacing: 20
            
            // Export type selection
            GroupBox {
                title: qsTr("Export Format")
                Layout.fillWidth: true
                
                ColumnLayout {
                    anchors.fill: parent
                    
                    RadioButton {
                        id: exportTypeImages
                        text: qsTr("Export as Image Sequence")
                        checked: true
                        ToolTip.text: qsTr("Export individual PNG images with transparency")
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                    }
                    
                    RadioButton {
                        id: exportTypeVideo
                        text: qsTr("Export as Video File")
                        enabled: videoExporter.isFFmpegAvailable()
                        ToolTip.text: videoExporter.isFFmpegAvailable() ? 
                            qsTr("Export a single video file with the overlay") : 
                            qsTr("FFmpeg not found - install FFmpeg to enable video export")
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                    }
                }
            }
            
            // Common export range options
            GroupBox {
                title: qsTr("Export Range")
                Layout.fillWidth: true
                
                ColumnLayout {
                    anchors.fill: parent
                    
                    RadioButton {
                        id: exportFullDive
                        text: qsTr("Export full dive")
                        checked: true
                    }
                    
                    RadioButton {
                        text: qsTr("Export only visible time range")
                        id: exportRangeOnly
                        enabled: !exportVideoRangeOnly.checked
                    }
                    
                    RadioButton {
                        text: qsTr("Export only video time range")
                        id: exportVideoRangeOnly
                        enabled: timelineView.videoPath !== "" && timelineView.timeline.videoDuration > 0
                        
                        onCheckedChanged: {
                            if (checked) {
                                exportRangeOnly.checked = false;
                            }
                        }
                    }
                }
            }
            
            // Image-specific options (visible when exporting images)
            GroupBox {
                title: qsTr("Image Export Settings")
                Layout.fillWidth: true
                visible: exportTypeImages.checked
                
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
                            if (exportTypeImages.checked) {
                                imageExporter.frameRate = value
                            }
                        }
                        
                        Component.onCompleted: {
                            imageExporter.frameRate = value
                        }
                    }
                }
            }
            
            // Video-specific options (visible when exporting video)
            GroupBox {
                title: qsTr("Video Export Settings")
                Layout.fillWidth: true
                visible: exportTypeVideo.checked
                enabled: exportTypeVideo.checked
                
                // Set a minimum height to ensure proper layout calculations
                Layout.minimumHeight: videoSettingsColumn.implicitHeight + 30
                
                ColumnLayout {
                    id: videoSettingsColumn
                    anchors.fill: parent
                    spacing: 8 // Reduce spacing for a more compact layout
                    
                    RowLayout {
                        Layout.fillWidth: true
                        
                        Label {
                            text: qsTr("Frame rate:")
                            Layout.preferredWidth: 100
                        }
                        
                        SpinBox {
                            id: videoFrameRateSpinBox
                            value: 30
                            from: 1
                            to: 60
                            Layout.fillWidth: true
                        }
                    }
                    
                    RowLayout {
                        Layout.fillWidth: true
                        
                        Label {
                            text: qsTr("Codec:")
                            Layout.preferredWidth: 100
                        }
                        
                        ComboBox {
                            id: codecComboBox
                            model: videoExporter.getAvailableCodecs()
                            Layout.fillWidth: true
                            
                            ToolTip {
                                visible: codecComboBox.hovered
                                text: {
                                    if (codecComboBox.currentText === "h264") 
                                        return qsTr("H.264 - most compatible format")
                                    else if (codecComboBox.currentText === "prores") 
                                        return qsTr("ProRes - high quality with alpha channel")
                                    else if (codecComboBox.currentText === "vp9") 
                                        return qsTr("VP9 - good for web with alpha support")
                                    else if (codecComboBox.currentText === "hevc") 
                                        return qsTr("HEVC - better compression, newer hardware")
                                    else 
                                        return ""
                                }
                                delay: 500
                            }
                        }
                    }
                    
                    RowLayout {
                        Layout.fillWidth: true
                        
                        Label {
                            text: qsTr("Quality:")
                            Layout.preferredWidth: 100
                        }
                        
                        Slider {
                            id: bitrateSlider
                            from: 1000  // 1 Mbps
                            to: 20000   // 20 Mbps
                            value: 8000 // 8 Mbps default
                            stepSize: 1000
                            Layout.fillWidth: true
                        }
                        
                        Label {
                            text: (bitrateSlider.value / 1000).toFixed(1) + " Mbps"
                            Layout.preferredWidth: 70
                        }
                    }
                    
                    // Resolution options
                    CheckBox {
                        id: matchVideoResolutionCheckbox
                        text: qsTr("Match video resolution")
                        enabled: timelineView.videoPath !== ""
                        checked: false
                        Layout.fillWidth: true
                        
                        ToolTip {
                            visible: parent.hovered
                            text: timelineView.videoPath !== "" ?
                                qsTr("Output video will match the resolution of the imported video") :
                                qsTr("Import a video first to enable this option")
                            delay: 500
                        }
                    }
                    
                    Label {
                        text: {
                            // Basic file size estimation
                            if (mainWindow.hasActiveDive) {
                                let rangeStart, rangeEnd;
                                
                                if (exportVideoRangeOnly.checked && timelineView.timeline.videoDuration > 0) {
                                    rangeStart = timelineView.timeline.getVideoStartTime();
                                    rangeEnd = timelineView.timeline.getVideoEndTime();
                                } else if (exportRangeOnly.checked) {
                                    rangeStart = timelineView.visibleStartTime;
                                    rangeEnd = timelineView.visibleEndTime;
                                } else {
                                    rangeStart = 0;
                                    rangeEnd = mainWindow.currentDive.durationSeconds;
                                }
                                
                                let durationSec = rangeEnd - rangeStart;
                                let bitrateMbps = bitrateSlider.value / 1000;
                                let sizeInMB = (durationSec * bitrateMbps) / 8;
                                
                                return qsTr("Estimated file size: ") + sizeInMB.toFixed(1) + " MB";
                            } else {
                                return "";
                            }
                        }
                        Layout.alignment: Qt.AlignRight
                        Layout.fillWidth: true // Ensure it has space to display
                        elide: Text.ElideRight // Allow text to be elided if too long
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

        // Ensure settings are applied when dialog is accepted
        onAccepted: {
            // Explicitly apply all settings to the generator
            overlayGenerator.showDepth = showDepthCheckbox.checked
            overlayGenerator.showTemperature = showTempCheckbox.checked
            overlayGenerator.showTime = showTimeCheckbox.checked
            overlayGenerator.showNDL = showNDLCheckbox.checked
            overlayGenerator.showPressure = showPressureCheckbox.checked
            
            // Update the preview
            previewImage.updatePreview()
            
            // For debugging
            console.log("Settings applied - Depth:", overlayGenerator.showDepth, 
                        "Temp:", overlayGenerator.showTemperature,
                        "Time:", overlayGenerator.showTime,
                        "NDL:", overlayGenerator.showNDL,
                        "Pressure:", overlayGenerator.showPressure)
        }
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            
            GroupBox {
                title: qsTr("Overlay Settings")
                Layout.fillWidth: true
                
                ColumnLayout {
                    anchors.fill: parent
                    
                    CheckBox {
                        id: showDepthCheckbox
                        text: qsTr("Show Depth")
                        checked: overlayGenerator.showDepth
                        onCheckedChanged: {
                            overlayGenerator.showDepth = checked
                            previewImage.updatePreview()
                        }
                    }
                    
                    CheckBox {
                        id: showTempCheckbox
                        text: qsTr("Show Temperature")
                        checked: overlayGenerator.showTemperature
                        onCheckedChanged: {
                            overlayGenerator.showTemperature = checked
                            previewImage.updatePreview()
                        }
                    }
                    
                    CheckBox {
                        id: showTimeCheckbox
                        text: qsTr("Show Time")
                        checked: overlayGenerator.showTime
                        onCheckedChanged: {
                            overlayGenerator.showTime = checked
                            previewImage.updatePreview()
                        }
                    }
                    
                    CheckBox {
                        id: showNDLCheckbox
                        text: qsTr("Show NDL")
                        checked: overlayGenerator.showNDL
                        onCheckedChanged: {
                            overlayGenerator.showNDL = checked
                            previewImage.updatePreview()
                        }
                    }
                    
                    CheckBox {
                        id: showPressureCheckbox
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
        id: videoExportProgressDialog
        title: qsTr("Exporting Video")
        modal: true
        closePolicy: Popup.NoAutoClose
        standardButtons: Dialog.Cancel
        width: 500
        height: 250
        
        property int value: 0
        property string statusText: qsTr("Preparing...")
        
        onRejected: {
            // Try to cancel the FFmpeg process
            videoExporter.cancelExport();
        }
        
        // Add a timer to force UI updates
        Timer {
            id: uiRefreshTimer
            interval: 100
            repeat: true
            running: videoExportProgressDialog.visible
            
            onTriggered: {
                // Force layout refresh
                progressBarContainer.Layout.preferredHeight = progressBarContainer.Layout.preferredHeight
            }
        }
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 20
            
            Label {
                text: videoExportProgressDialog.statusText
                Layout.fillWidth: true
                font.bold: true
            }
            
            // Wrap the ProgressBar in a container
            Item {
                id: progressBarContainer
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                
                ProgressBar {
                    id: exportProgressBar
                    anchors.fill: parent
                    value: videoExportProgressDialog.value / 100
                    
                    // Add a pulse animation to indicate activity
                    Rectangle {
                        id: pulseAnimation
                        width: Math.min(30, exportProgressBar.width / 10) // Make width proportional but not too wide
                        height: Math.min(4, parent.height * 0.5)
                        radius: height / 2 // Rounded corners to match progress bar
                        color: Qt.rgba(1, 1, 1, 0.25) // Subtle white with low opacity
                        visible: parent.value > 0 && parent.value < 1.0
                        y: (parent.height - height) / 2
                        
                        SequentialAnimation on x {
                            loops: Animation.Infinite
                            running: pulseAnimation.visible
                            // Start from the left side
                            PropertyAnimation {
                                from: 0
                                to: exportProgressBar.width - pulseAnimation.width
                                duration: 1500
                                easing.type: Easing.InOutQuad // Smoother easing
                            }
                            // Go back to the start
                            PropertyAnimation {
                                from: exportProgressBar.width - pulseAnimation.width
                                to: 0
                                duration: 1500
                                easing.type: Easing.InOutQuad
                            }
                        }
                    }
                    
                    onValueChanged: {
                        console.log("Progress bar value updated to:", Math.round(value * 100), "%");
                    }
                }
            }
            
            Label {
                text: qsTr("Progress: %1%").arg(videoExportProgressDialog.value)
                Layout.alignment: Qt.AlignHCenter
            }
            
            Item {
                Layout.fillHeight: true
            }
            
            Label {
                text: qsTr("This process can take several minutes depending on video length and settings.\nDo not close the application during export.")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.italic: true
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