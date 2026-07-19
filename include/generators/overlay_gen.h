#ifndef OVERLAY_GEN_H
#define OVERLAY_GEN_H

#include <QObject>
#include <QString>
#include <QImage>
#include <QFont>
#include <QColor>
#include <QVector>
#include "include/core/dive_data.h"
#include "include/core/config.h"
#include "include/core/units.h"
#include "include/core/cell_data.h"
#include "include/core/overlay_template.h"
#include "include/generators/i_frame_generator.h"

class OverlayGenerator : public QObject, public IFrameGenerator
{
    Q_OBJECT
    
    Q_PROPERTY(QString templatePath READ templatePath WRITE setTemplatePath NOTIFY templateChanged)
    Q_PROPERTY(int templateWidth READ templateWidth NOTIFY templateChanged)
    Q_PROPERTY(int templateHeight READ templateHeight NOTIFY templateChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY textColorChanged)
    Q_PROPERTY(bool showLabel READ showLabel WRITE setShowLabel NOTIFY showLabelChanged)
    Q_PROPERTY(double backgroundOpacity READ backgroundOpacity WRITE setBackgroundOpacity NOTIFY backgroundOpacityChanged)
    Q_PROPERTY(bool showDepth READ showDepth WRITE setShowDepth NOTIFY showDepthChanged)
    Q_PROPERTY(bool showTemperature READ showTemperature WRITE setShowTemperature NOTIFY showTemperatureChanged)
    Q_PROPERTY(bool showNDL READ showNDL WRITE setShowNDL NOTIFY showNDLChanged)
    Q_PROPERTY(bool showPressure READ showPressure WRITE setShowPressure NOTIFY showPressureChanged)
    Q_PROPERTY(bool showTime READ showTime WRITE setShowTime NOTIFY showTimeChanged)
    Q_PROPERTY(bool showCNS READ showCNS WRITE setShowCNS NOTIFY showCNSChanged)
    Q_PROPERTY(bool showMeanDepth READ showMeanDepth WRITE setShowMeanDepth NOTIFY showMeanDepthChanged)
    Q_PROPERTY(bool showMaxDepth READ showMaxDepth WRITE setShowMaxDepth NOTIFY showMaxDepthChanged)
    Q_PROPERTY(bool showGas READ showGas WRITE setShowGas NOTIFY showGasChanged)

    // CCR properties
    Q_PROPERTY(bool showPO2Cell1 READ showPO2Cell1 WRITE setShowPO2Cell1 NOTIFY showPO2Cell1Changed)
    Q_PROPERTY(bool showPO2Cell2 READ showPO2Cell2 WRITE setShowPO2Cell2 NOTIFY showPO2Cell2Changed)
    Q_PROPERTY(bool showPO2Cell3 READ showPO2Cell3 WRITE setShowPO2Cell3 NOTIFY showPO2Cell3Changed)
    Q_PROPERTY(bool showCompositePO2 READ showCompositePO2 WRITE setShowCompositePO2 NOTIFY showCompositePO2Changed)

    // Cell selection for per-cell editing
    Q_PROPERTY(QString selectedCellId READ selectedCellId WRITE setSelectedCellId NOTIFY selectedCellIdChanged)

    // Cell background visibility (editor only, not for export/preview)
    Q_PROPERTY(bool showCellBackgrounds READ showCellBackgrounds WRITE setShowCellBackgrounds NOTIFY showCellBackgroundsChanged)

    // Snap-to-grid settings
    Q_PROPERTY(bool snapToGrid READ snapToGrid WRITE setSnapToGrid NOTIFY snapToGridChanged)
    Q_PROPERTY(int gridSpacing READ gridSpacing WRITE setGridSpacing NOTIFY gridSpacingChanged)
    Q_PROPERTY(bool showGrid READ showGrid WRITE setShowGrid NOTIFY showGridChanged)

public:
    explicit OverlayGenerator(QObject *parent = nullptr);
    
    // Getters
    QString templatePath() const { return m_templatePath; }
    int templateWidth() const { return m_templateWidth; }
    int templateHeight() const { return m_templateHeight; }
    QFont font() const { return m_font; }
    QColor textColor() const { return m_textColor; }
    bool showLabel() const { return m_showLabel; }
    double backgroundOpacity() const { return m_backgroundOpacity; }
    bool showDepth() const { return m_showDepth; }
    bool showTemperature() const { return m_showTemperature; }
    bool showNDL() const { return m_showNDL; }
    bool showPressure() const { return m_showPressure; }
    bool showTime() const { return m_showTime; }
    bool showCNS() const { return m_showCNS; }
    bool showMeanDepth() const { return m_showMeanDepth; }
    bool showMaxDepth() const { return m_showMaxDepth; }
    bool showGas() const { return m_showGas; }

