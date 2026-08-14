#include "include/generators/overlay_gen.h"
#include <QPainter>
#include <QtMath>
#include <QFontMetrics>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

OverlayGenerator::OverlayGenerator(QObject *parent)
    : QObject(parent)
    , m_templatePath(":/images/DC_Faces/unabara_round_ocean.png")
    , m_templateWidth(640)
    , m_templateHeight(120)
    , m_font(QFont("Sans Serif", 12))
    , m_labelColor(Unabara::ColorDefaults::text())
    , m_valueColor(Unabara::ColorDefaults::text())
    , m_showLabel(true)
    , m_shadowEnabled(Unabara::ShadowDefaults::enabled)
    , m_shadowType(Unabara::ShadowDefaults::type)
    , m_shadowColor(Unabara::ShadowDefaults::color())
    , m_shadowSize(Unabara::ShadowDefaults::size)
    , m_shadowOpacity(Unabara::ShadowDefaults::opacity)
    , m_backgroundOpacity(1.0)
    , m_showDepth(true)
    , m_showTemperature(true)
    , m_showNDL(true)
    , m_showPressure(true)
    , m_showTime(true)
    , m_showCNS(false)
    , m_showMeanDepth(false)
    , m_showMaxDepth(false)
    , m_showGas(false)
    , m_showTTS(false)
    , m_showStopDepth(true)
    , m_showStopTime(true)
    , m_showPO2Cell1(false)
    , m_showPO2Cell2(false)
    , m_showPO2Cell3(false)
    , m_showCompositePO2(false)
    , m_useCellBasedLayout(false)
    , m_showCellBackgrounds(true)
    , m_snapToGrid(true)
    , m_gridSpacing(10)
    , m_showGrid(false)
{
    // Initialize with values from Config
    Config* config = Config::instance();
    m_templatePath = config->templatePath();
    updateTemplateDimensions();
    m_font = config->font();
    m_labelColor = config->labelColor();
    m_valueColor = config->valueColor();
    m_backgroundOpacity = config->backgroundOpacity();
    m_showDepth = config->showDepth();
    m_showTemperature = config->showTemperature();
    m_showNDL = config->showNDL();
    m_showPressure = config->showPressure();
    m_showTime = config->showTime();
    m_showCNS = config->showCNS();
    m_showMeanDepth = config->showMeanDepth();
    m_showMaxDepth = config->showMaxDepth();
    m_showGas = config->showGas();
    m_showTTS = config->showTTS();
    m_showStopDepth = config->showStopDepth();
    m_showStopTime = config->showStopTime();
    m_showPO2Cell1 = config->showPO2Cell1();
    m_showPO2Cell2 = config->showPO2Cell2();
    m_showPO2Cell3 = config->showPO2Cell3();
    m_showCompositePO2 = config->showCompositePO2();

    // Connect to config changes to stay in sync
    connect(config, &Config::backgroundOpacityChanged, this, [this]() {
        m_backgroundOpacity = Config::instance()->backgroundOpacity();
        emit backgroundOpacityChanged();
    });

    // Initialize default cell layout
    initializeDefaultCellLayout();

    // Load previously active template, or first bundled template on first run
    QString activeTemplate = config->activeTemplatePath();
    if (!activeTemplate.isEmpty() && QFile::exists(activeTemplate)) {
        loadTemplateFromFile(activeTemplate);
    } else {
        // First run: load first available bundled template
        refreshTemplateList();
        if (!m_templatePaths.isEmpty()) {
            loadTemplateFromFile(m_templatePaths.first());
        }
    }
}

void OverlayGenerator::setTemplatePath(const QString &path)
{
    // Normalize qrc:/ URLs to :/ resource paths for QImage compatibility
    QString normalizedPath = path;
    if (normalizedPath.startsWith("qrc:/")) {
        normalizedPath = normalizedPath.mid(3); // "qrc:/" -> ":/"
    }
    if (m_templatePath != normalizedPath) {
        m_templatePath = normalizedPath;
        updateTemplateDimensions();
        emit templateChanged();
    }
}

void OverlayGenerator::updateTemplateDimensions()
{
    // Load template image to get dimensions
    QImage templateImage(m_templatePath);
    if (templateImage.isNull()) {
        // Use default dimensions if template can't be loaded
        m_templateWidth = 640;
        m_templateHeight = 120;
        qWarning() << "Could not load template for dimensions, using defaults:" << m_templatePath;
    } else {
        m_templateWidth = templateImage.width();
        m_templateHeight = templateImage.height();
        qDebug() << "Template dimensions:" << m_templateWidth << "x" << m_templateHeight;
    }
}

void OverlayGenerator::setFont(const QFont &font)
{
    if (m_selectedCellId.isEmpty()) {
        // No cell selected - apply to all cells (global default)
        if (m_font != font) {
            m_font = font;
            Config::instance()->setFont(font); // Update Config

            // Apply to all cells that don't have custom fonts
            for (auto& cell : m_cells) {
                if (!cell.hasCustomFont()) {
                    cell.setFont(font, false); // false = not custom, inherited from global
                }
            }

            emit fontChanged();
            emit cellsChanged();
        }
    } else {
        // Cell selected - apply only to selected cell
        Unabara::CellData* cell = getCellData(m_selectedCellId);
        if (cell && cell->font() != font) {
            cell->setFont(font, true); // true = custom font
            emit cellsChanged();
        }
    }
}

void OverlayGenerator::setLabelColor(const QColor &color)
{
    if (m_selectedCellId.isEmpty()) {
        // No cell selected - apply to all cells (global default)
        if (m_labelColor != color) {
            m_labelColor = color;
            Config::instance()->setLabelColor(color); // Update Config

            // Apply to all cells that don't have custom label colors
            for (auto& cell : m_cells) {
                if (!cell.hasCustomLabelColor()) {
                    cell.setLabelColor(color, false); // false = not custom, inherited from global
                }
            }

            emit labelColorChanged();
            emit cellsChanged();
        }
    } else {
        // Cell selected - apply only to selected cell
        Unabara::CellData* cell = getCellData(m_selectedCellId);
        if (cell && cell->labelColor() != color) {
            cell->setLabelColor(color, true); // true = custom color
            emit cellsChanged();
        }
    }
}

void OverlayGenerator::setValueColor(const QColor &color)
{
    if (m_selectedCellId.isEmpty()) {
        // No cell selected - apply to all cells (global default)
        if (m_valueColor != color) {
            m_valueColor = color;
            Config::instance()->setValueColor(color); // Update Config

            // Apply to all cells that don't have custom value colors
            for (auto& cell : m_cells) {
                if (!cell.hasCustomValueColor()) {
                    cell.setValueColor(color, false); // false = not custom, inherited from global
                }
            }

            emit valueColorChanged();
            emit cellsChanged();
        }
    } else {
        // Cell selected - apply only to selected cell
        Unabara::CellData* cell = getCellData(m_selectedCellId);
        if (cell && cell->valueColor() != color) {
            cell->setValueColor(color, true); // true = custom color
            emit cellsChanged();
        }
    }
}

void OverlayGenerator::setShowLabel(bool show)
{
    if (m_selectedCellId.isEmpty()) {
        // No cell selected - apply to all cells (global default)
        if (m_showLabel != show) {
            m_showLabel = show;

            // Apply to all cells that don't have a per-cell override
            for (auto& cell : m_cells) {
                if (!cell.hasCustomShowLabel()) {
                    cell.setShowLabel(show, false); // false = inherited from global
                }
            }

            emit showLabelChanged();
            emit cellsChanged();
        }
    } else {
        // Cell selected - apply only to selected cell as a per-cell override
        Unabara::CellData* cell = getCellData(m_selectedCellId);
        if (cell && cell->showLabel() != show) {
            cell->setShowLabel(show, true); // true = custom override
            emit cellsChanged();
        }
    }
}

void OverlayGenerator::setShadowEnabled(bool enabled)
{
    if (m_selectedCellId.isEmpty()) {
        // No cell selected - apply to all cells (global default)
        if (m_shadowEnabled != enabled) {
            m_shadowEnabled = enabled;

            // Apply to all cells that don't have a per-cell override
            for (auto& cell : m_cells) {
                if (!cell.hasCustomShadow()) {
                    cell.setShadowEnabled(enabled, false); // false = inherited from global
                }
            }

            emit shadowChanged();
            emit cellsChanged();
        }
    } else {
        // Cell selected - apply only to selected cell as a per-cell override
        Unabara::CellData* cell = getCellData(m_selectedCellId);
        if (cell && cell->shadowEnabled() != enabled) {
            cell->setShadowEnabled(enabled, true); // true = custom override
            emit cellsChanged();
        }
    }
}

void OverlayGenerator::setShadowType(int type)
{
    auto shadowType = static_cast<Unabara::ShadowType>(
        qBound(0, type, static_cast<int>(Unabara::ShadowType::Outline)));

    if (m_selectedCellId.isEmpty()) {
        // No cell selected - apply to all cells (global default)
        if (m_shadowType != shadowType) {
            m_shadowType = shadowType;

            // Apply to all cells that don't have a per-cell override
            for (auto& cell : m_cells) {
                if (!cell.hasCustomShadow()) {
                    cell.setShadowType(shadowType, false); // false = inherited from global
                }
            }

            emit shadowChanged();
            emit cellsChanged();
        }
    } else {
        // Cell selected - apply only to selected cell as a per-cell override
        Unabara::CellData* cell = getCellData(m_selectedCellId);
        if (cell && cell->shadowType() != shadowType) {
            cell->setShadowType(shadowType, true); // true = custom override
            emit cellsChanged();
        }
    }
}

void OverlayGenerator::setShadowColor(const QColor& color)
{
    if (m_selectedCellId.isEmpty()) {
        // No cell selected - apply to all cells (global default)
        if (m_shadowColor != color) {
            m_shadowColor = color;

            // Apply to all cells that don't have a per-cell override
            for (auto& cell : m_cells) {
                if (!cell.hasCustomShadow()) {
                    cell.setShadowColor(color, false); // false = inherited from global
                }
            }

            emit shadowChanged();
            emit cellsChanged();
        }
    } else {
        // Cell selected - apply only to selected cell as a per-cell override
        Unabara::CellData* cell = getCellData(m_selectedCellId);
        if (cell && cell->shadowColor() != color) {
            cell->setShadowColor(color, true); // true = custom override
            emit cellsChanged();
        }
    }
}

void OverlayGenerator::setShadowSize(int size)
{
    size = qBound(1, size, 10);

    if (m_selectedCellId.isEmpty()) {
        // No cell selected - apply to all cells (global default)
        if (m_shadowSize != size) {
            m_shadowSize = size;

            // Apply to all cells that don't have a per-cell override
            for (auto& cell : m_cells) {
                if (!cell.hasCustomShadow()) {
                    cell.setShadowSize(size, false); // false = inherited from global
                }
            }

            emit shadowChanged();
            emit cellsChanged();
        }
    } else {
        // Cell selected - apply only to selected cell as a per-cell override
        Unabara::CellData* cell = getCellData(m_selectedCellId);
        if (cell && cell->shadowSize() != size) {
            cell->setShadowSize(size, true); // true = custom override
            emit cellsChanged();
        }
    }
}

void OverlayGenerator::setShadowOpacity(double opacity)
{
    opacity = qBound(0.0, opacity, 1.0);

    if (m_selectedCellId.isEmpty()) {
        // No cell selected - apply to all cells (global default)
        if (m_shadowOpacity != opacity) {
            m_shadowOpacity = opacity;

            // Apply to all cells that don't have a per-cell override
            for (auto& cell : m_cells) {
                if (!cell.hasCustomShadow()) {
                    cell.setShadowOpacity(opacity, false); // false = inherited from global
                }
            }

            emit shadowChanged();
            emit cellsChanged();
        }
    } else {
        // Cell selected - apply only to selected cell as a per-cell override
        Unabara::CellData* cell = getCellData(m_selectedCellId);
        if (cell && cell->shadowOpacity() != opacity) {
            cell->setShadowOpacity(opacity, true); // true = custom override
            emit cellsChanged();
        }
    }
}

