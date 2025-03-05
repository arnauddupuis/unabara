import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: root
    
    property var generator
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 20
        
        // Template preview
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: width * 0.5625  // 16:9 aspect ratio
            
            Rectangle {
                anchors.fill: parent
                color: "black"
                
                Image {
                    id: templatePreview
                    anchors.fill: parent
                    source: generator ? generator.templatePath : ""
                    fillMode: Image.PreserveAspectFit
                }
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
                    model: ["Default", "Modern", "Classic", "Minimal"]
                    
                    onCurrentTextChanged: {
                        var path = "qrc:/resources/templates/" + currentText.toLowerCase() + "_overlay.png"
                        if (generator) generator.templatePath = path
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
                    value: generator ? generator.font.pixelSize : 12
                    
                    onValueChanged: {
                        if (generator) {
                            var font = generator.font
                            font.pixelSize = value
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
                        if (generator) generator.showDepth = checked
                    }
                }
                
                CheckBox {
                    id: showTempCheckbox
                    text: qsTr("Show Temperature")
                    checked: generator ? generator.showTemperature : true
                    onCheckedChanged: {
                        if (generator) generator.showTemperature = checked
                    }
                }
                
                CheckBox {
                    id: showTimeCheckbox
                    text: qsTr("Show Time")
                    checked: generator ? generator.showTime : true
                    onCheckedChanged: {
                        if (generator) generator.showTime = checked
                    }
                }
                
                CheckBox {
                    id: showNDLCheckbox
                    text: qsTr("Show No Decompression Limit")
                    checked: generator ? generator.showNDL : true
                    onCheckedChanged: {
                        if (generator) generator.showNDL = checked
                    }
                }
                
                CheckBox {
                    id: showPressureCheckbox
                    text: qsTr("Show Tank Pressure")
                    checked: generator ? generator.showPressure : true
                    onCheckedChanged: {
                        if (generator) generator.showPressure = checked
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
            if (generator) generator.templatePath = selectedFile
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
