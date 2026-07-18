import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Unabara.Core 1.0
import Unabara.Generators 1.0

Item {
    id: root

    property var generator
    implicitHeight: mainColumn.implicitHeight
    implicitWidth: 400

    // Which color the active ColorDialog is currently editing.
    // Set when one of the three color buttons is clicked; consumed by the dialog's onAccepted.
    property string colorTarget: ""

    ColumnLayout {
        id: mainColumn
        anchors.fill: parent
        anchors.margins: 10
        spacing: 12

        GroupBox {
            title: qsTr("Background")
            Layout.fillWidth: true

            GridLayout {
                anchors.fill: parent
                columns: 3
                rowSpacing: 8
                columnSpacing: 8

                Label { text: qsTr("Color:") }
                Button {
                    id: bgColorButton
                    Layout.fillWidth: true
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        color: root.generator ? root.generator.backgroundColor : "black"
                        border.color: palette.mid
                        border.width: 1
                    }
                    onClicked: {
                        root.colorTarget = "background"
                        colorDialog.selectedColor = root.generator ? root.generator.backgroundColor : "black"
                        colorDialog.open()
                    }
                }
                Item { Layout.preferredWidth: 40 }

                Label { text: qsTr("Opacity:") }
                Slider {
                    id: bgOpacitySlider
                    Layout.fillWidth: true
                    from: 0.0
                    to: 1.0
                    value: root.generator ? root.generator.backgroundOpacity : 0.0
                    onMoved: {
                        if (root.generator) root.generator.backgroundOpacity = value
                    }
                }
                Label {
                    text: bgOpacitySlider.value.toFixed(2)
                    Layout.preferredWidth: 40
                }
            }
        }

        GroupBox {
            title: qsTr("Depth Curve")
            Layout.fillWidth: true

            GridLayout {
                anchors.fill: parent
                columns: 2
                rowSpacing: 8
                columnSpacing: 8

                Label { text: qsTr("Color:") }
                Button {
                    id: curveColorButton
                    Layout.fillWidth: true
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        color: root.generator ? root.generator.curveColor : "purple"
                        border.color: palette.mid
                        border.width: 1
                    }
                    onClicked: {
                        root.colorTarget = "curve"
                        colorDialog.selectedColor = root.generator ? root.generator.curveColor : "purple"
                        colorDialog.open()
                    }
                }

                Label { text: qsTr("Thickness (px):") }
                SpinBox {
                    id: curveWidthSpin
                    Layout.fillWidth: true
                    from: 1
                    to: 32
                    value: root.generator ? root.generator.curveWidth : 2
                    onValueModified: {
                        if (root.generator) root.generator.curveWidth = value
                    }
                }
            }
        }

        GroupBox {
            title: qsTr("Deco Zone")
            Layout.fillWidth: true

            GridLayout {
                anchors.fill: parent
                columns: 3
                rowSpacing: 8
                columnSpacing: 8

                Label { text: qsTr("Color:") }
                Button {
                    id: decoColorButton
                    Layout.fillWidth: true
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        color: root.generator ? root.generator.decoZoneColor : "red"
                        border.color: palette.mid
                        border.width: 1
                    }
                    onClicked: {
                        root.colorTarget = "deco"
                        colorDialog.selectedColor = root.generator ? root.generator.decoZoneColor : "red"
                        colorDialog.open()
                    }
                }
                Item { Layout.preferredWidth: 40 }

                Label { text: qsTr("Opacity:") }
                Slider {
                    id: decoOpacitySlider
                    Layout.fillWidth: true
                    from: 0.0
                    to: 1.0
                    value: root.generator ? root.generator.decoZoneOpacity : 0.35
                    onMoved: {
                        if (root.generator) root.generator.decoZoneOpacity = value
                    }
                }
                Label {
                    text: decoOpacitySlider.value.toFixed(2)
                    Layout.preferredWidth: 40
                }
            }
        }

        GroupBox {
            title: qsTr("Indicator")
            Layout.fillWidth: true

            GridLayout {
                anchors.fill: parent
                columns: 2
                rowSpacing: 8
                columnSpacing: 8

                Label { text: qsTr("Color:") }
                Button {
                    id: indicatorColorButton
                    Layout.fillWidth: true
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        color: root.generator ? root.generator.indicatorColor : "lime"
                        border.color: palette.mid
                        border.width: 1
                    }
                    onClicked: {
                        root.colorTarget = "indicator"
                        colorDialog.selectedColor = root.generator ? root.generator.indicatorColor : "lime"
                        colorDialog.open()
                    }
                }

                Label { text: qsTr("Mode:") }
                ComboBox {
                    id: modeCombo
                    Layout.fillWidth: true
                    model: [qsTr("Static"), qsTr("Pulsing")]
                    currentIndex: root.generator ? root.generator.indicatorMode : 0
                    onActivated: {
                        if (root.generator) root.generator.indicatorMode = currentIndex
                    }
                }

                Label { text: qsTr("Radius (px):") }
                SpinBox {
                    id: radiusSpin
                    Layout.fillWidth: true
                    from: 1
                    to: 64
                    value: root.generator ? root.generator.indicatorRadius : 8
                    onValueModified: {
                        if (root.generator) root.generator.indicatorRadius = value
                    }
                }

                Label {
                    text: qsTr("Pulse period (ms):")
                    visible: modeCombo.currentIndex === 1
                }
                SpinBox {
                    id: pulseSpin
                    Layout.fillWidth: true
                    from: 200
                    to: 10000
                    stepSize: 100
                    value: root.generator ? root.generator.pulsePeriodMs : 2000
                    visible: modeCombo.currentIndex === 1
                    onValueModified: {
                        if (root.generator) root.generator.pulsePeriodMs = value
                    }
                }
            }
        }

        GroupBox {
            title: qsTr("Output Resolution")
            Layout.fillWidth: true

            GridLayout {
                anchors.fill: parent
                columns: 2
                rowSpacing: 8
                columnSpacing: 8

                Label { text: qsTr("Width (px):") }
                SpinBox {
                    id: widthSpin
                    Layout.fillWidth: true
                    from: 64
                    to: 7680
                    stepSize: 16
                    value: root.generator ? root.generator.outputWidth : 1920
                    onValueModified: {
                        if (root.generator) root.generator.outputWidth = value
                    }
                }

                Label { text: qsTr("Height (px):") }
                SpinBox {
                    id: heightSpin
                    Layout.fillWidth: true
                    from: 64
                    to: 4320
                    stepSize: 16
                    value: root.generator ? root.generator.outputHeight : 400
                    onValueModified: {
                        if (root.generator) root.generator.outputHeight = value
                    }
                }
            }
        }

        GroupBox {
            title: qsTr("Grid")
            Layout.fillWidth: true

            GridLayout {
                anchors.fill: parent
                columns: 3
                rowSpacing: 8
                columnSpacing: 8

                CheckBox {
                    id: gridEnabledCheck
                    text: qsTr("Enable grid")
                    Layout.columnSpan: 3
                    checked: root.generator ? root.generator.gridEnabled : false
                    onToggled: {
                        if (root.generator) root.generator.gridEnabled = checked
                    }
                }

                Label {
                    text: qsTr("Depth interval:")
                    enabled: gridEnabledCheck.checked
                }
                SpinBox {
                    id: gridDepthIntervalSpin
                    Layout.fillWidth: true
                    from: 1
                    to: 200
                    enabled: gridEnabledCheck.checked
                    value: root.generator ? root.generator.gridDepthInterval : 10
                    onValueModified: {
                        if (root.generator) root.generator.gridDepthInterval = value
                    }
                }
                Label {
                    text: (config && config.unitSystem === Units.Metric) ? qsTr("m") : qsTr("ft")
                    Layout.preferredWidth: 40
                    enabled: gridEnabledCheck.checked
                }

                Label {
                    text: qsTr("Time interval:")
                    enabled: gridEnabledCheck.checked
                }
                SpinBox {
                    id: gridTimeIntervalSpin
                    Layout.fillWidth: true
                    from: 30
                    to: 7200
                    stepSize: 60
                    enabled: gridEnabledCheck.checked
                    value: root.generator ? root.generator.gridTimeInterval : 600
                    onValueModified: {
                        if (root.generator) root.generator.gridTimeInterval = value
                    }
                }
                Label {
                    text: qsTr("sec")
                    Layout.preferredWidth: 40
                    enabled: gridEnabledCheck.checked
                }

                Label {
                    text: qsTr("Color:")
                    enabled: gridEnabledCheck.checked
                }
                Button {
                    id: gridColorButton
                    Layout.fillWidth: true
                    enabled: gridEnabledCheck.checked
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        color: root.generator ? root.generator.gridColor : "#b4b4b4"
                        border.color: palette.mid
                        border.width: 1
                    }
                    onClicked: {
                        root.colorTarget = "grid"
                        colorDialog.selectedColor = root.generator ? root.generator.gridColor : "#b4b4b4"
                        colorDialog.open()
                    }
                }
                Item { Layout.preferredWidth: 40 }

                Label {
                    text: qsTr("Opacity:")
                    enabled: gridEnabledCheck.checked
                }
                Slider {
                    id: gridOpacitySlider
                    Layout.fillWidth: true
                    from: 0.0
                    to: 1.0
                    enabled: gridEnabledCheck.checked
                    value: root.generator ? root.generator.gridOpacity : 0.5
                    onMoved: {
                        if (root.generator) root.generator.gridOpacity = value
                    }
                }
                Label {
                    text: gridOpacitySlider.value.toFixed(2)
                    Layout.preferredWidth: 40
                    enabled: gridEnabledCheck.checked
                }

                Label {
                    text: qsTr("Line width (px):")
                    enabled: gridEnabledCheck.checked
                }
                SpinBox {
                    id: gridLineWidthSpin
                    Layout.fillWidth: true
                    from: 1
                    to: 8
                    enabled: gridEnabledCheck.checked
                    value: root.generator ? root.generator.gridLineWidth : 1
                    onValueModified: {
                        if (root.generator) root.generator.gridLineWidth = value
                    }
                }
                Item { Layout.preferredWidth: 40 }

                CheckBox {
                    id: gridShowLabelsCheck
                    text: qsTr("Show axis labels")
                    Layout.columnSpan: 3
                    enabled: gridEnabledCheck.checked
                    checked: root.generator ? root.generator.gridShowLabels : true
                    onToggled: {
                        if (root.generator) root.generator.gridShowLabels = checked
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    ColorDialog {
        id: colorDialog
        title: qsTr("Select Color")
        onAccepted: {
            if (!root.generator) return
            switch (root.colorTarget) {
            case "background": root.generator.backgroundColor = selectedColor; break
            case "curve":      root.generator.curveColor = selectedColor; break
            case "indicator":  root.generator.indicatorColor = selectedColor; break
            case "deco":       root.generator.decoZoneColor = selectedColor; break
            case "grid":       root.generator.gridColor = selectedColor; break
            }
        }
    }
}
