import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * Unified canvas for the Dive Computer Overlay tab.
 *
 * One main surface with two modes:
 *   - Edit:   the interactive cell editor (drag, snap, guides, selection)
 *   - Render: the exact C++ render of the overlay at the current dive time
 *
 * Clicking a cell in Render mode switches back to Edit with that cell
 * selected (hit-testing runs in C++ with the renderer's own font metrics,
 * so it matches what is actually on screen).
 *
 * This component also owns the cell-visibility *behavior* that used to live
 * in OverlayEditor.qml: reacting to the generator's show* flags by
 * creating/toggling cells, and re-initializing/adjusting the layout when the
 * dive changes. That wiring must exist exactly once — here.
 */
Item {
    id: root

    property var generator: null
    property var dive: null
    property var timeline: null
    property var cellModel: null
    // True while the Dive Computer Overlay tab is the active tab. Gates
    // refresh work so video playback (which moves currentTime) doesn't
    // re-render this canvas while it is hidden.
    property bool tabActive: false

    // 0 = Edit, 1 = Render
    readonly property alias mode: modeTabs.currentIndex

    // Emitted by the no-dive placeholder's Import button.
    signal importRequested()

    // --- Refresh plumbing -------------------------------------------------

    function updateCellModel() {
        if (root.generator && root.dive && root.timeline && root.cellModel) {
            root.cellModel.updateFromGenerator(root.generator, root.dive,
                                               root.timeline.currentTime)
        }
    }

    // The C++ render is only produced while the Render page is actually
    // visible; otherwise we just mark it dirty and regenerate on switch.
    property bool renderDirty: true

    function refreshRender() {
        if (root.mode === 1 && root.tabActive && root.dive) {
            renderTimer.restart()
        } else {
            root.renderDirty = true
        }
    }

    function refreshAll() {
        updateCellModel()
        refreshRender()
    }

    onModeChanged: {
        if (mode === 1 && renderDirty)
            renderTimer.restart()
    }
    onTabActiveChanged: {
        if (tabActive && mode === 1 && renderDirty)
            renderTimer.restart()
    }

    // Short debounce so bursts of property changes cost one render.
    Timer {
        id: renderTimer
        interval: 100
        repeat: false
        onTriggered: {
            root.renderDirty = false
            // Two-step refresh forces the image provider to be re-queried.
            renderImage.source = ""
            Qt.callLater(function() {
                renderImage.source = "image://overlay/preview/" + Date.now()
            })
        }
    }

    // --- Behavior: dive / generator / timeline reactions ------------------

    Connections {
        target: root
        function onGeneratorChanged() { root.refreshAll() }
        function onTimelineChanged() { root.refreshAll() }
        function onDiveChanged() {
            // Only initialize the default layout when no cells exist yet
            // (don't wipe a loaded template on dive import).
            if (root.generator && root.dive && root.generator.cellCount() === 0) {
                root.generator.initializeDefaultCellLayout(root.dive)
            }
            // Hide tank pressure cells that exceed the dive's actual tank count
            if (root.generator && root.dive) {
                root.generator.adjustTankCellVisibility(root.dive)
            }
            root.refreshAll()
        }
    }

    Connections {
        target: root.timeline
        enabled: root.timeline !== null
        function onCurrentTimeChanged() {
            // currentTime also moves during video playback on the Video
            // Preview tab — skip the work while this tab is hidden.
            if (root.tabActive)
                root.refreshAll()
        }
    }

    Connections {
        target: config
        enabled: config !== null
        function onUnitSystemChanged() { root.refreshAll() }
    }

    Connections {
        target: root.generator
        enabled: root.generator !== null

        function onCellsChanged() { root.refreshAll() }
        function onCellLayoutChanged() { root.refreshAll() }
        function onTemplateChanged() { root.refreshAll() }
        function onFontChanged() { root.refreshAll() }
        function onLabelColorChanged() { root.refreshAll() }
        function onValueColorChanged() { root.refreshAll() }
        function onShowLabelChanged() { root.refreshAll() }
        function onShadowChanged() { root.refreshAll() }
        function onBackgroundOpacityChanged() { root.refreshRender() }

        // Toggle cell visibility without destroying the layout.
        // setCellTypeVisible creates a default cell when the current
        // template has none for that data type.
        function onShowDepthChanged() {
            root.generator.setCellTypeVisible("depth", root.generator.showDepth)
            root.refreshAll()
        }
        function onShowTemperatureChanged() {
            root.generator.setCellTypeVisible("temperature", root.generator.showTemperature)
            root.refreshAll()
        }
        function onShowNDLChanged() {
            root.generator.setCellTypeVisible("ndl", root.generator.showNDL)
            root.refreshAll()
        }
        function onShowPressureChanged() {
            root.generator.setPressureCellsVisible(root.generator.showPressure, root.dive)
            root.refreshAll()
        }
        function onShowTimeChanged() {
            root.generator.setCellTypeVisible("time", root.generator.showTime)
            root.refreshAll()
        }
        function onShowCNSChanged() {
            root.generator.setCellTypeVisible("cns", root.generator.showCNS)
            root.refreshAll()
        }
        function onShowMeanDepthChanged() {
            root.generator.setCellTypeVisible("mean_depth", root.generator.showMeanDepth)
            root.refreshAll()
        }
        function onShowMaxDepthChanged() {
            root.generator.setCellTypeVisible("max_depth", root.generator.showMaxDepth)
            root.refreshAll()
        }
        function onShowGasChanged() {
            root.generator.setCellTypeVisible("gas", root.generator.showGas)
            root.refreshAll()
        }
        function onShowTTSChanged() {
            root.generator.setCellTypeVisible("tts", root.generator.showTTS)
            root.refreshAll()
        }
        function onShowStopDepthChanged() {
            root.generator.setCellTypeVisible("stop_depth", root.generator.showStopDepth)
            root.refreshAll()
        }
        function onShowStopTimeChanged() {
            root.generator.setCellTypeVisible("stop_time", root.generator.showStopTime)
            root.refreshAll()
        }
        function onShowPO2Cell1Changed() {
            root.generator.setCellTypeVisible("po2_cell1", root.generator.showPO2Cell1)
            root.refreshAll()
        }
        function onShowPO2Cell2Changed() {
            root.generator.setCellTypeVisible("po2_cell2", root.generator.showPO2Cell2)
            root.refreshAll()
        }
        function onShowPO2Cell3Changed() {
            root.generator.setCellTypeVisible("po2_cell3", root.generator.showPO2Cell3)
            root.refreshAll()
        }
        function onShowCompositePO2Changed() {
            root.generator.setCellTypeVisible("composite_po2", root.generator.showCompositePO2)
            root.refreshAll()
        }
    }

    Component.onCompleted: {
        Qt.callLater(root.refreshAll)
    }

    // --- Layout -----------------------------------------------------------

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Mode strip: Edit/Render toggle + Edit-mode canvas tools
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: stripRow.implicitHeight + 8
            color: palette.window

            RowLayout {
                id: stripRow
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 8

                TabBar {
                    id: modeTabs
                    Layout.preferredWidth: implicitWidth

                    TabButton {
                        text: qsTr("Edit")
                        width: implicitWidth + 16
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: qsTr("Interactive editor: drag cells to position them")
                    }
                    TabButton {
                        text: qsTr("Render")
                        width: implicitWidth + 16
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: qsTr("Exact render, as it will be exported. Click a cell to edit it.")
                    }
                }

                ToolSeparator {
                    visible: root.mode === 0
                }

                CheckBox {
                    visible: root.mode === 0
                    enabled: root.dive !== null
                    text: qsTr("Snap to grid")
                    checked: root.generator ? root.generator.snapToGrid : false
                    onToggled: {
                        if (root.generator)
                            root.generator.snapToGrid = checked
                    }
                }

                SpinBox {
                    visible: root.mode === 0
                    enabled: root.dive !== null
                             && root.generator && root.generator.snapToGrid
                    from: 5
                    to: 100
                    stepSize: 5
                    value: root.generator ? root.generator.gridSpacing : 10
                    onValueModified: {
                        if (root.generator)
                            root.generator.gridSpacing = value
                    }
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("Grid spacing (px)")
                }

                CheckBox {
                    visible: root.mode === 0
                    enabled: root.dive !== null
                    text: qsTr("Show grid")
                    checked: root.generator ? root.generator.showGrid : false
                    onToggled: {
                        if (root.generator)
                            root.generator.showGrid = checked
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        // Canvas area
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            StackLayout {
                anchors.fill: parent
                visible: root.dive !== null
                currentIndex: root.mode

                // Edit page: interactive cell editor
                InteractiveOverlayPreview {
                    id: interactivePreview
                    generator: root.generator
                    dive: root.dive
                    timePoint: root.timeline ? root.timeline.currentTime : 0.0
                    cellModel: root.cellModel

                    // Sync preview selection when the generator selection
                    // changes externally (Deselect button, Render-mode click).
                    Connections {
                        target: root.generator
                        enabled: root.generator !== null
                        function onSelectedCellIdChanged() {
                            interactivePreview.selectedCellId = root.generator.selectedCellId
                        }
                    }

                    onCellSelected: function(cellId) {
                        if (root.generator)
                            root.generator.selectedCellId = cellId
                    }

                    onCellDeselected: {
                        if (root.generator)
                            root.generator.selectedCellId = ""
                    }

                    onCellPositionChanged: function(cellId, newPosition) {
                        if (root.generator) {
                            root.generator.setCellPosition(cellId, newPosition)
                            root.updateCellModel()
                        }
                    }
                }

                // Render page: the C++ generator's exact output
                Rectangle {
                    color: palette.dark

                    Image {
                        id: renderImage
                        anchors.fill: parent
                        anchors.margins: 10
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        asynchronous: true
                    }

                    // Click a cell to jump back to Edit mode with it selected
                    // (clicking empty space just switches, clearing selection).
                    MouseArea {
                        anchors.fill: renderImage
                        enabled: root.mode === 1 && root.dive !== null

                        onClicked: function(mouse) {
                            var pw = renderImage.paintedWidth
                            var ph = renderImage.paintedHeight
                            var cellId = ""
                            if (pw > 0 && ph > 0 && root.generator && root.timeline) {
                                var nx = (mouse.x - (renderImage.width - pw) / 2) / pw
                                var ny = (mouse.y - (renderImage.height - ph) / 2) / ph
                                if (nx >= 0 && nx <= 1 && ny >= 0 && ny <= 1) {
                                    cellId = root.generator.cellIdAt(
                                                root.dive, root.timeline.currentTime,
                                                Qt.point(nx, ny))
                                }
                            }
                            modeTabs.currentIndex = 0
                            if (root.generator)
                                root.generator.selectedCellId = cellId
                        }
                    }
                }
            }

            // Placeholder when no dive is loaded
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.6
                height: Math.min(parent.height * 0.5, placeholderColumn.implicitHeight + 60)
                color: palette.window
                radius: 10
                visible: root.dive === null

                ColumnLayout {
                    id: placeholderColumn
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
                        onClicked: root.importRequested()
                    }
                }
            }
        }
    }
}
