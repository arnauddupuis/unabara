import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Unabara.Core 1.0
import Unabara.UI 1.0

Item {
    id: root

    property var generator
    property var timeline: null
    property var dive: null
    property bool hasSelection: root.generator ? root.generator.selectedCellId !== "" : false
    property string selectedCellId: root.generator ? root.generator.selectedCellId : ""

    // Reactive properties that update when selection or cells change
    property var currentFont: getCurrentFont()
    property var currentColor: getCurrentColor()
    property bool currentHasCustomFont: getSelectedCellHasCustomFont()
    property bool currentHasCustomColor: getSelectedCellHasCustomColor()

    implicitHeight: mainColumn.implicitHeight

    // Get cell properties from the repeater
    function getCellProperty(cellId, propertyName) {
        if (!cellId) return null

        // Access the cell from the preview's repeater
        if (interactivePreview && interactivePreview.cellRepeater) {
            for (var i = 0; i < interactivePreview.cellRepeater.count; i++) {
                var cell = interactivePreview.cellRepeater.itemAt(i)
                if (cell && cell.cellId === cellId) {
                    return cell[propertyName]
                }
            }
        }
        return null
    }

    // Get the effective font (selected cell or global)
    function getCurrentFont() {
        if (!generator) return Qt.font({family: "Arial", pointSize: 12})

        if (hasSelection && selectedCellId) {
            var cellFont = getCellProperty(selectedCellId, "cellFont")
            if (cellFont) return cellFont
        }

        // Return global font
        return generator.font
    }

    // Get the effective color (selected cell or global)
    function getCurrentColor() {
        if (!generator) return "white"

        if (hasSelection && selectedCellId) {
            var cellColor = getCellProperty(selectedCellId, "cellTextColor")
            if (cellColor) return cellColor
        }

        // Return global color
        return generator.textColor
    }

    function getSelectedCellHasCustomFont() {
        if (!hasSelection || !selectedCellId) return false

        var hasCustom = getCellProperty(selectedCellId, "hasCustomFont")
        return hasCustom === true
    }

    function getSelectedCellHasCustomColor() {
        if (!hasSelection || !selectedCellId) return false

        var hasCustom = getCellProperty(selectedCellId, "hasCustomColor")
        return hasCustom === true
    }

    // Update reactive properties when selection or cells change
    onSelectedCellIdChanged: {
        console.log("Selection changed to:", selectedCellId)
        updateCurrentProperties()
    }

    Connections {
        target: generator
        function onCellsChanged() {
            console.log(">>> onCellsChanged triggered")
            console.log("Cells changed, updating properties")
            updateCurrentProperties()
        }

        function onFontChanged() {
            if (!hasSelection) {
                console.log("Global font changed")
                updateCurrentProperties()
            }
        }

        function onTextColorChanged() {
            if (!hasSelection) {
                console.log("Global color changed")
                updateCurrentProperties()
            }
        }
    }

    function updateCurrentProperties() {
        var newFont = getCurrentFont()
        var newColor = getCurrentColor()
        console.log("Updating properties - Font:", newFont ? newFont.family : "null", "Size:", newFont ? newFont.pointSize : "null", "Color:", newColor)
        currentFont = newFont
        currentColor = newColor
        currentHasCustomFont = getSelectedCellHasCustomFont()
        currentHasCustomColor = getSelectedCellHasCustomColor()
    }

    // Cell model for interactive preview
    CellModel {
        id: cellModel
    }

    ColumnLayout {
        id: mainColumn
        width: parent.width
        spacing: 20

        // Editing mode indicator
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: root.hasSelection ? "#2a4a2a" : "#4a4a4a"
            border.color: root.hasSelection ? "lime" : "#808080"
            border.width: 2
            radius: 4

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 12

                Label {
                    text: root.hasSelection ? "✓ Cell Selected:" : "⊞ Editing All Cells"
                    font.bold: true
                    color: root.hasSelection ? "lime" : "white"
                }

                Label {
                    text: root.hasSelection ? root.selectedCellId : ""
                    font.family: "monospace"
                    color: "white"
                    visible: root.hasSelection
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: "Deselect"
                    visible: root.hasSelection
                    onClicked: {
                        if (root.generator) {
                            root.generator.selectedCellId = ""
                        }
                    }
                }
            }
        }

        // Interactive overlay preview
        Item {
            id: previewContainer
            Layout.fillWidth: true
            Layout.preferredHeight: width * 0.5625  // 16:9 aspect ratio

            function updateCellModel() {
                console.log(">>> updateCellModel called")
                if (root.generator && root.dive && root.timeline) {
                    console.log("Updating cell model with current time: ", root.timeline.currentTime)
                    cellModel.updateFromGenerator(root.generator, root.dive, root.timeline.currentTime)
                }
            }

            InteractiveOverlayPreview {
                id: interactivePreview
                anchors.fill: parent
                generator: root.generator
                dive: root.dive
                timePoint: root.timeline ? root.timeline.currentTime : 0.0
                cellModel: cellModel

                // Sync preview selection when generator selection changes externally
                // (e.g., from Deselect button or other UI)
                Connections {
                    target: root.generator
                    function onSelectedCellIdChanged() {
                        if (root.generator) {
                            interactivePreview.selectedCellId = root.generator.selectedCellId
                        }
                    }
                }

                onCellSelected: function(cellId) {
                    console.log("Cell selected:", cellId)
                    // Update generator's selected cell
                    if (root.generator) {
                        root.generator.selectedCellId = cellId
                    }
                }

                onCellDeselected: {
                    console.log("Cell deselected")
                    // Clear generator's selected cell
                    if (root.generator) {
                        root.generator.selectedCellId = ""
                    }
                }

                onCellPositionChanged: function(cellId, newPosition) {
                    console.log("Cell position changed:", cellId, "to", newPosition)
                    // Update generator with new position
                    if (root.generator) {
                        root.generator.setCellPosition(cellId, newPosition)
                        // Update cell model to reflect the change
                        previewContainer.updateCellModel()
                    }
                }
            }

            // Update cell model when properties change
            Connections {
                target: root

                function onGeneratorChanged() { previewContainer.updateCellModel() }
                function onDiveChanged() {
                    // Regenerate cells when dive changes to handle multi-tank layout
                    if (root.generator && root.dive) {
                        root.generator.initializeDefaultCellLayout(root.dive)
                    }
                    previewContainer.updateCellModel()
                }
                function onTimelineChanged() { previewContainer.updateCellModel() }
            }

            Connections {
                target: root.timeline
                enabled: root.timeline !== null

                function onCurrentTimeChanged() { previewContainer.updateCellModel() }
            }

            Connections {
                target: root.generator
                enabled: root.generator !== null

                function onCellsChanged() { previewContainer.updateCellModel() }
                function onCellLayoutChanged() { previewContainer.updateCellModel() }
                function onFontChanged() {
                    previewContainer.updateCellModel()
                }
                function onTextColorChanged() {
                    previewContainer.updateCellModel()
                }

                // Regenerate cells when display options change
                function onShowDepthChanged() {
                    if (root.dive) {
                        root.generator.initializeDefaultCellLayout(root.dive)
                    }
                }
                function onShowTemperatureChanged() {
                    if (root.dive) {
                        root.generator.initializeDefaultCellLayout(root.dive)
                    }
                }
                function onShowNDLChanged() {
                    if (root.dive) {
                        root.generator.initializeDefaultCellLayout(root.dive)
                    }
                }
                function onShowPressureChanged() {
                    if (root.dive) {
                        root.generator.initializeDefaultCellLayout(root.dive)
                    }
                }
                function onShowTimeChanged() {
                    if (root.dive) {
                        root.generator.initializeDefaultCellLayout(root.dive)
                    }
                }
                function onShowPO2Cell1Changed() {
                    if (root.dive) {
                        root.generator.initializeDefaultCellLayout(root.dive)
                    }
                }
                function onShowPO2Cell2Changed() {
                    if (root.dive) {
                        root.generator.initializeDefaultCellLayout(root.dive)
                    }
                }
                function onShowPO2Cell3Changed() {
                    if (root.dive) {
                        root.generator.initializeDefaultCellLayout(root.dive)
                    }
                }
                function onShowCompositePO2Changed() {
                    if (root.dive) {
                        root.generator.initializeDefaultCellLayout(root.dive)
                    }
                }
            }

            Connections {
                target: config
                enabled: config !== null

                function onUnitSystemChanged() { previewContainer.updateCellModel() }
            }

            Component.onCompleted: {
                Qt.callLater(previewContainer.updateCellModel)
            }
        }
        
        // Template selection
        GroupBox {
            title: qsTr("Template")
            Layout.fillWidth: true
            
            RowLayout {
                anchors.fill: parent
                
                ComboBox {
                    id: templateSelector
                    Layout.fillWidth: true
                    model: ["Default", "Default New", "Default Old"]

                    Component.onCompleted: {
                        if (generator) {
                            // Set initial selection based on generator's current template
                            var currentPath = generator.templatePath
                            if (currentPath === ":/resources/templates/default_overlay_new.png") {
                                currentIndex = 1
                            } else if (currentPath === ":/resources/templates/default_overlay_old.png") {
                                currentIndex = 2
                            } else {
                                currentIndex = 0 // Default
                            }
                        }
                    }

                    onCurrentTextChanged: {
                        var path
                        if (currentText === "Default") {
                            path = ":/default_overlay.png"
                        } else if (currentText === "Default New") {
                            path = ":/resources/templates/default_overlay_new.png"
                        } else if (currentText === "Default Old") {
                            path = ":/resources/templates/default_overlay_old.png"
                        }
                        if (generator && path) {
                            generator.templatePath = path
                        }
                    }
                }
                
                Button {
                    text: qsTr("Custom...")
                    onClicked: templateFileDialog.open()
                }
            }
        }

        // Template Save/Load
        GroupBox {
            title: qsTr("Template Management")
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                spacing: 10

                Button {
                    text: qsTr("Save Template...")
                    Layout.fillWidth: true
                    icon.name: "document-save"
                    onClicked: saveTemplateDialog.open()
                }

                Button {
                    text: qsTr("Load Template...")
                    Layout.fillWidth: true
                    icon.name: "document-open"
                    onClicked: loadTemplateDialog.open()
                }

                Button {
                    text: qsTr("Reset Layout")
                    Layout.fillWidth: true
                    icon.name: "edit-undo"
                    onClicked: {
                        if (root.generator && root.dive) {
                            root.generator.initializeDefaultCellLayout(root.dive)
                        }
                    }
                }
            }
        }

        // Grid Settings
        GroupBox {
            title: qsTr("Grid Settings")
            Layout.fillWidth: true

            GridLayout {
                anchors.fill: parent
                columns: 2

                Label { text: qsTr("Snap to Grid:") }
                CheckBox {
                    id: snapToGridCheckBox
                    checked: generator ? generator.snapToGrid : false
                    onCheckedChanged: {
                        if (generator) {
                            generator.snapToGrid = checked
                        }
                    }
                }

                Label { text: qsTr("Grid Spacing (px):") }
                SpinBox {
                    id: gridSpacingSpinBox
                    from: 5
                    to: 100
                    stepSize: 5
                    value: generator ? generator.gridSpacing : 10
                    onValueModified: {
                        if (generator) {
                            generator.gridSpacing = value
                        }
                    }
                }

                Label { text: qsTr("Show Grid:") }
                CheckBox {
                    id: showGridCheckBox
                    checked: generator ? generator.showGrid : false
                    onCheckedChanged: {
                        if (generator) {
                            generator.showGrid = checked
                        }
                    }
                }
            }
        }

        // Text settings
        GroupBox {
            title: root.hasSelection ?
                qsTr("Text Settings - Cell: ") + root.selectedCellId :
                qsTr("Text Settings - All Cells")
            Layout.fillWidth: true
            
            GridLayout {
                anchors.fill: parent
                columns: 3

                Label { text: qsTr("Font:") }
                ComboBox {
                    id: fontSelector
                    Layout.fillWidth: true
                    model: Qt.fontFamilies()

                    Component.onCompleted: {
                        updateFontSelector()
                    }

                    Connections {
                        target: root
                        function onCurrentFontChanged() {
                            fontSelector.updateFontSelector()
                        }
                    }

                    function updateFontSelector() {
                        if (root.currentFont) {
                            var index = model.indexOf(root.currentFont.family)
                            if (index !== -1) {
                                currentIndex = index
                            }
                        }
                    }

                    onActivated: {
                        if (generator) {
                            var font = root.currentFont
                            font.family = currentText
                            generator.font = font
                        }
                    }
                }

                Button {
                    text: "↺"
                    Layout.preferredWidth: 40
                    enabled: root.hasSelection && root.currentHasCustomFont
                    opacity: enabled ? 1.0 : 0.3
                    ToolTip.visible: hovered
                    ToolTip.text: enabled ? qsTr("Reset to global font") : qsTr("No custom font")
                    onClicked: {
                        if (root.generator && root.selectedCellId) {
                            root.generator.resetCellFont(root.selectedCellId)
                        }
                    }
                }
                
                Label { text: qsTr("Size:") }
                SpinBox {
                    id: fontSizeSpinBox
                    from: 8
                    to: 72
                    value: root.currentFont ? root.currentFont.pointSize : 12

                    Connections {
                        target: root
                        function onCurrentFontChanged() {
                            if (root.currentFont) {
                                fontSizeSpinBox.value = root.currentFont.pointSize
                            }
                        }
                    }

                    onValueModified: {
                        if (generator) {
                            var font = root.currentFont
                            font.pointSize = value
                            generator.font = font
                        }
                    }
                }
                // No reset button for size - it's part of the font property
                Item { Layout.preferredWidth: 40 }

                Label { text: qsTr("Color:") }
                Button {
                    id: colorButton
                    Layout.fillWidth: true

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        color: root.currentColor
                    }

                    onClicked: colorDialog.open()
                }

                Button {
                    text: "↺"
                    Layout.preferredWidth: 40
                    enabled: root.hasSelection && root.currentHasCustomColor
                    opacity: enabled ? 1.0 : 0.3
                    ToolTip.visible: hovered
                    ToolTip.text: enabled ? qsTr("Reset to global color") : qsTr("No custom color")
                    onClicked: {
                        if (root.generator && root.selectedCellId) {
                            root.generator.resetCellColor(root.selectedCellId)
                        }
                    }
                }

                Label { text: qsTr("Background Opacity:") }
                RowLayout {
                    Layout.fillWidth: true

                    Slider {
                        id: opacitySlider
                        Layout.fillWidth: true
                        from: 0.0
                        to: 1.0
                        stepSize: 0.01
                        value: generator ? generator.backgroundOpacity : 1.0

                        onValueChanged: {
                            if (generator && Math.abs(generator.backgroundOpacity - value) > 0.001) {
                                generator.backgroundOpacity = value
                            }
                        }
                    }

                    Label {
                        text: Math.round(opacitySlider.value * 100) + "%"
                        Layout.preferredWidth: 40
                    }
                }
                // No reset button for opacity - it's a global property
                Item { Layout.preferredWidth: 40 }
            }
        }
        
        // Display options
        GroupBox {
            title: qsTr("Display Options")
            Layout.fillWidth: true
            
            ColumnLayout {
                anchors.fill: parent
                
                CheckBox {
                    id: showDepthCheckbox
                    text: qsTr("Show Depth")
                    checked: generator ? generator.showDepth : true
                    onCheckedChanged: {
                        if (generator && generator.showDepth !== checked) {
                            generator.showDepth = checked
                        }
                    }
                }
                
                CheckBox {
                    id: showTempCheckbox
                    text: qsTr("Show Temperature")
                    checked: generator ? generator.showTemperature : true
                    onCheckedChanged: {
                        if (generator && generator.showTemperature !== checked) {
                            generator.showTemperature = checked
                        }
                    }
                }
                
                CheckBox {
                    id: showTimeCheckbox
                    text: qsTr("Show Time")
                    checked: generator ? generator.showTime : true
                    onCheckedChanged: {
                        if (generator && generator.showTime !== checked) {
                            generator.showTime = checked
                        }
                    }
                }
                
                CheckBox {
                    id: showNDLCheckbox
                    text: qsTr("Show No Decompression Limit")
                    checked: generator ? generator.showNDL : true
                    onCheckedChanged: {
                        if (generator && generator.showNDL !== checked) {
                            generator.showNDL = checked
                        }
                    }
                }
                
                CheckBox {
                    id: showPressureCheckbox
                    text: qsTr("Show Tank Pressure")
                    checked: generator ? generator.showPressure : true
                    onCheckedChanged: {
                        if (generator && generator.showPressure !== checked) {
                            generator.showPressure = checked
                        }
                    }
                }
            }
        }

        // CCR Settings
        GroupBox {
            title: qsTr("CCR Settings")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent

                CheckBox {
                    id: showPO2Cell1Checkbox
                    text: qsTr("Show Cell 1 PO2")
                    checked: generator ? generator.showPO2Cell1 : false
                    onCheckedChanged: {
                        if (generator && generator.showPO2Cell1 !== checked) {
                            generator.showPO2Cell1 = checked
                        }
                    }
                }

                CheckBox {
                    id: showPO2Cell2Checkbox
                    text: qsTr("Show Cell 2 PO2")
                    checked: generator ? generator.showPO2Cell2 : false
                    onCheckedChanged: {
                        if (generator && generator.showPO2Cell2 !== checked) {
                            generator.showPO2Cell2 = checked
                        }
                    }
                }

                CheckBox {
                    id: showPO2Cell3Checkbox
                    text: qsTr("Show Cell 3 PO2")
                    checked: generator ? generator.showPO2Cell3 : false
                    onCheckedChanged: {
                        if (generator && generator.showPO2Cell3 !== checked) {
                            generator.showPO2Cell3 = checked
                        }
                    }
                }

                CheckBox {
                    id: showCompositePO2Checkbox
                    text: qsTr("Show Composite PO2")
                    checked: generator ? generator.showCompositePO2 : false
                    onCheckedChanged: {
                        if (generator && generator.showCompositePO2 !== checked) {
                            generator.showCompositePO2 = checked
                        }
                    }
                }
            }
        }

        // Units Settings
        GroupBox {
            title: qsTr("Units")
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent

                RadioButton {
                    id: metricUnitsRadio
                    text: qsTr("Metric (m, °C, bar)")
                    checked: config ? config.unitSystem === Units.Metric : true
                    onCheckedChanged: {
                        if (checked && config && config.unitSystem !== Units.Metric) {
                            config.unitSystem = Units.Metric
                        }
                    }
                }

                RadioButton {
                    id: imperialUnitsRadio
                    text: qsTr("Imperial (ft, °F, psi)")
                    checked: config ? config.unitSystem === Units.Imperial : false
                    onCheckedChanged: {
                        if (checked && config && config.unitSystem !== Units.Imperial) {
                            config.unitSystem = Units.Imperial
                        }
                    }
                }
            }
        }
    }
    
    // Dialogs
    FileDialog {
        id: templateFileDialog
        title: qsTr("Select Template Image")
        nameFilters: ["Image files (*.png *.jpg *.jpeg)"]
        onAccepted: {
            if (generator) {
                // Convert file URL to local path
                var rootWindow = root
                while (rootWindow.parent) {
                    rootWindow = rootWindow.parent
                }
                var localPath = rootWindow.urlToLocalFile ?
                    rootWindow.urlToLocalFile(selectedFile.toString()) :
                    selectedFile.toString().replace("file://", "")

                generator.templatePath = localPath
            }
        }
    }
    
    ColorDialog {
        id: colorDialog
        title: qsTr("Select Text Color")
        selectedColor: root.currentColor

        Connections {
            target: root
            function onCurrentColorChanged() {
                colorDialog.selectedColor = root.currentColor
            }
        }

        onAccepted: {
            if (generator) generator.textColor = selectedColor
        }
    }

    FileDialog {
        id: saveTemplateDialog
        title: qsTr("Save Template As")
        fileMode: FileDialog.SaveFile
        nameFilters: ["Unabara Template (*.utp)", "All Files (*)"]
        defaultSuffix: "utp"
        onAccepted: {
            if (generator) {
                // Convert file URL to local path
                var rootWindow = root
                while (rootWindow.parent) {
                    rootWindow = rootWindow.parent
                }
                var localPath = rootWindow.urlToLocalFile ?
                    rootWindow.urlToLocalFile(selectedFile.toString()) :
                    selectedFile.toString().replace("file://", "")

                console.log("Saving template to:", localPath)
                var success = generator.saveTemplateToFile(localPath)
                if (success) {
                    console.log("Template saved successfully!")
                } else {
                    console.error("Failed to save template")
                }
            }
        }
    }

    FileDialog {
        id: loadTemplateDialog
        title: qsTr("Load Template")
        fileMode: FileDialog.OpenFile
        nameFilters: ["Unabara Template (*.utp)", "All Files (*)"]
        onAccepted: {
            if (generator) {
                // Convert file URL to local path
                var rootWindow = root
                while (rootWindow.parent) {
                    rootWindow = rootWindow.parent
                }
                var localPath = rootWindow.urlToLocalFile ?
                    rootWindow.urlToLocalFile(selectedFile.toString()) :
                    selectedFile.toString().replace("file://", "")

                console.log("Loading template from:", localPath)
                var success = generator.loadTemplateFromFile(localPath)
                if (success) {
                    console.log("Template loaded successfully!")
                    // Update cell model to reflect loaded template
                    if (root.timeline && root.dive) {
                        cellModel.updateFromGenerator(root.generator, root.dive, root.timeline.currentTime)
                    }
                } else {
                    console.error("Failed to load template")
                }
            }
        }
    }
}
