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

    // Handle tank section sizing separately
    if (m_showPressure && tankCount > 0) {
        // Determine tank section width based on number of tanks
        // Give more space for multiple tanks
        if (tankCount <= 2) {
            tankSectionWidth = width / (numSections + 1);
        } else if (tankCount <= 4) {
            tankSectionWidth = width / (numSections + 2);
            numSections += 2; // Reserve 2 sections for tanks
        } else {
            tankSectionWidth = width / (numSections + 3);
            numSections += 3; // Reserve 3 sections for tanks
        }
    } else if (m_showPressure) {
        numSections++; // Single pressure section
    }

    if (numSections == 0) {
        // If nothing to show, just return the template
        return result;
    }
    
    int sectionWidth = width / numSections;
    int currentX = 0;
    
    // Draw each enabled data section
    if (m_showDepth) {
        drawDepth(painter, dataPoint.depth, QRect(currentX, 0, sectionWidth, height));
        currentX += sectionWidth;
    }
    
    if (m_showTemperature) {
        drawTemperature(painter, dataPoint.temperature, QRect(currentX, 0, sectionWidth, height));
        currentX += sectionWidth;
    }
    
    // Show either NDL or TTS based on decompression status
    // In generateOverlay method, before calling drawTTS:
    qDebug() << "TTS data - tts:" << dataPoint.tts 
            << "ceiling:" << dataPoint.ceiling
            << "in deco:" << inDeco;

    // When calling drawTTS:
    if (inDeco) {
        // In decompression, show TTS with ceiling
        qDebug() << "Drawing TTS with ceiling:" << dataPoint.ceiling;
        drawTTS(painter, dataPoint.tts, QRect(currentX, 0, sectionWidth, height), dataPoint.ceiling);
        currentX += sectionWidth;
    } else if (m_showNDL) {
        // Not in decompression, show NDL
        drawNDL(painter, dataPoint.ndl, QRect(currentX, 0, sectionWidth, height));
        currentX += sectionWidth;
    }

    if (m_showPressure) {
        // Show pressure for each tank in a grid layout
        int tankCount = dataPoint.tankCount();
        if (tankCount > 0) {
            // Calculate grid layout (max 2 tanks per row)
            int cols = qMin(2, tankCount);
            int rows = (tankCount + cols - 1) / cols; // Ceiling division
            
            // Calculate space needed for tanks
            int tankSpaceWidth = tankCount <= 2 ? tankSectionWidth : 
                            (tankCount <= 4 ? tankSectionWidth * 2 : tankSectionWidth * 3);
            
            // Calculate cell dimensions
            int cellWidth = tankSectionWidth;
            int cellHeight = height / rows;
            
            // Draw each tank in its grid cell
            for (int i = 0; i < tankCount; i++) {
                int row = i / cols;
                int col = i % cols;
                
                QRect tankRect(
                    currentX + (col * cellWidth),
                    row * cellHeight,
                    cellWidth,
                    cellHeight
                );
                drawPressure(painter, dataPoint.getPressure(i), tankRect, i, dive);
            }
            
            currentX += tankSpaceWidth;
        } else {
            // No pressure data available, but section was allocated
            drawPressure(painter, 0.0, QRect(currentX, 0, sectionWidth, height), -1, dive);
            currentX += sectionWidth;
        }
    }
    
    if (m_showTime) {
        drawTime(painter, dataPoint.timestamp, QRect(currentX, 0, sectionWidth, height));
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

void OverlayGenerator::drawDepth(QPainter &painter, double depth, const QRect &rect)
{
    QString depthStr = QString::number(depth, 'f', 1) + " m";
    QFontMetrics fm = painter.fontMetrics();
    
    // Draw label
    painter.save();
    QFont labelFont = painter.font();
    labelFont.setPointSize(labelFont.pointSize() * 0.8);
    painter.setFont(labelFont);
    
    QString label = tr("DEPTH");
    QRect labelRect = rect.adjusted(10, 10, -10, -rect.height() / 2);
    painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignTop, label);
    
    // Draw value
    painter.setFont(m_font);
    QRect valueRect = rect.adjusted(10, rect.height() / 3, -10, -10);
    painter.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter, depthStr);
    
    painter.restore();
}

