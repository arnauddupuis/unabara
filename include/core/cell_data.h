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
    Unknown
};

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
    QColor textColor() const { return m_textColor; }
    QSizeF calculatedSize() const { return m_calculatedSize; }
    bool hasCustomFont() const { return m_hasCustomFont; }
    bool hasCustomColor() const { return m_hasCustomColor; }
    int tankIndex() const { return m_tankIndex; }

    // Setters
    void setCellId(const QString& id) { m_cellId = id; }
    void setCellType(CellType type) { m_cellType = type; }
    void setPosition(const QPointF& pos) { m_position = pos; }
    void setVisible(bool visible) { m_visible = visible; }
    void setFont(const QFont& font, bool isCustom = true);
    void setTextColor(const QColor& color, bool isCustom = true);
    void setCalculatedSize(const QSizeF& size) { m_calculatedSize = size; }
    void setTankIndex(int index) { m_tankIndex = index; }

    // Reset custom properties to inherit from global
    void resetFont() { m_hasCustomFont = false; }
    void resetColor() { m_hasCustomColor = false; }

    // Serialization
    QJsonObject toJson() const;
    static CellData fromJson(const QJsonObject& json);

    // Helper methods
    static QString cellTypeToString(CellType type);
    static CellType cellTypeFromString(const QString& str);

private:
    QString m_cellId;              // Unique identifier (e.g., "depth", "tank_0")
    CellType m_cellType;           // Type of data displayed
    QPointF m_position;            // Normalized position (0.0-1.0)
    bool m_visible;                // Whether cell is shown
    QFont m_font;                  // Cell-specific font
    QColor m_textColor;            // Cell-specific text color
    QSizeF m_calculatedSize;       // Calculated size based on content and font
    bool m_hasCustomFont;          // True if font differs from global default
    bool m_hasCustomColor;         // True if color differs from global default
    int m_tankIndex;               // For pressure cells: which tank (0-based), -1 for N/A
};

} // namespace Unabara

#endif // CELL_DATA_H