void OverlayGenerator::setBackgroundOpacity(double opacity)
{
    opacity = qBound(0.0, opacity, 1.0); // Clamp between 0.0 and 1.0
    if (qAbs(m_backgroundOpacity - opacity) > 0.001) { // Use floating point comparison
        m_backgroundOpacity = opacity;
        Config::instance()->setBackgroundOpacity(opacity); // Update Config
        emit backgroundOpacityChanged();
    }
}

void OverlayGenerator::setShowDepth(bool show)
{
    if (m_showDepth != show) {
        m_showDepth = show;
        // Note: Cell regeneration is handled by QML with dive data
        emit showDepthChanged();
    }
}

void OverlayGenerator::setShowTemperature(bool show)
{
    if (m_showTemperature != show) {
        m_showTemperature = show;
        // Note: Cell regeneration is handled by QML with dive data
        emit showTemperatureChanged();
    }
}

void OverlayGenerator::setShowNDL(bool show)
{
    if (m_showNDL != show) {
        m_showNDL = show;
        // Note: Cell regeneration is handled by QML with dive data
        emit showNDLChanged();
    }
}

void OverlayGenerator::setShowCNS(bool show)
{
    if (m_showCNS != show) {
        m_showCNS = show;
        // Note: Cell regeneration is handled by QML with dive data
        emit showCNSChanged();
    }
}

void OverlayGenerator::setShowMeanDepth(bool show)
{
    if (m_showMeanDepth != show) {
        m_showMeanDepth = show;
        // Note: Cell regeneration is handled by QML with dive data
        emit showMeanDepthChanged();
    }
}

void OverlayGenerator::setShowMaxDepth(bool show)
{
    if (m_showMaxDepth != show) {
        m_showMaxDepth = show;
        // Note: Cell regeneration is handled by QML with dive data
        emit showMaxDepthChanged();
    }
}

void OverlayGenerator::setShowGas(bool show)
{
    if (m_showGas != show) {
        m_showGas = show;
        // Note: Cell regeneration is handled by QML with dive data
        emit showGasChanged();
    }
}

void OverlayGenerator::setShowTTS(bool show)
{
    if (m_showTTS != show) {
        m_showTTS = show;
        // Note: Cell regeneration is handled by QML with dive data
        emit showTTSChanged();
    }
}

void OverlayGenerator::setShowStopDepth(bool show)
{
    if (m_showStopDepth != show) {
        m_showStopDepth = show;
        // Note: Cell regeneration is handled by QML with dive data
        emit showStopDepthChanged();
    }
}

void OverlayGenerator::setShowStopTime(bool show)
{
    if (m_showStopTime != show) {
        m_showStopTime = show;
        // Note: Cell regeneration is handled by QML with dive data
        emit showStopTimeChanged();
    }
}

void OverlayGenerator::setShowPressure(bool show)
{
    if (m_showPressure != show) {
        m_showPressure = show;
        // Note: Cell regeneration is handled by QML with dive data
        emit showPressureChanged();
    }
}

void OverlayGenerator::setShowTime(bool show)
{
    if (m_showTime != show) {
        m_showTime = show;
        // Note: Cell regeneration is handled by QML with dive data
        emit showTimeChanged();
    }
}

// CCR setter implementations
void OverlayGenerator::setShowPO2Cell1(bool show)
{
    if (m_showPO2Cell1 != show) {
        m_showPO2Cell1 = show;
        // Note: Cell regeneration is handled by QML with dive data
        emit showPO2Cell1Changed();
    }
}

void OverlayGenerator::setShowPO2Cell2(bool show)
{
    if (m_showPO2Cell2 != show) {
        m_showPO2Cell2 = show;
        // Note: Cell regeneration is handled by QML with dive data
        emit showPO2Cell2Changed();
    }
}

void OverlayGenerator::setShowPO2Cell3(bool show)
{
    if (m_showPO2Cell3 != show) {
        m_showPO2Cell3 = show;
        // Note: Cell regeneration is handled by QML with dive data
        emit showPO2Cell3Changed();
    }
}

void OverlayGenerator::setShowCompositePO2(bool show)
{
    if (m_showCompositePO2 != show) {
        m_showCompositePO2 = show;
        // Note: Cell regeneration is handled by QML with dive data
        emit showCompositePO2Changed();
    }
}

void OverlayGenerator::setSelectedCellId(const QString& cellId)
{
    if (m_selectedCellId != cellId) {
        m_selectedCellId = cellId;
        emit selectedCellIdChanged();
    }
}

void OverlayGenerator::setSnapToGrid(bool enabled)
{
    if (m_snapToGrid != enabled) {
        m_snapToGrid = enabled;
        emit snapToGridChanged();
    }
}

void OverlayGenerator::setGridSpacing(int spacing)
{
    if (m_gridSpacing != spacing && spacing > 0) {
        m_gridSpacing = spacing;
        emit gridSpacingChanged();
    }
}

void OverlayGenerator::setShowGrid(bool show)
{
    if (m_showGrid != show) {
        m_showGrid = show;
        emit showGridChanged();
    }
}

void OverlayGenerator::setShowCellBackgrounds(bool show)
{
    if (m_showCellBackgrounds != show) {
        m_showCellBackgrounds = show;
        emit showCellBackgroundsChanged();
    }
}

void OverlayGenerator::beginExport()
{
    // Cell backgrounds are an editor-only affordance — never render them
    // into export frames. Stash the user's current value and force off.
    m_savedShowCellBackgrounds = m_showCellBackgrounds;
    m_showCellBackgrounds = false;
}

void OverlayGenerator::endExport()
{
    m_showCellBackgrounds = m_savedShowCellBackgrounds;
}

QStringList OverlayGenerator::getAvailableTemplates()
{
    if (m_templateNames.isEmpty()) {
        refreshTemplateList();
    }
    return m_templateNames;
}

QString OverlayGenerator::getTemplatePath(int index)
{
    if (index >= 0 && index < m_templatePaths.size()) {
        return m_templatePaths.at(index);
    }
    return QString();
}

int OverlayGenerator::indexOfTemplatePath(const QString& filePath)
{
    // Try exact match first
    int idx = m_templatePaths.indexOf(filePath);
    if (idx >= 0) return idx;

    // Try canonical path match
    QString canonical = QFileInfo(filePath).canonicalFilePath();
    for (int i = 0; i < m_templatePaths.size(); ++i) {
        if (QFileInfo(m_templatePaths[i]).canonicalFilePath() == canonical)
            return i;
    }

    // Not found — read template name from file and append to lists
    QString displayName = QFileInfo(filePath).baseName().replace('_', ' ');
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject()) {
            QString name = doc.object().value("templateName").toString();
            if (!name.isEmpty()) displayName = name;
        }
    }
    m_templateNames.append(displayName);
    m_templatePaths.append(filePath);
    return m_templateNames.size() - 1;
}

void OverlayGenerator::refreshTemplateList()
{
    m_templateNames.clear();
    m_templatePaths.clear();

    // Scan bundled templates from Qt resources
    QDir resourceDir(":/templates");
    if (resourceDir.exists()) {
        QStringList utpFiles = resourceDir.entryList(QStringList() << "*.utp", QDir::Files);
        for (const QString& fileName : utpFiles) {
            QString filePath = ":/templates/" + fileName;
            // Try to read templateName from the file
            QFile file(filePath);
            QString displayName = QFileInfo(fileName).baseName().replace('_', ' ');
            if (file.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                if (doc.isObject()) {
                    QString name = doc.object().value("templateName").toString();
                    if (!name.isEmpty()) {
                        displayName = name.replace('_', ' ');
                    }
                }
            }
            m_templateNames.append(displayName);
            m_templatePaths.append(filePath);
        }
    }

    // Scan user template directory
    Config* config = Config::instance();
    QString templateDir = config->templateDirectory();
    if (!templateDir.isEmpty()) {
        QDir userDir(templateDir);
        if (userDir.exists()) {
            QStringList utpFiles = userDir.entryList(QStringList() << "*.utp", QDir::Files);
            for (const QString& fileName : utpFiles) {
                QString filePath = userDir.absoluteFilePath(fileName);
                // Try to read templateName from the file
                QFile file(filePath);
                QString displayName = QFileInfo(fileName).baseName().replace('_', ' ');
                if (file.open(QIODevice::ReadOnly)) {
                    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                    if (doc.isObject()) {
                        QString name = doc.object().value("templateName").toString();
                        if (!name.isEmpty()) {
                            displayName = name.replace('_', ' ');
                        }
                    }
                }
                m_templateNames.append(displayName);
                m_templatePaths.append(filePath);
            }
        }
    }
}

void OverlayGenerator::resetCellFont(const QString& cellId)
{
    Unabara::CellData* cell = getCellData(cellId);
    if (cell && cell->hasCustomFont()) {
        // Reset to global default font
        cell->setFont(m_font, false);  // false = inherited from global
        emit cellsChanged();
    }
}

void OverlayGenerator::resetCellLabelColor(const QString& cellId)
{
    Unabara::CellData* cell = getCellData(cellId);
    if (cell && cell->hasCustomLabelColor()) {
        // Reset to global default color
        cell->setLabelColor(m_labelColor, false);  // false = inherited from global
        emit cellsChanged();
    }
}

void OverlayGenerator::resetCellValueColor(const QString& cellId)
{
    Unabara::CellData* cell = getCellData(cellId);
    if (cell && cell->hasCustomValueColor()) {
        // Reset to global default color
        cell->setValueColor(m_valueColor, false);  // false = inherited from global
        emit cellsChanged();
    }
}

void OverlayGenerator::resetCellShowLabel(const QString& cellId)
{
    Unabara::CellData* cell = getCellData(cellId);
    if (cell && cell->hasCustomShowLabel()) {
        // Reset to global default
        cell->setShowLabel(m_showLabel, false);  // false = inherited from global
        emit cellsChanged();
    }
}

void OverlayGenerator::resetCellShadow(const QString& cellId)
{
    Unabara::CellData* cell = getCellData(cellId);
    if (cell && cell->hasCustomShadow()) {
        // Reset the whole shadow group to the global defaults
        cell->setShadowEnabled(m_shadowEnabled, false);  // false = inherited from global
        cell->setShadowType(m_shadowType, false);
        cell->setShadowColor(m_shadowColor, false);
        cell->setShadowSize(m_shadowSize, false);
        cell->setShadowOpacity(m_shadowOpacity, false);
        emit cellsChanged();
    }
}

// Cell-based layout management methods

void OverlayGenerator::seedCellColors(Unabara::CellData& cell) const
{
    cell.setLabelColor(m_labelColor, false);  // false = inherited from global
    cell.setValueColor(m_valueColor, false);
}

Unabara::CellData* OverlayGenerator::getCellData(const QString& cellId)
{
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].cellId() == cellId) {
            return &m_cells[i];
        }
    }
    return nullptr;
}

const Unabara::CellData* OverlayGenerator::getCellData(const QString& cellId) const
{
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].cellId() == cellId) {
            return &m_cells[i];
        }
    }
    return nullptr;
}

void OverlayGenerator::setCellPosition(const QString& cellId, const QPointF& pos)
{
    Unabara::CellData* cell = getCellData(cellId);
    if (cell) {
        cell->setPosition(pos);
        emit cellLayoutChanged();
    } else {
        qWarning() << "setCellPosition: Cell not found:" << cellId;
    }
}

QFont OverlayGenerator::getCellFont(const QString& cellId) const
{
    const auto* cell = getCellData(cellId);
    if (cell && cell->hasCustomFont()) {
        return cell->font();
    }
    return m_font;
}

QColor OverlayGenerator::getCellLabelColor(const QString& cellId) const
{
    const auto* cell = getCellData(cellId);
    if (cell && cell->hasCustomLabelColor()) {
        return cell->labelColor();
    }
    return m_labelColor;
}

QColor OverlayGenerator::getCellValueColor(const QString& cellId) const
{
    const auto* cell = getCellData(cellId);
    if (cell && cell->hasCustomValueColor()) {
        return cell->valueColor();
    }
    return m_valueColor;
}