void OverlayGenerator::drawTemperature(QPainter &painter, double temp, const QRect &rect)
{
    QString tempStr = QString::number(temp, 'f', 1) + " °C";
    QFontMetrics fm = painter.fontMetrics();
    
    // Draw label
    painter.save();
    QFont labelFont = painter.font();
    labelFont.setPointSize(labelFont.pointSize() * 0.8);
    painter.setFont(labelFont);
    
    QString label = tr("TEMP");
    QRect labelRect = rect.adjusted(10, 10, -10, -rect.height() / 2);
    painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignTop, label);
    
    // Draw value
    painter.setFont(m_font);
    QRect valueRect = rect.adjusted(10, rect.height() / 3, -10, -10);
    painter.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter, tempStr);
    
    painter.restore();
}

void OverlayGenerator::drawNDL(QPainter &painter, double ndl, const QRect &rect)
{
    // Format NDL as minutes or show DECO if in decompression
    QString ndlStr;
    
    if (ndl <= 0) {
        // If NDL is 0 or negative, the diver is in decompression
        ndlStr = "DECO";
    } else {
        ndlStr = QString::number(qRound(ndl)) + " min";
    }
    
    QFontMetrics fm = painter.fontMetrics();
    
    // Draw label
    painter.save();
    QFont labelFont = painter.font();
    labelFont.setPointSize(labelFont.pointSize() * 0.8);
    painter.setFont(labelFont);
    
    QString label = tr("NDL");
    QRect labelRect = rect.adjusted(10, 10, -10, -rect.height() / 2);
    painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignTop, label);
    
    // Draw value
    painter.setFont(m_font);
    QRect valueRect = rect.adjusted(10, rect.height() / 3, -10, -10);
    painter.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter, ndlStr);
    
    painter.restore();
}

void OverlayGenerator::drawPressure(QPainter &painter, double pressure, const QRect &rect, int tankIndex, DiveData* dive)
{
    QString pressureStr = QString::number(qRound(pressure)) + " bar";
    
    // Adjust label based on tank index and gas mix
    QString label;
    if (tankIndex >= 0) {
        // Get gas mix for the tank from the dive data
        QString gasMix;
        if (dive && tankIndex < dive->cylinderCount()) {
            const CylinderInfo& cylinder = dive->cylinderInfo(tankIndex);
            
            // Format gas mix information
            if (cylinder.hePercent > 0.0) {
                // Trimix: show O2/He
                gasMix = QString("(%1/%2)").arg(qRound(cylinder.o2Percent)).arg(qRound(cylinder.hePercent));
            } else if (cylinder.o2Percent != 21.0) {
                // Nitrox or pure O2
                gasMix = QString("(%1%)").arg(qRound(cylinder.o2Percent));
            }
        }
        
        // Create tank label with gas mix if available
        if (!gasMix.isEmpty()) {
            label = tr("TANK %1 %2").arg(tankIndex + 1).arg(gasMix);
        } else {
            label = tr("TANK %1").arg(tankIndex + 1);
        }
    } else {
        label = tr("PRESSURE");
    }
    
    drawDataItem(painter, label, pressureStr, rect, true);
}

void OverlayGenerator::drawTime(QPainter &painter, double timestamp, const QRect &rect)
{
    // Format time as minutes:seconds
    int minutes = qFloor(timestamp / 60);
    int seconds = qRound(timestamp) % 60;
    QString timeStr = QString("%1:%2")
                         .arg(minutes)
                         .arg(seconds, 2, 10, QChar('0'));
    
    QFontMetrics fm = painter.fontMetrics();
    
    // Draw label
    painter.save();
    QFont labelFont = painter.font();
    labelFont.setPointSize(labelFont.pointSize() * 0.8);
    painter.setFont(labelFont);
    
    QString label = tr("DIVE TIME");
    QRect labelRect = rect.adjusted(10, 10, -10, -rect.height() / 2);
    painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignTop, label);
    
    // Draw value
    painter.setFont(m_font);
    QRect valueRect = rect.adjusted(10, rect.height() / 3, -10, -10);
    painter.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter, timeStr);
    
    painter.restore();
}

