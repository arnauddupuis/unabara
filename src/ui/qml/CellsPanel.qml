import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/**
 * Cells list ("layers panel") for the overlay inspector.
 *
 * One row per data field: a visibility checkbox bound to the generator's
 * show* flag (toggling creates the cell when the template lacks it — the
 * canvas owns that wiring), the field name (click to select the cell on the
 * canvas), and override badges showing per-cell font/color/shadow
 * customizations. Badge colors match the corner dots drawn on the canvas
 * cells (cyan/magenta/orange).
 *
 * Tank pressure is special: one master checkbox drives all tank cells
 * (their number depends on the dive), with one selectable row per existing
 * pressure cell below it.
 */
ColumnLayout {
    id: panel

    property var generator: null
    property var dive: null
    property var cellModel: null

    // Bumped when per-cell state changes so badge/selection bindings that
    // read generator invokables re-evaluate.
    property int rev: 0

    spacing: 2

    Connections {
        target: panel.generator
        enabled: panel.generator !== null
        function onCellsChanged() { panel.rev++ }
        function onSelectedCellIdChanged() { panel.rev++ }
    }

    // Display names come from C++ (CellData::displayName via the generator)
    // so this panel and the editing-scope combo can never disagree; these
    // arrays only map ids to their generator visibility flags.
    function fieldName(id) {
        return panel.generator ? panel.generator.cellDisplayName(id) : id
    }

    readonly property var standardFields: [
        { id: "depth",       flag: "showDepth" },
        { id: "temperature", flag: "showTemperature" },
        { id: "time",        flag: "showTime" },
        { id: "gas",         flag: "showGas" },
        { id: "cns",         flag: "showCNS" },
        { id: "mean_depth",  flag: "showMeanDepth" },
        { id: "max_depth",   flag: "showMaxDepth" }
    ]

    readonly property var decoFields: [
        { id: "ndl",        flag: "showNDL" },
        { id: "tts",        flag: "showTTS" },
        { id: "stop_depth", flag: "showStopDepth" },
        { id: "stop_time",  flag: "showStopTime" }
    ]

    readonly property var ccrFields: [
        { id: "po2_cell1",     flag: "showPO2Cell1" },
        { id: "po2_cell2",     flag: "showPO2Cell2" },
        { id: "po2_cell3",     flag: "showPO2Cell3" },
        { id: "composite_po2", flag: "showCompositePO2" }
    ]

    function toggleSelection(cellId) {
        if (!panel.generator)
            return
        panel.generator.selectedCellId =
                (panel.generator.selectedCellId === cellId) ? "" : cellId
    }

    // One list row: [eye] Name  ●●● (override badges)
    component FieldRow: Rectangle {
        id: row

        property string cellId: ""
        property string flagName: ""      // "" = row has no visibility flag of its own
        property string displayName: ""
        property bool selectable: true

        readonly property bool isSelected: {
            panel.rev
            return panel.generator && cellId !== ""
                    && panel.generator.selectedCellId === cellId
        }
        readonly property bool fieldVisible: {
            return flagName !== "" && panel.generator ? panel.generator[flagName] : true
        }

        Layout.fillWidth: true
        implicitHeight: 28
        radius: 4
        color: isSelected ? palette.highlight
                          : (rowMouse.containsMouse && selectable && fieldVisible
                             ? palette.midlight : "transparent")

        MouseArea {
            id: rowMouse
            anchors.fill: parent
            hoverEnabled: true
            onClicked: {
                if (row.selectable && row.fieldVisible)
                    panel.toggleSelection(row.cellId)
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 4
            // Generous right margin: the sidebar ScrollView's scrollbar
            // overlays the content, and the badges must stay clear of it.
            anchors.rightMargin: 20
            spacing: 6

            CheckBox {
                id: eye
                visible: row.flagName !== ""
                Layout.preferredWidth: visible ? implicitWidth : 0
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: checked ? qsTr("Hide this cell") : qsTr("Show this cell")
                onToggled: {
                    if (panel.generator)
                        panel.generator[row.flagName] = checked
                }
            }
            // Explicit Binding so an external flag change (e.g. template
            // load) still updates the checkbox after the user has clicked it
            // (interaction breaks a plain `checked:` binding).
            Binding {
                target: eye
                property: "checked"
                value: panel.generator && row.flagName !== ""
                       ? panel.generator[row.flagName] : false
                restoreMode: Binding.RestoreBinding
            }

            Label {
                text: row.displayName
                elide: Text.ElideRight
                Layout.fillWidth: true
                opacity: row.fieldVisible ? 1.0 : 0.45
                color: row.isSelected ? palette.highlightedText : palette.windowText
            }

            // Override badges — same colors as the canvas corner dots
            Repeater {
                model: [
                    { color: "cyan",    tip: qsTr("Custom font"),
                      has: function(g, id) { return g.getCellHasCustomFont(id) } },
                    { color: "magenta", tip: qsTr("Custom color"),
                      has: function(g, id) { return g.getCellHasCustomLabelColor(id)
                                                 || g.getCellHasCustomValueColor(id) } },
                    { color: "orange",  tip: qsTr("Custom shadow"),
                      has: function(g, id) { return g.getCellHasCustomShadow(id) } }
                ]
                delegate: Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: modelData.color
                    visible: {
                        panel.rev
                        return panel.generator && row.cellId !== ""
                                && modelData.has(panel.generator, row.cellId)
                    }
                    ToolTip.visible: badgeMouse.containsMouse
                    ToolTip.text: modelData.tip
                    MouseArea {
                        id: badgeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                }
            }
        }
    }

    component GroupHeader: Label {
        Layout.fillWidth: true
        Layout.topMargin: 6
        font.pixelSize: 11
        font.bold: true
        opacity: 0.6
    }

    GroupHeader { text: qsTr("STANDARD") }
    Repeater {
        model: panel.standardFields
        delegate: FieldRow {
            cellId: modelData.id
            flagName: modelData.flag
            displayName: panel.fieldName(modelData.id)
        }
    }

    GroupHeader { text: qsTr("DECOMPRESSION") }
    Repeater {
        model: panel.decoFields
        delegate: FieldRow {
            cellId: modelData.id
            flagName: modelData.flag
            displayName: panel.fieldName(modelData.id)
        }
    }

    GroupHeader { text: qsTr("CCR") }
    Repeater {
        model: panel.ccrFields
        delegate: FieldRow {
            cellId: modelData.id
            flagName: modelData.flag
            displayName: panel.fieldName(modelData.id)
        }
    }

    GroupHeader { text: qsTr("TANK PRESSURE") }
    // Master toggle: the number of tank cells depends on the dive, so the
    // canvas routes this flag through setPressureCellsVisible(dive).
    FieldRow {
        flagName: "showPressure"
        displayName: qsTr("Tank Pressure (all tanks)")
        selectable: false
    }
    // One selectable row per existing pressure cell ("tank_N" or legacy
    // "pressure" id), indented under the master toggle.
    Repeater {
        model: panel.cellModel
        delegate: FieldRow {
            readonly property bool isPressureCell:
                model.cellId === "pressure" || model.cellId.indexOf("tank_") === 0
            visible: isPressureCell && model.visible
            Layout.leftMargin: 24
            implicitHeight: visible ? 28 : 0
            cellId: model.cellId
            displayName: model.tankIndex >= 0
                         ? qsTr("Tank %1").arg(model.tankIndex + 1)
                         : qsTr("Pressure")
        }
    }
}
