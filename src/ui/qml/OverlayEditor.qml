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
    // Shared cell model owned by main.qml (also drives the canvas editor)
    property var cellModel: null
    property bool hasSelection: root.generator ? root.generator.selectedCellId !== "" : false
    property string selectedCellId: root.generator ? root.generator.selectedCellId : ""

    // A template picked in the Template combo failed to load (corrupt or
    // unreadable .utp). main.qml owns the error dialog.
    signal templateLoadFailed(string path)

    // Editing-scope routing: with a cell selected, edits create per-cell
    // overrides; with "All cells", they write the global defaults. The
    // getters already read scope-aware values — these are their write-side
    // counterparts, and every input handler must go through them.
    function applyFont(font) {
        if (!generator)
            return
        if (hasSelection && selectedCellId)
            generator.setCellFont(selectedCellId, font)
        else
            generator.font = font
    }

    function applyShadow(enabled, type, color, size, opacity) {
        if (!generator)
            return
        if (hasSelection && selectedCellId) {
            // hasCustomShadow is one flag for all five shadow properties, so
            // a per-cell edit pins the full effective group with the edited
            // value swapped in
            generator.setCellShadow(selectedCellId, enabled, type, color, size, opacity)
        } else {
            generator.shadowEnabled = enabled
            generator.shadowType = type
            generator.shadowColor = color
            generator.shadowSize = size
            generator.shadowOpacity = opacity
        }
    }

    // Reactive properties that update when selection or cells change
    property var currentFont: getCurrentFont()
    property var currentLabelColor: getCurrentLabelColor()
    property var currentValueColor: getCurrentValueColor()
    property bool currentHasCustomFont: getSelectedCellHasCustomFont()
    property bool currentHasCustomLabelColor: getSelectedCellHasCustomLabelColor()
    property bool currentHasCustomValueColor: getSelectedCellHasCustomValueColor()
    property bool currentShowLabel: getCurrentShowLabel()
    property bool currentHasCustomShowLabel: getSelectedCellHasCustomShowLabel()
    property bool currentShadowEnabled: getCurrentShadowEnabled()
    property int currentShadowType: getCurrentShadowType()
    property color currentShadowColor: getCurrentShadowColor()
    property int currentShadowSize: getCurrentShadowSize()
    property real currentShadowOpacity: getCurrentShadowOpacity()
    property bool currentHasCustomShadow: getSelectedCellHasCustomShadow()
    // v1.2 cell geometry (per-cell only — no global default)
    property int currentHAlign: getCurrentHAlign()
    property int currentVAlign: getCurrentVAlign()
    property bool currentAutoSize: getCurrentAutoSize()

    implicitHeight: mainColumn.implicitHeight

    // Get the effective font (selected cell or global) - reads unscaled font from generator
    function getCurrentFont() {
        if (!generator) return Qt.font({family: "Arial", pointSize: 12})

        if (hasSelection && selectedCellId) {
            return generator.getCellFont(selectedCellId)
        }

        return generator.font
    }

    // Get the effective colors (selected cell or global) - read from generator
    function getCurrentLabelColor() {
        if (!generator) return "white"

        if (hasSelection && selectedCellId) {
            return generator.getCellLabelColor(selectedCellId)
        }

        return generator.labelColor
    }

    function getCurrentValueColor() {
        if (!generator) return "white"

        if (hasSelection && selectedCellId) {
            return generator.getCellValueColor(selectedCellId)
        }

        return generator.valueColor
    }

    function getSelectedCellHasCustomFont() {
        if (!hasSelection || !selectedCellId || !generator) return false
        return generator.getCellHasCustomFont(selectedCellId)
    }

    function getSelectedCellHasCustomLabelColor() {
        if (!hasSelection || !selectedCellId || !generator) return false
        return generator.getCellHasCustomLabelColor(selectedCellId)
    }

    function getSelectedCellHasCustomValueColor() {
        if (!hasSelection || !selectedCellId || !generator) return false
        return generator.getCellHasCustomValueColor(selectedCellId)
    }

    // Get the effective showLabel (selected cell or global)
    function getCurrentShowLabel() {
        if (!generator) return true

        if (hasSelection && selectedCellId) {
            return generator.getCellShowLabel(selectedCellId)
        }

        return generator.showLabel
    }

    function getSelectedCellHasCustomShowLabel() {
        if (!hasSelection || !selectedCellId || !generator) return false
        return generator.getCellHasCustomShowLabel(selectedCellId)
    }

    // Get the effective shadow settings (selected cell or global)
    function getCurrentShadowEnabled() {
        if (!generator) return false

        if (hasSelection && selectedCellId) {
            return generator.getCellShadowEnabled(selectedCellId)
        }

        return generator.shadowEnabled
    }

    function getCurrentShadowType() {
        if (!generator) return 0

        if (hasSelection && selectedCellId) {
            return generator.getCellShadowType(selectedCellId)
        }

        return generator.shadowType
    }

    function getCurrentShadowColor() {
        if (!generator) return "black"

        if (hasSelection && selectedCellId) {
            return generator.getCellShadowColor(selectedCellId)
        }

        return generator.shadowColor
    }

    function getCurrentShadowSize() {
        if (!generator) return 2

        if (hasSelection && selectedCellId) {
            return generator.getCellShadowSize(selectedCellId)
        }

        return generator.shadowSize
    }

    function getCurrentShadowOpacity() {
        if (!generator) return 0.7

        if (hasSelection && selectedCellId) {
            return generator.getCellShadowOpacity(selectedCellId)
        }

        return generator.shadowOpacity
    }

    function getSelectedCellHasCustomShadow() {
        if (!hasSelection || !selectedCellId || !generator) return false
        return generator.getCellHasCustomShadow(selectedCellId)
    }

    function getCurrentHAlign() {
        if (!hasSelection || !selectedCellId || !generator) return 0
        return generator.getCellHAlign(selectedCellId)
    }

    function getCurrentVAlign() {
        if (!hasSelection || !selectedCellId || !generator) return 0
        return generator.getCellVAlign(selectedCellId)
    }

    function getCurrentAutoSize() {
        if (!hasSelection || !selectedCellId || !generator) return true
        return !generator.getCellHasFixedSize(selectedCellId)
    }

    // Update reactive properties when selection or cells change
    onSelectedCellIdChanged: {
        updateCurrentProperties()
    }

    Connections {
        target: generator
        function onCellsChanged() {
            updateCurrentProperties()
        }

        // Geometry edits (alignment, resize, auto-size) ride this signal
        function onCellLayoutChanged() {
            updateCurrentProperties()
        }

        function onFontChanged() {
            if (!hasSelection) {
                updateCurrentProperties()
            }
        }

        function onLabelColorChanged() {
            if (!hasSelection) {
                updateCurrentProperties()
            }
        }

        function onValueColorChanged() {
            if (!hasSelection) {
                updateCurrentProperties()
            }
        }

        function onShowLabelChanged() {
            if (!hasSelection) {
                updateCurrentProperties()
            }
        }

        function onShadowChanged() {
            if (!hasSelection) {
                updateCurrentProperties()
            }
        }
    }

    function updateCurrentProperties() {
        var newFont = getCurrentFont()
        currentFont = newFont
        currentLabelColor = getCurrentLabelColor()
        currentValueColor = getCurrentValueColor()
        currentHasCustomFont = getSelectedCellHasCustomFont()
        currentHasCustomLabelColor = getSelectedCellHasCustomLabelColor()
        currentHasCustomValueColor = getSelectedCellHasCustomValueColor()
        currentShowLabel = getCurrentShowLabel()
        currentHasCustomShowLabel = getSelectedCellHasCustomShowLabel()
        currentShadowEnabled = getCurrentShadowEnabled()
        currentShadowType = getCurrentShadowType()
        currentShadowColor = getCurrentShadowColor()
        currentShadowSize = getCurrentShadowSize()
        currentShadowOpacity = getCurrentShadowOpacity()
        currentHasCustomShadow = getSelectedCellHasCustomShadow()
        currentHAlign = getCurrentHAlign()
        currentVAlign = getCurrentVAlign()
        currentAutoSize = getCurrentAutoSize()
    }

    ColumnLayout {
        id: mainColumn
        width: parent.width
        spacing: 20

        // Cells list — replaces the old Display Options / CCR checkbox walls
        // All sections but the last start collapsed so new users see at a
        // glance that the inspector holds more than fits the first screen.
        CollapsibleSection {
            title: qsTr("Cells")
            Layout.fillWidth: true
            expanded: false
            settingsKey: "overlay_cells"

            CellsPanel {
                Layout.fillWidth: true
                generator: root.generator
                dive: root.dive
                cellModel: root.cellModel
            }
        }

        // Text settings
        CollapsibleSection {
            title: qsTr("Text")
            Layout.fillWidth: true
            expanded: false
            settingsKey: "overlay_text"

            // Editing scope: the controls below target either every cell or a
            // single selected cell. The switcher makes the target explicit at
            // the point of editing and stays in sync with canvas selection.
            RowLayout {
                Layout.fillWidth: true

                Label {
                    text: qsTr("Editing:")
                    font.bold: true
                }

                ComboBox {
                    id: scopeCombo
                    Layout.fillWidth: true

                    property var ids: []

                    function rebuild() {
                        var fresh = root.cellModel ? root.cellModel.visibleCellIds() : []
                        // Reassigning the model resets currentIndex and
                        // re-instantiates the popup's delegates; skip it when
                        // the id list is unchanged (every timeline tick ends
                        // up here via modelUpdated).
                        var same = fresh.length === ids.length
                        for (var i = 0; same && i < fresh.length; ++i)
                            same = fresh[i] === ids[i]
                        if (!same) {
                            ids = fresh
                            model = [qsTr("All cells")].concat(ids)
                        }
                        syncIndex()
                    }

                    function syncIndex() {
                        // The generator drops the selection itself when the
                        // selected cell is hidden or removed, so a selected
                        // id is always in the visible list here.
                        var sel = root.generator ? root.generator.selectedCellId : ""
                        var idx = sel === "" ? 0 : ids.indexOf(sel) + 1
                        currentIndex = idx > 0 ? idx : 0
                    }

                    Component.onCompleted: rebuild()

                    onActivated: {
                        if (root.generator)
                            root.generator.selectedCellId =
                                    currentIndex <= 0 ? "" : ids[currentIndex - 1]
                    }

                    Connections {
                        target: root.cellModel
                        enabled: root.cellModel !== null
                        function onModelUpdated() { scopeCombo.rebuild() }
                    }

                    Connections {
                        target: root.generator
                        enabled: root.generator !== null
                        function onSelectedCellIdChanged() { scopeCombo.syncIndex() }
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
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
                        var font = root.currentFont
                        font.family = currentText
                        root.applyFont(font)
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
                        var font = root.currentFont
                        font.pointSize = value
                        root.applyFont(font)
                    }
                }
                // No reset button for size - it's part of the font property
                Item { Layout.preferredWidth: 40 }

                Label { text: qsTr("Style:") }
                Row {
                    Layout.fillWidth: true
                    spacing: 10
                    CheckBox {
                        id: boldCheckBox
                        text: qsTr("Bold")
                        checked: root.currentFont ? root.currentFont.bold : false
                        Connections {
                            target: root
                            function onCurrentFontChanged() {
                                boldCheckBox.checked = root.currentFont ? root.currentFont.bold : false
                            }
                        }
                        onClicked: {
                            var font = root.currentFont
                            font.bold = checked
                            root.applyFont(font)
                        }
                    }
                    CheckBox {
                        id: italicCheckBox
                        text: qsTr("Italic")
                        checked: root.currentFont ? root.currentFont.italic : false
                        Connections {
                            target: root
                            function onCurrentFontChanged() {
                                italicCheckBox.checked = root.currentFont ? root.currentFont.italic : false
                            }
                        }
                        onClicked: {
                            var font = root.currentFont
                            font.italic = checked
                            root.applyFont(font)
                        }
                    }
                }
                Item { Layout.preferredWidth: 40 }

                Label { text: qsTr("Label color:") }
                Button {
                    id: labelColorButton
                    Layout.fillWidth: true

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        color: root.currentLabelColor
                    }

                    onClicked: labelColorDialog.open()
                }

                Button {
                    text: "↺"
                    Layout.preferredWidth: 40
                    enabled: root.hasSelection && root.currentHasCustomLabelColor
                    opacity: enabled ? 1.0 : 0.3
                    ToolTip.visible: hovered
                    ToolTip.text: enabled ? qsTr("Reset to global label color") : qsTr("No custom label color")
                    onClicked: {
                        if (root.generator && root.selectedCellId) {
                            root.generator.resetCellLabelColor(root.selectedCellId)
                        }
                    }
                }

                Label { text: qsTr("Data color:") }
                Button {
                    id: valueColorButton
                    Layout.fillWidth: true

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        color: root.currentValueColor
                    }

                    onClicked: valueColorDialog.open()
                }

                Button {
                    text: "↺"
                    Layout.preferredWidth: 40
                    enabled: root.hasSelection && root.currentHasCustomValueColor
                    opacity: enabled ? 1.0 : 0.3
                    ToolTip.visible: hovered
                    ToolTip.text: enabled ? qsTr("Reset to global data color") : qsTr("No custom data color")
                    onClicked: {
                        if (root.generator && root.selectedCellId) {
                            root.generator.resetCellValueColor(root.selectedCellId)
                        }
                    }
                }

                Label { text: qsTr("Label:") }
                CheckBox {
                    id: showLabelCheckBox
                    Layout.fillWidth: true
                    text: qsTr("Show label")
                    checked: root.currentShowLabel
                    Connections {
                        target: root
                        function onCurrentShowLabelChanged() {
                            showLabelCheckBox.checked = root.currentShowLabel
                        }
                    }
                    onClicked: {
                        if (!root.generator)
                            return
                        if (root.hasSelection && root.selectedCellId)
                            root.generator.setCellShowLabel(root.selectedCellId, checked)
                        else
                            root.generator.showLabel = checked
                    }
                }

                Button {
                    text: "↺"
                    Layout.preferredWidth: 40
                    enabled: root.hasSelection && root.currentHasCustomShowLabel
                    opacity: enabled ? 1.0 : 0.3
                    ToolTip.visible: hovered
                    ToolTip.text: enabled ? qsTr("Reset to global label setting") : qsTr("No custom label setting")
                    onClicked: {
                        if (root.generator && root.selectedCellId) {
                            root.generator.resetCellShowLabel(root.selectedCellId)
                        }
                    }
                }

                Label { text: qsTr("Shadow:") }
                CheckBox {
                    id: shadowEnabledCheckBox
                    Layout.fillWidth: true
                    text: qsTr("Enable text shadow")
                    checked: root.currentShadowEnabled
                    Connections {
                        target: root
                        function onCurrentShadowEnabledChanged() {
                            shadowEnabledCheckBox.checked = root.currentShadowEnabled
                        }
                    }
                    onClicked: {
                        root.applyShadow(checked, root.currentShadowType,
                                         root.currentShadowColor, root.currentShadowSize,
                                         root.currentShadowOpacity)
                    }
                }

                Button {
                    text: "↺"
                    Layout.preferredWidth: 40
                    enabled: root.hasSelection && root.currentHasCustomShadow
                    opacity: enabled ? 1.0 : 0.3
                    ToolTip.visible: hovered
                    ToolTip.text: enabled ? qsTr("Reset to global shadow settings") : qsTr("No custom shadow settings")
                    onClicked: {
                        if (root.generator && root.selectedCellId) {
                            root.generator.resetCellShadow(root.selectedCellId)
                        }
                    }
                }

                Label { text: qsTr("Shadow type:") }
                ComboBox {
                    id: shadowTypeCombo
                    Layout.fillWidth: true
                    enabled: root.currentShadowEnabled
                    model: [qsTr("Crisp offset"), qsTr("Soft blurred"), qsTr("Outline")]
                    currentIndex: root.currentShadowType
                    Connections {
                        target: root
                        function onCurrentShadowTypeChanged() {
                            shadowTypeCombo.currentIndex = root.currentShadowType
                        }
                    }
                    onActivated: {
                        root.applyShadow(root.currentShadowEnabled, currentIndex,
                                         root.currentShadowColor, root.currentShadowSize,
                                         root.currentShadowOpacity)
                    }
                }
                Item { Layout.preferredWidth: 40 }

                Label { text: qsTr("Shadow color:") }
                Button {
                    id: shadowColorButton
                    Layout.fillWidth: true
                    enabled: root.currentShadowEnabled

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        color: root.currentShadowColor
                        opacity: shadowColorButton.enabled ? 1.0 : 0.3
                    }

                    onClicked: shadowColorDialog.open()
                }
                Item { Layout.preferredWidth: 40 }

                Label { text: qsTr("Shadow size:") }
                SpinBox {
                    id: shadowSizeSpinBox
                    from: 1
                    to: 10
                    enabled: root.currentShadowEnabled
                    value: root.currentShadowSize
                    Connections {
                        target: root
                        function onCurrentShadowSizeChanged() {
                            shadowSizeSpinBox.value = root.currentShadowSize
                        }
                    }
                    onValueModified: {
                        root.applyShadow(root.currentShadowEnabled, root.currentShadowType,
                                         root.currentShadowColor, value,
                                         root.currentShadowOpacity)
                    }
                }
                Item { Layout.preferredWidth: 40 }

                Label { text: qsTr("Shadow opacity:") }
                Slider {
                    id: shadowOpacitySlider
                    Layout.fillWidth: true
                    from: 0.0
                    to: 1.0
                    enabled: root.currentShadowEnabled
                    value: root.currentShadowOpacity
                    Connections {
                        target: root
                        function onCurrentShadowOpacityChanged() {
                            shadowOpacitySlider.value = root.currentShadowOpacity
                        }
                    }
                    onMoved: {
                        root.applyShadow(root.currentShadowEnabled, root.currentShadowType,
                                         root.currentShadowColor, root.currentShadowSize,
                                         value)
                    }
                }
                Item { Layout.preferredWidth: 40 }

            }
        }
        
        // Cell layout (v1.2 geometry) — per-cell only: the anchor alignment
        // and the auto/fixed size have no global default by design.
        CollapsibleSection {
            title: qsTr("Layout")
            Layout.fillWidth: true
            expanded: false
            settingsKey: "overlay_layout"

            Label {
                Layout.fillWidth: true
                text: root.hasSelection
                      ? qsTr("Cell: %1").arg(root.selectedCellId)
                      : qsTr("Select a cell to edit its layout.")
                opacity: root.hasSelection ? 1.0 : 0.6
                elide: Text.ElideRight
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                // Needs a dive: the setters measure the cell's current box
                // (text at the current time) to re-anchor it in place, and
                // without one the alignment change would move the cell and
                // the auto-size toggle would silently do nothing.
                enabled: root.hasSelection && root.dive !== null

                // Alignment picks which edge/center of the cell box pins to
                // its position — e.g. a right-aligned cell keeps its right
                // edge fixed as the value's digits change. The setters pass
                // dive + time so the cell doesn't move when alignment changes.
                Label { text: qsTr("Horizontal:") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Repeater {
                        model: [qsTr("Left"), qsTr("Center"), qsTr("Right")]
                        delegate: Button {
                            text: modelData
                            Layout.fillWidth: true
                            highlighted: root.currentHAlign === index
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            ToolTip.text: [
                                qsTr("Anchor the left edge — the box grows rightward as content changes"),
                                qsTr("Anchor the center — the box grows evenly in both directions"),
                                qsTr("Anchor the right edge — the box grows leftward as content changes")
                            ][index]
                            onClicked: {
                                if (root.generator && root.selectedCellId)
                                    root.generator.setCellHAlign(
                                        root.selectedCellId, index, root.dive,
                                        root.timeline ? root.timeline.currentTime : 0.0)
                            }
                        }
                    }
                }

                Label { text: qsTr("Vertical:") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Repeater {
                        model: [qsTr("Top"), qsTr("Middle"), qsTr("Bottom")]
                        delegate: Button {
                            text: modelData
                            Layout.fillWidth: true
                            highlighted: root.currentVAlign === index
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            ToolTip.text: [
                                qsTr("Anchor the top edge"),
                                qsTr("Anchor the vertical center"),
                                qsTr("Anchor the bottom edge")
                            ][index]
                            onClicked: {
                                if (root.generator && root.selectedCellId)
                                    root.generator.setCellVAlign(
                                        root.selectedCellId, index, root.dive,
                                        root.timeline ? root.timeline.currentTime : 0.0)
                            }
                        }
                    }
                }

                Label { text: qsTr("Size:") }
                CheckBox {
                    id: autoSizeCheckBox
                    Layout.fillWidth: true
                    text: qsTr("Auto size (fit content)")
                    checked: root.currentAutoSize
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    ToolTip.text: qsTr("Unchecking freezes the current box size; drag the handles on the canvas to adjust it")
                    Connections {
                        target: root
                        function onCurrentAutoSizeChanged() {
                            autoSizeCheckBox.checked = root.currentAutoSize
                        }
                    }
                    onClicked: {
                        if (root.generator && root.selectedCellId)
                            root.generator.setCellAutoSize(
                                root.selectedCellId, checked, root.dive,
                                root.timeline ? root.timeline.currentTime : 0.0)
                        // The click already flipped the box; if the setter
                        // declined (no cell, nothing to measure) the model
                        // did not change and no signal re-syncs us — so
                        // always re-read the truth.
                        checked = root.currentAutoSize
                    }
                }
            }
        }

        // Template Management
        CollapsibleSection {
            title: qsTr("Template")
            Layout.fillWidth: true
            settingsKey: "overlay_template"

            GridLayout {
                Layout.fillWidth: true
                columns: 2

                // Background Image
                Label { text: qsTr("Background Image:") }
                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        id: bgImageLabel
                        Layout.fillWidth: true
                        text: {
                            if (!generator || !generator.templatePath) return qsTr("None")
                            var path = generator.templatePath
                            // Extract filename from path
                            var parts = path.split("/")
                            return parts[parts.length - 1]
                        }
                        elide: Text.ElideMiddle
                    }

                    Button {
                        text: qsTr("Change...")
                        onClicked: backgroundImageDialog.open()
                    }
                }

                // Template selector
                Label { text: qsTr("Template:") }
                RowLayout {
                    Layout.fillWidth: true

                    ComboBox {
                        id: templateSelector
                        Layout.fillWidth: true
                        model: root.generator ? root.generator.getAvailableTemplates() : []

                        Component.onCompleted: {
                            if (config && config.activeTemplatePath && root.generator) {
                                var idx = root.generator.indexOfTemplatePath(config.activeTemplatePath)
                                if (idx >= 0) {
                                    currentIndex = idx
                                }
                            }
                        }

                        onActivated: function(index) {
                            if (!root.generator)
                                return
                            var path = root.generator.getTemplatePath(index)
                            if (!path)
                                return
                            // The combo has already moved to the clicked
                            // entry; a failed load (corrupt/unreadable .utp
                            // — loadTemplateFromFile returns false silently)
                            // must put it back on the template that is
                            // actually rendering, and tell the user.
                            if (!root.generator.loadTemplateFromFile(path)) {
                                var active = config ? config.activeTemplatePath : ""
                                var idx = active ? root.generator.indexOfTemplatePath(active) : -1
                                currentIndex = idx >= 0 ? idx : 0
                                root.templateLoadFailed(path)
                            }
                            // No explicit cellModel refresh: templateChanged
                            // already drives it through OverlayCanvas.
                        }

                        // The template directory is edited on the Settings
                        // tab — refresh the list (and keep the active
                        // template selected) when it changes.
                        Connections {
                            target: config
                            enabled: config !== null
                            function onTemplateDirectoryChanged() {
                                if (!root.generator)
                                    return
                                root.generator.refreshTemplateList()
                                templateSelector.model = root.generator.getAvailableTemplates()
                                var idx = root.generator.indexOfTemplatePath(
                                            config.activeTemplatePath)
                                templateSelector.currentIndex = idx >= 0 ? idx : -1
                            }
                        }
                    }
                }

                // Background Opacity
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

                // Profile color scheme carried by the template (optional).
                // Saved as defaultPrimaryColor/defaultSecondaryColor (v1.1).
                Label {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: qsTr("Optional color scheme saved with the template: it can recolor the dive profile to match (curve and deco zone from the primary color, indicator and grid from the secondary).")
                }
                Label { text: qsTr("Primary Color:") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Button {
                        id: primaryColorButton
                        Layout.fillWidth: true
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Used for the profile curve and the deco zone")

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 4
                            color: generator && generator.hasPrimaryColor
                                   ? generator.primaryColor : "transparent"
                            border.color: "#808080"
                            border.width: generator && generator.hasPrimaryColor ? 0 : 1

                            Label {
                                anchors.centerIn: parent
                                text: qsTr("not set")
                                opacity: 0.6
                                visible: !(generator && generator.hasPrimaryColor)
                            }
                        }

                        onClicked: primaryColorDialog.open()
                    }

                    Button {
                        text: "×"
                        Layout.preferredWidth: 40
                        enabled: generator && (generator.hasPrimaryColor || generator.hasSecondaryColor)
                        opacity: enabled ? 1.0 : 0.3
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Remove the color scheme from this template")
                        onClicked: {
                            if (generator) generator.clearColorScheme()
                        }
                    }
                }

                Label { text: qsTr("Secondary Color:") }
                Button {
                    id: secondaryColorButton
                    Layout.fillWidth: true
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Used for the profile position indicator and the grid")

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        color: generator && generator.hasSecondaryColor
                               ? generator.secondaryColor : "transparent"
                        border.color: "#808080"
                        border.width: generator && generator.hasSecondaryColor ? 0 : 1

                        Label {
                            anchors.centerIn: parent
                            text: qsTr("not set")
                            opacity: 0.6
                            visible: !(generator && generator.hasSecondaryColor)
                        }
                    }

                    onClicked: secondaryColorDialog.open()
                }

                // Action buttons
                RowLayout {
                    Layout.columnSpan: 2
                    Layout.fillWidth: true
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
        }

    }

    
    // Dialogs
    FileDialog {
        id: backgroundImageDialog
        title: qsTr("Select Background Image")
        nameFilters: ["Image files (*.png *.jpg *.jpeg)"]
        onAccepted: {
            if (generator) {
                var localPath = mainWindow.urlToLocalFile(selectedFile.toString())
                generator.templatePath = localPath
            }
        }
    }
    
    ColorDialog {
        id: primaryColorDialog
        title: qsTr("Select Template Primary Color")

        Connections {
            target: root.generator
            function onColorSchemeChanged() {
                if (root.generator && root.generator.hasPrimaryColor)
                    primaryColorDialog.selectedColor = root.generator.primaryColor
            }
        }

        onAccepted: {
            if (generator) generator.primaryColor = selectedColor
        }
    }

    ColorDialog {
        id: secondaryColorDialog
        title: qsTr("Select Template Secondary Color")

        Connections {
            target: root.generator
            function onColorSchemeChanged() {
                if (root.generator && root.generator.hasSecondaryColor)
                    secondaryColorDialog.selectedColor = root.generator.secondaryColor
            }
        }

        onAccepted: {
            if (generator) generator.secondaryColor = selectedColor
        }
    }

    ColorDialog {
        id: labelColorDialog
        title: qsTr("Select Label Color")
        selectedColor: root.currentLabelColor

        Connections {
            target: root
            function onCurrentLabelColorChanged() {
                labelColorDialog.selectedColor = root.currentLabelColor
            }
        }

        onAccepted: {
            if (!generator)
                return
            if (root.hasSelection && root.selectedCellId)
                generator.setCellLabelColor(root.selectedCellId, selectedColor)
            else
                generator.labelColor = selectedColor
        }
    }

    ColorDialog {
        id: valueColorDialog
        title: qsTr("Select Data Color")
        selectedColor: root.currentValueColor

        Connections {
            target: root
            function onCurrentValueColorChanged() {
                valueColorDialog.selectedColor = root.currentValueColor
            }
        }

        onAccepted: {
            if (!generator)
                return
            if (root.hasSelection && root.selectedCellId)
                generator.setCellValueColor(root.selectedCellId, selectedColor)
            else
                generator.valueColor = selectedColor
        }
    }

    ColorDialog {
        id: shadowColorDialog
        title: qsTr("Select Shadow Color")
        selectedColor: root.currentShadowColor

        Connections {
            target: root
            function onCurrentShadowColorChanged() {
                shadowColorDialog.selectedColor = root.currentShadowColor
            }
        }

        onAccepted: {
            root.applyShadow(root.currentShadowEnabled, root.currentShadowType,
                             selectedColor, root.currentShadowSize,
                             root.currentShadowOpacity)
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
                var localPath = mainWindow.urlToLocalFile(selectedFile.toString())
                var success = generator.saveTemplateToFile(localPath)
                if (success) {
                    // Refresh ComboBox and select the saved template
                    generator.refreshTemplateList()
                    var idx = generator.indexOfTemplatePath(localPath)
                    templateSelector.model = generator.getAvailableTemplates()
                    if (idx >= 0) {
                        templateSelector.currentIndex = idx
                    }
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
                var localPath = mainWindow.urlToLocalFile(selectedFile.toString())
                var success = generator.loadTemplateFromFile(localPath)
                if (success) {
                    // Update cell model to reflect loaded template
                    if (root.timeline && root.dive) {
                        cellModel.updateFromGenerator(root.generator, root.dive, root.timeline.currentTime)
                    }
                    // Refresh ComboBox and select the loaded template
                    generator.refreshTemplateList()
                    var idx = generator.indexOfTemplatePath(localPath)
                    templateSelector.model = generator.getAvailableTemplates()
                    if (idx >= 0) {
                        templateSelector.currentIndex = idx
                    }
                } else {
                    console.error("Failed to load template")
                }
            }
        }
    }
}
