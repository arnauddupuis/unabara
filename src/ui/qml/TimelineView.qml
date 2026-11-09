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

    // Palette-bound colors for canvas drawing
    property color canvasBackground: palette.base
    property color canvasGridColor: palette.mid
    property color canvasTextColor: palette.windowText

    // True while the Video tab is active with a video loaded. In this
    // mode the red cursor is driven by video playback (cursorTime = videoPos +
    // videoOffset), so clicking the timeline must NOT seek the cursor — that
    // would clobber the derived position. Dragging the zone/cursor still adjusts
    // videoOffset (the sync gesture).
    property bool videoSyncMode: false

    // Manual, session-only collapse: hides the canvas + data readout, leaving
    // the header row. Deliberately not persisted and never set
    // programmatically — the timeline is central to navigation and video
    // sync, so every session starts expanded and only the user's click on
    // the header chevron collapses it.
    property bool collapsed: false

    // Slim, icon-only header buttons that fit the 32px header row.
    component TimelineButton: ToolButton {
        padding: 4
        icon.width: 16
        icon.height: 16
        ToolTip.visible: hovered
        ToolTip.delay: 500
    }

    // Reference to C++ Timeline object. Its videoPath/videoOffset/
    // videoDuration properties are a facade over clipList's active clip
    // (PR A2) — same names/signals, data lives on the VideoClip.
    Timeline {
        id: timeline
        diveData: mainWindow.currentDive
        clipModel: clipList
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        
        // Toolbar for timeline controls
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: palette.mid

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 6
                spacing: 2

                // Same chevron glyphs as CollapsibleSection so the timeline
                // reads as collapsible the way inspector sections do.
                TimelineButton {
                    text: root.collapsed ? "▸" : "▾"
                    font.bold: true
                    ToolTip.text: root.collapsed ? qsTr("Show the timeline")
                                                 : qsTr("Hide the timeline")
                    onClicked: root.collapsed = !root.collapsed
                }

                TimelineButton {
                    icon.name: "go-first"
                    ToolTip.text: qsTr("Go to Start")
                    onClicked: timeline.goToStart()
                }

                TimelineButton {
                    icon.name: "go-previous"
                    ToolTip.text: qsTr("Move Left")
                    onClicked: timeline.moveLeft()
                }

                TimelineButton {
                    icon.name: "zoom-out"
                    ToolTip.text: qsTr("Zoom Out")
                    onClicked: timeline.zoomOut()
                }

                TimelineButton {
                    icon.name: "zoom-original"
                    ToolTip.text: qsTr("Reset Zoom")
                    onClicked: timeline.resetZoom()
                }

                TimelineButton {
                    icon.name: "zoom-in"
                    ToolTip.text: qsTr("Zoom In")
                    onClicked: timeline.zoomIn()
                }

                TimelineButton {
                    icon.name: "go-next"
                    ToolTip.text: qsTr("Move Right")
                    onClicked: timeline.moveRight()
                }

                TimelineButton {
                    icon.name: "go-last"
                    ToolTip.text: qsTr("Go to End")
                    onClicked: timeline.goToEnd()
                }

                Item { Layout.fillWidth: true }

                Label {
                    text: qsTr("Time: ") + formatTime(timeline.currentTime)
                    color: palette.windowText
                    font.bold: true

                    function formatTime(seconds) {
                        let mins = Math.floor(seconds / 60)
                        let secs = Math.floor(seconds % 60)
                        return mins + ":" + (secs < 10 ? "0" : "") + secs
                    }
                }

                // Explicit sync-mode hint: the only prior signal was the
                // resize cursor over the video band, which is easy to miss.
                Label {
                    visible: root.videoSyncMode
                    text: qsTr("cursor follows video — drag the orange band to sync")
                    color: "#ff8c00"
                    font.italic: true
                    elide: Text.ElideRight
                    Layout.maximumWidth: 360
                    Layout.leftMargin: 12
                }

                Item { Layout.fillWidth: true }
                
                // Video offset controls (visible when video is loaded)
                RowLayout {
                    spacing: 5
                    visible: timeline.videoPath !== ""
                    
                    Label {
                        text: qsTr("Video Offset:")
                        color: palette.windowText
                    }
                    
                    // Whole-second view of the active clip's syncOffset (a
                    // double — timecode auto-sync yields fractions).
                    //
                    // Write contract (the old flag + 50 ms timer echo
                    // suppression lost typed input — see the follow-up plan):
                    //  - ONLY user edits write timeline.videoOffset, and a
                    //    facade write marks the clip SyncSource::Manual:
                    //    arrow clicks / unfocused wheel commit per step via
                    //    valueModified; typed text commits on Enter or
                    //    focus-out (commitText), never per keystroke. The
                    //    focus gate matters under the KDE desktop style,
                    //    whose editable field sets value AND emits
                    //    valueModified on every acceptable keystroke
                    //    ("-3" of "-3723").
                    //  - A commit writes only when the entered whole second
                    //    differs from the displayed rounding, so a bare
                    //    focus-out can't shave a stored sub-second fraction.
                    //  - Facade echoes (auto-sync, camera profile, restore,
                    //    our own writes) only re-display via showOffset():
                    //    programmatic value writes never emit valueModified,
                    //    so they can't write back. No flag, no timer.
                    SpinBox {
                        id: videoOffsetSpinBox
                        from: -18000
                        to: 18000
                        editable: true
                        Layout.maximumHeight: 28

                        // Every style's contentItem is a TextInput (the KDE
                        // style's T.TextField derives from it).
                        readonly property TextInput field: contentItem as TextInput
                        // The exact offset currently displayed; facade
                        // re-emits of an unchanged offset (refreshActiveClip
                        // fires on any clip-model change) are ignored so they
                        // can't wipe text the user is still typing.
                        property real shownOffset: 0

                        function showOffset() {
                            shownOffset = timeline.videoOffset
                            value = Math.round(shownOffset)
                            // Re-sync the text even when value didn't change
                            // (declined/invalid/no-op entry left stale text):
                            // re-installing the style's own text binding
                            // re-evaluates it.
                            if (field)
                                field.text = Qt.binding(function() {
                                    return videoOffsetSpinBox.displayText
                                })
                        }

                        function commitOffset(seconds) {
                            if (!isNaN(seconds) && seconds !== Math.round(timeline.videoOffset))
                                timeline.videoOffset = seconds
                            showOffset()
                        }

                        // No group separators ("-3 723" under Fusion in
                        // some locales); the KDE style already omits them.
                        textFromValue: function(value, locale) {
                            return Number(value).toLocaleString(locale, 'f', 0)
                                    .split(locale.groupSeparator).join("")
                        }

                        // Unparseable text ("" or a lone "-") means "keep the
                        // current offset". The default throws (or yields
                        // NaN), which SpinBox's own focus-out update turns
                        // into 0 — a lone "-" then committed a 0 offset.
                        valueFromText: function(text, locale) {
                            var v = NaN
                            try {
                                v = Number.fromLocaleString(locale, text)
                            } catch (e) {
                                v = NaN
                            }
                            return isNaN(v) ? Math.round(timeline.videoOffset) : v
                        }

                        function commitText() {
                            // Non-acceptable text only re-syncs the display.
                            var v = (field && field.acceptableInput)
                                    ? Math.max(from, Math.min(to, Math.round(valueFromText(field.text, locale))))
                                    : NaN
                            commitOffset(v)
                        }

                        Component.onCompleted: showOffset()

                        onValueModified: {
                            // While the field has focus, value moves with
                            // typing (KDE style) — defer to commitText().
                            // The up/down.pressed escape covers keyboard
                            // Up/Down and press-and-hold auto-repeat, which
                            // emit while pressed is still true; a single
                            // indicator CLICK clears pressed before emitting
                            // and is committed by the release-edge handlers
                            // below instead.
                            if (!field || !field.activeFocus || up.pressed || down.pressed)
                                commitOffset(value)
                        }

                        // Single indicator click while the text field keeps
                        // focus: Qt clears up/down.pressed BEFORE emitting
                        // valueModified on release, so the focus gate above
                        // would defer it and the preview would lag the
                        // display. Commit on the release edge, but only when
                        // the step really changed the whole second — a
                        // cancelled press (drag off the arrow) must not wipe
                        // text the user is still typing via showOffset().
                        Connections {
                            target: videoOffsetSpinBox.up
                            function onPressedChanged() {
                                var sb = videoOffsetSpinBox
                                if (!sb.up.pressed && sb.field && sb.field.activeFocus
                                        && sb.value !== Math.round(timeline.videoOffset))
                                    sb.commitOffset(sb.value)
                            }
                        }
                        Connections {
                            target: videoOffsetSpinBox.down
                            function onPressedChanged() {
                                var sb = videoOffsetSpinBox
                                if (!sb.down.pressed && sb.field && sb.field.activeFocus
                                        && sb.value !== Math.round(timeline.videoOffset))
                                    sb.commitOffset(sb.value)
                            }
                        }

                        Connections {
                            target: timeline
                            function onVideoOffsetChanged() {
                                if (timeline.videoOffset !== videoOffsetSpinBox.shownOffset)
                                    videoOffsetSpinBox.showOffset()
                            }
                            // A clip switch is never an echo: re-display
                            // unconditionally, wiping any half-typed text so
                            // it can't commit onto the NEW clip (the equal-
                            // offset case would otherwise slip past the
                            // shownOffset guard above).
                            function onActiveClipChanged() {
                                videoOffsetSpinBox.showOffset()
                            }
                        }

                        Connections {
                            target: videoOffsetSpinBox.field
                            function onEditingFinished() {
                                videoOffsetSpinBox.commitText()
                            }
                            // editingFinished needs acceptable input; this
                            // also restores the display after "" or "-".
                            function onActiveFocusChanged() {
                                if (!videoOffsetSpinBox.field.activeFocus)
                                    videoOffsetSpinBox.commitText()
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
            visible: !root.collapsed
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Draw the depth profile
            Canvas {
                id: depthCanvas
                anchors.fill: parent
                anchors.margins: 10
                
                property var timelineData: []

                // Per-clip timeline bands (clipList.bandData()), refreshed
                // off the clip model's signals. Each entry carries path,
                // displayName, start, end, enabled, active, synced,
                // probeState, plus (follow-up K) outOfDive, stacked and
                // visualStart/visualEnd — see ClipListModel::bandData().
                property var clipBands: []

                function refreshBands() {
                    clipBands = clipList.bandData()
                    requestPaint()
                }

                // Pixel-space geometry for all bands, shared by onPaint and
                // the mouse hit-testing so the clickable regions always
                // match what is drawn. A clip whose duration is still
                // unknown (probe pending or failed, player duration not in
                // yet) keeps the historical provisional length: 5 minutes
                // or half the visible range.
                //
                // Stacked bands (clip FULLY outside the dive, follow-up K)
                // are drawn at the C++-assigned slot [visualStart,
                // visualEnd] — never recomputed here, so slots can't drift
                // from what the model laid out — and are listed FIRST: the
                // inactive paint pass is ascending and the inactive hit-test
                // descending, so a real band sharing pixels with a stacked
                // one always paints on top AND wins the click.
                function bandRangesPx() {
                    var ranges = []
                    var timeRange = timeline.endTime - timeline.startTime
                    if (timeRange <= 0)
                        return ranges
                    var w = depthCanvas.width
                    for (var pass = 0; pass < 2; pass++) {
                        for (var i = 0; i < clipBands.length; i++) {
                            var b = clipBands[i]
                            if ((pass === 0) !== b.stacked)
                                continue
                            var t0, t1, provisional
                            if (b.stacked) {
                                t0 = b.visualStart
                                t1 = b.visualEnd
                                provisional = b.end - b.start <= 0
                            } else {
                                var dur = b.end - b.start
                                provisional = dur <= 0
                                if (provisional)
                                    dur = Math.min(300, timeRange / 2)
                                t0 = b.start
                                t1 = b.start + dur
                            }
                            ranges.push({
                                index: i,
                                startX: ((t0 - timeline.startTime) / timeRange) * w,
                                endX: ((t1 - timeline.startTime) / timeRange) * w,
                                // Dive time of the band's drawn left edge:
                                // the drag origin for a stacked band.
                                visualStartTime: t0,
                                active: b.active,
                                enabled: b.enabled,
                                stacked: b.stacked,
                                outOfDive: b.outOfDive,
                                name: b.displayName,
                                duration: provisional ? -1 : b.end - b.start
                            })
                        }
                    }
                    return ranges
                }

                // Rounded-rect path for a band (fill, clip and stroke share it).
                function bandPath(ctx, x0, x1, height) {
                    var cornerRadius = 5
                    ctx.beginPath()
                    ctx.moveTo(x0 + cornerRadius, 0)
                    ctx.lineTo(x1 - cornerRadius, 0)
                    ctx.quadraticCurveTo(x1, 0, x1, cornerRadius)
                    ctx.lineTo(x1, height - cornerRadius)
                    ctx.quadraticCurveTo(x1, height, x1 - cornerRadius, height)
                    ctx.lineTo(x0 + cornerRadius, height)
                    ctx.quadraticCurveTo(x0, height, x0, height - cornerRadius)
                    ctx.lineTo(x0, cornerRadius)
                    ctx.quadraticCurveTo(x0, 0, x0 + cornerRadius, 0)
                    ctx.closePath()
                }

                // One rounded clip band. Active keeps the historical orange;
                // inactive bands are the same hue dimmed; disabled is grey.
                // A STACKED band (clip fully outside the dive, drawn at a
                // placeholder slot — not a real sync) is dimmed further,
                // diagonally hatched, dash-outlined and labelled
                // "UNPLACED", so it can never pass for a synced position.
                // A band only PARTLY outside keeps its real look plus a ⚠
                // label prefix.
                function drawBand(ctx, x0, x1, height, band) {
                    var fill, stroke, labelColor, hatch
                    if (!band.enabled) {
                        fill = "rgba(128, 128, 128, 0.15)"
                        stroke = "rgba(128, 128, 128, 0.5)"
                        labelColor = "rgba(160, 160, 160, 0.8)"
                        hatch = "rgba(160, 160, 160, 0.35)"
                    } else if (band.active) {
                        fill = band.stacked ? "rgba(255, 165, 0, 0.12)"
                                            : "rgba(255, 165, 0, 0.3)"
                        stroke = "rgba(255, 140, 0, 0.8)"
                        labelColor = "rgba(255, 140, 0, 0.9)"
                        hatch = "rgba(255, 140, 0, 0.45)"
                    } else {
                        fill = band.stacked ? "rgba(255, 165, 0, 0.05)"
                                            : "rgba(255, 165, 0, 0.12)"
                        stroke = "rgba(255, 140, 0, 0.35)"
                        labelColor = "rgba(255, 140, 0, 0.5)"
                        hatch = "rgba(255, 140, 0, 0.25)"
                    }

                    depthCanvas.bandPath(ctx, x0, x1, height)
                    ctx.fillStyle = fill
                    ctx.fill()

                    if (band.stacked) {
                        // Diagonal hatching clipped to the band.
                        ctx.save()
                        depthCanvas.bandPath(ctx, x0, x1, height)
                        ctx.clip()
                        ctx.strokeStyle = hatch
                        ctx.lineWidth = 1.5
                        ctx.beginPath()
                        var step = 10
                        for (var hx = x0 - height; hx < x1; hx += step) {
                            ctx.moveTo(hx, height)
                            ctx.lineTo(hx + height, 0)
                        }
                        ctx.stroke()
                        ctx.restore()
                    }

                    depthCanvas.bandPath(ctx, x0, x1, height)
                    ctx.strokeStyle = stroke
                    ctx.lineWidth = 2
                    if (band.stacked)
                        ctx.setLineDash([6, 4])
                    ctx.stroke()
                    if (band.stacked)
                        ctx.setLineDash([])

                    if (x1 - x0 > 80) {
                        var label = band.name
                        if (band.duration >= 0) {
                            var durationMins = Math.floor(band.duration / 60)
                            var durationSecs = Math.floor(band.duration % 60)
                            label += " (" + durationMins + ":"
                                   + (durationSecs < 10 ? "0" : "") + durationSecs + ")"
                        }
                        // Marker FIRST: a stacked slot is often narrow and
                        // the label is clipped to the band.
                        if (band.stacked)
                            label = "\u26A0 " + qsTr("UNPLACED") + " · " + label
                        else if (band.outOfDive)
                            label = "\u26A0 " + label
                        ctx.fillStyle = labelColor
                        ctx.font = "bold 12px sans-serif"
                        // Clip the label to the band so a narrow stacked
                        // slot never spills its text over its neighbours.
                        ctx.save()
                        ctx.beginPath()
                        ctx.rect(x0, 0, x1 - x0 - 4, height)
                        ctx.clip()
                        ctx.fillText(label, x0 + 10, band.active ? 20 : 34)
                        ctx.restore()
                    }
                }

                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    
                    if (timelineData.length === 0) return;
                    
                    var width = depthCanvas.width;
                    var height = depthCanvas.height;
                    
                    // Draw background
                    ctx.fillStyle = root.canvasBackground;
                    ctx.fillRect(0, 0, width, height);

                    // Draw grid lines
                    ctx.strokeStyle = root.canvasGridColor;
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
                        ctx.fillStyle = root.canvasTextColor;
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
                        ctx.fillStyle = root.canvasTextColor;
                        ctx.font = "10px sans-serif";
                        ctx.fillText(d + "m", 2, y - 2);
                    }
                    
                    // Draw deco zone: filled polygons from surface (y=0) down to
                    // the ceiling at each timestamp, for each consecutive run of
                    // samples where ceiling > 0. Reads color/opacity from the
                    // shared profile config so the timeline and the profile
                    // graphic visually match.
                    if (typeof config !== "undefined" && config) {
                        var decoOpacity = config.profileDecoZoneOpacity;
                        if (decoOpacity > 0) {
                            var decoColor = config.profileDecoZoneColor;
                            // Qt.rgba, not a hand-built "rgba(...)" string: fractional
                            // channel values (any color whose channels aren't 0 or 255)
                            // make an invalid CSS string that Canvas silently rejects
                            ctx.fillStyle = Qt.rgba(decoColor.r, decoColor.g,
                                                    decoColor.b, decoOpacity);
                            var inRun = false;
                            var runStartX = 0;
                            for (var j = 0; j < timelineData.length; j++) {
                                var dp = timelineData[j];
                                var dx = ((dp.timestamp - timeline.startTime) / timeRange) * width;
                                if (dp.ceiling > 0) {
                                    if (!inRun) {
                                        ctx.beginPath();
                                        ctx.moveTo(dx, 0);
                                        runStartX = dx;
                                        inRun = true;
                                    }
                                    var dy = (dp.ceiling / maxDepth) * height;
                                    ctx.lineTo(dx, dy);
                                } else if (inRun) {
                                    // Close: walk back along y=0 to runStartX
                                    var prevDp = timelineData[j - 1];
                                    var prevX = ((prevDp.timestamp - timeline.startTime) / timeRange) * width;
                                    ctx.lineTo(prevX, 0);
                                    ctx.closePath();
                                    ctx.fill();
                                    inRun = false;
                                }
                            }
                            if (inRun) {
                                var lastDp = timelineData[timelineData.length - 1];
                                var lastX = ((lastDp.timestamp - timeline.startTime) / timeRange) * width;
                                ctx.lineTo(lastX, 0);
                                ctx.closePath();
                                ctx.fill();
                            }
                        }
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
                    
                    // Draw every clip as a band (PR A4). Inactive bands first
                    // so the active one paints on top where they overlap.
                    var bands = depthCanvas.bandRangesPx();
                    for (var pass = 0; pass < 2; pass++) {
                        for (var bi = 0; bi < bands.length; bi++) {
                            var band = bands[bi];
                            if ((pass === 1) !== band.active) continue;
                            if (band.endX < 0 || band.startX > width) continue;
                            var bx0 = Math.max(0, band.startX);
                            var bx1 = Math.min(width, band.endX);
                            depthCanvas.drawBand(ctx, bx0, bx1, height, band);
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
                }

                // Band changes all flow through the clip model: add/remove
                // and offset/enabled/duration edits emit clipsChanged, a
                // finished probe emits clipProbed, and activation changes
                // which band is highlighted. (The Timeline facade's video*
                // signals mirror the active clip, so listening there too
                // would only double the repaints.)
                Connections {
                    target: clipList
                    function onClipsChanged() { depthCanvas.refreshBands() }
                    function onClipProbed(clip) { depthCanvas.refreshBands() }
                    function onActiveIndexChanged() { depthCanvas.refreshBands() }
                }

                // Repaint when palette colors change
                Connections {
                    target: root
                    function onCanvasBackgroundChanged() { depthCanvas.requestPaint() }
                    function onCanvasGridColorChanged() { depthCanvas.requestPaint() }
                    function onCanvasTextColorChanged() { depthCanvas.requestPaint() }
                }

                // Repaint when deco zone styling changes in the profile editor.
                Connections {
                    target: typeof config !== "undefined" ? config : null
                    ignoreUnknownSignals: true
                    function onProfileDecoZoneColorChanged() { depthCanvas.requestPaint() }
                    function onProfileDecoZoneOpacityChanged() { depthCanvas.requestPaint() }
                }
                
                function updateTimelineData() {
                    var numPoints = Math.max(100, depthCanvas.width);
                    timelineData = timeline.getTimelineData(numPoints);
                    requestPaint();
                }
                
                Component.onCompleted: {
                    updateTimelineData()
                    refreshBands()
                }
                
                // Handle mouse interactions
                MouseArea {
                    id: canvasMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    
                    property bool isDraggingVideo: false
                    // Offset writes start only once the press has moved
                    // past dragThreshold: a plain click (or sub-threshold
                    // jitter) on the active band must not rewrite its sync —
                    // for a stacked band that would destroy the preserved
                    // out-of-dive offset.
                    property bool dragEngaged: false
                    property double dragStartX: 0
                    property double initialVideoOffset: 0
                    property int dragThreshold: 3
                    // Row index of the inactive band under the press;
                    // release without drag activates it (click-to-activate).
                    property int pendingActivateIndex: -1
                    property double pressX: 0
                    // Set in onReleased when the gesture was a band drag or
                    // a band activation: the trailing clicked event must
                    // then neither seek the cursor nor double-handle.
                    property bool suppressClick: false

                    onClicked: function(mouseEvent) {
                        if (suppressClick) {
                            suppressClick = false;
                            return;
                        }
                        // In video-sync mode the cursor mirrors the video position;
                        // a click must not seek it independently.
                        if (root.videoSyncMode) return;
                        var timeRange = timeline.endTime - timeline.startTime;
                        var clickTime = timeline.startTime + (mouseEvent.x / parent.width) * timeRange;
                        timeline.currentTime = clickTime;
                    }

                    onPressed: function(mouseEvent) {
                        pressX = mouseEvent.x;
                        pendingActivateIndex = -1;
                        // A release outside the area emits no clicked, so a
                        // stale flag from that gesture must not swallow the
                        // next legitimate click.
                        suppressClick = false;

                        var bands = depthCanvas.bandRangesPx();

                        // Only the ACTIVE band drags (it paints on top, so
                        // it also wins overlap hit-testing).
                        for (var i = 0; i < bands.length; i++) {
                            var b = bands[i];
                            if (b.active && mouseEvent.x >= b.startX && mouseEvent.x <= b.endX) {
                                isDraggingVideo = true;
                                dragEngaged = false;
                                dragStartX = mouseEvent.x;
                                // Dragging a STACKED band (follow-up K) is a
                                // normal sync gesture from where the band is
                                // DRAWN: the drag origin is its slot, so the
                                // written offset follows the pointer and the
                                // clip becomes a placed clip. (Starting from
                                // the real out-of-dive offset would move an
                                // invisible span and leave the band stacked.)
                                initialVideoOffset = b.stacked ? b.visualStartTime
                                                               : timeline.videoOffset;

                                // Change cursor to indicate draggable
                                cursorShape = Qt.SizeHorCursor;
                                return;
                            }
                        }

                        // An inactive band never moves on drag; a clean
                        // click on it activates its clip on release.
                        // Reverse order: overlapping inactive bands paint
                        // ascending (higher index on top), so the topmost
                        // painted band must win the click too.
                        for (var j = bands.length - 1; j >= 0; j--) {
                            var ib = bands[j];
                            if (!ib.active && mouseEvent.x >= ib.startX && mouseEvent.x <= ib.endX) {
                                pendingActivateIndex = ib.index;
                                break;
                            }
                        }
                    }

                    onReleased: function(mouseEvent) {
                        // A band drag must not fall through to the
                        // click-to-seek handler at the release position.
                        if (isDraggingVideo && Math.abs(mouseEvent.x - pressX) > dragThreshold)
                            suppressClick = true;
                        if (pendingActivateIndex >= 0) {
                            if (Math.abs(mouseEvent.x - pressX) <= dragThreshold) {
                                clipList.activeIndex = pendingActivateIndex;
                                suppressClick = true;
                            }
                            pendingActivateIndex = -1;
                        }
                        isDraggingVideo = false;
                        dragEngaged = false;
                        cursorShape = Qt.ArrowCursor;
                    }

                    onPositionChanged: function(mouseEvent) {
                        if (isDraggingVideo) {
                            var timeRange = timeline.endTime - timeline.startTime;
                            var dx = mouseEvent.x - dragStartX;
                            if (!dragEngaged) {
                                if (Math.abs(dx) <= dragThreshold)
                                    return;
                                dragEngaged = true;
                            }

                            // Convert pixel movement to time offset
                            var timeOffset = (dx / depthCanvas.width) * timeRange;

                            // Update video offset with new position (writes
                            // through the Timeline facade: active clip only,
                            // marks the clip manually synced)
                            timeline.videoOffset = initialVideoOffset + timeOffset;
                        } else {
                            // Hover cursor per band: the active band keeps
                            // the sync-mode resize hint; inactive bands show
                            // a pointing hand (click to activate).
                            var bands = depthCanvas.bandRangesPx();
                            var shape = Qt.ArrowCursor;
                            for (var i = 0; i < bands.length; i++) {
                                var b = bands[i];
                                if (mouseEvent.x < b.startX || mouseEvent.x > b.endX)
                                    continue;
                                if (b.active) {
                                    // In sync mode dragging the zone/cursor adjusts the
                                    // offset horizontally — signal that with a resize cursor.
                                    shape = root.videoSyncMode ? Qt.SizeHorCursor
                                                               : Qt.PointingHandCursor;
                                    break; // active band wins overlaps
                                }
                                shape = Qt.PointingHandCursor;
                            }
                            cursorShape = shape;
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
                    
                    function updateDetailsText() {
                        var dataPoint = timeline.getCurrentDataPoint();
                        if (!dataPoint || dataPoint.depth === undefined) return;
                        
                        var infoText = "";
                        
                        // Add depth
                        infoText += "Depth: " + dataPoint.depth.toFixed(1) + "m  ";
                        
                        // Add temperature if available
                        if (dataPoint.temperature > 0) {
                            infoText += "Temp: " + dataPoint.temperature.toFixed(1) + "°C  ";
                        }
                        
                        // Add NDL or TTS based on decompression status.
                        // ndl < 0 means the computer never reported NDL — show neither.
                        if (dataPoint.ndl === 0) {
                            infoText += "TTS: " + Math.round(dataPoint.tts) + "min (DECO)  ";
                        } else if (dataPoint.ndl > 0) {
                            infoText += "NDL: " + Math.round(dataPoint.ndl) + "min  ";
                        }

                        // Add CNS if the dive computer reported it
                        if (dataPoint.cns >= 0) {
                            infoText += "CNS: " + Math.round(dataPoint.cns) + "%  ";
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
                            detailsText.updateDetailsText();
                        }
                        function onDiveDataChanged() {
                            detailsText.updateDetailsText();
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