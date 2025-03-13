#include "include/core/config.h"
#include <QStandardPaths>
#include <QDir>

// Initialize static instance
Config* Config::s_instance = nullptr;

Config* Config::instance()
{
    if (!s_instance) {
        s_instance = new Config();
    }
    return s_instance;
}

Config::Config(QObject *parent)
    : QObject(parent)
    , m_settings("UnabaraProject", "Unabara")
    , m_font("Arial", 12)
    , m_textColor(Qt::white)
    , m_showDepth(true)
    , m_showTemperature(true)
    , m_showNDL(true)
    , m_showPressure(true)
    , m_showTime(true)
    , m_frameRate(10.0)
{
    // Load settings from disk
    loadConfig();
}

Config::~Config()
{
    // Save settings when destroyed
    saveConfig();
}

QString Config::lastImportPath() const
{
    return m_lastImportPath;
}

void Config::setLastImportPath(const QString &path)
{
    if (m_lastImportPath != path) {
        m_lastImportPath = path;
        emit lastImportPathChanged();
    }
}

QString Config::lastExportPath() const
{
    return m_lastExportPath;
}

void Config::setLastExportPath(const QString &path)
{
    if (m_lastExportPath != path) {
        m_lastExportPath = path;
        emit lastExportPathChanged();
    }
}

QString Config::templatePath() const
{
    return m_templatePath;
}

void Config::setTemplatePath(const QString &path)
{
    if (m_templatePath != path) {
        m_templatePath = path;
        emit templatePathChanged();
    }
}

QFont Config::font() const
{
    return m_font;
}

void Config::setFont(const QFont &font)
{
    if (m_font != font) {
        m_font = font;
        emit fontChanged();
    }
}

QColor Config::textColor() const
{
    return m_textColor;
}

void Config::setTextColor(const QColor &color)
{
    if (m_textColor != color) {
        m_textColor = color;
        emit textColorChanged();
    }
}

bool Config::showDepth() const
{
    return m_showDepth;
}

void Config::setShowDepth(bool show)
{
    if (m_showDepth != show) {
        m_showDepth = show;
        emit showDepthChanged();
    }
}

bool Config::showTemperature() const
{
    return m_showTemperature;
}

void Config::setShowTemperature(bool show)
{
    if (m_showTemperature != show) {
        m_showTemperature = show;
        emit showTemperatureChanged();
    }
}

bool Config::showNDL() const
{
    return m_showNDL;
}

void Config::setShowNDL(bool show)
{
    if (m_showNDL != show) {
        m_showNDL = show;
        emit showNDLChanged();
    }
}

bool Config::showPressure() const
{
    return m_showPressure;
}

void Config::setShowPressure(bool show)
{
    if (m_showPressure != show) {
        m_showPressure = show;
        emit showPressureChanged();
    }
}

bool Config::showTime() const
{
    return m_showTime;
}

void Config::setShowTime(bool show)
{
    if (m_showTime != show) {
        m_showTime = show;
        emit showTimeChanged();
    }
}

double Config::frameRate() const
{
    return m_frameRate;
}

void Config::setFrameRate(double fps)
{
    if (m_frameRate != fps) {
        m_frameRate = fps;
        emit frameRateChanged();
    }
}

void Config::loadConfig()
{
    // Load general settings
    m_lastImportPath = m_settings.value("paths/lastImport", 
                                        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    m_lastExportPath = m_settings.value("paths/lastExport", 
                                        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/Unabara").toString();
    
    // Load overlay settings
    m_templatePath = m_settings.value("overlay/template", ":/default_overlay.png").toString();
    
    // Load font
    QString fontFamily = m_settings.value("overlay/fontFamily", "Arial").toString();
    int fontSize = m_settings.value("overlay/fontSize", 12).toInt();
    bool fontBold = m_settings.value("overlay/fontBold", false).toBool();
    bool fontItalic = m_settings.value("overlay/fontItalic", false).toBool();
    
    m_font = QFont(fontFamily, fontSize);
    m_font.setBold(fontBold);
    m_font.setItalic(fontItalic);
    
    // Load text color
    int r = m_settings.value("overlay/textColorR", 255).toInt();
    int g = m_settings.value("overlay/textColorG", 255).toInt();
    int b = m_settings.value("overlay/textColorB", 255).toInt();
    m_textColor = QColor(r, g, b);
    
    // Load display flags
    m_showDepth = m_settings.value("overlay/showDepth", true).toBool();
    m_showTemperature = m_settings.value("overlay/showTemperature", true).toBool();
    m_showNDL = m_settings.value("overlay/showNDL", true).toBool();
    m_showPressure = m_settings.value("overlay/showPressure", true).toBool();
    m_showTime = m_settings.value("overlay/showTime", true).toBool();
    
    // Load export settings
    m_frameRate = m_settings.value("export/frameRate", 10.0).toDouble();
}

void Config::saveConfig()
{
    // Save general settings
    m_settings.setValue("paths/lastImport", m_lastImportPath);
    m_settings.setValue("paths/lastExport", m_lastExportPath);
    
    // Save overlay settings
    m_settings.setValue("overlay/template", m_templatePath);
    
    // Save font
    m_settings.setValue("overlay/fontFamily", m_font.family());
    m_settings.setValue("overlay/fontSize", m_font.pointSize());
    m_settings.setValue("overlay/fontBold", m_font.bold());
    m_settings.setValue("overlay/fontItalic", m_font.italic());
    
    // Save text color
    m_settings.setValue("overlay/textColorR", m_textColor.red());
    m_settings.setValue("overlay/textColorG", m_textColor.green());
    m_settings.setValue("overlay/textColorB", m_textColor.blue());
    
    // Save display flags
    m_settings.setValue("overlay/showDepth", m_showDepth);
    m_settings.setValue("overlay/showTemperature", m_showTemperature);
    m_settings.setValue("overlay/showNDL", m_showNDL);
    m_settings.setValue("overlay/showPressure", m_showPressure);
    m_settings.setValue("overlay/showTime", m_showTime);
    
    // Save export settings
    m_settings.setValue("export/frameRate", m_frameRate);
    
    // Force write to disk
    m_settings.sync();
}
