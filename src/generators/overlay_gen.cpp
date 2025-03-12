#include "include/generators/overlay_gen.h"
#include <QPainter>
#include <QFontMetrics>
#include <QDateTime>
#include <QDebug>

OverlayGenerator::OverlayGenerator(QObject *parent)
    : QObject(parent)
    , m_templatePath(":/resources/templates/default_overlay.png")
    , m_font(QFont("Arial", 12))
    , m_textColor(Qt::white)
    , m_showDepth(true)
    , m_showTemperature(true)
    , m_showNDL(true)
    , m_showPressure(true)
    , m_showTime(true)
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
            drawPressure(painter, dataPoint.getPressure(0), sectionRects[currentSection++], 0, dive);
        } 
        else if (tankCount > 1) {
            // For multiple tanks, use the grid layout
            // Calculate how many section slots we need for tanks
            int cols = 2;
            int rows = (tankCount + cols - 1) / cols;
            int tanksWidth = sectionWidth * rows;
            
            // Create a wider rectangle that spans multiple sections
            QRect tankGridRect(currentSection * sectionWidth, 0, tanksWidth, height);
            
            // Calculate cell dimensions
            int cellWidth = tankGridRect.width() / cols;
            int cellHeight = height / rows;
            
            // Draw each tank in its grid cell
            for (int i = 0; i < tankCount; i++) {
                int row = i / cols;
                int col = i % cols;
                
                QRect tankRect(
                    tankGridRect.left() + (col * cellWidth),
                    row * cellHeight,
                    cellWidth,
                    cellHeight
                );
                drawPressure(painter, dataPoint.getPressure(i), tankRect, i, dive);
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
    QString depthStr = QString::number(depth, 'f', 1) + " m";
    
    painter.save();
    
    // Draw the header using the helper function
    drawSectionHeader(painter, tr("DEPTH"), rect);
    
    // Draw the value in the middle portion
    QFont valueFont = painter.font();
    valueFont.setPointSize(12);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    
    // Position value in the middle of the rect
    QRect valueRect(rect.left() + 5, rect.top() + 35, 
                    rect.width() - 10, 20); // Fixed position and height
    
    painter.drawText(valueRect, Qt::AlignCenter, depthStr);
    
    painter.restore();
}

void OverlayGenerator::drawTemperature(QPainter &painter, double temp, const QRect &rect) {
    QString tempStr = QString::number(temp, 'f', 1) + " °C";
    
    painter.save();
    
    // Draw the header using the helper function
    drawSectionHeader(painter, tr("TEMP"), rect);
    
    // Draw the value in the middle portion
    QFont valueFont = painter.font();
    valueFont.setPointSize(12);
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
        
        if (!gasMix.isEmpty()) {
            label = tr("TANK %1 %2").arg(tankIndex + 1).arg(gasMix);
        } else {
            label = tr("TANK %1").arg(tankIndex + 1);
        }
    } else {
        label = tr("PRESSURE");
    }
    
    // Draw the header using the helper function - SAME POSITION as other headers
    drawSectionHeader(painter, label, rect);
    
    // Draw pressure value at SAME POSITION as other values
    QFont valueFont = painter.font();
    valueFont.setPointSize(12);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    
    QString pressureStr = QString::number(qRound(pressure)) + " bar";
    
    // Position value in the middle of the rect - SAME as other sections
    QRect valueRect(rect.left() + 5, rect.top() + 35, 
                    rect.width() - 10, 20); // Fixed position and height
    
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
    valueFont.setPointSize(12);
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
    valueFont.setPointSize(12);
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
    valueFont.setPointSize(12);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    
    QString ttsStr = QString::number(qRound(tts > 0 ? tts : 1)) + " min";
    
    // Position value in the middle of the rect - SAME as other sections
    QRect valueRect(rect.left() + 5, rect.top() + 35, 
                    rect.width() - 10, 20); // Fixed position and height
    
    painter.drawText(valueRect, Qt::AlignCenter, ttsStr);
    
    // Draw DECO text in the bottom portion
    QFont decoFont = painter.font();
    decoFont.setPointSize(10);
    decoFont.setBold(true);
    painter.setFont(decoFont);
    
    QString decoText = tr("DECO");
    if (ceiling > 0.0) {
        decoText += QString(" (%1m)").arg(ceiling, 0, 'f', 1);
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
    labelFont.setPointSize(10);
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
    valueFont.setPointSize(12);
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
    labelFont.setPointSize(10);
    painter.setFont(labelFont);
    
    painter.drawText(labelRect, Qt::AlignCenter, label);
}