void OverlayGenerator::setCellFont(const QString& cellId, const QFont& font)
{
    Unabara::CellData* cell = getCellData(cellId);
    if (cell) {
        cell->setFont(font, true);  // true = custom font
        emit cellLayoutChanged();
    } else {
        qWarning() << "setCellFont: Cell not found:" << cellId;
    }
}

void OverlayGenerator::setCellLabelColor(const QString& cellId, const QColor& color)
{
    Unabara::CellData* cell = getCellData(cellId);
    if (cell) {
        cell->setLabelColor(color, true);  // true = custom color
        emit cellLayoutChanged();
    } else {
        qWarning() << "setCellLabelColor: Cell not found:" << cellId;
    }
}

void OverlayGenerator::setCellValueColor(const QString& cellId, const QColor& color)
{
    Unabara::CellData* cell = getCellData(cellId);
    if (cell) {
        cell->setValueColor(color, true);  // true = custom color
        emit cellLayoutChanged();
    } else {
        qWarning() << "setCellValueColor: Cell not found:" << cellId;
    }
}

void OverlayGenerator::setCellShowLabel(const QString& cellId, bool show)
{
    Unabara::CellData* cell = getCellData(cellId);
    if (cell) {
        cell->setShowLabel(show, true);  // true = per-cell override
        emit cellsChanged();
    } else {
        qWarning() << "setCellShowLabel: Cell not found:" << cellId;
    }
}

bool OverlayGenerator::getCellShowLabel(const QString& cellId) const
{
    const auto* cell = getCellData(cellId);
    if (cell && cell->hasCustomShowLabel()) {
        return cell->showLabel();
    }
    return m_showLabel;
}

bool OverlayGenerator::getCellShadowEnabled(const QString& cellId) const
{
    const auto* cell = getCellData(cellId);
    if (cell && cell->hasCustomShadow()) {
        return cell->shadowEnabled();
    }
    return m_shadowEnabled;
}

int OverlayGenerator::getCellShadowType(const QString& cellId) const
{
    const auto* cell = getCellData(cellId);
    if (cell && cell->hasCustomShadow()) {
        return static_cast<int>(cell->shadowType());
    }
    return static_cast<int>(m_shadowType);
}

QColor OverlayGenerator::getCellShadowColor(const QString& cellId) const
{
    const auto* cell = getCellData(cellId);
    if (cell && cell->hasCustomShadow()) {
        return cell->shadowColor();
    }
    return m_shadowColor;
}

int OverlayGenerator::getCellShadowSize(const QString& cellId) const
{
    const auto* cell = getCellData(cellId);
    if (cell && cell->hasCustomShadow()) {
        return cell->shadowSize();
    }
    return m_shadowSize;
}

double OverlayGenerator::getCellShadowOpacity(const QString& cellId) const
{
    const auto* cell = getCellData(cellId);
    if (cell && cell->hasCustomShadow()) {
        return cell->shadowOpacity();
    }
    return m_shadowOpacity;
}

void OverlayGenerator::setCellVisible(const QString& cellId, bool visible)
{
    Unabara::CellData* cell = getCellData(cellId);
    if (cell) {
        cell->setVisible(visible);
        emit cellLayoutChanged();
    } else {
        qWarning() << "setCellVisible: Cell not found:" << cellId;
    }
}

void OverlayGenerator::setCellTypeVisible(const QString& cellId, bool visible)
{
    Unabara::CellData* cell = getCellData(cellId);
    if (cell) {
        cell->setVisible(visible);
        emit cellLayoutChanged();
        return;
    }

    if (!visible) {
        // Nothing to hide
        return;
    }

    // The template has no cell for this data type — create a default one
    // (mirrors setPressureCellsVisible). Pressure cells have their own method
    // because they depend on the dive's tank count.
    static const QMap<QString, Unabara::CellType> idToType = {
        {"depth", Unabara::CellType::Depth},
        {"temperature", Unabara::CellType::Temperature},
        {"time", Unabara::CellType::Time},
        {"ndl", Unabara::CellType::NDL},
        {"tts", Unabara::CellType::TTS},
        {"cns", Unabara::CellType::CNS},
        {"mean_depth", Unabara::CellType::MeanDepth},
        {"max_depth", Unabara::CellType::MaxDepth},
        {"gas", Unabara::CellType::Gas},
        {"stop_depth", Unabara::CellType::StopDepth},
        {"stop_time", Unabara::CellType::StopTime},
        {"po2_cell1", Unabara::CellType::PO2Cell1},
        {"po2_cell2", Unabara::CellType::PO2Cell2},
        {"po2_cell3", Unabara::CellType::PO2Cell3},
        {"composite_po2", Unabara::CellType::CompositePO2},
    };
    if (!idToType.contains(cellId)) {
        qWarning() << "setCellTypeVisible: Unknown cell id:" << cellId;
        return;
    }

    QImage templateImage(m_templatePath);
    QSizeF templateSize = templateImage.isNull()
        ? QSizeF(640, 120)
        : QSizeF(templateImage.width(), templateImage.height());

    Unabara::CellData newCell(cellId, idToType.value(cellId));
    newCell.setPosition(QPointF(0.5, 0.5));
    newCell.setFont(m_font, false);
    seedCellColors(newCell);
    newCell.setCalculatedSize(calculateCellSize(newCell.cellType(), m_font, templateSize));
    newCell.setVisible(true);
    m_cells.append(newCell);

    emit cellsChanged();
    emit cellLayoutChanged();
}

void OverlayGenerator::adjustTankCellVisibility(DiveData* dive)
{
    if (!dive || dive->cylinderCount() == 0) {
        return;
    }

    int tankCount = dive->cylinderCount();
    bool changed = false;

    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].cellType() == Unabara::CellType::Pressure) {
            bool shouldBeVisible = m_showPressure && m_cells[i].tankIndex() < tankCount;
            if (m_cells[i].visible() != shouldBeVisible) {
                m_cells[i].setVisible(shouldBeVisible);
                changed = true;
            }
        }
    }

    if (changed) {
        emit cellLayoutChanged();
    }
}

void OverlayGenerator::setPressureCellsVisible(bool visible, DiveData* dive)
{
    int tankCount = dive ? dive->cylinderCount() : INT_MAX;

    // Check if any pressure cells exist
    bool hasPressureCells = false;
    for (const auto& cell : m_cells) {
        if (cell.cellType() == Unabara::CellType::Pressure) {
            hasPressureCells = true;
            break;
        }
    }

    if (visible && !hasPressureCells) {
        // Create default pressure cell(s) since the template has none
        QImage templateImage(m_templatePath);
        QSizeF templateSize = templateImage.isNull()
            ? QSizeF(640, 120)
            : QSizeF(templateImage.width(), templateImage.height());

        int actualTanks = (dive && dive->cylinderCount() > 0) ? dive->cylinderCount() : 1;
        if (actualTanks == 1) {
            Unabara::CellData cell("pressure", Unabara::CellType::Pressure);
            cell.setTankIndex(0);
            cell.setPosition(QPointF(0.5, 0.5));
            cell.setFont(m_font, false);
            seedCellColors(cell);
            cell.setCalculatedSize(calculateCellSize(Unabara::CellType::Pressure, m_font, templateSize));
            cell.setVisible(true);
            m_cells.append(cell);
        } else {
            for (int i = 0; i < actualTanks; ++i) {
                QString cellId = QString("tank_%1").arg(i);
                Unabara::CellData cell(cellId, Unabara::CellType::Pressure);
                cell.setTankIndex(i);
                int col = i % 2;
                int row = i / 2;
                cell.setPosition(QPointF(0.5 + col * 0.25, 0.5 + row * 0.2));
                cell.setFont(m_font, false);
                seedCellColors(cell);
                cell.setCalculatedSize(calculateCellSize(Unabara::CellType::Pressure, m_font, templateSize));
                cell.setVisible(true);
                m_cells.append(cell);
            }
        }
        emit cellsChanged();
        emit cellLayoutChanged();
        return;
    }

    // Toggle existing pressure cells
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].cellType() == Unabara::CellType::Pressure) {
            bool shouldBeVisible = visible && m_cells[i].tankIndex() < tankCount;
            m_cells[i].setVisible(shouldBeVisible);
        }
    }

    emit cellLayoutChanged();
}

void OverlayGenerator::setUseCellBasedLayout(bool use)
{
    if (m_useCellBasedLayout != use) {
        m_useCellBasedLayout = use;
        if (use && m_cells.isEmpty()) {
            // Auto-initialize cells if switching to cell-based layout with no cells
            initializeDefaultCellLayout();
        }
        emit cellLayoutChanged();
    }
}

void OverlayGenerator::loadTemplate(const Unabara::OverlayTemplate& templ)
{
    // Normalize qrc:/ URLs to :/ resource paths for QImage compatibility
    m_templatePath = templ.backgroundImagePath();
    if (m_templatePath.startsWith("qrc:/")) {
        m_templatePath = m_templatePath.mid(3);
    }
    updateTemplateDimensions();  // Update width/height for new template image
    m_backgroundOpacity = templ.backgroundOpacity();
    m_font = templ.defaultFont();
    m_labelColor = templ.defaultLabelColor();
    m_valueColor = templ.defaultValueColor();
    m_shadowEnabled = templ.defaultShadowEnabled();
    m_shadowType = templ.defaultShadowType();
    m_shadowColor = templ.defaultShadowColor();
    m_shadowSize = templ.defaultShadowSize();
    m_shadowOpacity = templ.defaultShadowOpacity();
    m_cells = templ.cells();
    m_useCellBasedLayout = true;

    // Re-propagate global shadow and color settings into cells without a
    // per-cell override — saved templates omit non-custom values, so loaded
    // cells otherwise sit at constructor defaults instead of the template's
    for (auto& cell : m_cells) {
        if (!cell.hasCustomShadow()) {
            cell.setShadowEnabled(m_shadowEnabled, false);
            cell.setShadowType(m_shadowType, false);
            cell.setShadowColor(m_shadowColor, false);
            cell.setShadowSize(m_shadowSize, false);
            cell.setShadowOpacity(m_shadowOpacity, false);
        }
        if (!cell.hasCustomLabelColor()) {
            cell.setLabelColor(m_labelColor, false);
        }
        if (!cell.hasCustomValueColor()) {
            cell.setValueColor(m_valueColor, false);
        }
    }

    // Reset all visibility flags before scanning cells
    m_showDepth = false;
    m_showTemperature = false;
    m_showTime = false;
    m_showNDL = false;
    m_showPressure = false;
    m_showCNS = false;
    m_showMeanDepth = false;
    m_showMaxDepth = false;
    m_showGas = false;
    m_showTTS = false;
    m_showStopDepth = false;
    m_showStopTime = false;
    m_showPO2Cell1 = false;
    m_showPO2Cell2 = false;
    m_showPO2Cell3 = false;
    m_showCompositePO2 = false;

    // Update visibility flags from cells that exist in the template
    for (const auto& cell : m_cells) {
        if (cell.cellType() == Unabara::CellType::Depth) {
            m_showDepth = cell.visible();
        } else if (cell.cellType() == Unabara::CellType::Temperature) {
            m_showTemperature = cell.visible();
        } else if (cell.cellType() == Unabara::CellType::Time) {
            m_showTime = cell.visible();
        } else if (cell.cellType() == Unabara::CellType::NDL) {
            m_showNDL = cell.visible();
        } else if (cell.cellType() == Unabara::CellType::Pressure) {
            m_showPressure = cell.visible();
        } else if (cell.cellType() == Unabara::CellType::CNS) {
            m_showCNS = cell.visible();
        } else if (cell.cellType() == Unabara::CellType::MeanDepth) {
            m_showMeanDepth = cell.visible();
        } else if (cell.cellType() == Unabara::CellType::MaxDepth) {
            m_showMaxDepth = cell.visible();
        } else if (cell.cellType() == Unabara::CellType::Gas) {
            m_showGas = cell.visible();
        } else if (cell.cellType() == Unabara::CellType::TTS) {
            m_showTTS = cell.visible();
        } else if (cell.cellType() == Unabara::CellType::StopDepth) {
            m_showStopDepth = cell.visible();
        } else if (cell.cellType() == Unabara::CellType::StopTime) {
            m_showStopTime = cell.visible();
        } else if (cell.cellType() == Unabara::CellType::PO2Cell1) {
            m_showPO2Cell1 = cell.visible();
        } else if (cell.cellType() == Unabara::CellType::PO2Cell2) {
            m_showPO2Cell2 = cell.visible();
        } else if (cell.cellType() == Unabara::CellType::PO2Cell3) {
            m_showPO2Cell3 = cell.visible();
        } else if (cell.cellType() == Unabara::CellType::CompositePO2) {
            m_showCompositePO2 = cell.visible();
        }
    }

    emit templateChanged();
    emit shadowChanged();
    emit cellsChanged();
    emit cellLayoutChanged();
    emit showDepthChanged();
    emit showTemperatureChanged();
    emit showTimeChanged();
    emit showNDLChanged();
    emit showPressureChanged();
    emit showCNSChanged();
    emit showMeanDepthChanged();
    emit showMaxDepthChanged();
    emit showGasChanged();
    emit showTTSChanged();
    emit showStopDepthChanged();
    emit showStopTimeChanged();
    emit showPO2Cell1Changed();
    emit showPO2Cell2Changed();
    emit showPO2Cell3Changed();
    emit showCompositePO2Changed();
}