    // CCR getters
    bool showPO2Cell1() const { return m_showPO2Cell1; }
    bool showPO2Cell2() const { return m_showPO2Cell2; }
    bool showPO2Cell3() const { return m_showPO2Cell3; }
    bool showCompositePO2() const { return m_showCompositePO2; }

    // Cell selection getter
    QString selectedCellId() const { return m_selectedCellId; }

    // Cell background visibility
    bool showCellBackgrounds() const { return m_showCellBackgrounds; }

    // Snap-to-grid getters
    bool snapToGrid() const { return m_snapToGrid; }
    int gridSpacing() const { return m_gridSpacing; }
    bool showGrid() const { return m_showGrid; }

    // Setters
    void setTemplatePath(const QString &path);
    void setFont(const QFont &font);
    void setTextColor(const QColor &color);
    void setShowLabel(bool show);
    void setBackgroundOpacity(double opacity);
    void setShowDepth(bool show);
    void setShowTemperature(bool show);
    void setShowNDL(bool show);
    void setShowPressure(bool show);
    void setShowTime(bool show);
    void setShowCNS(bool show);
    void setShowMeanDepth(bool show);
    void setShowMaxDepth(bool show);
    void setShowGas(bool show);

    // CCR setters
    void setShowPO2Cell1(bool show);
    void setShowPO2Cell2(bool show);
    void setShowPO2Cell3(bool show);
    void setShowCompositePO2(bool show);

    // Cell selection setter
    void setSelectedCellId(const QString& cellId);

    // Cell background visibility setter
    void setShowCellBackgrounds(bool show);

    // Snap-to-grid setters
    void setSnapToGrid(bool enabled);
    void setGridSpacing(int spacing);
    void setShowGrid(bool show);

    // Cell-based layout management
    Unabara::CellData* getCellData(const QString& cellId);
    const Unabara::CellData* getCellData(const QString& cellId) const;
    QVector<Unabara::CellData> cells() const { return m_cells; }
    Q_INVOKABLE int cellCount() const { return m_cells.size(); }
    Q_INVOKABLE void setCellPosition(const QString& cellId, const QPointF& pos);
    Q_INVOKABLE QFont getCellFont(const QString& cellId) const;
    Q_INVOKABLE QColor getCellColor(const QString& cellId) const;
    Q_INVOKABLE void setCellFont(const QString& cellId, const QFont& font);
    Q_INVOKABLE void setCellColor(const QString& cellId, const QColor& color);
    Q_INVOKABLE void setCellShowLabel(const QString& cellId, bool show);
    Q_INVOKABLE bool getCellShowLabel(const QString& cellId) const;
    Q_INVOKABLE void setCellVisible(const QString& cellId, bool visible);
    Q_INVOKABLE void setCellTypeVisible(const QString& cellId, bool visible);
    Q_INVOKABLE void resetCellFont(const QString& cellId);
    Q_INVOKABLE void resetCellColor(const QString& cellId);
    Q_INVOKABLE void resetCellShowLabel(const QString& cellId);
    bool useCellBasedLayout() const { return m_useCellBasedLayout; }
    void setUseCellBasedLayout(bool use);

    // Template management
    Q_INVOKABLE void loadTemplate(const Unabara::OverlayTemplate& templ);
    Q_INVOKABLE Unabara::OverlayTemplate exportTemplate() const;
    Q_INVOKABLE bool saveTemplateToFile(const QString& filePath);
    Q_INVOKABLE bool loadTemplateFromFile(const QString& filePath);
    Q_INVOKABLE void initializeDefaultCellLayout(DiveData* dive = nullptr);
    Q_INVOKABLE void adjustTankCellVisibility(DiveData* dive);
    Q_INVOKABLE void setPressureCellsVisible(bool visible, DiveData* dive = nullptr);
    Q_INVOKABLE void migrateLegacySettings();

    // Template listing
    Q_INVOKABLE QStringList getAvailableTemplates();
    Q_INVOKABLE QString getTemplatePath(int index);
    Q_INVOKABLE int indexOfTemplatePath(const QString& filePath);
    Q_INVOKABLE void refreshTemplateList();

    // Generate overlay for a specific time point
    Q_INVOKABLE QImage generateOverlay(DiveData* dive, double timePoint);

