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

    // Signals
    signal cellSelected(string cellId)
    signal cellDeselected()
    signal cellPositionChanged(string cellId, point newPosition)

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
                // 2. Absolute filesystem paths (/...) need file:// prefix
                // 3. Already prefixed paths should be used as-is
                if (path.startsWith(":/")) {
                    path = "qrc" + path
                } else if (path.startsWith("/")) {
                    // Absolute filesystem path - add file:// prefix
                    path = "file://" + path
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

                // Repeater for cells
                Repeater {
                    model: cellModel

                    delegate: OverlayCell {
                        // Cell properties from model
                        cellId: model.cellId
                        cellType: model.cellType
                        cellVisible: model.visible
                        cellFont: model.font
                        cellTextColor: model.textColor
                        displayText: model.displayText
                        // Convert normalized size to pixels
                        cellCalculatedSize: Qt.size(
                            model.calculatedSize.width * cellContainer.width,
                            model.calculatedSize.height * cellContainer.height
                        )
                        hasCustomFont: model.hasCustomFont
                        hasCustomColor: model.hasCustomColor

                        // Selection state
                        selected: model.cellId === interactivePreview.selectedCellId

                        // Position based on normalized coordinates
                        // Convert normalized (0.0-1.0) to actual pixel position
                        x: model.position.x * cellContainer.width
                        y: model.position.y * cellContainer.height

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

                        // Drag behavior will be added in Phase 3
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
