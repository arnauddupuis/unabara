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
    
    // Reference to C++ Timeline object
    Timeline {
        id: timeline
        diveData: mainWindow.currentDive
    }
    
    function setVideoPath(path) {
        timeline.videoPath = path
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
                    }
                    
                    SpinBox {
                        id: videoOffsetSpinBox
                        from: -3600
                        to: 3600
                        value: timeline.videoOffset
                        
                        onValueChanged: {
                            if (value !== timeline.videoOffset) {
                                timeline.videoOffset = value
                            }
                        }
                        
                        Connections {
                            target: timeline
                            function onVideoOffsetChanged() {
                                videoOffsetSpinBox.value = timeline.videoOffset
                            }
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
                    anchors.fill: parent
                    
                    onClicked: function(mouseEvent) {
                        var timeRange = timeline.endTime - timeline.startTime;
                        var clickTime = timeline.startTime + (mouseEvent.x / parent.width) * timeRange;
                        timeline.currentTime = clickTime;
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