import QtQuick

/**
 * Interactive overlay preview component
 *
 * Displays the overlay background with draggable, selectable cells.
 * Replaces the static image preview with an interactive editor.
 */
Item {
    id: interactivePreview

    // Properties
    property var generator: null
    property var dive: null
    property real timePoint: 0.0
    property string selectedCellId: ""
    property var cellModel: null
    property var overlappingCells: ({})  // Map of cellId -> bool (true if overlapping)
    property alias cellRepeater: cellRepeater  // Expose repeater for property access
    property int detectOverlapsCount: 0

    // Alignment guides properties
    property var alignmentGuides: []  // Array of alignment guide objects
    property int alignmentThreshold: 5  // Pixels within which cells are considered aligned

    // Counter-based drag detection (QML bindings can't track dependencies in loops)
    property int draggingCellCount: 0  // Incremented/decremented by cell delegates
    property bool anyDragging: draggingCellCount > 0
    property int resizingCellCount: 0  // Same, for resize-handle drags
    property bool anyResizing: resizingCellCount > 0

    // Signals
    signal cellSelected(string cellId)
    signal cellDeselected()
    signal cellPositionChanged(string cellId, point newPosition)
    // Resize-handle commit: normalized anchor + normalized fixed size
    signal cellGeometryChanged(string cellId, point newPosition, size newSize)

    // Function to check if two rectangles overlap
    function rectsOverlap(r1, r2) {
        return !(r1.x + r1.width <= r2.x ||
                 r2.x + r2.width <= r1.x ||
                 r1.y + r1.height <= r2.y ||
                 r2.y + r2.height <= r1.y)
    }

    // Function to detect all overlapping cells
    function detectOverlaps() {
        detectOverlapsCount++
        var overlaps = {}
        var cells = []

        // Gather all cell bounds from the repeater
        for (var i = 0; i < cellRepeater.count; i++) {
            var cell = cellRepeater.itemAt(i)
            if (cell && cell.cellVisible) {
                cells.push({
                    id: cell.cellId,
                    bounds: Qt.rect(cell.x, cell.y, cell.width, cell.height)
                })
            }
        }

        // Check each cell against all others
        for (var i = 0; i < cells.length; i++) {
            var hasOverlap = false
            for (var j = 0; j < cells.length; j++) {
                if (i !== j && rectsOverlap(cells[i].bounds, cells[j].bounds)) {
                    hasOverlap = true
                    break
                }
            }
            overlaps[cells[i].id] = hasOverlap
        }

        overlappingCells = overlaps
    }

    // Function to detect alignment between the active (dragging or resizing)
    // cell and other cells. During a resize only the moving edges produce
    // guides (the opposite edge is pinned, and centers shift at half speed).
    function detectAlignments() {
        var guides = []

        // Find the active cell
        var draggingCell = null
        var resizeMode = false
        for (var i = 0; i < cellRepeater.count; i++) {
            var cell = cellRepeater.itemAt(i)
            if (cell && (cell.dragging || cell.resizing)) {
                draggingCell = cell
                resizeMode = cell.resizing
                break
            }
        }

        if (!draggingCell) {
            alignmentGuides = []
            return
        }

        // Scale threshold based on current preview size vs template size
        // This ensures alignment detection works consistently regardless of zoom level
        var scaleX = (generator && generator.templateWidth > 0) ?
            cellContainer.width / generator.templateWidth : 1.0
        var threshold = alignmentThreshold * Math.max(scaleX, 0.5)  // Ensure min threshold
        var dc = draggingCell  // Shorthand

        // Check against all other visible cells
        for (var j = 0; j < cellRepeater.count; j++) {
            var other = cellRepeater.itemAt(j)
            if (!other || !other.cellVisible || other === draggingCell) continue

            // Top edge alignment
            if ((!resizeMode || dc.resizeYe < 0) && Math.abs(dc.y - other.y) < threshold) {
                guides.push({
                    type: "horizontal",
                    y: other.y,
                    x1: Math.min(dc.x, other.x),
                    x2: Math.max(dc.x + dc.width, other.x + other.width)
                })
            }

            // Bottom edge alignment
            if ((!resizeMode || dc.resizeYe > 0) && Math.abs((dc.y + dc.height) - (other.y + other.height)) < threshold) {
                guides.push({
                    type: "horizontal",
                    y: other.y + other.height,
                    x1: Math.min(dc.x, other.x),
                    x2: Math.max(dc.x + dc.width, other.x + other.width)
                })
            }

            // Left edge alignment
            if ((!resizeMode || dc.resizeXe < 0) && Math.abs(dc.x - other.x) < threshold) {
                guides.push({
                    type: "vertical",
                    x: other.x,
                    y1: Math.min(dc.y, other.y),
                    y2: Math.max(dc.y + dc.height, other.y + other.height)
                })
            }

            // Right edge alignment
            if ((!resizeMode || dc.resizeXe > 0) && Math.abs((dc.x + dc.width) - (other.x + other.width)) < threshold) {
                guides.push({
                    type: "vertical",
                    x: other.x + other.width,
                    y1: Math.min(dc.y, other.y),
                    y2: Math.max(dc.y + dc.height, other.y + other.height)
                })
            }

            // Center-X alignment (vertical centers aligned)
            var dcCenterX = dc.x + dc.width / 2
            var otherCenterX = other.x + other.width / 2
            if (!resizeMode && Math.abs(dcCenterX - otherCenterX) < threshold) {
                guides.push({
                    type: "vertical-center",
                    x: otherCenterX,
                    y1: Math.min(dc.y, other.y),
                    y2: Math.max(dc.y + dc.height, other.y + other.height)
                })
            }

            // Center-Y alignment (horizontal centers aligned)
            var dcCenterY = dc.y + dc.height / 2
            var otherCenterY = other.y + other.height / 2
            if (!resizeMode && Math.abs(dcCenterY - otherCenterY) < threshold) {
                guides.push({
                    type: "horizontal-center",
                    y: otherCenterY,
                    x1: Math.min(dc.x, other.x),
                    x2: Math.max(dc.x + dc.width, other.x + other.width)
                })
            }
        }

        alignmentGuides = guides
    }

    // Background image
    Rectangle {
        anchors.fill: parent
        color: palette.dark

        Image {
            id: backgroundImage
            anchors.fill: parent
            source: {
                if (!generator) return ""
                var path = generator.templatePath

                // Handle different path formats:
                // 1. Qt resource paths (:/...) should be converted to qrc:/ format
                // 2. Absolute filesystem paths (/...) use Qt.url() for proper encoding
                // 3. Already prefixed paths should be used as-is
                if (path.startsWith(":/")) {
                    path = "qrc" + path
                } else if (path.startsWith("/")) {
                    // Use Qt.url() which properly handles spaces and special characters
                    path = Qt.url("file://" + path)
                } else if (path.startsWith("file://") || path.startsWith("qrc:/")) {
                    // Already properly prefixed - use as-is
                    path = path
                } else if (path.length >= 2 && path[1] === ':') {
                    // Windows drive letter path (e.g., "Z:/file.png" or "C:\file.png")
                    path = "file:///" + path
                }

                return path
            }
            fillMode: Image.PreserveAspectFit
            asynchronous: true

            onStatusChanged: {
                if (status === Image.Error) {
                    console.error("Failed to load background image:", source)
                } else if (status === Image.Ready) {
                }
            }

            // Calculate the actual rendered size of the image
            // This is needed to properly position cells relative to the image
            property size actualImageSize: {
                if (status !== Image.Ready) return Qt.size(0, 0)

                var aspectRatio = sourceSize.width / sourceSize.height
                var containerAspectRatio = width / height

                if (aspectRatio > containerAspectRatio) {
                    // Image is wider - constrained by width
                    return Qt.size(width, width / aspectRatio)
                } else {
                    // Image is taller - constrained by height
                    return Qt.size(height * aspectRatio, height)
                }
            }

            property point actualImageOffset: {
                return Qt.point(
                    (width - actualImageSize.width) / 2,
                    (height - actualImageSize.height) / 2
                )
            }

            // Container for cells - positioned over the actual image area
            Item {
                id: cellContainer
                x: backgroundImage.actualImageOffset.x
                y: backgroundImage.actualImageOffset.y
                width: backgroundImage.actualImageSize.width
                height: backgroundImage.actualImageSize.height

                // Grid overlay visualization
                Canvas {
                    id: gridCanvas
                    anchors.fill: parent
                    visible: generator && generator.showGrid
                    z: 0  // Below cells

                    onVisibleChanged: requestPaint()
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()

                    Connections {
                        target: generator
                        function onGridSpacingChanged() { gridCanvas.requestPaint() }
                        function onShowGridChanged() { gridCanvas.requestPaint() }
                    }

                    onPaint: {
                        if (!generator) return

                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)

                        // Calculate grid spacing scaled to current container size
                        var scaleX = generator.templateWidth > 0 ? width / generator.templateWidth : 1.0
                        var scaleY = generator.templateHeight > 0 ? height / generator.templateHeight : 1.0
                        var spacingX = generator.gridSpacing * scaleX
                        var spacingY = generator.gridSpacing * scaleY

                        // Guard against infinite loop: skip if spacing is too small
                        if (spacingX < 1 || spacingY < 1 || width < 1 || height < 1) {
                            return
                        }

                        // Draw grid lines
                        ctx.strokeStyle = "rgba(255, 255, 255, 0.3)"
                        ctx.lineWidth = 1

                        // Vertical lines
                        for (var x = 0; x <= width; x += spacingX) {
                            ctx.beginPath()
                            ctx.moveTo(x, 0)
                            ctx.lineTo(x, height)
                            ctx.stroke()
                        }

                        // Horizontal lines
                        for (var y = 0; y <= height; y += spacingY) {
                            ctx.beginPath()
                            ctx.moveTo(0, y)
                            ctx.lineTo(width, y)
                            ctx.stroke()
                        }
                    }
                }

                // Alignment guides overlay
                Canvas {
                    id: alignmentCanvas
                    anchors.fill: parent
                    visible: (interactivePreview.anyDragging || interactivePreview.anyResizing)
                             && interactivePreview.alignmentGuides.length > 0
                    z: 0.5  // Between grid and cells

                    // Repaint when visibility changes
                    onVisibleChanged: requestPaint()

                    // Repaint when alignment guides change
                    Connections {
                        target: interactivePreview
                        function onAlignmentGuidesChanged() {
                            alignmentCanvas.requestPaint()
                        }
                    }

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)

                        var guides = interactivePreview.alignmentGuides
                        for (var i = 0; i < guides.length; i++) {
                            var guide = guides[i]

                            // Choose color based on alignment type
                            if (guide.type.includes("center")) {
                                ctx.strokeStyle = "rgba(255, 215, 0, 0.9)"  // Gold for center alignment
                            } else {
                                ctx.strokeStyle = "rgba(0, 255, 255, 0.9)"  // Cyan for edge alignment
                            }
                            ctx.lineWidth = 2

                            ctx.beginPath()
                            if (guide.type.startsWith("horizontal")) {
                                ctx.moveTo(guide.x1, guide.y)
                                ctx.lineTo(guide.x2, guide.y)
                            } else {
                                ctx.moveTo(guide.x, guide.y1)
                                ctx.lineTo(guide.x, guide.y2)
                            }
                            ctx.stroke()
                        }
                    }
                }

                // Repeater for cells
                Repeater {
                    id: cellRepeater
                    model: cellModel

                    delegate: OverlayCell {
                        // Cell properties from model
                        cellId: model.cellId
                        cellType: model.cellType
                        cellVisible: model.visible
                        cellFont: {
                            var scaleX = interactivePreview.generator && interactivePreview.generator.templateWidth > 0
                                ? cellContainer.width / interactivePreview.generator.templateWidth : 1.0
                            var f = model.font
                            // 1.8 matches the C++ getScaledFontSize scale factor in renderCellBasedOverlay
                            var scaledSize = f.pointSize * 1.8 * scaleX
                            return Qt.font({
                                family: f.family,
                                pointSize: Math.max(1, Math.round(scaledSize)),
                                bold: f.bold,
                                italic: f.italic
                            })
                        }
                        cellLabelColor: model.labelColor
                        cellValueColor: model.valueColor
                        displayText: model.displayText
                        // Scale pixel size to current container size
                        // calculatedSize is in pixels, scale proportionally to fit container
                        cellCalculatedSize: {
                            var scaleX = interactivePreview.generator && interactivePreview.generator.templateWidth > 0 ? cellContainer.width / interactivePreview.generator.templateWidth : 1.0
                            var scaleY = interactivePreview.generator && interactivePreview.generator.templateHeight > 0 ? cellContainer.height / interactivePreview.generator.templateHeight : 1.0
                            return Qt.size(
                                model.calculatedSize.width * scaleX,
                                model.calculatedSize.height * scaleY
                            )
                        }
                        hasCustomFont: model.hasCustomFont
                        hasCustomLabelColor: model.hasCustomLabelColor
                        hasCustomValueColor: model.hasCustomValueColor

                        // Shadow settings from model
                        shadowEnabled: model.shadowEnabled
                        shadowType: model.shadowType
                        shadowColor: model.shadowColor
                        shadowOpacity: model.shadowOpacity
                        shadowPx: {
                            var scaleX = interactivePreview.generator && interactivePreview.generator.templateWidth > 0
                                ? cellContainer.width / interactivePreview.generator.templateWidth : 1.0
                            // 1.8 matches the C++ shadow scale in renderCellBasedOverlay
                            return Math.max(1, model.shadowSize * 1.8 * scaleX)
                        }
                        hasCustomShadow: model.hasCustomShadow

                        // v1.2 geometry: anchor alignment + optional fixed size
                        // (normalized in the model, scaled to container px here)
                        hAlign: model.hAlign
                        vAlign: model.vAlign
                        hasFixedSize: model.hasFixedSize
                        fixedSizePx: Qt.size(
                            model.fixedSize.width * cellContainer.width,
                            model.fixedSize.height * cellContainer.height)

                        // Selection state
                        selected: model.cellId === interactivePreview.selectedCellId

                        // Overlap state
                        overlapping: interactivePreview.overlappingCells[model.cellId] || false

                        // Generator reference for snap-to-grid
                        generator: interactivePreview.generator

                        // Box position is derived inside OverlayCell from this
                        // anchor point + alignment (so the cell can re-install
                        // its own x/y bindings after a drag or resize).
                        cellPosition: model.position

                        // Trigger overlap and alignment detection when position or size changes
                        onXChanged: {
                            Qt.callLater(interactivePreview.detectOverlaps)
                            Qt.callLater(interactivePreview.detectAlignments)
                        }
                        onYChanged: {
                            Qt.callLater(interactivePreview.detectOverlaps)
                            Qt.callLater(interactivePreview.detectAlignments)
                        }
                        onWidthChanged: {
                            Qt.callLater(interactivePreview.detectOverlaps)
                            Qt.callLater(interactivePreview.detectAlignments)
                        }
                        onHeightChanged: {
                            Qt.callLater(interactivePreview.detectOverlaps)
                            Qt.callLater(interactivePreview.detectAlignments)
                        }

                        // Track drag state for grid visibility
                        onDraggingChanged: {
                            if (dragging) {
                                interactivePreview.draggingCellCount++
                            } else {
                                interactivePreview.draggingCellCount--
                                interactivePreview.alignmentGuides = []
                            }
                        }

                        // Track resize state the same way (drives the
                        // alignment-guide canvas during handle drags)
                        onResizingChanged: {
                            if (resizing) {
                                interactivePreview.resizingCellCount++
                            } else {
                                interactivePreview.resizingCellCount--
                                interactivePreview.alignmentGuides = []
                            }
                        }

                        // Ensure counters are decremented if delegate is destroyed mid-drag
                        // (e.g., when updateCellModel() recreates delegates during a drag)
                        Component.onDestruction: {
                            if (dragging) {
                                interactivePreview.draggingCellCount--
                            }
                            if (resizing) {
                                interactivePreview.resizingCellCount--
                            }
                        }

                        // Handle cell selection
                        onClicked: {
                            if (interactivePreview.selectedCellId === model.cellId) {
                                // Clicking the same cell again deselects it
                                interactivePreview.selectedCellId = ""
                                interactivePreview.cellDeselected()
                            } else {
                                interactivePreview.selectedCellId = model.cellId
                                interactivePreview.cellSelected(model.cellId)
                            }
                        }

                        // Drag behavior
                        onPositionChanged: function(newPos) {
                            interactivePreview.cellPositionChanged(model.cellId, newPos)
                        }

                        // Resize-handle commit: convert the box rect (container
                        // px) to a normalized anchor + normalized fixed size
                        onGeometryCommitted: function(newRect) {
                            var ax = newRect.x
                                   + (model.hAlign === 1 ? newRect.width / 2
                                    : model.hAlign === 2 ? newRect.width : 0)
                            var ay = newRect.y
                                   + (model.vAlign === 1 ? newRect.height / 2
                                    : model.vAlign === 2 ? newRect.height : 0)
                            interactivePreview.cellGeometryChanged(
                                model.cellId,
                                Qt.point(ax / cellContainer.width,
                                         ay / cellContainer.height),
                                Qt.size(newRect.width / cellContainer.width,
                                        newRect.height / cellContainer.height))
                        }
                    }
                }
            }
        }
    }

    // Click outside cells to deselect
    MouseArea {
        anchors.fill: parent
        z: -1  // Behind everything

        onClicked: {
            interactivePreview.selectedCellId = ""
            interactivePreview.cellDeselected()
        }
    }

    // Helper text when no dive is loaded
    Text {
        anchors.centerIn: parent
        text: dive ? "" : "Load a dive to preview overlay"
        color: palette.windowText
        font.pointSize: 14
        visible: !dive
    }
}
