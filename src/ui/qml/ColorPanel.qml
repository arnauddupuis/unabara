import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// Colour grading panel (v0.4 track C, PR C2). Fills the video inspector's
// Colour section and talks straight to the `colorGrade` context property
// (ColorGradeController) — same pattern as the inspector reading clipList.
//
// Every control that a click/drag writes imperatively gets a Binding
// element for the controller→UI direction (checkbox/slider pattern from
// VideoInspector's Enabled checkbox): slider drags write the controller,
// clip switches and resets re-assert the model value. Slider.onMoved fires
// only on user interaction, so there is no write-back loop to guard.
//
// Depth-anchor rows land here in C3; eyedropper / LUT import / presets in
// C4.
ColumnLayout {
    id: root
    spacing: 8

    Label {
        visible: !colorGrade.hasClip
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: palette.placeholderText
        text: qsTr("No clip selected — import or select a video to grade it.")
    }

    ColumnLayout {
        visible: colorGrade.hasClip
        Layout.fillWidth: true
        spacing: 8

        CheckBox {
            id: enableCheck
            text: qsTr("Enable colour grade")
            onToggled: colorGrade.enabled = checked
        }
        Binding {
            target: enableCheck
            property: "checked"
            value: colorGrade.enabled
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 8
            rowSpacing: 2
            enabled: colorGrade.enabled

            Label { text: qsTr("Red recovery") }
            Slider {
                id: redSlider
                Layout.fillWidth: true
                from: 0
                to: 1
                onMoved: colorGrade.redRecovery = value
            }
            Label {
                text: Math.round(colorGrade.redRecovery * 100) + "%"
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 42
            }

            Label { text: qsTr("Water tint") }
            Slider {
                id: tintSlider
                Layout.fillWidth: true
                from: -1   // − = warm (blue water), + = magenta (green water)
                to: 1
                onMoved: colorGrade.waterTint = value
            }
            Label {
                text: (colorGrade.waterTint > 0 ? "+" : "")
                      + Math.round(colorGrade.waterTint * 100) + "%"
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 42
            }

            Label { text: qsTr("Clarity") }
            Slider {
                id: claritySlider
                Layout.fillWidth: true
                from: 0
                to: 1
                onMoved: colorGrade.clarity = value
            }
            Label {
                text: Math.round(colorGrade.clarity * 100) + "%"
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 42
            }
        }
        Binding { target: redSlider; property: "value"; value: colorGrade.redRecovery }
        Binding { target: tintSlider; property: "value"; value: colorGrade.waterTint }
        Binding { target: claritySlider; property: "value"; value: colorGrade.clarity }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            enabled: colorGrade.enabled

            Button {
                id: compareButton
                text: qsTr("Hold to compare")
                // Hold-to-compare: original while pressed. The player
                // deactivates the whole effect on bypass (cheapest exact
                // original); releasing restores the graded preview.
                onPressedChanged: colorGrade.bypass = pressed
                ToolTip.visible: hovered
                ToolTip.delay: 300
                ToolTip.text: qsTr("Show the ungraded video while pressed")
            }

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Reset")
                onClicked: colorGrade.reset()
                ToolTip.visible: hovered
                ToolTip.delay: 300
                ToolTip.text: qsTr("Reset the three sliders to zero")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            enabled: colorGrade.enabled

            Label { text: qsTr("Split preview") }
            // Graded LEFT of the split, original right; full right (1.0,
            // the default) = fully graded. Not persisted.
            Slider {
                id: splitSlider
                Layout.fillWidth: true
                from: 0
                to: 1
                onMoved: colorGrade.splitPos = value
                ToolTip.visible: hovered
                ToolTip.delay: 300
                ToolTip.text: qsTr("Graded left of the split, original right")
            }
        }
        Binding { target: splitSlider; property: "value"; value: colorGrade.splitPos }

        // ── Eyedropper (C4) ──────────────────────────────────────────
        // "Pick neutral" arms colorGrade.pickingActive; the crosshair
        // MouseArea over the video content area (VideoSyncPlayer) does
        // the actual capture and a successful pick disarms the mode, so
        // the toggle releases itself — the Binding re-asserts the
        // controller state. The swatch shows the selected anchor's
        // stored sample; × clears it (removes the white balance).
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            enabled: colorGrade.enabled

            Button {
                id: pickNeutralButton
                text: qsTr("Pick neutral")
                checkable: true
                onClicked: colorGrade.pickingActive = checked
                ToolTip.visible: hovered
                ToolTip.delay: 300
                ToolTip.text: qsTr("Then click something gray or white in the video to white-balance the selected anchor")
            }

            Rectangle {
                visible: colorGrade.hasNeutralSample
                implicitWidth: 22
                implicitHeight: 22
                radius: 3
                color: colorGrade.neutralSampleColor
                border.color: palette.mid
                border.width: 1
            }

            Button {
                text: "×"
                visible: colorGrade.hasNeutralSample
                Layout.preferredWidth: 32
                onClicked: colorGrade.clearNeutralSample()
                ToolTip.visible: hovered
                ToolTip.delay: 300
                ToolTip.text: qsTr("Clear the picked sample (removes the white balance)")
            }

            Item { Layout.fillWidth: true }
        }
        Binding {
            target: pickNeutralButton
            property: "checked"
            value: colorGrade.pickingActive
        }

        // ── LUT (C4) ─────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            enabled: colorGrade.enabled

            Label { text: qsTr("LUT") }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: colorGrade.hasLut ? palette.text : palette.placeholderText
                text: colorGrade.hasLut ? colorGrade.lutLabel : qsTr("none")
            }
            Button {
                text: qsTr("Import…")
                onClicked: lutFileDialog.open()
                ToolTip.visible: hovered
                ToolTip.delay: 300
                ToolTip.text: qsTr("Import a .cube 3D LUT, applied after the sliders")
            }
            Button {
                text: "×"
                Layout.preferredWidth: 32
                enabled: colorGrade.hasLut
                onClicked: colorGrade.clearLut()
                ToolTip.visible: hovered
                ToolTip.delay: 300
                ToolTip.text: qsTr("Remove the LUT")
            }
        }
        // Missing-file warning (restored clip whose .cube moved): the
        // parametric grade still applies, only the LUT is skipped.
        Label {
            visible: colorGrade.lutMissing
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: "#e0a030"
            text: qsTr("LUT file missing: %1 — grading without it.").arg(colorGrade.lutLabel)
        }

        // ── Presets (C4) ─────────────────────────────────────────────
        // Apply writes the preset's three slider values into the SELECTED
        // anchor; the anchor's neutral sample, placement and the LUT are
        // untouched. Save as… stores the selected anchor's sliders in the
        // user preset folder (name collisions get a numeric suffix).
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            enabled: colorGrade.enabled

            Label { text: qsTr("Preset") }
            ComboBox {
                id: presetCombo
                Layout.fillWidth: true
                model: colorGrade.presets
                textRole: "name"
            }
            Button {
                text: qsTr("Apply")
                enabled: presetCombo.currentIndex >= 0
                onClicked: colorGrade.applyPreset(
                               colorGrade.presets[presetCombo.currentIndex].path)
                ToolTip.visible: hovered
                ToolTip.delay: 300
                ToolTip.text: qsTr("Write this preset's slider values into the selected anchor")
            }
            Button {
                text: qsTr("Save as…")
                onClicked: {
                    presetNameField.text = ""
                    savePresetDialog.open()
                }
                ToolTip.visible: hovered
                ToolTip.delay: 300
                ToolTip.text: qsTr("Save the selected anchor's sliders as a preset")
            }
        }

        // ── Depth link (C3) ──────────────────────────────────────────
        // Flow: tick "Link to depth", pick the anchor to edit (radio),
        // press "Set" to bind it to the current frame's depth and video
        // time, THEN grade it with the sliders above (they edit the
        // SELECTED anchor). Set-first matters: an unplaced anchor sits
        // out of the ramp, so pre-Set slider moves on a second anchor
        // don't blend; once bound, its weight is ≈1 at that depth and
        // the sliders give live feedback. The preview always shows the
        // result interpolated at the current depth; all depth strings
        // come formatted from C++ (Units).
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 4
            enabled: colorGrade.enabled

            CheckBox {
                id: depthLinkCheck
                text: qsTr("Link to depth")
                enabled: colorGrade.enabled && mainWindow.hasActiveDive
                onToggled: colorGrade.depthLinked = checked
                ToolTip.visible: hovered
                ToolTip.delay: 300
                ToolTip.text: qsTr("Interpolate the grade between two depth anchors as the dive gets deeper")
            }

            Label {
                visible: !mainWindow.hasActiveDive
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: palette.placeholderText
                text: qsTr("Import a dive log to link the grade to depth.")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                enabled: colorGrade.depthLinked && mainWindow.hasActiveDive

                RadioButton {
                    id: anchor0Radio
                    text: qsTr("Anchor 1")
                    onClicked: colorGrade.selectedAnchor = 0
                    ToolTip.visible: hovered
                    ToolTip.delay: 300
                    ToolTip.text: qsTr("The sliders above edit this anchor")
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: colorGrade.anchor0DepthText === "" ? palette.placeholderText
                                                              : palette.text
                    text: colorGrade.anchor0DepthText === ""
                          ? qsTr("not set")
                          : colorGrade.anchor0DepthText
                            + (colorGrade.anchor0RoleText !== ""
                               ? " (" + colorGrade.anchor0RoleText + ")" : "")
                }
                Button {
                    text: qsTr("Set")
                    Layout.preferredWidth: 48
                    onClicked: colorGrade.setAnchorFromCurrentFrame(0)
                    ToolTip.visible: hovered
                    ToolTip.delay: 300
                    ToolTip.text: qsTr("Bind this anchor to the current frame's depth and video time — set first, then grade with the sliders")
                }
                Button {
                    text: qsTr("Go")
                    Layout.preferredWidth: 44
                    enabled: colorGrade.anchor0HasVideoTime
                    onClicked: colorGrade.goToAnchor(0)
                    ToolTip.visible: hovered
                    ToolTip.delay: 300
                    ToolTip.text: qsTr("Seek the video to where this anchor was set")
                }
                Button {
                    text: "×"
                    Layout.preferredWidth: 32
                    enabled: colorGrade.anchor0Set
                    onClicked: colorGrade.clearAnchor(0)
                    ToolTip.visible: hovered
                    ToolTip.delay: 300
                    ToolTip.text: qsTr("Remove this anchor")
                }
            }
            Binding {
                target: anchor0Radio
                property: "checked"
                value: colorGrade.selectedAnchor === 0
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                enabled: colorGrade.depthLinked && mainWindow.hasActiveDive

                RadioButton {
                    id: anchor1Radio
                    text: qsTr("Anchor 2")
                    onClicked: colorGrade.selectedAnchor = 1
                    ToolTip.visible: hovered
                    ToolTip.delay: 300
                    ToolTip.text: qsTr("The sliders above edit this anchor")
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    color: colorGrade.anchor1DepthText === "" ? palette.placeholderText
                                                              : palette.text
                    text: colorGrade.anchor1DepthText === ""
                          ? qsTr("not set")
                          : colorGrade.anchor1DepthText
                            + (colorGrade.anchor1RoleText !== ""
                               ? " (" + colorGrade.anchor1RoleText + ")" : "")
                }
                Button {
                    text: qsTr("Set")
                    Layout.preferredWidth: 48
                    onClicked: colorGrade.setAnchorFromCurrentFrame(1)
                    ToolTip.visible: hovered
                    ToolTip.delay: 300
                    ToolTip.text: qsTr("Bind this anchor to the current frame's depth and video time — set first, then grade with the sliders")
                }
                Button {
                    text: qsTr("Go")
                    Layout.preferredWidth: 44
                    enabled: colorGrade.anchor1HasVideoTime
                    onClicked: colorGrade.goToAnchor(1)
                    ToolTip.visible: hovered
                    ToolTip.delay: 300
                    ToolTip.text: qsTr("Seek the video to where this anchor was set")
                }
                Button {
                    text: "×"
                    Layout.preferredWidth: 32
                    enabled: colorGrade.anchor1Set
                    onClicked: colorGrade.clearAnchor(1)
                    ToolTip.visible: hovered
                    ToolTip.delay: 300
                    ToolTip.text: qsTr("Remove this anchor")
                }
            }
            Binding {
                target: anchor1Radio
                property: "checked"
                value: colorGrade.selectedAnchor === 1
            }

            // Live readout, assembled in C++: "Now: 14.2 m — 62% toward
            // Deep". Empty (hidden) while the link is off or no playback
            // sample has arrived.
            Label {
                visible: colorGrade.currentDepthText !== ""
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: colorGrade.currentDepthText
            }
        }
        Binding {
            target: depthLinkCheck
            property: "checked"
            value: colorGrade.depthLinked
        }
    }

    FileDialog {
        id: lutFileDialog
        title: qsTr("Import LUT")
        nameFilters: [qsTr("3D LUT files (*.cube)")]
        // Parse errors surface through colorGrade.errorOccurred → the
        // main-window toast (main.qml owns the toast).
        onAccepted: colorGrade.importLut(selectedFile)
    }

    // Invisible host: the Dialog reparents to the window Overlay when it
    // opens, but as a direct child of the root ColumnLayout its anchors
    // would trip qmllint's layout-positioning check; an invisible Item is
    // skipped by the layout entirely (no phantom spacing row).
    Item {
        visible: false

        Dialog {
            id: savePresetDialog
            title: qsTr("Save preset")
            modal: true
            parent: Overlay.overlay
            anchors.centerIn: parent
            standardButtons: Dialog.Save | Dialog.Cancel
            onAccepted: colorGrade.savePreset(presetNameField.text)

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Label { text: qsTr("Preset name") }
                TextField {
                    id: presetNameField
                    Layout.fillWidth: true
                    Layout.minimumWidth: 220
                    onAccepted: savePresetDialog.accept()
                }
            }
        }
    }
}
