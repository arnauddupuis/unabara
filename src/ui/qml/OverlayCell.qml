import QtQuick
import QtQuick.Controls
import QtQuick.Effects

/**
 * Individual overlay cell component
 *
 * Displays a single data cell with:
 * - Visual selection indicator (bright green border when selected)
 * - Click handling for selection
 * - Drag behavior for positioning, resize handles when selected
 * - Cell content rendering (alignment-aware, mirroring cellGeometry() in C++)
 */
Rectangle {
    id: cellRoot

    // Properties
    property string cellId: ""
    property int cellType: 0
    property point cellPosition: Qt.point(0, 0)  // Normalized position (0.0-1.0)
    property bool cellVisible: true
    property font cellFont: Qt.font({family: "DejaVu Sans", pointSize: 12})
    property color cellLabelColor: "white"
    property color cellValueColor: "white"
    property string displayText: ""
    property size cellCalculatedSize: Qt.size(100, 40)
    property bool selected: false
    property bool overlapping: false
    property bool hasCustomFont: false
    property bool hasCustomLabelColor: false
    property bool hasCustomValueColor: false
    property bool shadowEnabled: false
    property int shadowType: 0            // 0 = offset, 1 = blurred, 2 = outline
    property color shadowColor: "black"
    property real shadowPx: 2             // pre-scaled by the delegate (size * 1.8 * scaleX)
    property real shadowOpacity: 0.7
    property bool hasCustomShadow: false
    property bool dragging: mouseArea.drag.active  // Expose drag state
    property var generator: null  // Reference to overlay generator for snap settings
    property bool showBackground: true  // Show cell background (editor only, not for export)

    // v1.2 geometry (mirrors OverlayGenerator::cellGeometry)
    property int hAlign: 0                // 0 = left, 1 = center, 2 = right (anchor edge)
    property int vAlign: 0                // 0 = top, 1 = middle, 2 = bottom (anchor edge)
    property bool hasFixedSize: false
    property size fixedSizePx: Qt.size(0, 0)  // fixed size pre-scaled to container px
    property bool resizing: false         // true while a resize handle is dragged
    property int resizeXe: 0              // moving edges during resize: -1 left, 1 right
    property int resizeYe: 0              // -1 top, 1 bottom (0 = edge not moving)

    // The box position the delegate binds x/y to is the anchor point minus the
    // alignment offset; these helpers convert box edge <-> anchor both ways.
    function anchorOffsetX() { return hAlign === 1 ? width / 2 : hAlign === 2 ? width : 0 }
    function anchorOffsetY() { return vAlign === 1 ? height / 2 : vAlign === 2 ? height : 0 }

    // The label line and the value line have independent colors, so the text
    // is built as StyledText with a <font> tag around the label. The shadow
    // copies keep drawing the plain displayText (uniform shadow color).
    function escapeHtml(s) {
        return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }
    function styledDisplayText() {
        var t = displayText
        var i = t.indexOf('\n')
        if (i < 0)
            return escapeHtml(t)
        return "<font color=\"" + cellLabelColor + "\">" + escapeHtml(t.substring(0, i))
             + "</font><br>" + escapeHtml(t.substring(i + 1))
    }

    // Signals
    signal clicked()
    signal positionChanged(point newPosition)   // normalized ANCHOR point
    // Box rect in container px after a resize-handle drag (position + size
    // both commit, so the anchor stays consistent with the new box)
    signal geometryCommitted(rect newRect)

    // Visual appearance
    visible: cellVisible
    // Auto: size from the actual text content (+8 padding, like the C++
    // renderer). Fixed: the template-defined box scaled to the container.
    width: hasFixedSize ? fixedSizePx.width : cellText.width + 8
    height: hasFixedSize ? fixedSizePx.height : cellText.height + 8
    // Box position: the model stores the ANCHOR point; alignment decides
    // which box edge/center pins to it (left/top = legacy top-left corner).
    // Mirrors OverlayGenerator::cellGeometry().
    x: cellPosition.x * (parent ? parent.width : 0) - anchorOffsetX()
    y: cellPosition.y * (parent ? parent.height : 0) - anchorOffsetY()
    color: "transparent"

    // Minimum box size a resize can shrink to, in container px.
    readonly property int resizeMinSize: 20

    // Shared by the handle-drag move and the release-time grid snap: clamp
    // the candidate edges against the container bounds and the minimum size.
    // Only the moving edges (resizeXe/resizeYe, set on handle press) are
    // touched — the opposite edge stays pinned. Returns {left,top,right,bottom}.
    function clampResizeEdges(left, top, right, bottom) {
        var c = cellRoot.parent
        if (resizeXe < 0) left = Math.max(0, Math.min(left, right - resizeMinSize))
        if (resizeXe > 0) right = Math.min(c.width, Math.max(right, left + resizeMinSize))
        if (resizeYe < 0) top = Math.max(0, Math.min(top, bottom - resizeMinSize))
        if (resizeYe > 0) bottom = Math.min(c.height, Math.max(bottom, top + resizeMinSize))
        return { left: left, top: top, right: right, bottom: bottom }
    }

    // MouseArea.drag and the resize handles assign x/y/width/height
    // imperatively, which permanently breaks the declarative bindings above.
    // The cell model is updated in place (no reset, so delegates are not
    // recreated) — nothing else would heal them. Re-install the bindings once
    // the new geometry has been committed to the model: they then evaluate
    // straight to the committed box and keep tracking container resizes.
    function restoreGeometryBindings() {
        x = Qt.binding(function() {
            return cellRoot.cellPosition.x * (cellRoot.parent ? cellRoot.parent.width : 0)
                   - cellRoot.anchorOffsetX()
        })
        y = Qt.binding(function() {
            return cellRoot.cellPosition.y * (cellRoot.parent ? cellRoot.parent.height : 0)
                   - cellRoot.anchorOffsetY()
        })
        width = Qt.binding(function() {
            return cellRoot.hasFixedSize ? cellRoot.fixedSizePx.width : cellText.width + 8
        })
        height = Qt.binding(function() {
            return cellRoot.hasFixedSize ? cellRoot.fixedSizePx.height : cellText.height + 8
        })
    }
    border.color: selected ? "lime" : "transparent"
    border.width: selected ? 3 : 0

    // Content lives in a clipping wrapper: a fixed-size box may be smaller
    // than its text, and the C++ renderer clips to the box. The selection
    // border and resize handles stay outside so they are never clipped.
    Item {
        anchors.fill: parent
        clip: cellRoot.hasFixedSize || cellRoot.resizing

        // Slight background for better visibility (editor only).
        // Kept as first child so shadows and text paint above it.
        Rectangle {
            anchors.fill: parent
            color: "#80000000"  // Semi-transparent black background
            radius: 4
            visible: cellRoot.showBackground
        }

        // Crisp offset (1 copy) / outline (8 copies) shadow — drawn beneath the text
        Repeater {
            model: (!cellRoot.shadowEnabled || cellRoot.shadowType === 1) ? []
                 : cellRoot.shadowType === 0 ? [Qt.point(1, 1)]
                 : [Qt.point(-1, -1), Qt.point(0, -1), Qt.point(1, -1), Qt.point(-1, 0),
                    Qt.point(1, 0), Qt.point(-1, 1), Qt.point(0, 1), Qt.point(1, 1)]
            delegate: Text {
                x: cellText.x + modelData.x * cellRoot.shadowPx
                y: cellText.y + modelData.y * cellRoot.shadowPx
                text: cellRoot.displayText
                font: cellRoot.cellFont
                color: cellRoot.shadowColor
                opacity: cellRoot.shadowOpacity
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignTop
                wrapMode: Text.NoWrap
            }
        }

        // Cell content. The text block keeps its measured size (label/value
        // lines stay centered relative to each other); alignment places the
        // block inside the box — same rules as cellGeometry() in C++. For
        // auto-sized boxes every branch degenerates to the legacy (4,4) inset.
        Text {
            id: cellText
            x: cellRoot.hAlign === 1 ? (cellRoot.width - width) / 2
             : cellRoot.hAlign === 2 ? cellRoot.width - 4 - width
             : 4
            y: cellRoot.vAlign === 1 ? (cellRoot.height - height) / 2
             : cellRoot.vAlign === 2 ? cellRoot.height - 4 - height
             : 4
            text: cellRoot.styledDisplayText()
            textFormat: Text.StyledText
            font: cellFont
            color: cellValueColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignTop
            wrapMode: Text.NoWrap
            // In blurred mode MultiEffect renders the text (plus its shadow);
            // an invisible Text still computes its size, so layout is unaffected
            visible: !(cellRoot.shadowEnabled && cellRoot.shadowType === 1)
        }

        // Soft blurred shadow (renders the text and its blurred shadow together)
        MultiEffect {
            source: cellText
            anchors.fill: cellText
            visible: cellRoot.shadowEnabled && cellRoot.shadowType === 1
            shadowEnabled: true
            shadowColor: cellRoot.shadowColor
            shadowOpacity: cellRoot.shadowOpacity
            shadowHorizontalOffset: cellRoot.shadowPx
            shadowVerticalOffset: cellRoot.shadowPx
            blurMax: 64
            shadowBlur: Math.min(1.0, cellRoot.shadowPx * 2 / 64)
            autoPaddingEnabled: true
        }
    }

    // Custom property indicators
    Row {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 2
        spacing: 2

        // Custom font indicator
        Rectangle {
            width: 8
            height: 8
            radius: 4
            color: "cyan"
            visible: hasCustomFont

            ToolTip.visible: customFontMouseArea.containsMouse
            ToolTip.text: "Custom font"

            MouseArea {
                id: customFontMouseArea
                anchors.fill: parent
                hoverEnabled: true
            }
        }

        // Custom color indicator (label and/or value color)
        Rectangle {
            width: 8
            height: 8
            radius: 4
            color: "magenta"
            visible: hasCustomLabelColor || hasCustomValueColor

            ToolTip.visible: customColorMouseArea.containsMouse
            ToolTip.text: "Custom color"

            MouseArea {
                id: customColorMouseArea
                anchors.fill: parent
                hoverEnabled: true
            }
        }

        // Custom shadow indicator
        Rectangle {
            width: 8
            height: 8
            radius: 4
            color: "orange"
            visible: hasCustomShadow

            ToolTip.visible: customShadowMouseArea.containsMouse
            ToolTip.text: "Custom shadow"

            MouseArea {
                id: customShadowMouseArea
                anchors.fill: parent
                hoverEnabled: true
            }
        }
    }

    // Drag and selection handler
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor

        // Enable drag
        drag.target: cellRoot
        drag.axis: Drag.XAndYAxis

        onClicked: {
            cellRoot.clicked()
        }

        onReleased: {
            if (drag.active) {
                // Calculate normalized position relative to parent container
                // Parent is the cellContainer in InteractiveOverlayPreview
                var containerWidth = cellRoot.parent.width
                var containerHeight = cellRoot.parent.height

                var finalX = cellRoot.x
                var finalY = cellRoot.y

                // Apply snap-to-grid if enabled
                if (generator && generator.snapToGrid) {
                    // Calculate grid spacing scaled to current container size
                    var scaleX = generator.templateWidth > 0 ? containerWidth / generator.templateWidth : 1.0
                    var scaleY = generator.templateHeight > 0 ? containerHeight / generator.templateHeight : 1.0
                    var spacingX = generator.gridSpacing * scaleX
                    var spacingY = generator.gridSpacing * scaleY

                    // Snap to nearest grid intersection. Same guard as the
                    // resize handles: a zero/degenerate spacing would divide
                    // by zero and commit NaN as the cell position.
                    if (spacingX >= 1 && spacingY >= 1) {
                        finalX = Math.round(finalX / spacingX) * spacingX
                        finalY = Math.round(finalY / spacingY) * spacingY
                    }
                }

                // Clamp position to boundaries [0, container size - cell size]
                var clampedX = Math.max(0, Math.min(finalX, containerWidth - cellRoot.width))
                var clampedY = Math.max(0, Math.min(finalY, containerHeight - cellRoot.height))

                // Convert the box's top-left back to the normalized ANCHOR
                // point (position + alignment offset) — the generator stores
                // anchors, not box corners
                var normalizedX = (clampedX + cellRoot.anchorOffsetX()) / containerWidth
                var normalizedY = (clampedY + cellRoot.anchorOffsetY()) / containerHeight

                // Commit (the handler updates the model synchronously), then
                // let the bindings place the box at the committed position —
                // this also snaps it visually to the clamped/gridded spot.
                cellRoot.positionChanged(Qt.point(normalizedX, normalizedY))
                cellRoot.restoreGeometryBindings()
            }
        }
    }

    // Resize handles (4 corners + 4 edges) on the selected cell. Dragging a
    // handle keeps the opposite edge fixed and commits on release, like the
    // position drag: the new box becomes a fixed size plus a re-derived
    // anchor, so the cell doesn't jump whatever its alignment is.
    Repeater {
        model: cellRoot.selected && !cellRoot.dragging
             ? [{xe: -1, ye: -1}, {xe: 0, ye: -1}, {xe: 1, ye: -1},
                {xe: -1, ye:  0},                  {xe: 1, ye:  0},
                {xe: -1, ye:  1}, {xe: 0, ye:  1}, {xe: 1, ye:  1}]
             : []
        delegate: Rectangle {
            readonly property int xe: modelData.xe   // -1 left, 0 none, 1 right
            readonly property int ye: modelData.ye   // -1 top, 0 none, 1 bottom

            width: 10
            height: 10
            radius: 2
            color: "lime"
            border.color: "#004000"
            border.width: 1
            x: (xe < 0 ? 0 : xe > 0 ? cellRoot.width : cellRoot.width / 2) - width / 2
            y: (ye < 0 ? 0 : ye > 0 ? cellRoot.height : cellRoot.height / 2) - height / 2
            z: 10

            MouseArea {
                anchors.fill: parent
                anchors.margins: -4   // easier grab
                preventStealing: true
                cursorShape: xe === 0 ? Qt.SizeVerCursor
                           : ye === 0 ? Qt.SizeHorCursor
                           : (xe === ye ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor)

                // Press state, all in container coordinates (the handle moves
                // with the box while dragging, so its own coords are unstable)
                property point pressPos
                property rect pressRect

                onPressed: function(mouse) {
                    pressPos = mapToItem(cellRoot.parent, mouse.x, mouse.y)
                    pressRect = Qt.rect(cellRoot.x, cellRoot.y,
                                        cellRoot.width, cellRoot.height)
                    cellRoot.resizeXe = xe
                    cellRoot.resizeYe = ye
                    cellRoot.resizing = true
                }

                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    var p = mapToItem(cellRoot.parent, mouse.x, mouse.y)
                    var dx = p.x - pressPos.x
                    var dy = p.y - pressPos.y

                    var e = cellRoot.clampResizeEdges(
                                pressRect.x + (xe < 0 ? dx : 0),
                                pressRect.y + (ye < 0 ? dy : 0),
                                pressRect.x + pressRect.width + (xe > 0 ? dx : 0),
                                pressRect.y + pressRect.height + (ye > 0 ? dy : 0))

                    cellRoot.x = e.left
                    cellRoot.y = e.top
                    cellRoot.width = e.right - e.left
                    cellRoot.height = e.bottom - e.top
                }

                onReleased: {
                    // Snap the moving edges to the grid (release-time, like the
                    // position drag); the fixed opposite edge is never touched
                    var left = cellRoot.x
                    var top = cellRoot.y
                    var right = left + cellRoot.width
                    var bottom = top + cellRoot.height

                    if (cellRoot.generator && cellRoot.generator.snapToGrid) {
                        var container = cellRoot.parent
                        var generator = cellRoot.generator
                        var scaleX = generator.templateWidth > 0 ? container.width / generator.templateWidth : 1.0
                        var scaleY = generator.templateHeight > 0 ? container.height / generator.templateHeight : 1.0
                        var spacingX = generator.gridSpacing * scaleX
                        var spacingY = generator.gridSpacing * scaleY

                        if (spacingX >= 1 && spacingY >= 1) {
                            var e = cellRoot.clampResizeEdges(
                                        xe < 0 ? Math.round(left / spacingX) * spacingX : left,
                                        ye < 0 ? Math.round(top / spacingY) * spacingY : top,
                                        xe > 0 ? Math.round(right / spacingX) * spacingX : right,
                                        ye > 0 ? Math.round(bottom / spacingY) * spacingY : bottom)

                            cellRoot.x = e.left
                            cellRoot.y = e.top
                            cellRoot.width = e.right - e.left
                            cellRoot.height = e.bottom - e.top
                        }
                    }

                    cellRoot.resizing = false
                    cellRoot.resizeXe = 0
                    cellRoot.resizeYe = 0
                    cellRoot.geometryCommitted(Qt.rect(cellRoot.x, cellRoot.y,
                                                       cellRoot.width, cellRoot.height))
                    // Model updated synchronously by the handler: the restored
                    // bindings evaluate straight to the committed box.
                    cellRoot.restoreGeometryBindings()
                }
            }
        }
    }

    // Visual feedback on hover and drag
    states: [
        State {
            name: "overlapping"
            when: overlapping && !selected && !mouseArea.drag.active
            PropertyChanges {
                target: cellRoot
                border.color: "red"
                border.width: 3
            }
        },
        State {
            name: "hovered"
            when: mouseArea.containsMouse && !selected && !overlapping && !mouseArea.drag.active
            PropertyChanges {
                target: cellRoot
                border.color: "#40FFFFFF"  // Subtle white border on hover
                border.width: 2
            }
        },
        State {
            name: "dragging"
            when: mouseArea.drag.active
            PropertyChanges {
                target: cellRoot
                opacity: 0.7
                border.color: overlapping ? "red" : "cyan"
                border.width: 3
            }
        }
    ]

    transitions: Transition {
        PropertyAnimation {
            properties: "border.color,border.width,opacity"
            duration: 150
        }
    }
}