    // Generate a preview image
    Q_INVOKABLE QImage generatePreview(DiveData* dive);

    // IFrameGenerator
    QImage generate(DiveData* dive, double timePoint) override { return generateOverlay(dive, timePoint); }
    void beginExport() override;
    void endExport() override;
    
signals:
    void templateChanged();
    void fontChanged();
    void textColorChanged();
    void showLabelChanged();
    void backgroundOpacityChanged();
    void showDepthChanged();
    void showTemperatureChanged();
    void showNDLChanged();
    void showPressureChanged();
    void showTimeChanged();
    void showCNSChanged();
    void showMeanDepthChanged();
    void showMaxDepthChanged();
    void showGasChanged();

    // CCR signals
    void showPO2Cell1Changed();
    void showPO2Cell2Changed();
    void showPO2Cell3Changed();
    void showCompositePO2Changed();

    // Cell-based layout signals
    void cellsChanged();
    void cellLayoutChanged();

    // Cell selection signal
    void selectedCellIdChanged();

    // Template save/load signals
    void templateSaved(const QString& filePath);
    void templateLoaded(const QString& filePath);

    // Cell background signal
    void showCellBackgroundsChanged();

    // Snap-to-grid signals
    void snapToGridChanged();
    void gridSpacingChanged();
    void showGridChanged();

private:
    QString m_templatePath;
    int m_templateWidth;
    int m_templateHeight;
    QFont m_font;
    QColor m_textColor;
    bool m_showLabel;
    double m_backgroundOpacity;
    bool m_showDepth;
    bool m_showTemperature;
    bool m_showNDL;
    bool m_showPressure;
    bool m_showTime;
    bool m_showCNS;
    bool m_showMeanDepth;
    bool m_showMaxDepth;
    bool m_showGas;

    // CCR settings
    bool m_showPO2Cell1;
    bool m_showPO2Cell2;
    bool m_showPO2Cell3;
    bool m_showCompositePO2;

    // Cell-based layout
    QVector<Unabara::CellData> m_cells;
    bool m_useCellBasedLayout;

    // Cell selection
    QString m_selectedCellId;

    // Cell background visibility
    bool m_showCellBackgrounds;

    // Template listing cache
    QStringList m_templateNames;
    QStringList m_templatePaths;

    // Snap-to-grid settings
    bool m_snapToGrid;
    int m_gridSpacing;  // Grid spacing in pixels
    bool m_showGrid;

    // Export-pass state stash (saved by beginExport, restored by endExport)
    bool m_savedShowCellBackgrounds = true;

    // Helper methods for drawing
    int getScaledFontSize(const QFont& baseFont, double scale = 1.0) const;
    QSizeF calculateCellSize(Unabara::CellType cellType, const QFont& font, const QSizeF& templateSize, const QString& sampleText = "") const;
    void updateTemplateDimensions();
    void drawDepth(QPainter &painter, double depth, const QRect &rect);
    void drawTemperature(QPainter &painter, double temp, const QRect &rect);
    void drawNDL(QPainter &painter, double ndl, const QRect &rect);
    void drawTTS(QPainter &painter, double tts, const QRect &rect, double ceiling = 0.0);
    void drawPressure(QPainter &painter, double pressure, const QRect &rect, int tankIndex = -1, DiveData* dive = nullptr);
    void drawTime(QPainter &painter, double timestamp, const QRect &rect);
    void drawDataItem(QPainter &painter, const QString &label, const QString &value, const QRect &rect, bool centerAlign);
    // Helper method to draw section headers with consistent positioning
    void drawSectionHeader(QPainter &painter, const QString &label, const QRect &rect);

    // CCR drawing methods
    void drawPO2Cell(QPainter &painter, double po2Value, const QRect &rect, int cellNumber);
    void drawCompositePO2(QPainter &painter, double po2Value, const QRect &rect);

    // Cell-based vs section-based rendering
    void renderCellBasedOverlay(QPainter& painter, const QSize& imageSize,
                                const DiveDataPoint& dataPoint, DiveData* dive);
    void renderSectionBasedOverlay(QPainter& painter, const QSize& imageSize,
                                   const DiveDataPoint& dataPoint, DiveData* dive);

    // Generate display text for a cell (matches CellModel::formatValue for QML consistency)
    QString generateCellDisplayText(Unabara::CellType cellType, const DiveDataPoint& dataPoint,
                                    int tankIndex, DiveData* dive,
                                    bool showLabel = true) const;
};

#endif // OVERLAY_GEN_H