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
             << "time:" << dataPoint.timestamp;
    
    // Set up the painter
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(m_font);
    painter.setPen(m_textColor);
    
    // Define layout areas based on template size
    int width = result.width();
    int height = result.height();
    
    // Simple layout with evenly spaced sections
    int numSections = 0;
    if (m_showDepth) numSections++;
    if (m_showTemperature) numSections++;
    if (m_showNDL) numSections++;
    if (m_showPressure) numSections++;
    if (m_showTime) numSections++;
    
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
    
    if (m_showNDL) {
        drawNDL(painter, dataPoint.ndl, QRect(currentX, 0, sectionWidth, height));
        currentX += sectionWidth;
    }
    
    if (m_showPressure) {
        drawPressure(painter, dataPoint.pressure, QRect(currentX, 0, sectionWidth, height));
        currentX += sectionWidth;
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

void OverlayGenerator::drawPressure(QPainter &painter, double pressure, const QRect &rect)
{
    QString pressureStr = QString::number(qRound(pressure)) + " bar";
    QFontMetrics fm = painter.fontMetrics();
    
    // Draw label
    painter.save();
    QFont labelFont = painter.font();
    labelFont.setPointSize(labelFont.pointSize() * 0.8);
    painter.setFont(labelFont);
    
    QString label = tr("PRESSURE");
    QRect labelRect = rect.adjusted(10, 10, -10, -rect.height() / 2);
    painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignTop, label);
    
    // Draw value
    painter.setFont(m_font);
    QRect valueRect = rect.adjusted(10, rect.height() / 3, -10, -10);
    painter.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter, pressureStr);
    
    painter.restore();
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