Unabara::OverlayTemplate OverlayGenerator::exportTemplate() const
{
    Unabara::OverlayTemplate templ;
    templ.setTemplateName("Custom Overlay");
    templ.setBackgroundImagePath(m_templatePath);
    templ.setBackgroundOpacity(m_backgroundOpacity);
    templ.setDefaultFont(m_font);
    templ.setDefaultLabelColor(m_labelColor);
    templ.setDefaultValueColor(m_valueColor);
    templ.setDefaultShadowEnabled(m_shadowEnabled);
    templ.setDefaultShadowType(m_shadowType);
    templ.setDefaultShadowColor(m_shadowColor);
    templ.setDefaultShadowSize(m_shadowSize);
    templ.setDefaultShadowOpacity(m_shadowOpacity);
    templ.setCells(m_cells);
    return templ;
}

bool OverlayGenerator::saveTemplateToFile(const QString& filePath)
{
    qDebug() << "Saving template to file:" << filePath;

    // Export current state to template
    Unabara::OverlayTemplate templ = exportTemplate();

    // Extract filename without extension for template name
    QFileInfo fileInfo(filePath);
    QString templateName = fileInfo.completeBaseName();
    templ.setTemplateName(templateName);

    // Save to file
    bool success = templ.saveToFile(filePath);

    if (success) {
        qDebug() << "Template saved successfully";
        Config::instance()->setActiveTemplatePath(filePath);
        emit templateSaved(filePath);
    } else {
        qWarning() << "Failed to save template";
    }

    return success;
}

bool OverlayGenerator::loadTemplateFromFile(const QString& filePath)
{
    qDebug() << "Loading template from file:" << filePath;

    QString errorMessage;
    Unabara::OverlayTemplate templ = Unabara::OverlayTemplate::loadFromFile(filePath, &errorMessage);

    // Check if load was successful (template has cells)
    if (templ.cellCount() == 0) {
        qWarning() << "Failed to load template or template is empty";
        if (!errorMessage.isEmpty()) {
            qWarning() << "Error:" << errorMessage;
        }
        return false;
    }

    // Load template into generator
    loadTemplate(templ);

    qDebug() << "Template loaded successfully";
    qDebug() << "  Name:" << templ.templateName();
    qDebug() << "  Cells:" << templ.cellCount();

    Config::instance()->setActiveTemplatePath(filePath);
    emit templateLoaded(filePath);
    return true;
}

void OverlayGenerator::initializeDefaultCellLayout(DiveData* dive)
{
    m_cells.clear();

    // This layout calculation mimics the section-based approach from generateOverlay()
    // to ensure the interactive preview matches the main preview exactly.

    // Load template to get dimensions for size calculation
    QImage templateImage(m_templatePath);
    QSizeF templateSize;
    if (templateImage.isNull()) {
        // Use default dimensions if template can't be loaded
        templateSize = QSizeF(640, 120);
    } else {
        templateSize = QSizeF(templateImage.width(), templateImage.height());
    }

    // Count sections needed (similar to generateOverlay logic)
    int numSections = 0;
    int tankCount = dive ? dive->cylinderCount() : 0;

    // Standard single-cell sections
    if (m_showDepth) numSections++;
    if (m_showTemperature) numSections++;
    if (m_showNDL) numSections++;  // NDL/TTS share the same section
    if (m_showStopDepth) numSections++;
    if (m_showStopTime) numSections++;
    if (m_showTTS) numSections++;
    if (m_showTime) numSections++;
    if (m_showCNS) numSections++;
    if (m_showMeanDepth) numSections++;
    if (m_showMaxDepth) numSections++;
    if (m_showGas) numSections++;

    // Tank sections (multi-tank uses grid, gets multiple sections)
    if (m_showPressure) {
        if (tankCount <= 1) {
            numSections++;  // Single tank = 1 section
        } else if (tankCount == 2) {
            numSections += 2;  // 2 tanks side-by-side = 2 sections
        } else {
            // 3+ tanks: use row-based allocation
            int cols = 2;
            int rows = (tankCount + cols - 1) / cols;
            numSections += rows;
        }
    }

    // CCR PO2 sections (placed on second row, not in section count)

    if (numSections == 0) {
        numSections = 1;  // Avoid division by zero
    }

    // Calculate section width (normalized)
    double sectionWidth = 1.0 / numSections;
    int currentSection = 0;

    // First row Y position
    double yPos = 0.05;

    // Create cells in section order
    if (m_showDepth) {
        Unabara::CellData cell("depth", Unabara::CellType::Depth);
        cell.setPosition(QPointF(currentSection * sectionWidth, yPos));
        cell.setFont(m_font, false);
        seedCellColors(cell);
        cell.setCalculatedSize(calculateCellSize(Unabara::CellType::Depth, m_font, templateSize));
        cell.setVisible(true);
        m_cells.append(cell);
        currentSection++;
    }

    if (m_showTemperature) {
        Unabara::CellData cell("temperature", Unabara::CellType::Temperature);
        cell.setPosition(QPointF(currentSection * sectionWidth, yPos));
        cell.setFont(m_font, false);
        seedCellColors(cell);
        cell.setCalculatedSize(calculateCellSize(Unabara::CellType::Temperature, m_font, templateSize));
        cell.setVisible(true);
        m_cells.append(cell);
        currentSection++;
    }

    if (m_showNDL) {
        Unabara::CellData cell("ndl", Unabara::CellType::NDL);
        cell.setPosition(QPointF(currentSection * sectionWidth, yPos));
        cell.setFont(m_font, false);
        seedCellColors(cell);
        cell.setCalculatedSize(calculateCellSize(Unabara::CellType::NDL, m_font, templateSize));
        cell.setVisible(true);
        m_cells.append(cell);
        currentSection++;
    }

    // Deco stop cells next to NDL/TTS: blank outside deco
    if (m_showStopDepth) {
        Unabara::CellData cell("stop_depth", Unabara::CellType::StopDepth);
        cell.setPosition(QPointF(currentSection * sectionWidth, yPos));
        cell.setFont(m_font, false);
        seedCellColors(cell);
        cell.setCalculatedSize(calculateCellSize(Unabara::CellType::StopDepth, m_font, templateSize));
        cell.setVisible(true);
        m_cells.append(cell);
        currentSection++;
    }

    if (m_showStopTime) {
        Unabara::CellData cell("stop_time", Unabara::CellType::StopTime);
        cell.setPosition(QPointF(currentSection * sectionWidth, yPos));
        cell.setFont(m_font, false);
        seedCellColors(cell);
        cell.setCalculatedSize(calculateCellSize(Unabara::CellType::StopTime, m_font, templateSize));
        cell.setVisible(true);
        m_cells.append(cell);
        currentSection++;
    }

    if (m_showTTS) {
        Unabara::CellData cell("tts", Unabara::CellType::TTS);
        cell.setPosition(QPointF(currentSection * sectionWidth, yPos));
        cell.setFont(m_font, false);
        seedCellColors(cell);
        cell.setCalculatedSize(calculateCellSize(Unabara::CellType::TTS, m_font, templateSize));
        cell.setVisible(true);
        m_cells.append(cell);
        currentSection++;
    }

    // Track maximum Y position for placing second row
    double maxYPos = yPos;

    // Create tank pressure cells
    if (m_showPressure && tankCount > 0) {
        if (tankCount == 1) {
            // Single tank: one section
            Unabara::CellData cell("pressure", Unabara::CellType::Pressure);
            cell.setTankIndex(0);
            cell.setPosition(QPointF(currentSection * sectionWidth, yPos));
            cell.setFont(m_font, false);
            seedCellColors(cell);
            cell.setCalculatedSize(calculateCellSize(Unabara::CellType::Pressure, m_font, templateSize));
            cell.setVisible(true);
            m_cells.append(cell);
            currentSection++;
        } else {
            // Multi-tank: grid layout
            int cols = 2;
            int rows = (tankCount + cols - 1) / cols;

            // Grid spans multiple sections
            int gridSections = (tankCount == 2) ? 2 : rows;
            double gridWidth = gridSections * sectionWidth;
            double gridStartX = currentSection * sectionWidth;

            // Cell dimensions within grid
            double cellWidth = gridWidth / cols;
            double cellHeight = 1.0 / rows;

            for (int i = 0; i < tankCount; ++i) {
                int row = i / cols;
                int col = i % cols;

                double tankX = gridStartX + (col * cellWidth);
                double tankY = yPos + (row * cellHeight);

                QString cellId = QString("tank_%1").arg(i);
                Unabara::CellData cell(cellId, Unabara::CellType::Pressure);
                cell.setTankIndex(i);
                cell.setPosition(QPointF(tankX, tankY));
                cell.setFont(m_font, false);
                seedCellColors(cell);
                cell.setCalculatedSize(calculateCellSize(Unabara::CellType::Pressure, m_font, templateSize));
                cell.setVisible(true);
                m_cells.append(cell);

                // Track max Y for second row placement
                maxYPos = qMax(maxYPos, tankY + cellHeight);
            }

            currentSection += gridSections;
        }
    } else if (m_showPressure) {
        // No dive data: create default pressure cell
        Unabara::CellData cell("pressure", Unabara::CellType::Pressure);
        cell.setTankIndex(0);
        cell.setPosition(QPointF(currentSection * sectionWidth, yPos));
        cell.setFont(m_font, false);
        seedCellColors(cell);
        cell.setCalculatedSize(calculateCellSize(Unabara::CellType::Pressure, m_font, templateSize));
        cell.setVisible(true);
        m_cells.append(cell);
        currentSection++;
    }

    if (m_showTime) {
        Unabara::CellData cell("time", Unabara::CellType::Time);
        cell.setPosition(QPointF(currentSection * sectionWidth, yPos));
        cell.setFont(m_font, false);
        seedCellColors(cell);
        cell.setCalculatedSize(calculateCellSize(Unabara::CellType::Time, m_font, templateSize));
        cell.setVisible(true);
        m_cells.append(cell);
        currentSection++;
    }

    if (m_showCNS) {
        Unabara::CellData cell("cns", Unabara::CellType::CNS);
        cell.setPosition(QPointF(currentSection * sectionWidth, yPos));
        cell.setFont(m_font, false);
        seedCellColors(cell);
        cell.setCalculatedSize(calculateCellSize(Unabara::CellType::CNS, m_font, templateSize));
        cell.setVisible(true);
        m_cells.append(cell);
        currentSection++;
    }

    if (m_showMeanDepth) {
        Unabara::CellData cell("mean_depth", Unabara::CellType::MeanDepth);
        cell.setPosition(QPointF(currentSection * sectionWidth, yPos));
        cell.setFont(m_font, false);
        seedCellColors(cell);
        cell.setCalculatedSize(calculateCellSize(Unabara::CellType::MeanDepth, m_font, templateSize));
        cell.setVisible(true);
        m_cells.append(cell);
        currentSection++;
    }

    if (m_showMaxDepth) {
        Unabara::CellData cell("max_depth", Unabara::CellType::MaxDepth);
        cell.setPosition(QPointF(currentSection * sectionWidth, yPos));
        cell.setFont(m_font, false);
        seedCellColors(cell);
        cell.setCalculatedSize(calculateCellSize(Unabara::CellType::MaxDepth, m_font, templateSize));
        cell.setVisible(true);
        m_cells.append(cell);
        currentSection++;
    }

    if (m_showGas) {
        Unabara::CellData cell("gas", Unabara::CellType::Gas);
        cell.setPosition(QPointF(currentSection * sectionWidth, yPos));
        cell.setFont(m_font, false);
        seedCellColors(cell);
        cell.setCalculatedSize(calculateCellSize(Unabara::CellType::Gas, m_font, templateSize));
        cell.setVisible(true);
        m_cells.append(cell);
        currentSection++;
    }

    // CCR PO2 cells - second row
    // Count how many PO2 cells to show
    int po2CellCount = 0;
    if (m_showPO2Cell1) po2CellCount++;
    if (m_showPO2Cell2) po2CellCount++;
    if (m_showPO2Cell3) po2CellCount++;
    if (m_showCompositePO2) po2CellCount++;

    if (po2CellCount > 0) {
        double po2YPos = 0.5;  // Below first row with spacing
        double po2SectionWidth = 1.0 / po2CellCount;  // Divide second row by PO2 cell count
        int po2Section = 0;

        if (m_showPO2Cell1) {
            Unabara::CellData cell("po2_cell1", Unabara::CellType::PO2Cell1);
            cell.setPosition(QPointF(po2Section * po2SectionWidth, po2YPos));
            cell.setFont(m_font, false);
            seedCellColors(cell);
            cell.setCalculatedSize(calculateCellSize(Unabara::CellType::PO2Cell1, m_font, templateSize));
            cell.setVisible(true);
            m_cells.append(cell);
            po2Section++;
        }

        if (m_showPO2Cell2) {
            Unabara::CellData cell("po2_cell2", Unabara::CellType::PO2Cell2);
            cell.setPosition(QPointF(po2Section * po2SectionWidth, po2YPos));
            cell.setFont(m_font, false);
            seedCellColors(cell);
            cell.setCalculatedSize(calculateCellSize(Unabara::CellType::PO2Cell2, m_font, templateSize));
            cell.setVisible(true);
            m_cells.append(cell);
            po2Section++;
        }

        if (m_showPO2Cell3) {
            Unabara::CellData cell("po2_cell3", Unabara::CellType::PO2Cell3);
            cell.setPosition(QPointF(po2Section * po2SectionWidth, po2YPos));
            cell.setFont(m_font, false);
            seedCellColors(cell);
            cell.setCalculatedSize(calculateCellSize(Unabara::CellType::PO2Cell3, m_font, templateSize));
            cell.setVisible(true);
            m_cells.append(cell);
            po2Section++;
        }

        if (m_showCompositePO2) {
            Unabara::CellData cell("composite_po2", Unabara::CellType::CompositePO2);
            cell.setPosition(QPointF(po2Section * po2SectionWidth, po2YPos));
            cell.setFont(m_font, false);
            seedCellColors(cell);
            cell.setCalculatedSize(calculateCellSize(Unabara::CellType::CompositePO2, m_font, templateSize));
            cell.setVisible(true);
            m_cells.append(cell);
            po2Section++;
        }
    }

    qDebug() << "Initialized" << m_cells.size() << "cells for default layout (sections:" << numSections << ")";

    emit cellsChanged();
}

