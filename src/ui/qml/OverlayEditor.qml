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

    implicitHeight: mainColumn.implicitHeight

    // Cell model for interactive preview
    CellModel {
        id: cellModel
    }

    ColumnLayout {
        id: mainColumn
        width: parent.width
        spacing: 20
        
        // Interactive overlay preview
        Item {
            id: previewContainer
            Layout.fillWidth: true
            Layout.preferredHeight: width * 0.5625  // 16:9 aspect ratio

            function updateCellModel() {
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

                onCellSelected: function(cellId) {
                    console.log("Cell selected:", cellId)
                    // TODO: Update cell editor panel to show this cell's properties
                }

                onCellDeselected: {
                    console.log("Cell deselected")
                    // TODO: Clear cell editor panel
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
                    if (root.dive) {
                        root.generator.initializeDefaultCellLayout(root.dive)
                    }
                    previewContainer.updateCellModel()
                }
                function onTextColorChanged() {
                    if (root.dive) {
                        root.generator.initializeDefaultCellLayout(root.dive)
                    }
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
        
        // Text settings
        GroupBox {
            title: qsTr("Text Settings")
            Layout.fillWidth: true
            
            GridLayout {
                anchors.fill: parent
                columns: 2
                
                Label { text: qsTr("Font:") }
                ComboBox {
                    id: fontSelector
                    Layout.fillWidth: true
                    model: Qt.fontFamilies()
                    
                    onCurrentTextChanged: {
                        if (generator) {
                            var font = generator.font
                            font.family = currentText
                            generator.font = font
                        }
                    }
                    
                    Component.onCompleted: {
                        if (generator) {
                            currentIndex = model.indexOf(generator.font.family)
                            if (currentIndex === -1) currentIndex = 0
                        }
                    }
                }
                
                Label { text: qsTr("Size:") }
                SpinBox {
                    id: fontSizeSpinBox
                    from: 8
                    to: 72
                    value: generator ? generator.font.pointSize : 12

                    onValueChanged: {
                        if (generator && generator.font.pointSize !== value) {
                            var font = generator.font
                            font.pointSize = value
                            generator.font = font
                        }
                    }
                }
                
                Label { text: qsTr("Color:") }
                Button {
                    id: colorButton
                    Layout.fillWidth: true

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        color: generator ? generator.textColor : "white"
                    }

                    onClicked: colorDialog.open()
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
        selectedColor: generator ? generator.textColor : "white"
        onAccepted: {
            if (generator) generator.textColor = selectedColor
        }
    }
}