void OverlayGenerator::drawTTS(QPainter &painter, double tts, const QRect &rect, double ceiling)
{
    qDebug() << "drawTTS called with ceiling:" << ceiling;
    // Format TTS as minutes (ensure it's positive)
    QString ttsStr;
    if (tts <= 0.0) {
        // Use a non-zero minimum value if TTS is invalid or zero
        ttsStr = "1 min";
        qDebug() << "Warning: TTS value is" << tts << "- displaying minimum value";
    } else {
        ttsStr = QString::number(qRound(tts)) + " min";
    }
    
    int padding = qMin(5, rect.width() / 20);
    
    painter.save();
    
    // Draw main label and value
    QFont labelFont = painter.font();
    labelFont.setPointSize(labelFont.pointSize() * 0.75);
    painter.setFont(labelFont);
    
    QRect labelRect = rect.adjusted(padding, padding, -padding, 0);
    labelRect.setHeight(rect.height() / 3);
    painter.drawText(labelRect, Qt::AlignCenter, tr("TTS"));
    
    // Draw value
    QFont valueFont = labelFont;
    valueFont.setPointSize(valueFont.pointSize() * 1.2);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    
    QRect valueRect = rect.adjusted(padding, rect.height() / 3, -padding, -rect.height() / 4);
    painter.drawText(valueRect, Qt::AlignCenter, ttsStr);
    
    // Add DECO indicator with ceiling information
    QFont decoFont = labelFont;
    decoFont.setPointSize(decoFont.pointSize() * 0.9);
    decoFont.setBold(true);
    painter.setFont(decoFont);
    
    QString decoText = tr("DECO");
    // Add ceiling if it's greater than zero
    if (ceiling > 0.0) {
        decoText += QString(" (%1m)").arg(ceiling, 0, 'f', 1);
        qDebug() << "Created DECO text with ceiling:" << decoText;
    } else {
        qDebug() << "Ceiling value not positive:" << ceiling;
    }
    
    QRect decoRect = rect.adjusted(padding, rect.height() * 2/3, -padding, -padding);
    
    // Check if the text fits in the space
    QFontMetrics fm(decoFont);
    if (fm.horizontalAdvance(decoText) > decoRect.width()) {
        // If too wide, use shorter format or reduce font size
        if (ceiling > 0.0) {
            // Try more compact format first
            decoText = tr("DECO %1m").arg(ceiling, 0, 'f', 1);
            
            // If still too wide, reduce font size
            if (fm.horizontalAdvance(decoText) > decoRect.width()) {
                decoFont.setPointSize(decoFont.pointSize() * 0.8);
                painter.setFont(decoFont);
                
                // If still too wide after reducing font, elide
                fm = QFontMetrics(decoFont);
                if (fm.horizontalAdvance(decoText) > decoRect.width()) {
                    decoText = fm.elidedText(decoText, Qt::ElideRight, decoRect.width());
                }
            }
        }
    }
    
    painter.drawText(decoRect, Qt::AlignCenter, decoText);
    
    painter.restore();
}

// Common drawing function to reduce code duplication
void OverlayGenerator::drawDataItem(QPainter &painter, const QString &label, const QString &value, const QRect &rect, bool centerAlign)
{
    int padding = qMin(5, rect.width() / 20); // Scale padding with cell size
    
    painter.save();
    
    // Draw label - ensure it fits in the available space
    QFont labelFont = painter.font();
    labelFont.setPointSize(labelFont.pointSize() * 0.75);
    painter.setFont(labelFont);
    
    QRect labelRect = rect.adjusted(padding, padding, -padding, 0);
    labelRect.setHeight(rect.height() / 3);
    
    // Calculate if we need to elide the text
    QFontMetrics fm(labelFont);
    QString displayLabel = label;
    if (fm.horizontalAdvance(label) > labelRect.width()) {
        // Text is too wide, scale down font size slightly for tank labels with gas mix
        if (label.contains("TANK") && label.contains("(")) {
            labelFont.setPointSize(labelFont.pointSize() * 0.85);
            painter.setFont(labelFont);
            fm = QFontMetrics(labelFont);
            
            // If still too wide, elide the text
            if (fm.horizontalAdvance(label) > labelRect.width()) {
                displayLabel = fm.elidedText(label, Qt::ElideRight, labelRect.width());
            }
        } else {
            // For other labels, just elide
            displayLabel = fm.elidedText(label, Qt::ElideRight, labelRect.width());
        }
    }
    
    Qt::Alignment labelAlign = centerAlign ? Qt::AlignCenter : Qt::AlignHCenter | Qt::AlignTop;
    painter.drawText(labelRect, labelAlign, displayLabel);
    
    // Draw value with slightly larger font
    QFont valueFont = labelFont;
    valueFont.setPointSize(valueFont.pointSize() * 1.2);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    
    QRect valueRect = rect.adjusted(padding, rect.height() / 3, -padding, -padding);
    Qt::Alignment valueAlign = centerAlign ? Qt::AlignCenter : Qt::AlignHCenter | Qt::AlignVCenter;
    painter.drawText(valueRect, valueAlign, value);
    
    painter.restore();
}