void OverlayGenerator::migrateLegacySettings()
{
    // This converts the current auto-layout settings to cell-based layout
    // by initializing cells with positions calculated from the current layout

    initializeDefaultCellLayout();
    m_useCellBasedLayout = true;

    qDebug() << "Migrated legacy settings to cell-based layout";
    emit cellLayoutChanged();
}


QImage OverlayGenerator::generateOverlay(DiveData* dive, double timePoint)
{
    if (!dive) {
        qWarning() << "No dive data provided for overlay generation";
        return QImage();
    }
    
    // qDebug() << "Generating overlay for time point:" << timePoint;
    
    // Load the template image
    QImage templateImage(m_templatePath);
    if (templateImage.isNull()) {
        qWarning() << "Failed to load template image:" << m_templatePath;
        // Create a default black background if template can't be loaded
        templateImage = QImage(640, 120, QImage::Format_ARGB32);
        templateImage.fill(QColor(0, 0, 0, 180));
    }
    
    // Create the result image and apply background opacity
    QImage result = templateImage.copy();

    // Apply background opacity if needed
    if (m_backgroundOpacity < 1.0) {
        QPainter opacityPainter(&result);
        opacityPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        QColor opacityColor(255, 255, 255, static_cast<int>(m_backgroundOpacity * 255));
        opacityPainter.fillRect(result.rect(), opacityColor);
        opacityPainter.end();
    }
    
    // Get the data for the current time point
    DiveDataPoint dataPoint = dive->dataAtTime(timePoint);
    
    // qDebug() << "Data point for overlay - depth:" << dataPoint.depth 
    //          << "temp:" << dataPoint.temperature
    //          << "time:" << dataPoint.timestamp
    //          << "ndl:" << dataPoint.ndl
    //          << "tts:" << dataPoint.tts;
    
    // Set up the painter
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    // Check if we should use cell-based layout (from loaded template)
    if (m_useCellBasedLayout && !m_cells.isEmpty()) {
        // Use cell-based rendering with custom positions from template
        renderCellBasedOverlay(painter, result.size(), dataPoint, dive);
    } else {
        // Use legacy section-based automatic layout
        renderSectionBasedOverlay(painter, result.size(), dataPoint, dive);
    }

    painter.end();
    return result;
}

// Generate display text for a cell - matches CellModel::formatValue() for QML consistency
QString OverlayGenerator::generateCellDisplayText(Unabara::CellType cellType,
                                                   const DiveDataPoint& dataPoint,
                                                   int tankIndex, DiveData* dive,
                                                   bool showLabel) const
{
    Units::UnitSystem unitSystem = Config::instance()->unitSystem();
    // ndl == 0 is a reported deco state; ndl < 0 means the computer never
    // reported NDL (must NOT read as deco — shows "NDL ---" instead).
    bool inDeco = (dataPoint.ndl == 0.0);

    auto format = [showLabel](const QString& label, const QString& value) {
        return showLabel ? QString("%1\n%2").arg(label, value) : value;
    };

    switch (cellType) {
    case Unabara::CellType::Depth:
        return format("DEPTH", Units::formatDepthValue(dataPoint.depth, unitSystem));

    case Unabara::CellType::Temperature:
        return format("TEMP", Units::formatTemperatureValue(dataPoint.temperature, unitSystem));

    case Unabara::CellType::Time: {
        int totalSeconds = static_cast<int>(dataPoint.timestamp);
        int minutes = totalSeconds / 60;
        int seconds = totalSeconds % 60;
        return format("DIVE TIME",
                      QString("%1:%2").arg(minutes).arg(seconds, 2, 10, QChar('0')));
    }

    case Unabara::CellType::NDL:
        // Dynamically switch between NDL and TTS based on deco status. The
        // deco details (stop depth/time) live in the StopDepth/StopTime cells
        // so this cell keeps a stable two-line geometry.
        if (inDeco) {
            QString ttsStr = dataPoint.tts > 0 ? QString("%1 min").arg(qRound(dataPoint.tts)) : "---";
            return format("TTS", ttsStr);
        } else {
            QString ndlStr = dataPoint.ndl > 0 ? QString("%1 min").arg(qRound(dataPoint.ndl)) : "---";
            return format("NDL", ndlStr);
        }

    case Unabara::CellType::TTS: {
        QString ttsStr = dataPoint.tts > 0 ? QString("%1 min").arg(qRound(dataPoint.tts)) : "---";
        return format("TTS", ttsStr);
    }

    case Unabara::CellType::StopDepth: {
        // Blank value outside deco: parsers carry stop values forward, so the
        // inDeco gate (not the value alone) decides visibility
        bool show = inDeco && dataPoint.ceiling > 0;
        QString value = show ? Units::formatDepthValue(dataPoint.ceiling, unitSystem)
                             : QString();
        return format("STOP", value);
    }

    case Unabara::CellType::StopTime: {
        bool show = inDeco && dataPoint.stopTime > 0;
        // Round up: a 22 s remaining stop is still a stop — "0 min" would
        // read as cleared (Garmin reports this field in seconds)
        QString value = show ? QString("%1 min").arg(qCeil(dataPoint.stopTime))
                             : QString();
        return format("TIME", value);
    }

    case Unabara::CellType::Pressure: {
        // Get the pressure value
        QString value;
        if (tankIndex >= 0 && tankIndex < dataPoint.pressures.size()) {
            value = Units::formatPressureValue(dataPoint.pressures[tankIndex], unitSystem);
        } else if (!dataPoint.pressures.isEmpty()) {
            value = Units::formatPressureValue(dataPoint.pressures[0], unitSystem);
        } else {
            value = "---";
        }

        // Create tank label with gas mix info
        QString label;
        int tankCount = dataPoint.tankCount();

        if (tankCount > 1 && dive && tankIndex < dive->cylinderCount()) {
            const CylinderInfo& cylinder = dive->cylinderInfo(tankIndex);
            QString gasMix;
            if (cylinder.hePercent > 0.0) {
                gasMix = QString("(%1/%2)").arg(qRound(cylinder.o2Percent)).arg(qRound(cylinder.hePercent));
            } else if (cylinder.o2Percent != 21.0) {
                gasMix = QString("(%1%)").arg(qRound(cylinder.o2Percent));
            }
            if (!gasMix.isEmpty()) {
                label = QString("T%1 %2").arg(tankIndex + 1).arg(gasMix);
            } else {
                label = QString("TNK %1").arg(tankIndex + 1);
            }
        } else if (tankCount == 1 && dive && tankIndex < dive->cylinderCount()) {
            const CylinderInfo& cylinder = dive->cylinderInfo(tankIndex);
            QString gasMix;
            if (cylinder.hePercent > 0.0) {
                gasMix = QString("(%1/%2)").arg(qRound(cylinder.o2Percent)).arg(qRound(cylinder.hePercent));
            } else if (cylinder.o2Percent != 21.0) {
                gasMix = QString("(%1%)").arg(qRound(cylinder.o2Percent));
            }
            if (!gasMix.isEmpty()) {
                label = QString("TANK %1 %2").arg(tankIndex + 1).arg(gasMix);
            } else {
                label = QString("TANK %1").arg(tankIndex + 1);
            }
        } else {
            label = "PRESSURE";
        }

        return format(label, value);
    }

    case Unabara::CellType::PO2Cell1: {
        QString value = (!dataPoint.po2Sensors.isEmpty() && dataPoint.po2Sensors.size() > 0)
            ? QString("%1").arg(dataPoint.po2Sensors[0], 0, 'f', 2)
            : "---";
        return format("CELL 1", value);
    }

    case Unabara::CellType::PO2Cell2: {
        QString value = (dataPoint.po2Sensors.size() > 1)
            ? QString("%1").arg(dataPoint.po2Sensors[1], 0, 'f', 2)
            : "---";
        return format("CELL 2", value);
    }

    case Unabara::CellType::PO2Cell3: {
        QString value = (dataPoint.po2Sensors.size() > 2)
            ? QString("%1").arg(dataPoint.po2Sensors[2], 0, 'f', 2)
            : "---";
        return format("CELL 3", value);
    }

    case Unabara::CellType::CompositePO2: {
        double compositePO2 = dataPoint.getCompositePO2();
        QString value = (compositePO2 > 0)
            ? QString("%1").arg(compositePO2, 0, 'f', 2)
            : "---";
        return format("PO2", value);
    }

    case Unabara::CellType::CNS: {
        QString value = (dataPoint.cns >= 0)
            ? QString("%1%").arg(qRound(dataPoint.cns))
            : "---";
        return format("CNS", value);
    }

    case Unabara::CellType::MeanDepth: {
        // Static per-dive value (reported by the dive computer or computed from samples)
        QString value = dive
            ? Units::formatDepthValue(dive->meanDepth(), unitSystem)
            : "---";
        return format("AVG", value);
    }

    case Unabara::CellType::MaxDepth: {
        // Running max: deepest point reached up to the current time
        QString value = dive
            ? Units::formatDepthValue(dive->maxDepthUntil(dataPoint.timestamp), unitSystem)
            : "---";
        return format("MAX", value);
    }

    case Unabara::CellType::Gas: {
        // Gas mix currently breathed: active cylinder per gas-switch events
        QString value = "---";
        if (dive && dive->cylinderCount() > 0) {
            int idx = dive->activeCylinderAtTime(dataPoint.timestamp);
            if (idx >= 0 && idx < dive->cylinderCount()) {
                const CylinderInfo& cyl = dive->cylinderInfo(idx);
                value = Units::formatGasMix(cyl.o2Percent, cyl.hePercent);
            }
        }
        return format("GAS", value);
    }

    default:
        return "Unknown";
    }
}

