#include "include/generators/overlay_gen.h"
#include <QPainter>
#include <QFontMetrics>
#include <QDateTime>
#include <QDebug>

OverlayGenerator::OverlayGenerator(QObject *parent)
    : QObject(parent)
    , m_templatePath(":/default_overlay.png")
    , m_font(QFont("Arial", 12))
    , m_textColor(Qt::white)
    , m_showDepth(true)
    , m_showTemperature(true)
    , m_showNDL(true)
    , m_showPressure(true)
    , m_showTime(true)
    , m_showPO2Cell1(false)
    , m_showPO2Cell2(false)
    , m_showPO2Cell3(false)
    , m_showCompositePO2(false)
{
}

void OverlayGenerator::setTemplatePath(const QString &path)
{
    if (m_templatePath != path) {
        m_templatePath = path;
        emit templateChanged();
    }
}

void OverlayGenerator::setFont(const QFont &font)
{
    if (m_font != font) {
        m_font = font;
        emit fontChanged();
    }
}

void OverlayGenerator::setTextColor(const QColor &color)
{
    if (m_textColor != color) {
        m_textColor = color;
        emit textColorChanged();
    }
}

void OverlayGenerator::setShowDepth(bool show)
{
    if (m_showDepth != show) {
        m_showDepth = show;
        emit showDepthChanged();
    }
}

void OverlayGenerator::setShowTemperature(bool show)
{
    if (m_showTemperature != show) {
        m_showTemperature = show;
        emit showTemperatureChanged();
    }
}

void OverlayGenerator::setShowNDL(bool show)
{
    if (m_showNDL != show) {
        m_showNDL = show;
        emit showNDLChanged();
    }
}

void OverlayGenerator::setShowPressure(bool show)
{
    if (m_showPressure != show) {
        m_showPressure = show;
        emit showPressureChanged();
    }
}

void OverlayGenerator::setShowTime(bool show)
{
    if (m_showTime != show) {
        m_showTime = show;
        emit showTimeChanged();
    }
}

// CCR setter implementations
void OverlayGenerator::setShowPO2Cell1(bool show)
{
    if (m_showPO2Cell1 != show) {
        m_showPO2Cell1 = show;
        emit showPO2Cell1Changed();
    }
}

void OverlayGenerator::setShowPO2Cell2(bool show)
{
    if (m_showPO2Cell2 != show) {
        m_showPO2Cell2 = show;
        emit showPO2Cell2Changed();
    }
}

void OverlayGenerator::setShowPO2Cell3(bool show)
{
    if (m_showPO2Cell3 != show) {
        m_showPO2Cell3 = show;
        emit showPO2Cell3Changed();
    }
}

void OverlayGenerator::setShowCompositePO2(bool show)
{
    if (m_showCompositePO2 != show) {
        m_showCompositePO2 = show;
        emit showCompositePO2Changed();
    }
}

