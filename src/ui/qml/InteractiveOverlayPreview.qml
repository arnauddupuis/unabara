import QtQuick
import QtQuick.Controls

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

    // Signals
    signal cellSelected(string cellId)
    signal cellDeselected()
    signal cellPositionChanged(string cellId, point newPosition)

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
        console.log(">>> detectOverlaps called, count: ", detectOverlapsCount)
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

    // Background image
    Rectangle {
        anchors.fill: parent
        color: "#5a5a5a"  // Gray background

        Image {
            id: backgroundImage
            anchors.fill: parent
            source: {
                if (!generator) return ""
                var path = generator.templatePath
                console.log("InteractiveOverlayPreview: Loading background image:", path)

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
                }

                console.log("InteractiveOverlayPreview: Final image source:", path)
                return path
            }
            fillMode: Image.PreserveAspectFit
            asynchronous: true

            onStatusChanged: {
                console.log(">>> BG Image status:", status, "(0=Null,1=Ready,2=Loading,3=Error)")
                if (status === Image.Error) {
                    console.error("Failed to load background image:", source)
                } else if (status === Image.Ready) {
                    console.log("Background image loaded successfully, size:", sourceSize.width, "x", sourceSize.height)
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
                    visible: generator && (generator.showGrid || anyDragging)
                    z: 0  // Below cells

                    property bool anyDragging: {
                        for (var i = 0; i < cellRepeater.count; i++) {
                            var cell = cellRepeater.itemAt(i)
                            if (cell && cell.dragging) {
                                return true
                            }
                        }
                        return false
                    }

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

                // Repeater for cells
                Repeater {
                    id: cellRepeater
                    model: cellModel

                    delegate: OverlayCell {
                        // Cell properties from model
                        cellId: model.cellId
                        cellType: model.cellType
                        cellVisible: model.visible
                        cellFont: model.font
                        cellTextColor: model.textColor
                        displayText: model.displayText
                        // Scale pixel size to current container size
                        // calculatedSize is in pixels, scale proportionally to fit container
                        cellCalculatedSize: {
                            var scaleX = root.generator.templateWidth > 0 ? cellContainer.width / root.generator.templateWidth : 1.0
                            var scaleY = root.generator.templateHeight > 0 ? cellContainer.height / root.generator.templateHeight : 1.0
                            return Qt.size(
                                model.calculatedSize.width * scaleX,
                                model.calculatedSize.height * scaleY
                            )
                        }
                        hasCustomFont: model.hasCustomFont
                        hasCustomColor: model.hasCustomColor

                        // Selection state
                        selected: model.cellId === interactivePreview.selectedCellId

                        // Overlap state
                        overlapping: interactivePreview.overlappingCells[model.cellId] || false

                        // Generator reference for snap-to-grid
                        generator: interactivePreview.generator

                        // Position based on normalized coordinates
                        // Convert normalized (0.0-1.0) to actual pixel position
                        x: model.position.x * cellContainer.width
                        y: model.position.y * cellContainer.height

                        // Trigger overlap detection when position or size changes
                        onXChanged: Qt.callLater(interactivePreview.detectOverlaps)
                        onYChanged: Qt.callLater(interactivePreview.detectOverlaps)
                        onWidthChanged: Qt.callLater(interactivePreview.detectOverlaps)
                        onHeightChanged: Qt.callLater(interactivePreview.detectOverlaps)

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
        color: "white"
        font.pointSize: 14
        visible: !dive
    }
}
