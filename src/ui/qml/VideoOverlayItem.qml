import QtQuick
import QtQuick.Controls

// Draggable / resizable overlay container that anchors itself to its parent
// using normalized [0..1] coordinates. The same `normalizedRect` flows into
// any future burn-in pass without further conversion.
Item {
    id: root

    // -- Inputs ----------------------------------------------------------
    property string imageSourceBase: "image://overlay/at/"
    property real diveTime: 0
    property int refreshTick: 0
    property bool placementVisible: true
    // x,y,w,h all in [0..1] of parent.
    property rect normalizedRect: Qt.rect(0.02, 0.55, 0.30, 0.40)
    property bool editable: true
    property real minNormSize: 0.05
    property color accentColor: "#ff3030"
    property int borderWidth: 3
    property int handleSize: 12

    // -- Outputs ---------------------------------------------------------
    // Fired after a drag or resize gesture completes (mouse released).
    signal layoutCommitted()

    visible: placementVisible

    // Pixel placement derived from normalized rect against parent.
    x: parent ? Math.round(normalizedRect.x * parent.width) : 0
    y: parent ? Math.round(normalizedRect.y * parent.height) : 0
    width:  parent ? Math.round(normalizedRect.width  * parent.width)  : 0
    height: parent ? Math.round(normalizedRect.height * parent.height) : 0

    // The rendered overlay image. Fits inside the item's bounds without
    // distortion so user-chosen aspect doesn't squish the dive computer.
    Image {
        id: overlayImage
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        smooth: true
        asynchronous: true
        // Disable QML's URL cache: we have a content-addressed cache behind
        // the image provider, and URLs change per refresh tick anyway.
        cache: false
        source: root.diveTime >= 0
                ? root.imageSourceBase + root.diveTime.toFixed(2) + "/" + root.refreshTick
                : ""
    }

    // Editor chrome — border + handles + move indicator. Only visible when
    // the parent has flagged this overlay as editable.
    Rectangle {
        id: border
        anchors.fill: parent
        color: "transparent"
        border.color: root.accentColor
        border.width: root.borderWidth
        visible: root.editable
    }

    // ---- Helpers -------------------------------------------------------
    function _clampRect(x, y, w, h) {
        w = Math.max(root.minNormSize, Math.min(1.0, w))
        h = Math.max(root.minNormSize, Math.min(1.0, h))
        x = Math.max(0.0, Math.min(1.0 - w, x))
        y = Math.max(0.0, Math.min(1.0 - h, y))
        return Qt.rect(x, y, w, h)
    }

    function _applyNormalized(x, y, w, h) {
        root.normalizedRect = _clampRect(x, y, w, h)
    }

    // ---- Center drag (move) -------------------------------------------
    MouseArea {
        id: moveArea
        anchors.fill: parent
        anchors.margins: root.handleSize * 1.5
        enabled: root.editable
        cursorShape: Qt.SizeAllCursor
        property real pressNormX: 0
        property real pressNormY: 0
        property real pressMouseSceneX: 0
        property real pressMouseSceneY: 0

        onPressed: (mouse) => {
            pressNormX = root.normalizedRect.x
            pressNormY = root.normalizedRect.y
            const sp = mapToItem(root.parent, mouse.x, mouse.y)
            pressMouseSceneX = sp.x
            pressMouseSceneY = sp.y
        }

        onPositionChanged: (mouse) => {
            if (!pressed || !root.parent) return
            const sp = mapToItem(root.parent, mouse.x, mouse.y)
            const dx = (sp.x - pressMouseSceneX) / root.parent.width
            const dy = (sp.y - pressMouseSceneY) / root.parent.height
            _applyNormalized(pressNormX + dx, pressNormY + dy,
                             root.normalizedRect.width, root.normalizedRect.height)
        }

        onReleased: root.layoutCommitted()
    }

    // Center move indicator — a small "+" / four-arrow chip the user can see
    // and grab when the overlay image fills the entire frame.
    Rectangle {
        anchors.centerIn: parent
        width: root.handleSize * 2
        height: root.handleSize * 2
        radius: width / 2
        color: Qt.rgba(0, 0, 0, 0.45)
        border.color: root.accentColor
        border.width: 2
        visible: root.editable
        Text {
            anchors.centerIn: parent
            text: "✥" // four-arrow-ish glyph; falls back gracefully
            color: root.accentColor
            font.pixelSize: parent.height * 0.7
        }
    }

    // ---- Resize handles -----------------------------------------------
    // One generic handle component reused for all 8 positions. `corner`
    // identifies which edges/corners the drag affects.
    component Handle : Rectangle {
        id: handle
        width: root.handleSize
        height: root.handleSize
        color: root.accentColor
        border.color: "white"
        border.width: 1
        visible: root.editable
        property string corner // "tl","t","tr","r","br","b","bl","l"

        property real pressRectX: 0
        property real pressRectY: 0
        property real pressRectW: 0
        property real pressRectH: 0
        property real pressSceneX: 0
        property real pressSceneY: 0

        MouseArea {
            anchors.fill: parent
            enabled: root.editable
            cursorShape: {
                switch (handle.corner) {
                case "tl": case "br": return Qt.SizeFDiagCursor
                case "tr": case "bl": return Qt.SizeBDiagCursor
                case "t":  case "b":  return Qt.SizeVerCursor
                case "l":  case "r":  return Qt.SizeHorCursor
                }
                return Qt.ArrowCursor
            }

            onPressed: (mouse) => {
                handle.pressRectX = root.normalizedRect.x
                handle.pressRectY = root.normalizedRect.y
                handle.pressRectW = root.normalizedRect.width
                handle.pressRectH = root.normalizedRect.height
                const sp = mapToItem(root.parent, mouse.x, mouse.y)
                handle.pressSceneX = sp.x
                handle.pressSceneY = sp.y
            }

            onPositionChanged: (mouse) => {
                if (!pressed || !root.parent) return
                const sp = mapToItem(root.parent, mouse.x, mouse.y)
                const dxN = (sp.x - handle.pressSceneX) / root.parent.width
                const dyN = (sp.y - handle.pressSceneY) / root.parent.height

                let nx = handle.pressRectX
                let ny = handle.pressRectY
                let nw = handle.pressRectW
                let nh = handle.pressRectH

                const c = handle.corner
                if (c === "tl" || c === "l" || c === "bl") {
                    nx = handle.pressRectX + dxN
                    nw = handle.pressRectW - dxN
                } else if (c === "tr" || c === "r" || c === "br") {
                    nw = handle.pressRectW + dxN
                }
                if (c === "tl" || c === "t" || c === "tr") {
                    ny = handle.pressRectY + dyN
                    nh = handle.pressRectH - dyN
                } else if (c === "bl" || c === "b" || c === "br") {
                    nh = handle.pressRectH + dyN
                }
                root._applyNormalized(nx, ny, nw, nh)
            }

            onReleased: root.layoutCommitted()
        }
    }

    // 4 corners
    Handle { corner: "tl"; x: -width/2;             y: -height/2 }
    Handle { corner: "tr"; x: parent.width - width/2; y: -height/2 }
    Handle { corner: "bl"; x: -width/2;             y: parent.height - height/2 }
    Handle { corner: "br"; x: parent.width - width/2; y: parent.height - height/2 }
    // 4 edge midpoints
    Handle { corner: "t"; x: (parent.width - width)/2;  y: -height/2 }
    Handle { corner: "b"; x: (parent.width - width)/2;  y: parent.height - height/2 }
    Handle { corner: "l"; x: -width/2;                  y: (parent.height - height)/2 }
    Handle { corner: "r"; x: parent.width - width/2;    y: (parent.height - height)/2 }
}
