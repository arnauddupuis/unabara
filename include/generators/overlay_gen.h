#ifndef OVERLAY_GEN_H
#define OVERLAY_GEN_H

#include <QObject>
#include <QString>
#include <QImage>
#include <QFont>
#include <QColor>
#include "include/core/dive_data.h"
#include "include/core/config.h"
#include "include/core/units.h"

class OverlayGenerator : public QObject
{
    Q_OBJECT
    
    Q_PROPERTY(QString templatePath READ templatePath WRITE setTemplatePath NOTIFY templateChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY textColorChanged)
    Q_PROPERTY(bool showDepth READ showDepth WRITE setShowDepth NOTIFY showDepthChanged)
    Q_PROPERTY(bool showTemperature READ showTemperature WRITE setShowTemperature NOTIFY showTemperatureChanged)
    Q_PROPERTY(bool showNDL READ showNDL WRITE setShowNDL NOTIFY showNDLChanged)
    Q_PROPERTY(bool showPressure READ showPressure WRITE setShowPressure NOTIFY showPressureChanged)
    Q_PROPERTY(bool showTime READ showTime WRITE setShowTime NOTIFY showTimeChanged)
    
    // CCR properties
    Q_PROPERTY(bool showPO2Cell1 READ showPO2Cell1 WRITE setShowPO2Cell1 NOTIFY showPO2Cell1Changed)
    Q_PROPERTY(bool showPO2Cell2 READ showPO2Cell2 WRITE setShowPO2Cell2 NOTIFY showPO2Cell2Changed)
    Q_PROPERTY(bool showPO2Cell3 READ showPO2Cell3 WRITE setShowPO2Cell3 NOTIFY showPO2Cell3Changed)
    Q_PROPERTY(bool showCompositePO2 READ showCompositePO2 WRITE setShowCompositePO2 NOTIFY showCompositePO2Changed)
    
public:
    explicit OverlayGenerator(QObject *parent = nullptr);
    
    // Getters
    QString templatePath() const { return m_templatePath; }
    QFont font() const { return m_font; }
    QColor textColor() const { return m_textColor; }
    bool showDepth() const { return m_showDepth; }
    bool showTemperature() const { return m_showTemperature; }
    bool showNDL() const { return m_showNDL; }
    bool showPressure() const { return m_showPressure; }
    bool showTime() const { return m_showTime; }
    
    // CCR getters
    bool showPO2Cell1() const { return m_showPO2Cell1; }
    bool showPO2Cell2() const { return m_showPO2Cell2; }
    bool showPO2Cell3() const { return m_showPO2Cell3; }
    bool showCompositePO2() const { return m_showCompositePO2; }
    
    // Setters
    void setTemplatePath(const QString &path);
    void setFont(const QFont &font);
    void setTextColor(const QColor &color);
    void setShowDepth(bool show);
    void setShowTemperature(bool show);
    void setShowNDL(bool show);
    void setShowPressure(bool show);
    void setShowTime(bool show);
    
    // CCR setters
    void setShowPO2Cell1(bool show);
    void setShowPO2Cell2(bool show);
    void setShowPO2Cell3(bool show);
    void setShowCompositePO2(bool show);
    
    // Generate overlay for a specific time point
    Q_INVOKABLE QImage generateOverlay(DiveData* dive, double timePoint);
    
    // Generate a preview image
    Q_INVOKABLE QImage generatePreview(DiveData* dive);
    
signals:
    void templateChanged();
    void fontChanged();
    void textColorChanged();
    void showDepthChanged();
    void showTemperatureChanged();
    void showNDLChanged();
    void showPressureChanged();
    void showTimeChanged();
    
    // CCR signals
    void showPO2Cell1Changed();
    void showPO2Cell2Changed();
    void showPO2Cell3Changed();
    void showCompositePO2Changed();
    
private:
    QString m_templatePath;
    QFont m_font;
    QColor m_textColor;
    bool m_showDepth;
    bool m_showTemperature;
    bool m_showNDL;
    bool m_showPressure;
    bool m_showTime;
    
    // CCR settings
    bool m_showPO2Cell1;
    bool m_showPO2Cell2;
    bool m_showPO2Cell3;
    bool m_showCompositePO2;
    
    // Helper methods for drawing
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
};

#endif // OVERLAY_GEN_H