#ifndef OVERLAY_TEMPLATE_H
#define OVERLAY_TEMPLATE_H

#include "include/core/cell_data.h"
#include <QString>
#include <QVector>
#include <QFont>
#include <QColor>
#include <QJsonObject>

namespace Unabara {

/**
 * @brief Complete overlay template configuration
 *
 * Contains all information needed to recreate a custom overlay layout:
 * - Background image and opacity
 * - All cell positions and properties
 * - Default font and color settings
 *
 * Templates can be saved to and loaded from JSON files (.utp format).
 */
class OverlayTemplate {
public:
    OverlayTemplate();

    // Getters
    QString templateName() const { return m_templateName; }
    QString backgroundImagePath() const { return m_backgroundImagePath; }
    double backgroundOpacity() const { return m_backgroundOpacity; }
    QVector<CellData> cells() const { return m_cells; }
    QFont defaultFont() const { return m_defaultFont; }
    QColor defaultLabelColor() const { return m_defaultLabelColor; }
    QColor defaultValueColor() const { return m_defaultValueColor; }
    bool defaultShadowEnabled() const { return m_defaultShadowEnabled; }
    ShadowType defaultShadowType() const { return m_defaultShadowType; }
    QColor defaultShadowColor() const { return m_defaultShadowColor; }
    int defaultShadowSize() const { return m_defaultShadowSize; }
    double defaultShadowOpacity() const { return m_defaultShadowOpacity; }

    // Setters
    void setTemplateName(const QString& name) { m_templateName = name; }
    void setBackgroundImagePath(const QString& path) { m_backgroundImagePath = path; }
    void setBackgroundOpacity(double opacity) { m_backgroundOpacity = opacity; }
    void setCells(const QVector<CellData>& cells) { m_cells = cells; }
    void setDefaultFont(const QFont& font) { m_defaultFont = font; }
    void setDefaultLabelColor(const QColor& color) { m_defaultLabelColor = color; }
    void setDefaultValueColor(const QColor& color) { m_defaultValueColor = color; }
    void setDefaultShadowEnabled(bool enabled) { m_defaultShadowEnabled = enabled; }
    void setDefaultShadowType(ShadowType type) { m_defaultShadowType = type; }
    void setDefaultShadowColor(const QColor& color) { m_defaultShadowColor = color; }
    void setDefaultShadowSize(int size) { m_defaultShadowSize = size; }
    void setDefaultShadowOpacity(double opacity) { m_defaultShadowOpacity = opacity; }

    // Cell management
    void addCell(const CellData& cell);
    void removeCell(const QString& cellId);
    CellData* getCellData(const QString& cellId);
    const CellData* getCellData(const QString& cellId) const;
    bool hasCell(const QString& cellId) const;
    int cellCount() const { return m_cells.size(); }

    // Validation
    bool isValid() const;
    QStringList validate() const;  // Returns list of validation errors

    // Serialization
    QJsonObject toJson() const;
    static OverlayTemplate fromJson(const QJsonObject& json);

    // File I/O
    bool saveToFile(const QString& filePath) const;
    static OverlayTemplate loadFromFile(const QString& filePath, QString* errorMessage = nullptr);

    // Path utilities
    static QString resolveBackgroundImagePath(const QString& templatePath,
                                              const QString& backgroundPath);

private:
    QString m_templateName;
    QString m_backgroundImagePath;
    double m_backgroundOpacity;
    QVector<CellData> m_cells;
    QFont m_defaultFont;
    QColor m_defaultLabelColor;
    QColor m_defaultValueColor;
    bool m_defaultShadowEnabled;
    ShadowType m_defaultShadowType;
    QColor m_defaultShadowColor;
    int m_defaultShadowSize;
    double m_defaultShadowOpacity;

    static const QString TEMPLATE_VERSION;  // Current template format version
};

} // namespace Unabara

#endif // OVERLAY_TEMPLATE_H