QImage OverlayGenerator::generateOverlay(DiveData* dive, double timePoint)
{
    if (!dive) {
        qWarning() << "No dive data provided for overlay generation";
        return QImage();
    }
    
    qDebug() << "Generating overlay for time point:" << timePoint;
    
    // Load the template image
    QImage templateImage(m_templatePath);
    if (templateImage.isNull()) {
        qWarning() << "Failed to load template image:" << m_templatePath;
        // Create a default black background if template can't be loaded
        templateImage = QImage(640, 120, QImage::Format_ARGB32);
        templateImage.fill(QColor(0, 0, 0, 180));
    }
    
    // Create the result image
    QImage result = templateImage.copy();
    
    // Get the data for the current time point
    DiveDataPoint dataPoint = dive->dataAtTime(timePoint);
    
    qDebug() << "Data point for overlay - depth:" << dataPoint.depth 
             << "temp:" << dataPoint.temperature
             << "time:" << dataPoint.timestamp
             << "ndl:" << dataPoint.ndl
             << "tts:" << dataPoint.tts;
    
    // Determine if we're in decompression
    bool inDeco = (dataPoint.ndl <= 0);
    
    // Ensure TTS is reasonable when in deco mode
    if (inDeco && dataPoint.tts <= 0.0) {
        qDebug() << "Warning: In decompression but TTS is" << dataPoint.tts 
                 << "- This might indicate a parsing issue";
        // Set a minimum value to ensure display makes sense
        dataPoint.tts = 1.0;
    }


    // Set up the painter
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(m_font);
    painter.setPen(m_textColor);

    // Define layout areas based on template size
    int width = result.width();
    int height = result.height();

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
    // This ensures consistent layout even when PO2 data is missing at some time points
    bool anyCCREnabled = Config::instance()->showPO2Cell1() || 
                         Config::instance()->showPO2Cell2() ||
                         Config::instance()->showPO2Cell3() ||
                         Config::instance()->showCompositePO2();
    
    if (anyCCREnabled && po2SensorCount > 0) {
        // Check if any individual cells should be shown
        if (Config::instance()->showPO2Cell1() || 
            (Config::instance()->showPO2Cell2() && po2SensorCount > 1) ||
            (Config::instance()->showPO2Cell3() && po2SensorCount > 2)) {
            numSections++; // All individual cells share one section (grid layout)
        }
        // Composite PO2 gets its own section
        if (Config::instance()->showCompositePO2()) {
            numSections++;
        }
    } else if (anyCCREnabled && po2SensorCount == 0) {
        // Still allocate sections for CCR even if no data is available for consistent layout
        if (Config::instance()->showPO2Cell1() || 
            Config::instance()->showPO2Cell2() ||
            Config::instance()->showPO2Cell3()) {
            numSections++; // All individual cells share one section (grid layout)
        }
        // Composite PO2 gets its own section
        if (Config::instance()->showCompositePO2()) {
            numSections++;
        }
    }

    // Handle tank section sizing separately - but more consistently
    if (m_showPressure && tankCount > 0) {
        // Always use a consistent approach for tanks
        // For a single tank, just use one section like other elements
        if (tankCount == 1) {
            numSections++; // One standard section for a single tank
            tankSectionWidth = 0; // We'll use standard section width
        } 
        // For 2+ tanks, allocate proportional space
        else if (tankCount > 1) {
            // Use 2 columns for layout
            int cols = 2;
            int rows = (tankCount + cols - 1) / cols; // Ceiling division
            
            // For multiple tanks, allocate space based on rows needed
            tankSectionWidth = width / (numSections + rows);
            numSections += rows; // Reserve space for rows of tanks
        }
    } else if (m_showPressure) {
        numSections++; // Single pressure section with no data
    }

    if (numSections == 0) {
        // If nothing to show, just return the template
        return result;
    }

    // Calculate section width
    int sectionWidth = width / numSections;

    // Create all section rectangles with identical dimensions
    QVector<QRect> sectionRects;
    for (int i = 0; i < numSections; i++) {
        sectionRects.append(QRect(i * sectionWidth, 0, sectionWidth, height));
    }

    // Current section index
    int currentSection = 0;

    // Draw each enabled data section using the pre-calculated rects
    if (m_showDepth) {
        drawDepth(painter, dataPoint.depth, sectionRects[currentSection++]);
    }

    if (m_showTemperature) {
        drawTemperature(painter, dataPoint.temperature, sectionRects[currentSection++]);
    }

    // Show either NDL or TTS based on decompression status
    if (inDeco) {
        // In decompression, show TTS with ceiling
        drawTTS(painter, dataPoint.tts, sectionRects[currentSection++], dataPoint.ceiling);
    } else if (m_showNDL) {
        // Not in decompression, show NDL
        drawNDL(painter, dataPoint.ndl, sectionRects[currentSection++]);
    }

    if (m_showPressure) {
        // Show pressure for each tank
        int tankCount = dataPoint.tankCount();
        
        if (tankCount == 1) {
            // For single tank, treat it like a regular section
            double pressure = dataPoint.getPressure(0);
            
            // If no explicit pressure data but we have start/end pressures, interpolate
            qDebug() << "Tank count:" << tankCount << "Pressure:" << pressure;
            const CylinderInfo &cylinder = dive->cylinderInfo(0);
            if (pressure == cylinder.startPressure && dive->cylinderCount() > 0) {
                if (cylinder.startPressure > 0.0 && cylinder.endPressure > 0.0) {
                    pressure = dive->interpolateCylinderPressure(0, dataPoint.timestamp);
                    qDebug() << "Using interpolated pressure for display:" << pressure;
                }
            }
            
            drawPressure(painter, pressure, sectionRects[currentSection++], 0, dive);
        } 
        else if (tankCount > 1) {
            // For multiple tanks, use the grid layout
            // Calculate how many section slots we need for tanks
            int cols = 2;
            int rows = (tankCount + cols - 1) / cols;
            int tanksWidth = sectionWidth * rows;
            qDebug() << "Tank grid - rows:" << rows << "cols:" << cols << "width:" << tanksWidth;
            
            // Create a wider rectangle that spans multiple sections
            QRect tankGridRect(currentSection * sectionWidth, 0, tanksWidth, height);
            
            // Calculate cell dimensions
            int cellWidth = tankGridRect.width() / cols;
            int cellHeight = height / ((tankCount > 2) ? ((tankCount + 1) / 2) : 1);

            qDebug() << "Tank cell dimensions: width =" << cellWidth << "height =" << cellHeight;
    
            
            // Draw each tank in its grid cell with padding between cells
            for (int i = 0; i < tankCount; i++) {
                int row = i / cols;
                int col = i % cols;
                
                QRect tankRect(
                    tankGridRect.left() + (col * cellWidth),
                    row * cellHeight,
                    cellWidth,
                    cellHeight
                );
                
                // Add a small margin around each cell for better separation
                tankRect.adjust(3, 3, -3, -3);
                qDebug() << "Drawing tank" << i << "at row" << row << "col" << col 
                 << "rect:" << tankRect;

                double pressure = dataPoint.getPressure(i);
                // // Check if the pressure equals the start pressure (suggesting it needs interpolation)
                // const CylinderInfo &cylinder = dive->cylinderInfo(i);
                // if (cylinder.startPressure > 0.0 && cylinder.endPressure > 0.0) {
                //     // Check if this cylinder is active at the current time
                //     if (dive->isCylinderActiveAtTime(i, dataPoint.timestamp)) {
                //         pressure = dive->interpolateCylinderPressure(i, dataPoint.timestamp);
                //         qDebug() << "Using interpolated pressure for tank" << i << ":" << pressure;
                //     }
                // }

                const CylinderInfo &cylinder = dive->cylinderInfo(i);
                // If the cylinder has valid pressure values for interpolation
                if (cylinder.startPressure > 0.0 && cylinder.endPressure > 0.0) {
                    // Check if this cylinder is active at the current time
                    if (dive->isCylinderActiveAtTime(i, dataPoint.timestamp)) {
                        // Active tank - use interpolation
                        pressure = dive->interpolateCylinderPressure(i, dataPoint.timestamp);
                        qDebug() << "Using interpolated pressure for active tank" << i << ":" << pressure;
                    } else {
                        // Inactive tank - check if we have a previously interpolated value
                        double lastInterpolated = dive->getLastInterpolatedPressure(i);
                        if (lastInterpolated > 0.0) {
                            // Use the last known interpolated value for continuity
                            pressure = lastInterpolated;
                            qDebug() << "Using last interpolated pressure for inactive tank" << i << ":" << pressure;
                        }
                    }
                }

                drawPressure(painter, pressure, tankRect, i, dive);
            }
            
            // Move current section past the tank grid
            currentSection += rows;
        }
        else {
            // No pressure data available
            drawPressure(painter, 0.0, sectionRects[currentSection++], -1, dive);
        }
    }

    if (m_showTime) {
        drawTime(painter, dataPoint.timestamp, sectionRects[currentSection++]);
    }
    
    // Draw CCR sensors (draw if CCR is enabled, handle missing data gracefully)
    if (anyCCREnabled) {
        // Draw individual cells in a grid layout (if any are enabled)
        if (Config::instance()->showPO2Cell1() || 
            Config::instance()->showPO2Cell2() ||
            Config::instance()->showPO2Cell3()) {
            QRect cellGridRect = sectionRects[currentSection++];
            
            // Count how many cells to show - always show enabled cells, handle missing data in draw method
            QVector<int> cellsToShow;
            if (Config::instance()->showPO2Cell1()) cellsToShow.append(1);
            if (Config::instance()->showPO2Cell2()) cellsToShow.append(2);
            if (Config::instance()->showPO2Cell3()) cellsToShow.append(3);
            
            // Layout cells in a grid (similar to tank layout)
            int cellCount = cellsToShow.size();
            if (cellCount == 1) {
                // Single cell - use full width
                int cellNum = cellsToShow[0];
                drawPO2Cell(painter, dataPoint.getPO2Sensor(cellNum - 1), cellGridRect, cellNum);
            } else {
                // Multiple cells - use grid layout
                int cols = (cellCount <= 2) ? cellCount : 2; // Max 2 columns
                int rows = (cellCount + cols - 1) / cols; // Ceiling division
                
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
                    
                    // Add small margin for better separation
                    cellRect.adjust(2, 2, -2, -2);
                    
                    int cellNum = cellsToShow[i];
                    drawPO2Cell(painter, dataPoint.getPO2Sensor(cellNum - 1), cellRect, cellNum);
                }
            }
        }
        
        // Draw composite PO2 in its own section
        if (Config::instance()->showCompositePO2()) {
            drawCompositePO2(painter, dataPoint.getCompositePO2(), sectionRects[currentSection++]);
        }
    }
    
    painter.end();
    return result;
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