namespace {

// One separable sliding-window box blur pass over a premultiplied ARGB32 image.
// Blurring all 4 channels of premultiplied data is alpha-correct.
void boxBlurPass(QImage& img, int radius)
{
    const int w = img.width();
    const int h = img.height();
    const int window = 2 * radius + 1;
    QVector<QRgb> line(qMax(w, h));

    // Horizontal pass
    for (int y = 0; y < h; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        int sumA = 0, sumR = 0, sumG = 0, sumB = 0;
        for (int x = -radius; x <= radius; ++x) {
            QRgb p = row[qBound(0, x, w - 1)];
            sumA += qAlpha(p); sumR += qRed(p); sumG += qGreen(p); sumB += qBlue(p);
        }
        for (int x = 0; x < w; ++x) {
            line[x] = qRgba(sumR / window, sumG / window, sumB / window, sumA / window);
            QRgb pOut = row[qBound(0, x - radius, w - 1)];
            QRgb pIn = row[qBound(0, x + radius + 1, w - 1)];
            sumA += qAlpha(pIn) - qAlpha(pOut);
            sumR += qRed(pIn) - qRed(pOut);
            sumG += qGreen(pIn) - qGreen(pOut);
            sumB += qBlue(pIn) - qBlue(pOut);
        }
        memcpy(row, line.constData(), w * sizeof(QRgb));
    }

    // Vertical pass
    const qsizetype stride = img.bytesPerLine() / sizeof(QRgb);
    QRgb* bits = reinterpret_cast<QRgb*>(img.bits());
    for (int x = 0; x < w; ++x) {
        int sumA = 0, sumR = 0, sumG = 0, sumB = 0;
        for (int y = -radius; y <= radius; ++y) {
            QRgb p = bits[qBound(0, y, h - 1) * stride + x];
            sumA += qAlpha(p); sumR += qRed(p); sumG += qGreen(p); sumB += qBlue(p);
        }
        for (int y = 0; y < h; ++y) {
            line[y] = qRgba(sumR / window, sumG / window, sumB / window, sumA / window);
            QRgb pOut = bits[qBound(0, y - radius, h - 1) * stride + x];
            QRgb pIn = bits[qBound(0, y + radius + 1, h - 1) * stride + x];
            sumA += qAlpha(pIn) - qAlpha(pOut);
            sumR += qRed(pIn) - qRed(pOut);
            sumG += qGreen(pIn) - qGreen(pOut);
            sumB += qBlue(pIn) - qBlue(pOut);
        }
        for (int y = 0; y < h; ++y) {
            bits[y * stride + x] = line[y];
        }
    }
}

// Three box blur passes approximate a gaussian blur.
void boxBlur(QImage& img, int radius)
{
    if (radius < 1) return;
    for (int i = 0; i < 3; ++i) {
        boxBlurPass(img, radius);
    }
}

} // anonymous namespace

void OverlayGenerator::renderCellBasedOverlay(QPainter& painter, const QSize& imageSize,
                                              const DiveDataPoint& dataPoint, DiveData* dive)
{
    int width = imageSize.width();
    int height = imageSize.height();

    // qDebug() << "Rendering cell-based overlay with" << m_cells.size() << "cells (QML-style)";

    for (const auto& cell : m_cells) {
        if (!cell.visible()) continue;

        // Get effective font and colors (same as before)
        QFont effectiveFont = cell.hasCustomFont() ? cell.font() : m_font;
        QColor effectiveLabelColor = cell.hasCustomLabelColor() ? cell.labelColor() : m_labelColor;
        QColor effectiveValueColor = cell.hasCustomValueColor() ? cell.valueColor() : m_valueColor;

        // Effective shadow settings (single hasCustomShadow flag covers the group)
        const bool customShadow = cell.hasCustomShadow();
        const bool shadowEnabled = customShadow ? cell.shadowEnabled() : m_shadowEnabled;
        const Unabara::ShadowType shadowType = customShadow ? cell.shadowType() : m_shadowType;
        QColor shadowColor = customShadow ? cell.shadowColor() : m_shadowColor;
        const int shadowSize = customShadow ? cell.shadowSize() : m_shadowSize;
        const double shadowOpacity = customShadow ? cell.shadowOpacity() : m_shadowOpacity;

        // Generate displayText (same format as QML CellModel)
        QString displayText = generateCellDisplayText(cell.cellType(), dataPoint,
                                                       cell.tankIndex(), dive,
                                                       cell.showLabel());

        // Scale font for template resolution (match calculateCellSize behavior)
        // QML renders at preview size, but C++ renders at full template resolution
        // then scales down, so we need scaled fonts to match
        QFont renderFont = effectiveFont;
        renderFont.setPixelSize(getScaledFontSize(effectiveFont, 1.8));

        // Calculate text size using font metrics (like QML does)
        painter.setFont(renderFont);
        QFontMetrics fm(renderFont);
        QRect textBounds = fm.boundingRect(QRect(0, 0, 1000, 1000),
                                           Qt::AlignHCenter | Qt::TextWordWrap, displayText);

        // Add padding (QML uses +8 for width and height)
        int cellWidth = textBounds.width() + 8;
        int cellHeight = textBounds.height() + 8;

        // Convert normalized position (0-1) to pixel position
        int pixelX = static_cast<int>(cell.position().x() * width);
        int pixelY = static_cast<int>(cell.position().y() * height);

        // Create cell rect for text
        QRect cellRect(pixelX + 4, pixelY + 4, cellWidth - 8, cellHeight - 8);

        // Draw semi-transparent background (like QML's "#80000000" Rectangle)
        // Only in editor mode, not for export/preview
        if (m_showCellBackgrounds) {
            QRect bgRect(pixelX, pixelY, cellWidth, cellHeight);
            painter.fillRect(bgRect, QColor(0, 0, 0, 128));
        }

        // Draw the shadow first, if enabled (same 1.8 scale factor as fonts)
        if (shadowEnabled && !displayText.isEmpty()) {
            shadowColor.setAlphaF(shadowColor.alphaF() * shadowOpacity);
            const int spx = qMax(1, qRound(shadowSize * 1.8));

            switch (shadowType) {
            case Unabara::ShadowType::Offset:
                painter.setPen(shadowColor);
                painter.drawText(cellRect.translated(spx, spx),
                                 Qt::AlignHCenter | Qt::TextDontClip, displayText);
                break;
            case Unabara::ShadowType::Outline: {
                static const QPoint dirs[8] = {{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}};
                painter.setPen(shadowColor);
                for (const QPoint& d : dirs) {
                    painter.drawText(cellRect.translated(d.x() * spx, d.y() * spx),
                                     Qt::AlignHCenter | Qt::TextDontClip, displayText);
                }
                break;
            }
            case Unabara::ShadowType::Blurred: {
                // Render the text into its own image, blur it, composite offset
                const int margin = spx * 3;  // room for the blur to spread
                QImage shadowImg(cellWidth + 2 * margin, cellHeight + 2 * margin,
                                 QImage::Format_ARGB32_Premultiplied);
                shadowImg.fill(Qt::transparent);
                {
                    QPainter sp(&shadowImg);
                    sp.setFont(renderFont);
                    sp.setPen(shadowColor);
                    sp.drawText(QRect(margin + 4, margin + 4, cellWidth - 8, cellHeight - 8),
                                Qt::AlignHCenter | Qt::TextDontClip, displayText);
                }
                boxBlur(shadowImg, spx);
                painter.drawImage(QPoint(pixelX - margin + spx, pixelY - margin + spx), shadowImg);
                break;
            }
            }
        }

        // Draw the text (center-aligned to match QML's Text.AlignHCenter).
        // The label line and the value line have independent colors: the
        // combined string is drawn twice with complementary clip bands so the
        // multi-line layout stays byte-identical to a single drawText (drawing
        // the lines separately drifts by a pixel for some font sizes).
        if (displayText.indexOf(QLatin1Char('\n')) < 0) {
            painter.setPen(effectiveValueColor);
            painter.drawText(cellRect, Qt::AlignHCenter, displayText);
        } else {
            const QRect labelBand(cellRect.x(), cellRect.y(),
                                  cellRect.width(), fm.lineSpacing());
            painter.save();
            painter.setClipRect(labelBand);
            painter.setPen(effectiveLabelColor);
            painter.drawText(cellRect, Qt::AlignHCenter, displayText);
            painter.setClipRect(QRect(labelBand.x(), labelBand.y() + labelBand.height(),
                                      labelBand.width(),
                                      cellRect.height() - labelBand.height()));
            painter.setPen(effectiveValueColor);
            painter.drawText(cellRect, Qt::AlignHCenter, displayText);
            painter.restore();
        }
    }
}

