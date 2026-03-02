import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Unabara.UI 1.0

Item {
    id: root
    
    property alias currentTime: timeline.currentTime
    // Expose the timeline object and its properties
    property alias timeline: timeline
    property alias visibleStartTime: timeline.startTime
    property alias visibleEndTime: timeline.endTime
    
    // References to video properties
    property alias videoPath: timeline.videoPath
    property alias videoDuration: timeline.videoDuration
    property alias videoOffset: timeline.videoOffset
    
    // Reference to C++ Timeline object
    Timeline {
        id: timeline
        diveData: mainWindow.currentDive
    }
    
    function setVideoPath(path) {
        timeline.videoPath = path
    }
    
    function setVideoDuration(duration) {
        timeline.videoDuration = duration
    }
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // Toolbar for timeline controls
        Rectangle {
            Layout.fillWidth: true
            height: 40
            color: "#d0d0d0"
            
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 5
                
                Button {
                    icon.name: "go-first"
                    ToolTip.text: qsTr("Go to Start")
                    onClicked: timeline.goToStart()
                }
                
                Button {
                    icon.name: "go-previous"
                    ToolTip.text: qsTr("Move Left")
                    onClicked: timeline.moveLeft()
                }
                
                Button {
                    icon.name: "zoom-out"
                    ToolTip.text: qsTr("Zoom Out")
                    onClicked: timeline.zoomOut()
                }
                
                Button {
                    icon.name: "zoom-original"
                    ToolTip.text: qsTr("Reset Zoom")
                    onClicked: timeline.resetZoom()
                }
                
                Button {
                    icon.name: "zoom-in"
                    ToolTip.text: qsTr("Zoom In")
                    onClicked: timeline.zoomIn()
                }
                
                Button {
                    icon.name: "go-next"
                    ToolTip.text: qsTr("Move Right")
                    onClicked: timeline.moveRight()
                }
                
                Button {
                    icon.name: "go-last"
                    ToolTip.text: qsTr("Go to End")
                    onClicked: timeline.goToEnd()
                }
                
                Item { Layout.fillWidth: true }
                
                Label {
                    text: qsTr("Time: ") + formatTime(timeline.currentTime)
                    color: "darkgrey"
                    font.bold: true
                    
                    function formatTime(seconds) {
                        let mins = Math.floor(seconds / 60)
                        let secs = Math.floor(seconds % 60)
                        return mins + ":" + (secs < 10 ? "0" : "") + secs
                    }
                }
                
                Item { Layout.fillWidth: true }
                
                // Video offset controls (visible when video is loaded)
                RowLayout {
                    spacing: 5
                    visible: timeline.videoPath !== ""
                    
                    Label {
                        text: qsTr("Video Offset:")
                        color: "black"
                    }
                    
                    SpinBox {
                        id: videoOffsetSpinBox
                        from: -18000
                        to: 18000
                        
                        Component.onCompleted: {
                            // Set initial value without creating a binding
                            value = timeline.videoOffset
                        }
                        
                        onValueChanged: {
                            // Only update if the change originated from UI
                            if (!updatingFromTimeline && Math.abs(value - timeline.videoOffset) > 0.01) {
                                timeline.videoOffset = value
                            }
                        }
                        
                        // Flag to prevent binding loops
                        property bool updatingFromTimeline: false
                        
                        Connections {
                            target: timeline
                            function onVideoOffsetChanged() {
                                // Set flag to prevent echoing the change back
                                videoOffsetSpinBox.updatingFromTimeline = true
                                videoOffsetSpinBox.value = Math.round(timeline.videoOffset)
                                // Use a Timer to reset the flag to avoid potential issues
                                resetTimer.start()
                            }
                        }
                        
                        Timer {
                            id: resetTimer
                            interval: 50
                            onTriggered: videoOffsetSpinBox.updatingFromTimeline = false
                        }
                    }
                    
                    Label {
                        text: qsTr("sec")
                    }
                }
            }
        }
        
        // Main timeline view
        Item {
            id: timelineView
            Layout.fillWidth: true
            Layout.fillHeight: true
            
            // Draw the depth profile
            Canvas {
                id: depthCanvas
                anchors.fill: parent
                anchors.margins: 10
                
                property var timelineData: []
                
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    
                    if (timelineData.length === 0) return;
                    
                    var width = depthCanvas.width;
                    var height = depthCanvas.height;
                    
                    // Draw background
                    ctx.fillStyle = "#f0f8ff";
                    ctx.fillRect(0, 0, width, height);
                    
                    // Draw grid lines
                    ctx.strokeStyle = "#c0c0c0";
                    ctx.lineWidth = 1;
                    
                    // Draw time grid lines
                    var timeRange = timeline.endTime - timeline.startTime;
                    var timeStep = Math.ceil(timeRange / 10); // Aim for about 10 vertical grid lines
                    
                    for (var t = Math.ceil(timeline.startTime / timeStep) * timeStep; t <= timeline.endTime; t += timeStep) {
                        var x = ((t - timeline.startTime) / timeRange) * width;
                        ctx.beginPath();
                        ctx.moveTo(x, 0);
                        ctx.lineTo(x, height);
                        ctx.stroke();
                        
                        // Draw time labels
                        ctx.fillStyle = "#404040";
                        ctx.font = "10px sans-serif";
                        var mins = Math.floor(t / 60);
                        var secs = Math.floor(t % 60);
                        var timeText = mins + ":" + (secs < 10 ? "0" : "") + secs;
                        ctx.fillText(timeText, x + 2, 10);
                    }
                    
                    // Draw depth grid lines
                    var maxDepth = timeline.maxDepth;
                    var depthStep = Math.ceil(maxDepth / 5); // Aim for about 5 horizontal grid lines
                    
                    for (var d = depthStep; d <= maxDepth; d += depthStep) {
                        var y = (d / maxDepth) * height;
                        ctx.beginPath();
                        ctx.moveTo(0, y);
                        ctx.lineTo(width, y);
                        ctx.stroke();
                        
                        // Draw depth labels
                        ctx.fillStyle = "#404040";
                        ctx.font = "10px sans-serif";
                        ctx.fillText(d + "m", 2, y - 2);
                    }
                    
                    // Draw depth profile
                    ctx.beginPath();
                    ctx.moveTo(0, 0);
                    
                    for (var i = 0; i < timelineData.length; i++) {
                        var point = timelineData[i];
                        var x = ((point.timestamp - timeline.startTime) / timeRange) * width;
                        var y = (point.depth / maxDepth) * height;
                        
                        if (i === 0) {
                            ctx.moveTo(x, y);
                        } else {
                            ctx.lineTo(x, y);
                        }
                    }
                    
                    ctx.strokeStyle = "#0066cc";
                    ctx.lineWidth = 2;
                    ctx.stroke();
                    
                    // Fill area under the curve
                    ctx.lineTo(width, height);
                    ctx.lineTo(0, height);
                    ctx.closePath();
                    ctx.fillStyle = "rgba(0, 102, 204, 0.2)";
                    ctx.fill();
                    
                    // Draw video representation if available
                    if (timeline.videoPath !== "") {
                        // Calculate video start position based on offset
                        var videoStartTime = timeline.videoOffset;
                        
                        // Use actual duration if available, otherwise use a default duration
                        var videoDuration = (timeline.videoDuration > 0) ? 
                                            timeline.videoDuration : 
                                            Math.min(300, (timeline.endTime - timeline.startTime) / 2); // Default to 5 minutes or half the visible range
                        
                        var videoEndTime = videoStartTime + videoDuration;
                        
                        // Check if video is visible in current view
                        if (videoEndTime >= timeline.startTime && videoStartTime <= timeline.endTime) {
                            // Calculate visible portion coordinates
                            var startX = Math.max(0, ((videoStartTime - timeline.startTime) / timeRange) * width);
                            var endX = Math.min(width, ((videoEndTime - timeline.startTime) / timeRange) * width);
                            var videoWidth = endX - startX;
                            
                            // Draw video rectangle - semi-transparent orange with rounded corners
                            ctx.beginPath();
                            var cornerRadius = 5;
                            
                            // Draw rounded rectangle
                            ctx.moveTo(startX + cornerRadius, 0);
                            ctx.lineTo(endX - cornerRadius, 0);
                            ctx.quadraticCurveTo(endX, 0, endX, cornerRadius);
                            ctx.lineTo(endX, height - cornerRadius);
                            ctx.quadraticCurveTo(endX, height, endX - cornerRadius, height);
                            ctx.lineTo(startX + cornerRadius, height);
                            ctx.quadraticCurveTo(startX, height, startX, height - cornerRadius);
                            ctx.lineTo(startX, cornerRadius);
                            ctx.quadraticCurveTo(startX, 0, startX + cornerRadius, 0);
                            
                            ctx.fillStyle = "rgba(255, 165, 0, 0.3)"; // Semi-transparent orange
                            ctx.fill();
                            
                            ctx.strokeStyle = "rgba(255, 140, 0, 0.8)"; // Darker orange for border
                            ctx.lineWidth = 2;
                            ctx.stroke();
                            
                            // Draw video label
                            ctx.fillStyle = "rgba(255, 140, 0, 0.9)";
                            ctx.font = "bold 12px sans-serif";
                            
                            // Format duration as MM:SS if available
                            var videoLabel = "Video";
                            
                            if (timeline.videoDuration > 0) {
                                var durationMins = Math.floor(timeline.videoDuration / 60);
                                var durationSecs = Math.floor(timeline.videoDuration % 60);
                                var durationText = durationMins + ":" + (durationSecs < 10 ? "0" : "") + durationSecs;
                                videoLabel += " (" + durationText + ")";
                            }
                            
                            // Check if there's enough space to draw the text
                            if (videoWidth > 80) {
                                ctx.fillText(videoLabel, startX + 10, 20);
                            }
                        }
                    }
                    
                    // Draw current time indicator
                    var currentX = ((timeline.currentTime - timeline.startTime) / timeRange) * width;
                    ctx.beginPath();
                    ctx.moveTo(currentX, 0);
                    ctx.lineTo(currentX, height);
                    ctx.strokeStyle = "#ff0000";
                    ctx.lineWidth = 2;
                    ctx.stroke();
                }
                
                // Update the data and redraw when timeline changes
                Connections {
                    target: timeline
                    function onViewRangeChanged() {
                        depthCanvas.updateTimelineData()
                    }
                    function onCurrentTimeChanged() {
                        depthCanvas.requestPaint()
                    }
                    function onDiveDataChanged() {
                        depthCanvas.updateTimelineData()
                    }
                    function onVideoPathChanged() {
                        depthCanvas.requestPaint()
                    }
                    function onVideoDurationChanged() {
                        depthCanvas.requestPaint()
                    }
                    function onVideoOffsetChanged() {
                        depthCanvas.requestPaint()
                    }
                }
                
                function updateTimelineData() {
                    var numPoints = Math.max(100, depthCanvas.width);
                    timelineData = timeline.getTimelineData(numPoints);
                    requestPaint();
                }
                
                Component.onCompleted: {
                    updateTimelineData()
                }
                
                // Handle mouse interactions
                MouseArea {
                    id: canvasMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    
                    property bool isDraggingVideo: false
                    property double dragStartX: 0
                    property double initialVideoOffset: 0
                    property int dragThreshold: 3
                    
                    onClicked: function(mouseEvent) {
                        if (!isDraggingVideo) {
                            var timeRange = timeline.endTime - timeline.startTime;
                            var clickTime = timeline.startTime + (mouseEvent.x / parent.width) * timeRange;
                            timeline.currentTime = clickTime;
                        }
                    }
                    
                    onPressed: function(mouseEvent) {
                        var timeRange = timeline.endTime - timeline.startTime;
                        var width = depthCanvas.width;
                        
                        // Check if click is on the video rectangle
                        if (timeline.videoPath !== "") {
                            var videoStartTime = timeline.videoOffset;
                            // Use actual duration if available, otherwise use a default
                            var videoDuration = (timeline.videoDuration > 0) ? 
                                                timeline.videoDuration : 
                                                Math.min(300, (timeline.endTime - timeline.startTime) / 2);
                            
                            var videoEndTime = videoStartTime + videoDuration;
                            
                            var startX = ((videoStartTime - timeline.startTime) / timeRange) * width;
                            var endX = ((videoEndTime - timeline.startTime) / timeRange) * width;
                            
                            // If mouse is over the video rectangle, start dragging
                            if (mouseEvent.x >= startX && mouseEvent.x <= endX) {
                                isDraggingVideo = true;
                                dragStartX = mouseEvent.x;
                                initialVideoOffset = timeline.videoOffset;
                                
                                // Change cursor to indicate draggable
                                cursorShape = Qt.SizeHorCursor;
                            }
                        }
                    }
                    
                    onReleased: function(mouseEvent) {
                        isDraggingVideo = false;
                        cursorShape = Qt.ArrowCursor;
                    }
                    
                    onPositionChanged: function(mouseEvent) {
                        if (isDraggingVideo) {
                            var timeRange = timeline.endTime - timeline.startTime;
                            var dx = mouseEvent.x - dragStartX;
                            
                            // Convert pixel movement to time offset
                            var timeOffset = (dx / depthCanvas.width) * timeRange;
                            
                            // Update video offset with new position
                            timeline.videoOffset = initialVideoOffset + timeOffset;
                        } else {
                            // Update cursor when hovering over video
                            if (timeline.videoPath !== "") {
                                var timeRange = timeline.endTime - timeline.startTime;
                                var width = depthCanvas.width;
                                
                                var videoStartTime = timeline.videoOffset;
                                // Use actual duration if available, otherwise use a default
                                var videoDuration = (timeline.videoDuration > 0) ? 
                                                    timeline.videoDuration : 
                                                    Math.min(300, (timeline.endTime - timeline.startTime) / 2);
                                
                                var videoEndTime = videoStartTime + videoDuration;
                                
                                var startX = ((videoStartTime - timeline.startTime) / timeRange) * width;
                                var endX = ((videoEndTime - timeline.startTime) / timeRange) * width;
                                
                                if (mouseEvent.x >= startX && mouseEvent.x <= endX) {
                                    cursorShape = Qt.PointingHandCursor;
                                } else {
                                    cursorShape = Qt.ArrowCursor;
                                }
                            }
                        }
                    }
                    
                    onExited: {
                        if (!isDraggingVideo) {
                            cursorShape = Qt.ArrowCursor;
                        }
                    }
                    
                    onWheel: function(wheelEvent) {
                        if (wheelEvent.angleDelta.y > 0) {
                            timeline.zoomIn()
                        } else {
                            timeline.zoomOut()
                        }
                    }
                }
            }
            // Data panel to show details at current time
            Rectangle {
                id: dataPanel
                anchors {
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                }
                height: 30
                color: "#000000cc"
                
                Label {
                    id: detailsText
                    anchors.fill: parent
                    anchors.margins: 5
                    color: "#ffffff"
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    
                    // Define the function first
                    function updateDetailsText() {
                        var dataPoint = timeline.getCurrentDataPoint();
                        if (!dataPoint) return;
                        
                        var infoText = "";
                        
                        // Add depth
                        infoText += "Depth: " + dataPoint.depth.toFixed(1) + "m  ";
                        
                        // Add temperature if available
                        if (dataPoint.temperature > 0) {
                            infoText += "Temp: " + dataPoint.temperature.toFixed(1) + "°C  ";
                        }
                        
                        // Add NDL or TTS based on decompression status
                        if (dataPoint.ndl <= 0) {
                            infoText += "TTS: " + Math.round(dataPoint.tts) + "min (DECO)  ";
                        } else {
                            infoText += "NDL: " + Math.round(dataPoint.ndl) + "min  ";
                        }
                        
                        // Add tank pressures
                        var tankCount = dataPoint.tankCount;
                        if (tankCount > 0) {
                            // Check if pressures array exists
                            if (dataPoint.pressures) {
                                // Use the pressures array
                                for (var i = 0; i < tankCount; i++) {
                                    if (i < dataPoint.pressures.length) {
                                        var pressure = dataPoint.pressures[i];
                                        infoText += "Tank " + (i+1) + ": " + Math.round(pressure) + "bar  ";
                                    }
                                }
                            } else {
                                // Fallback: use getPressure method or individual pressure properties
                                for (var i = 0; i < tankCount; i++) {
                                    // Try different ways to get the pressure value
                                    var pressure;
                                    if (typeof dataPoint.getPressure === 'function') {
                                        pressure = dataPoint.getPressure(i);
                                    } else if (i === 0 && dataPoint.pressure !== undefined) {
                                        pressure = dataPoint.pressure;
                                    } else {
                                        // Try using indexed properties
                                        var propName = "pressure" + i;
                                        pressure = dataPoint[propName];
                                    }
                                    
                                    if (pressure !== undefined) {
                                        infoText += "Tank " + (i+1) + ": " + Math.round(pressure) + "bar  ";
                                    }
                                }
                            }
                        }
                        
                        detailsText.text = infoText;
                    }
                    
                    // Then set up the connections to call it
                    Connections {
                        target: timeline
                        function onCurrentTimeChanged() {
                            detailsText.updateDetailsText();  // Use detailsText. prefix
                        }
                        function onDiveDataChanged() {
                            detailsText.updateDetailsText();  // Use detailsText. prefix
                        }
                    }
                    
                    Component.onCompleted: {
                        updateDetailsText();  // This should work since it's in the same scope
                    }
                }
            }
        }
    }
}