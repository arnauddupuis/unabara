#ifndef CELL_DATA_H
#define CELL_DATA_H

#include <QString>
#include <QPointF>
#include <QSizeF>
#include <QFont>
#include <QColor>
#include <QJsonObject>

namespace Unabara {

/**
 * @brief Type of telemetry data displayed in a cell
 */
enum class CellType {
    Depth,
    Temperature,
    Time,
    NDL,              // No Decompression Limit
    TTS,              // Time To Surface (decompression)
    Pressure,         // Tank pressure
    PO2Cell1,         // CCR oxygen sensor 1
    PO2Cell2,         // CCR oxygen sensor 2
    PO2Cell3,         // CCR oxygen sensor 3
    CompositePO2,     // CCR composite PO2
    CNS,              // CNS oxygen toxicity (percent)
    MeanDepth,        // Average depth of the dive (static, reported by the dive computer)
    MaxDepth,         // Maximum depth reached so far (running max, like the DC's MAX field)
    Gas,              // Currently breathed gas mix (from gas switches)
    StopDepth,        // Current deco stop depth ("STOP")
    StopTime,         // Time at the current deco stop ("TIME")
    Unknown
};

/**
 * @brief Style of the text shadow rendered behind cell text
 */
enum class ShadowType {
    Offset,           // Crisp copy of the text offset down-right
    Blurred,          // Soft gaussian-like halo
    Outline           // Text redrawn at 8 surrounding offsets (contour)
};

/**
 * @brief Horizontal anchor/alignment of a cell (v1.2 geometry)
 *
 * Picks which edge/center of the cell box pins to the cell's normalized
 * position. Left is the legacy behavior (position = top-left corner).
 */
enum class HAlign {
    Left,
    Center,
    Right
};

/**
 * @brief Vertical anchor/alignment of a cell (v1.2 geometry)
 */
enum class VAlign {
    Top,
    Middle,
    Bottom
};

/**
 * @brief Default cell geometry shared by CellData, OverlayGenerator and the QML preview
 *
 * Absent keys in a .utp file mean these values, which reproduce the pre-1.2
 * rendering exactly: box anchored by its top-left corner, auto-sized from
 * the measured text.
 */
namespace GeometryDefaults {
    constexpr HAlign hAlign = HAlign::Left;
    constexpr VAlign vAlign = VAlign::Top;
}

/**
 * @brief Default shadow values shared by CellData, OverlayGenerator and OverlayTemplate
 *
 * The "no hasCustom flag = inherit global" model only works when all three
 * layers agree on defaults before the first global propagation.
 */
namespace ShadowDefaults {
    constexpr bool enabled = false;
    constexpr ShadowType type = ShadowType::Offset;
    inline QColor color() { return QColor(0, 0, 0); }
    constexpr int size = 2;
    constexpr double opacity = 0.7;
}

/**
 * @brief Default text color shared by CellData, OverlayGenerator and OverlayTemplate
 *
 * Applies to both the label and the value color.
 */
namespace ColorDefaults {
    inline QColor text() { return QColor(Qt::white); }
}

/**
 * @brief Represents a single data cell in the overlay
 *
 * Each cell displays one type of telemetry data and can be positioned
 * and styled independently. Positions are stored as normalized coordinates
 * (0.0-1.0) relative to the template background dimensions.
 */
class CellData {
public:
    CellData();
    CellData(const QString& cellId, CellType cellType);

    // Getters
    QString cellId() const { return m_cellId; }
    CellType cellType() const { return m_cellType; }
    QPointF position() const { return m_position; }
    bool visible() const { return m_visible; }
    QFont font() const { return m_font; }
    QColor labelColor() const { return m_labelColor; }
    QColor valueColor() const { return m_valueColor; }
    QSizeF calculatedSize() const { return m_calculatedSize; }
    bool hasCustomFont() const { return m_hasCustomFont; }
    bool hasCustomLabelColor() const { return m_hasCustomLabelColor; }
    bool hasCustomValueColor() const { return m_hasCustomValueColor; }
    bool hasCustomShowLabel() const { return m_hasCustomShowLabel; }
    int tankIndex() const { return m_tankIndex; }
    bool showLabel() const { return m_showLabel; }
    bool shadowEnabled() const { return m_shadowEnabled; }
    ShadowType shadowType() const { return m_shadowType; }
    QColor shadowColor() const { return m_shadowColor; }
    int shadowSize() const { return m_shadowSize; }
    double shadowOpacity() const { return m_shadowOpacity; }
    bool hasCustomShadow() const { return m_hasCustomShadow; }
    HAlign hAlign() const { return m_hAlign; }
    VAlign vAlign() const { return m_vAlign; }
    QSizeF fixedSize() const { return m_fixedSize; }
    // An invalid/empty size means auto-size from content (legacy behavior)
    bool hasFixedSize() const { return m_fixedSize.width() > 0.0 && m_fixedSize.height() > 0.0; }