// Frozen legacy path: only reachable with an empty cell list, which cannot
// happen in practice (the constructor always builds a default layout). Kept
// as-is; it still renders the old combined TTS + DECO line.
void OverlayGenerator::renderSectionBasedOverlay(QPainter& painter, const QSize& imageSize,
                                                  const DiveDataPoint& dataPoint, DiveData* dive)
{
    painter.setFont(m_font);
    painter.setPen(m_valueColor);

    int width = imageSize.width();
    int height = imageSize.height();
    bool inDeco = (dataPoint.ndl <= 0);

    // Calculate how many sections we need and their relative sizes
    int numSections = 0;
    int tankCount = dataPoint.tankCount();
    int tankSectionWidth = 0;

    // Count standard sections
    if (m_showDepth) numSections++;
    if (m_showTemperature) numSections++;
    if (m_showNDL && !inDeco) numSections++; // Show NDL only if not in deco
    if (inDeco) numSections++; // Show TTS when in deco
    if (m_showTime) numSections++;

    // Count CCR sensor sections (only if dive has PO2 data)
    int po2SensorCount = dataPoint.po2SensorCount();

    // Check if CCR settings are enabled regardless of current PO2 data availability
    bool anyCCREnabled = m_showPO2Cell1 ||
                         m_showPO2Cell2 ||
                         m_showPO2Cell3 ||
                         m_showCompositePO2;

    if (anyCCREnabled && po2SensorCount > 0) {
        if (m_showPO2Cell1 ||
            (m_showPO2Cell2 && po2SensorCount > 1) ||
            (m_showPO2Cell3 && po2SensorCount > 2)) {
            numSections++;
        }
        if (m_showCompositePO2) {
            numSections++;
        }
    } else if (anyCCREnabled && po2SensorCount == 0) {
        if (m_showPO2Cell1 ||
            m_showPO2Cell2 ||
            m_showPO2Cell3) {
            numSections++;
        }
        if (m_showCompositePO2) {
            numSections++;
        }
    }

    // Handle tank section sizing
    if (m_showPressure && tankCount > 0) {
        if (tankCount == 1) {
            numSections++;
            tankSectionWidth = 0;
        } else if (tankCount > 1) {
            int cols = 2;
            int rows = (tankCount + cols - 1) / cols;
            if (tankCount == 2) {
                numSections += 2;
            } else {
                numSections += rows;
            }
        }
    } else if (m_showPressure) {
        numSections++;
    }

    if (numSections == 0) {
        return;
    }

    // Calculate section width
    int sectionWidth = width / numSections;

    // Create all section rectangles
    QVector<QRect> sectionRects;
    for (int i = 0; i < numSections; i++) {
        sectionRects.append(QRect(i * sectionWidth, 0, sectionWidth, height));
    }

    // Current section index
    int currentSection = 0;

    // Draw each enabled data section
    if (m_showDepth) {
        drawDepth(painter, dataPoint.depth, sectionRects[currentSection++]);
    }

    if (m_showTemperature) {
        drawTemperature(painter, dataPoint.temperature, sectionRects[currentSection++]);
    }

    if (inDeco) {
        drawTTS(painter, dataPoint.tts, sectionRects[currentSection++], dataPoint.ceiling);
    } else if (m_showNDL) {
        drawNDL(painter, dataPoint.ndl, sectionRects[currentSection++]);
    }

    if (m_showPressure) {
        int tankCount = dataPoint.tankCount();

        if (tankCount == 1) {
            double pressure = dataPoint.getPressure(0);
            drawPressure(painter, pressure, sectionRects[currentSection++], 0, dive);
        } else if (tankCount > 1) {
            int cols = 2;
            int rows = (tankCount + cols - 1) / cols;

            int tanksWidth;
            if (tankCount == 2) {
                tanksWidth = sectionWidth * 2;
            } else {
                tanksWidth = sectionWidth * rows;
            }

            QRect tankGridRect(currentSection * sectionWidth, 0, tanksWidth, height);
            int cellWidth = tankGridRect.width() / cols;
            int cellHeight = height / ((tankCount > 2) ? ((tankCount + 1) / 2) : 1);

            for (int i = 0; i < tankCount; i++) {
                int row = i / cols;
                int col = i % cols;

                QRect tankRect(
                    tankGridRect.left() + (col * cellWidth),
                    row * cellHeight,
                    cellWidth,
                    cellHeight
                );
                tankRect.adjust(3, 3, -3, -3);

                double pressure = dataPoint.getPressure(i);
                drawPressure(painter, pressure, tankRect, i, dive);
            }

            if (tankCount == 2) {
                currentSection += 2;
            } else {
                currentSection += rows;
            }
        } else {
            drawPressure(painter, 0.0, sectionRects[currentSection++], -1, dive);
        }
    }

    if (m_showTime) {
        drawTime(painter, dataPoint.timestamp, sectionRects[currentSection++]);
    }

    // Draw CCR sensors
    if (anyCCREnabled) {
        if (m_showPO2Cell1 ||
            m_showPO2Cell2 ||
            m_showPO2Cell3) {
            QRect cellGridRect = sectionRects[currentSection++];

            QVector<int> cellsToShow;
            if (m_showPO2Cell1) cellsToShow.append(1);
            if (m_showPO2Cell2) cellsToShow.append(2);
            if (m_showPO2Cell3) cellsToShow.append(3);

            int cellCount = cellsToShow.size();
            if (cellCount == 1) {
                int cellNum = cellsToShow[0];
                drawPO2Cell(painter, dataPoint.getPO2Sensor(cellNum - 1), cellGridRect, cellNum);
            } else {
                int cols = (cellCount <= 2) ? cellCount : 2;
                int rows = (cellCount + cols - 1) / cols;

                int cellWidth = cellGridRect.width() / cols;
                int cellHeight = cellGridRect.height() / rows;

                for (int i = 0; i < cellCount; i++) {
                    int row = i / cols;
                    int col = i % cols;

                    QRect cellRect(
                        cellGridRect.left() + (col * cellWidth),
                        cellGridRect.top() + (row * cellHeight),
                        cellWidth,
                        cellHeight
                    );
                    cellRect.adjust(2, 2, -2, -2);

                    int cellNum = cellsToShow[i];
                    drawPO2Cell(painter, dataPoint.getPO2Sensor(cellNum - 1), cellRect, cellNum);
                }
            }
        }

        if (m_showCompositePO2) {
            drawCompositePO2(painter, dataPoint.getCompositePO2(), sectionRects[currentSection++]);
        }
    }
}

QImage OverlayGenerator::generatePreview(DiveData* dive)
{
    if (!dive) {
        qWarning() << "No dive data provided for preview generation";
        return QImage();
    }

    // For preview, use data from the middle of the dive
    double timePoint = dive->durationSeconds() / 2.0;
    return generateOverlay(dive, timePoint);
}

int OverlayGenerator::getScaledFontSize(const QFont& baseFont, double scale) const {
    int baseSize = baseFont.pointSize();
    if (baseSize <= 0) {
        // Fallback to pixel size if point size is not set
        baseSize = baseFont.pixelSize();
        if (baseSize <= 0) {
            baseSize = 12; // Final fallback
        }
    }

    // Convert point size to approximate pixel size and apply scale
    return static_cast<int>(baseSize * 1.33 * scale); // 1.33 is approximate point-to-pixel ratio
}

QSizeF OverlayGenerator::calculateCellSize(Unabara::CellType cellType, const QFont& font, const QSizeF& templateSize, const QString& sampleText) const {
    // Base padding
    int padding = 5;

    // Create fonts for header and value
    QFont headerFont = font;
    headerFont.setPixelSize(getScaledFontSize(font, 1.5));

    QFont valueFont = font;
    valueFont.setPixelSize(getScaledFontSize(font, 1.8));
    valueFont.setBold(true);

    // Get font metrics
    QFontMetrics headerMetrics(headerFont);
    QFontMetrics valueMetrics(valueFont);

    // Calculate dimensions based on cell type
    QString header;
    QString value = sampleText;

    // Set default sample values if not provided
    // Values chosen to represent typical display values for reasonable sizing
    if (value.isEmpty()) {
        switch (cellType) {
            case Unabara::CellType::Depth:
                header = tr("DEPTH");
                value = "99.9 m";    // Typical recreational diving depth
                break;
            case Unabara::CellType::Temperature:
                header = tr("TEMP");
                value = "25.5°C";    // Typical water temperature
                break;
            case Unabara::CellType::Time:
                header = tr("TIME");
                value = "99:99";     // Typical dive time format
                break;
            case Unabara::CellType::NDL:
                header = tr("NDL");
                value = "99 min";    // Typical NDL value
                break;
            case Unabara::CellType::TTS:
                header = tr("TTS");
                value = "99 min";    // Typical TTS value
                break;
            case Unabara::CellType::Pressure:
                header = tr("TANK 1");   // Typical tank number
                value = "199 bar";       // Typical pressure value
                break;
            case Unabara::CellType::PO2Cell1:
            case Unabara::CellType::PO2Cell2:
            case Unabara::CellType::PO2Cell3:
                header = tr("CELL 1");
                value = "1.29 bar";      // Typical PO2 for CCR cells
                break;
            case Unabara::CellType::CompositePO2:
                header = tr("PPO2");
                value = "1.29 bar";      // Typical composite PO2
                break;
            case Unabara::CellType::CNS:
                header = tr("CNS");
                value = "99%";           // Typical CNS toxicity percentage
                break;
            case Unabara::CellType::MeanDepth:
                header = tr("AVG");
                value = "99.9 m";        // Typical mean depth
                break;
            case Unabara::CellType::MaxDepth:
                header = tr("MAX");
                value = "99.9 m";        // Typical max depth
                break;
            case Unabara::CellType::Gas:
                header = tr("GAS");
                value = "EAN32";         // Widest plausible mix string
                break;
            case Unabara::CellType::StopDepth:
                header = tr("STOP");
                value = "99.9 m";        // Sized for the populated (in-deco) state
                break;
            case Unabara::CellType::StopTime:
                header = tr("TIME");
                value = "99 min";        // Sized for the populated (in-deco) state
                break;
            default:
                header = "UNKNOWN";
                value = "99.99";
                break;
        }
    }

    // Calculate header size
    QRect headerBounds = headerMetrics.boundingRect(header);

    // Calculate value size (handle multi-line for TTS)
    QRect valueBounds;
    if (value.contains('\n')) {
        // Multi-line text (TTS with ceiling)
        QStringList lines = value.split('\n');
        int maxWidth = 0;
        int totalHeight = 0;
        for (const QString& line : lines) {
            QRect lineBounds = valueMetrics.boundingRect(line);
            maxWidth = qMax(maxWidth, lineBounds.width());
            totalHeight += lineBounds.height();
        }
        valueBounds = QRect(0, 0, maxWidth, totalHeight);
    } else {
        valueBounds = valueMetrics.boundingRect(value);
    }

    // Calculate total dimensions in pixels
    int pixelWidth = qMax(headerBounds.width(), valueBounds.width()) + 2 * padding;
    int pixelHeight = headerBounds.height() + valueBounds.height() + 4 * padding;  // Extra padding between header and value

    // Ensure minimum size for readability
    pixelWidth = qMax(pixelWidth, 80);
    pixelHeight = qMax(pixelHeight, 60);

    // Return pixel dimensions (not normalized)
    // QML will normalize these based on actual template dimensions
    return QSizeF(pixelWidth, pixelHeight);
}

void OverlayGenerator::drawDepth(QPainter &painter, double depth, const QRect &rect) {
    Units::UnitSystem unitSystem = Config::instance()->unitSystem();
    QString depthStr = Units::formatDepthValue(depth, unitSystem);

    painter.save();

    // Draw the header using the helper function
    drawSectionHeader(painter, tr("DEPTH"), rect);

    // Proportional positioning for value
    int padding = qMax(2, rect.height() / 20);
    int headerHeight = rect.height() * 35 / 100;
    int valueHeight = rect.height() * 50 / 100;

    // Draw the value below the header
    QFont valueFont = painter.font();
    int valueFontSize = qMax(10, valueHeight * 80 / 100);  // 80% of value height
    valueFont.setPixelSize(valueFontSize);
    valueFont.setBold(true);
    painter.setFont(valueFont);

    // Position value below header
    QRect valueRect(rect.left() + padding, rect.top() + headerHeight + padding,
                    rect.width() - 2*padding, valueHeight);

    painter.drawText(valueRect, Qt::AlignCenter, depthStr);

    painter.restore();
}

void OverlayGenerator::drawTemperature(QPainter &painter, double temp, const QRect &rect) {
    Units::UnitSystem unitSystem = Config::instance()->unitSystem();
    QString tempStr = Units::formatTemperatureValue(temp, unitSystem);

    painter.save();

    // Draw the header using the helper function
    drawSectionHeader(painter, tr("TEMP"), rect);

    // Proportional positioning for value
    int padding = qMax(2, rect.height() / 20);
    int headerHeight = rect.height() * 35 / 100;
    int valueHeight = rect.height() * 50 / 100;

    // Draw the value below the header
    QFont valueFont = painter.font();
    int valueFontSize = qMax(10, valueHeight * 80 / 100);
    valueFont.setPixelSize(valueFontSize);
    valueFont.setBold(true);
    painter.setFont(valueFont);

    // Position value below header
    QRect valueRect(rect.left() + padding, rect.top() + headerHeight + padding,
                    rect.width() - 2*padding, valueHeight);

    painter.drawText(valueRect, Qt::AlignCenter, tempStr);

    painter.restore();
}