void OverlayGenerator::drawDepth(QPainter &painter, double depth, const QRect &rect) {
    Units::UnitSystem unitSystem = Config::instance()->unitSystem();
    QString depthStr = Units::formatDepthValue(depth, unitSystem);
    
    painter.save();
    
    // Draw the header using the helper function
    drawSectionHeader(painter, tr("DEPTH"), rect);
    
    // Draw the value in the middle portion
    QFont valueFont = painter.font();
    // valueFont.setPointSize(12);
    valueFont.setPixelSize(24);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    
    // Position value in the middle of the rect
    QRect valueRect(rect.left() + 5, rect.top() + 35, 
                    rect.width() - 10, 20); // Fixed position and height
    
    painter.drawText(valueRect, Qt::AlignCenter, depthStr);
    
    painter.restore();
}

void OverlayGenerator::drawTemperature(QPainter &painter, double temp, const QRect &rect) {
    Units::UnitSystem unitSystem = Config::instance()->unitSystem();
    QString tempStr = Units::formatTemperatureValue(temp, unitSystem);
    
    painter.save();
    
    // Draw the header using the helper function
    drawSectionHeader(painter, tr("TEMP"), rect);
    
    // Draw the value in the middle portion
    QFont valueFont = painter.font();
    // valueFont.setPointSize(12);
    valueFont.setPixelSize(24);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    
    // Position value in the middle of the rect
    QRect valueRect(rect.left() + 5, rect.top() + 35, 
                    rect.width() - 10, 20); // Fixed position and height
    
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
        
        // Use shorter labels for multi-tank displays
        if (tankCount > 2) {
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
    
    // Adjust font size based on number of tanks
    QFont headerFont = painter.font();
    if (tankCount > 2) {
        headerFont.setPixelSize(16); // Smaller for multi-tank
    } else {
        headerFont.setPixelSize(20); // Normal size
    }
    painter.setFont(headerFont);
    
    // Calculate label position - adjust for multi-tank
    int padding = 2; // Use smaller padding for multi-tank
    QRect labelRect(rect.left() + padding, rect.top() + padding, 
                   rect.width() - 2*padding, 20); // Fixed height of 20px
    
    // Draw the label with eliding if necessary
    QFontMetrics fm(headerFont);
    QString displayLabel = label;
    if (fm.horizontalAdvance(label) > labelRect.width()) {
        displayLabel = fm.elidedText(label, Qt::ElideRight, labelRect.width());
    }
    
    painter.drawText(labelRect, Qt::AlignCenter, displayLabel);
    
    // Draw pressure value with adaptive sizing
    QFont valueFont = painter.font();
    if (tankCount > 2) {
        valueFont.setPixelSize(20); // Smaller for multi-tank
    } else {
        valueFont.setPixelSize(24); // Normal size
    }
    valueFont.setBold(true);
    painter.setFont(valueFont);
    
    Units::UnitSystem unitSystem = Config::instance()->unitSystem();
    QString pressureStr = Units::formatPressureValue(pressure, unitSystem);
    
    // Position value - adjust for multi-tank
    QRect valueRect;
    if (tankCount > 2) {
        valueRect = QRect(rect.left() + padding, rect.top() + 25, 
                        rect.width() - 2*padding, 20);
    } else {
        valueRect = QRect(rect.left() + 5, rect.top() + 35, 
                        rect.width() - 10, 20);
    }
    
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
    
    // Draw the value in the middle portion
    QFont valueFont = painter.font();
    // valueFont.setPointSize(12);
    valueFont.setPixelSize(24);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    
    // Position value in the middle of the rect
    QRect valueRect(rect.left() + 5, rect.top() + 35, 
                    rect.width() - 10, 20); // Fixed position and height
    
    painter.drawText(valueRect, Qt::AlignCenter, timeStr);
    
    painter.restore();
}


void OverlayGenerator::drawNDL(QPainter &painter, double ndl, const QRect &rect) {
    painter.save();
    
    // Draw the header using the helper function - SAME POSITION as other headers
    drawSectionHeader(painter, tr("NDL"), rect);
    
    // Draw NDL value at SAME POSITION as other values
    QFont valueFont = painter.font();
    // valueFont.setPointSize(12);
    valueFont.setPixelSize(24);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    
    QString ndlStr = ndl > 0 ? QString::number(qRound(ndl)) + " min" : "DECO";
    
    // Position value in the middle of the rect - SAME as other sections
    QRect valueRect(rect.left() + 5, rect.top() + 35, 
                    rect.width() - 10, 20); // Fixed position and height
    
    painter.drawText(valueRect, Qt::AlignCenter, ndlStr);
    
    painter.restore();
}

void OverlayGenerator::drawTTS(QPainter &painter, double tts, const QRect &rect, double ceiling) {
    painter.save();
    
    // Draw the header using the helper function - SAME POSITION as other headers
    drawSectionHeader(painter, tr("TTS"), rect);
    
    // Draw TTS value at SAME POSITION as other values
    QFont valueFont = painter.font();
    // valueFont.setPointSize(12);
    valueFont.setPixelSize(24);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    
    QString ttsStr = QString::number(qRound(tts > 0 ? tts : 1)) + " min";
    
    // Position value in the middle of the rect - SAME as other sections
    QRect valueRect(rect.left() + 5, rect.top() + 35, 
                    rect.width() - 10, 20); // Fixed position and height
    
    painter.drawText(valueRect, Qt::AlignCenter, ttsStr);
    
    // Draw DECO text in the bottom portion
    QFont decoFont = painter.font();
    // decoFont.setPointSize(10);
    decoFont.setPixelSize(14);
    decoFont.setBold(true);
    painter.setFont(decoFont);
    
    QString decoText = tr("DECO");
    if (ceiling > 0.0) {
        Units::UnitSystem unitSystem = Config::instance()->unitSystem();
        QString ceilingStr = Units::formatDepthValue(ceiling, unitSystem);
        decoText += QString(" (%1)").arg(ceilingStr);
    }
    
    // Position DECO text in the bottom of the rect
    QRect decoRect(rect.left() + 5, rect.top() + 65, 
                   rect.width() - 10, 20); // Fixed position and height
    
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
    
    // Draw label with fixed font size
    QFont labelFont = painter.font();
    // labelFont.setPointSize(10);
    labelFont.setPixelSize(20);
    painter.setFont(labelFont);
    
    // Calculate if we need to elide the text
    QFontMetrics fm(labelFont);
    QString displayLabel = label;
    if (fm.horizontalAdvance(label) > labelRect.width()) {
        displayLabel = fm.elidedText(label, Qt::ElideRight, labelRect.width());
    }
    
    Qt::Alignment labelAlign = Qt::AlignCenter;
    painter.drawText(labelRect, labelAlign, displayLabel);
    
    // Draw value with fixed font size
    QFont valueFont = labelFont;
    // valueFont.setPointSize(12);
    valueFont.setPixelSize(24);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    
    painter.drawText(valueRect, Qt::AlignCenter, value);
    
    painter.restore();
}

// Create a helper function to draw section headers with consistent positioning
void OverlayGenerator::drawSectionHeader(QPainter &painter, const QString &label, const QRect &rect) {
    // Fixed padding
    int padding = 5;
    // Fixed position for all labels - very top of the rect with fixed height
    QRect labelRect(rect.left() + padding, rect.top() + padding, 
                   rect.width() - 2*padding, 20); // Fixed height of 20px
    
    // Use consistent font for all labels
    QFont labelFont = painter.font();
    // labelFont.setPointSize(10);
    labelFont.setPixelSize(20);
    painter.setFont(labelFont);
    
    painter.drawText(labelRect, Qt::AlignCenter, label);
}

// CCR PO2 sensor drawing methods
void OverlayGenerator::drawPO2Cell(QPainter &painter, double po2Value, const QRect &rect, int cellNumber) {
    painter.save();
    
    // Determine if this is a small cell (grid layout) based on rect size
    bool isSmallCell = (rect.width() < 150 || rect.height() < 80);
    
    // Adjust font sizes for small cells
    QFont headerFont = painter.font();
    QFont valueFont = painter.font();
    
    if (isSmallCell) {
        headerFont.setPixelSize(14);
        valueFont.setPixelSize(18);
    } else {
        headerFont.setPixelSize(20);
        valueFont.setPixelSize(24);
    }
    
    painter.setFont(headerFont);
    
    // Draw header
    int padding = isSmallCell ? 2 : 5;
    QRect labelRect(rect.left() + padding, rect.top() + padding, 
                   rect.width() - 2*padding, isSmallCell ? 16 : 20);
    
    QString label = tr("CELL %1").arg(cellNumber);
    painter.drawText(labelRect, Qt::AlignCenter, label);
    
    // Draw the value
    valueFont.setBold(true);
    painter.setFont(valueFont);
    
    // Format PO2 value - handle missing data gracefully
    QString po2Str;
    if (po2Value > 0.0) {
        po2Str = QString("%1").arg(po2Value, 0, 'f', 2);
    } else {
        po2Str = "--"; // Show placeholder when no data is available
    }
    
    // Position value
    QRect valueRect(rect.left() + padding, 
                    rect.top() + (isSmallCell ? 20 : 35), 
                    rect.width() - 2*padding, 
                    isSmallCell ? 16 : 20);
    
    painter.drawText(valueRect, Qt::AlignCenter, po2Str);
    
    painter.restore();
}

void OverlayGenerator::drawCompositePO2(QPainter &painter, double po2Value, const QRect &rect) {
    painter.save();
    
    // Draw the header using the helper function
    drawSectionHeader(painter, tr("PO2"), rect);
    
    // Draw the value in the middle portion
    QFont valueFont = painter.font();
    valueFont.setPixelSize(24);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    
    // Format PO2 value - handle missing data gracefully
    QString po2Str;
    if (po2Value > 0.0) {
        po2Str = QString("%1").arg(po2Value, 0, 'f', 2);
    } else {
        po2Str = "--"; // Show placeholder when no data is available
    }
    
    // Position value in the middle of the rect
    QRect valueRect(rect.left() + 5, rect.top() + 35, 
                    rect.width() - 10, 20); // Fixed position and height
    
    painter.drawText(valueRect, Qt::AlignCenter, po2Str);
    
    painter.restore();
}