    // Setters
    void setCellId(const QString& id) { m_cellId = id; }
    void setCellType(CellType type) { m_cellType = type; }
    void setPosition(const QPointF& pos) { m_position = pos; }
    void setVisible(bool visible) { m_visible = visible; }
    void setFont(const QFont& font, bool isCustom = true);
    void setLabelColor(const QColor& color, bool isCustom = true);
    void setValueColor(const QColor& color, bool isCustom = true);
    void setShowLabel(bool show, bool isCustom = true);
    void setShadowEnabled(bool enabled, bool isCustom = true);
    void setShadowType(ShadowType type, bool isCustom = true);
    void setShadowColor(const QColor& color, bool isCustom = true);
    void setShadowSize(int size, bool isCustom = true);
    void setShadowOpacity(double opacity, bool isCustom = true);
    void setCalculatedSize(const QSizeF& size) { m_calculatedSize = size; }
    void setTankIndex(int index) { m_tankIndex = index; }
    void setHAlign(HAlign align) { m_hAlign = align; }
    void setVAlign(VAlign align) { m_vAlign = align; }
    void setFixedSize(const QSizeF& size) { m_fixedSize = size; }
    void clearFixedSize() { m_fixedSize = QSizeF(); }

    // Reset custom properties to inherit from global
    void resetFont() { m_hasCustomFont = false; }
    void resetShowLabel() { m_hasCustomShowLabel = false; }
    void resetShadow() { m_hasCustomShadow = false; }

    // Serialization
    QJsonObject toJson() const;
    static CellData fromJson(const QJsonObject& json);

    // Helper methods
    static QString cellTypeToString(CellType type);
    static CellType cellTypeFromString(const QString& str);
    static QString shadowTypeToString(ShadowType type);
    static ShadowType shadowTypeFromString(const QString& str);
    static QString hAlignToString(HAlign align);
    static HAlign hAlignFromString(const QString& str);
    static QString vAlignToString(VAlign align);
    static VAlign vAlignFromString(const QString& str);

private:
    QString m_cellId;              // Unique identifier (e.g., "depth", "tank_0")
    CellType m_cellType;           // Type of data displayed
    QPointF m_position;            // Normalized position (0.0-1.0)
    bool m_visible;                // Whether cell is shown
    QFont m_font;                  // Cell-specific font
    QColor m_labelColor;           // Cell-specific label ("DEPTH" etc.) text color
    QColor m_valueColor;           // Cell-specific value ("29.3 m" etc.) text color
    QSizeF m_calculatedSize;       // Calculated size based on content and font
    bool m_hasCustomFont;          // True if font differs from global default
    bool m_hasCustomLabelColor;    // True if label color differs from global default
    bool m_hasCustomValueColor;    // True if value color differs from global default
    bool m_showLabel;              // Whether the label row ("DEPTH" etc.) is rendered above the value
    bool m_hasCustomShowLabel;     // True if showLabel differs from global default
    bool m_shadowEnabled;          // Whether a shadow is drawn behind the text
    ShadowType m_shadowType;       // Shadow style (offset, blurred, outline)
    QColor m_shadowColor;          // Shadow color (opacity applied separately at render time)
    int m_shadowSize;              // Shadow size in px (offset distance / blur radius / outline thickness)
    double m_shadowOpacity;        // Shadow opacity (0.0-1.0)
    bool m_hasCustomShadow;        // True if any shadow setting differs from global default
    int m_tankIndex;               // For pressure cells: which tank (0-based), -1 for N/A
    HAlign m_hAlign;               // Which horizontal edge/center of the box pins to position
    VAlign m_vAlign;               // Which vertical edge/center of the box pins to position
    QSizeF m_fixedSize;            // Normalized fixed size; invalid/empty = auto-size from content
};

} // namespace Unabara

#endif // CELL_DATA_H