void OverlayGenerator::drawPressure(QPainter &painter, double pressure, const QRect &rect, int tankIndex, DiveData* dive) {
    painter.save();

    qDebug() << "Drawing pressure for tank index:" << tankIndex
             << "Pressure:" << pressure;
    // Get tank count for adaptive layout
    int tankCount = dive ? dive->cylinderCount() : 1;

    qDebug() << "Tank count:" << tankCount;

    // Proportional positioning
    int padding = qMax(2, rect.height() / 20);
    int headerHeight = rect.height() * 35 / 100;
    int valueHeight = rect.height() * 50 / 100;

    // Create tank label
    QString label;
    if (tankIndex >= 0) {
        // Get gas mix info
        QString gasMix;
        if (dive && tankIndex < dive->cylinderCount()) {
            const CylinderInfo& cylinder = dive->cylinderInfo(tankIndex);

            if (cylinder.hePercent > 0.0) {
                gasMix = QString("(%1/%2)").arg(qRound(cylinder.o2Percent)).arg(qRound(cylinder.hePercent));
            } else if (cylinder.o2Percent != 21.0) {
                gasMix = QString("(%1%)").arg(qRound(cylinder.o2Percent));
            }
        }

        // Use shorter labels for multi-tank displays (2 or more tanks)
        if (tankCount > 1) {
            if (!gasMix.isEmpty()) {
                label = tr("T%1 %2").arg(tankIndex + 1).arg(gasMix);
            } else {
                label = tr("TNK %1").arg(tankIndex + 1);
            }
        } else {
            if (!gasMix.isEmpty()) {
                label = tr("TANK %1 %2").arg(tankIndex + 1).arg(gasMix);
            } else {
                label = tr("TANK %1").arg(tankIndex + 1);
            }
        }
    } else {
        label = tr("PRESSURE");
    }

    // Draw header with proportional font size
    QFont headerFont = painter.font();
    int headerFontSize = qMax(8, headerHeight * (tankCount > 1 ? 60 : 70) / 100);
    headerFont.setPixelSize(headerFontSize);
    painter.setFont(headerFont);

    // Proportional label rect
    QRect labelRect(rect.left() + padding, rect.top() + padding,
                   rect.width() - 2*padding, headerHeight);

    // Draw the label with eliding if necessary
    QFontMetrics fm(headerFont);
    QString displayLabel = label;
    if (fm.horizontalAdvance(label) > labelRect.width()) {
        displayLabel = fm.elidedText(label, Qt::ElideRight, labelRect.width());
    }

    painter.drawText(labelRect, Qt::AlignCenter, displayLabel);

    // Draw pressure value with proportional sizing
    QFont valueFont = painter.font();
    int valueFontSize = qMax(10, valueHeight * (tankCount > 1 ? 70 : 80) / 100);
    valueFont.setPixelSize(valueFontSize);
    valueFont.setBold(true);
    painter.setFont(valueFont);

    Units::UnitSystem unitSystem = Config::instance()->unitSystem();
    QString pressureStr = Units::formatPressureValue(pressure, unitSystem);

    // Position value below header
    QRect valueRect(rect.left() + padding, rect.top() + headerHeight + padding,
                    rect.width() - 2*padding, valueHeight);

    painter.drawText(valueRect, Qt::AlignCenter, pressureStr);

    painter.restore();
}

void OverlayGenerator::drawTime(QPainter &painter, double timestamp, const QRect &rect) {
    int minutes = qFloor(timestamp / 60);
    int seconds = qRound(timestamp) % 60;
    QString timeStr = QString("%1:%2")
                         .arg(minutes)
                         .arg(seconds, 2, 10, QChar('0'));

    painter.save();

    // Draw the header using the helper function
    drawSectionHeader(painter, tr("DIVE TIME"), rect);

    // Proportional positioning for value
    int padding = qMax(2, rect.height() / 20);
    int headerHeight = rect.height() * 35 / 100;
    int valueHeight = rect.height() * 50 / 100;

    // Draw the value below the header
    QFont valueFont = painter.font();
    int valueFontSize = qMax(10, valueHeight * 80 / 100);
    valueFont.setPixelSize(valueFontSize);
    valueFont.setBold(true);
    painter.setFont(valueFont);

    // Position value below header
    QRect valueRect(rect.left() + padding, rect.top() + headerHeight + padding,
                    rect.width() - 2*padding, valueHeight);

    painter.drawText(valueRect, Qt::AlignCenter, timeStr);

    painter.restore();
}


void OverlayGenerator::drawNDL(QPainter &painter, double ndl, const QRect &rect) {
    painter.save();

    // Draw the header using the helper function
    drawSectionHeader(painter, tr("NDL"), rect);

    // Proportional positioning for value
    int padding = qMax(2, rect.height() / 20);
    int headerHeight = rect.height() * 35 / 100;
    int valueHeight = rect.height() * 50 / 100;

    // Draw the value below the header
    QFont valueFont = painter.font();
    int valueFontSize = qMax(10, valueHeight * 80 / 100);
    valueFont.setPixelSize(valueFontSize);
    valueFont.setBold(true);
    painter.setFont(valueFont);

    QString ndlStr = ndl > 0 ? QString::number(qRound(ndl)) + " min" : "DECO";

    // Position value below header
    QRect valueRect(rect.left() + padding, rect.top() + headerHeight + padding,
                    rect.width() - 2*padding, valueHeight);

    painter.drawText(valueRect, Qt::AlignCenter, ndlStr);

    painter.restore();
}

void OverlayGenerator::drawTTS(QPainter &painter, double tts, const QRect &rect, double ceiling) {
    painter.save();

    // Draw the header using the helper function
    drawSectionHeader(painter, tr("TTS"), rect);

    // Proportional positioning
    int padding = qMax(2, rect.height() / 20);
    int headerHeight = rect.height() * 30 / 100;  // 30% for header
    int valueHeight = rect.height() * 35 / 100;   // 35% for value
    int decoHeight = rect.height() * 25 / 100;    // 25% for deco text

    // Draw TTS value
    QFont valueFont = painter.font();
    int valueFontSize = qMax(10, valueHeight * 80 / 100);
    valueFont.setPixelSize(valueFontSize);
    valueFont.setBold(true);
    painter.setFont(valueFont);

    QString ttsStr = QString::number(qRound(tts > 0 ? tts : 1)) + " min";

    // Position value below header
    QRect valueRect(rect.left() + padding, rect.top() + headerHeight + padding,
                    rect.width() - 2*padding, valueHeight);

    painter.drawText(valueRect, Qt::AlignCenter, ttsStr);

    // Draw DECO text in the bottom portion
    QFont decoFont = painter.font();
    int decoFontSize = qMax(8, decoHeight * 70 / 100);
    decoFont.setPixelSize(decoFontSize);
    decoFont.setBold(true);
    painter.setFont(decoFont);

    QString decoText = tr("DECO");
    if (ceiling > 0.0) {
        Units::UnitSystem unitSystem = Config::instance()->unitSystem();
        QString ceilingStr = Units::formatDepthValue(ceiling, unitSystem);
        decoText += QString(" (%1)").arg(ceilingStr);
    }

    // Position DECO text below value
    QRect decoRect(rect.left() + padding, rect.top() + headerHeight + valueHeight + padding,
                   rect.width() - 2*padding, decoHeight);

    painter.drawText(decoRect, Qt::AlignCenter, decoText);

    painter.restore();
}

// Common drawing function to reduce code duplication
void OverlayGenerator::drawDataItem(QPainter &painter, const QString &label, const QString &value, const QRect &rect, bool centerAlign)
{
    painter.save();
    
    // Use fixed padding
    int padding = 5;
    
    // Explicitly divide the rectangle into 3 equal parts
    int sectionHeight = rect.height() / 3;
    
    // Top section for label
    QRect labelRect(rect.left() + padding, rect.top() + padding, 
                    rect.width() - 2*padding, sectionHeight - padding);
    
    // Middle section for value
    QRect valueRect(rect.left() + padding, rect.top() + sectionHeight, 
                   rect.width() - 2*padding, sectionHeight);
    
    // Bottom section (if needed)
    QRect bottomRect(rect.left() + padding, rect.top() + 2*sectionHeight, 
                    rect.width() - 2*padding, sectionHeight - padding);
    
    // Draw label with proportional font size
    QFont labelFont = painter.font();
    int labelFontSize = qMax(8, sectionHeight * 70 / 100);
    labelFont.setPixelSize(labelFontSize);
    painter.setFont(labelFont);

    // Calculate if we need to elide the text
    QFontMetrics fm(labelFont);
    QString displayLabel = label;
    if (fm.horizontalAdvance(label) > labelRect.width()) {
        displayLabel = fm.elidedText(label, Qt::ElideRight, labelRect.width());
    }

    Qt::Alignment labelAlign = Qt::AlignCenter;
    painter.drawText(labelRect, labelAlign, displayLabel);

    // Draw value with proportional font size
    QFont valueFont = labelFont;
    int valueFontSize = qMax(10, sectionHeight * 80 / 100);
    valueFont.setPixelSize(valueFontSize);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    
    painter.drawText(valueRect, Qt::AlignCenter, value);
    
    painter.restore();
}

// Create a helper function to draw section headers with consistent positioning
void OverlayGenerator::drawSectionHeader(QPainter &painter, const QString &label, const QRect &rect) {
    // Proportional padding and height based on rect size
    int padding = qMax(2, rect.height() / 20);
    int headerHeight = rect.height() * 35 / 100;  // 35% of rect height for header

    QRect labelRect(rect.left() + padding, rect.top() + padding,
                   rect.width() - 2*padding, headerHeight);

    // Use proportional font size based on header height
    QFont labelFont = painter.font();
    int fontSize = qMax(8, headerHeight * 70 / 100);  // 70% of header height
    labelFont.setPixelSize(fontSize);
    painter.setFont(labelFont);

    painter.drawText(labelRect, Qt::AlignCenter, label);
}

// CCR PO2 sensor drawing methods
void OverlayGenerator::drawPO2Cell(QPainter &painter, double po2Value, const QRect &rect, int cellNumber) {
    painter.save();

    // Proportional positioning
    int padding = qMax(2, rect.height() / 20);
    int headerHeight = rect.height() * 35 / 100;
    int valueHeight = rect.height() * 50 / 100;

    // Draw header with proportional font
    QFont headerFont = painter.font();
    int headerFontSize = qMax(8, headerHeight * 70 / 100);
    headerFont.setPixelSize(headerFontSize);
    painter.setFont(headerFont);

    QRect labelRect(rect.left() + padding, rect.top() + padding,
                   rect.width() - 2*padding, headerHeight);

    QString label = tr("CELL %1").arg(cellNumber);
    painter.drawText(labelRect, Qt::AlignCenter, label);

    // Draw value with proportional font
    QFont valueFont = painter.font();
    int valueFontSize = qMax(10, valueHeight * 80 / 100);
    valueFont.setPixelSize(valueFontSize);
    valueFont.setBold(true);
    painter.setFont(valueFont);

    // Format PO2 value - handle missing data gracefully
    QString po2Str;
    if (po2Value > 0.0) {
        po2Str = QString("%1").arg(po2Value, 0, 'f', 2);
    } else {
        po2Str = "--"; // Show placeholder when no data is available
    }

    // Position value below header
    QRect valueRect(rect.left() + padding, rect.top() + headerHeight + padding,
                    rect.width() - 2*padding, valueHeight);

    painter.drawText(valueRect, Qt::AlignCenter, po2Str);

    painter.restore();
}

void OverlayGenerator::drawCompositePO2(QPainter &painter, double po2Value, const QRect &rect) {
    painter.save();

    // Draw the header using the helper function
    drawSectionHeader(painter, tr("PO2"), rect);

    // Proportional positioning for value
    int padding = qMax(2, rect.height() / 20);
    int headerHeight = rect.height() * 35 / 100;
    int valueHeight = rect.height() * 50 / 100;

    // Draw value with proportional font
    QFont valueFont = painter.font();
    int valueFontSize = qMax(10, valueHeight * 80 / 100);
    valueFont.setPixelSize(valueFontSize);
    valueFont.setBold(true);
    painter.setFont(valueFont);

    // Format PO2 value - handle missing data gracefully
    QString po2Str;
    if (po2Value > 0.0) {
        po2Str = QString("%1").arg(po2Value, 0, 'f', 2);
    } else {
        po2Str = "--"; // Show placeholder when no data is available
    }

    // Position value below header
    QRect valueRect(rect.left() + padding, rect.top() + headerHeight + padding,
                    rect.width() - 2*padding, valueHeight);

    painter.drawText(valueRect, Qt::AlignCenter, po2Str);

    painter.restore();
}
