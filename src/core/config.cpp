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
    , m_font("Sans Serif", 12)
    , m_textColor(Qt::white)
    , m_backgroundOpacity(1.0)
    , m_showDepth(true)
    , m_showTemperature(true)
    , m_showNDL(true)
    , m_showPressure(true)
    , m_showTime(true)
    , m_unitSystem(Units::UnitSystem::Metric)
    , m_frameRate(10.0)
    , m_showPO2Cell1(false)
    , m_showPO2Cell2(false)
    , m_showPO2Cell3(false)
    , m_showCompositePO2(false)
    , m_profileBackgroundColor(Qt::black)
    , m_profileBackgroundOpacity(0.0)
    , m_profileCurveColor(QColor("#9C27B0"))
    , m_profileIndicatorColor(QColor(0, 255, 0))
    , m_profileIndicatorMode(0)   // 0 = Static, 1 = Pulsing
    , m_profileIndicatorRadius(8)
    , m_profilePulsePeriodMs(2000)
    , m_profileOutputWidth(1920)
    , m_profileOutputHeight(400)
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

double Config::backgroundOpacity() const
{
    return m_backgroundOpacity;
}

void Config::setBackgroundOpacity(double opacity)
{
    opacity = qBound(0.0, opacity, 1.0); // Clamp between 0.0 and 1.0
    if (qAbs(m_backgroundOpacity - opacity) > 0.001) { // Use floating point comparison
        m_backgroundOpacity = opacity;
        emit backgroundOpacityChanged();
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

Units::UnitSystem Config::unitSystem() const
{
    return m_unitSystem;
}

void Config::setUnitSystem(Units::UnitSystem system)
{
    if (m_unitSystem != system) {
        m_unitSystem = system;
        emit unitSystemChanged();
    }
}

void Config::setFrameRate(double fps)
{
    if (m_frameRate != fps) {
        m_frameRate = fps;
        emit frameRateChanged();
    }
}

QString Config::templateDirectory() const
{
    return m_templateDirectory;
}

void Config::setTemplateDirectory(const QString &path)
{
    if (m_templateDirectory != path) {
        m_templateDirectory = path;
        // Create the directory if it doesn't exist
        QDir dir(m_templateDirectory);
        if (!dir.exists()) {
            dir.mkpath(".");
        }
        saveConfig();
        emit templateDirectoryChanged();
    }
}

// Active template path
QString Config::activeTemplatePath() const
{
    return m_activeTemplatePath;
}

void Config::setActiveTemplatePath(const QString &path)
{
    if (m_activeTemplatePath != path) {
        m_activeTemplatePath = path;
        saveConfig();
        emit activeTemplatePathChanged();
    }
}

// CCR settings implementation
bool Config::showPO2Cell1() const
{
    return m_showPO2Cell1;
}

void Config::setShowPO2Cell1(bool show)
{
    if (m_showPO2Cell1 != show) {
        m_showPO2Cell1 = show;
        emit showPO2Cell1Changed();
    }
}

bool Config::showPO2Cell2() const
{
    return m_showPO2Cell2;
}

void Config::setShowPO2Cell2(bool show)
{
    if (m_showPO2Cell2 != show) {
        m_showPO2Cell2 = show;
        emit showPO2Cell2Changed();
    }
}

bool Config::showPO2Cell3() const
{
    return m_showPO2Cell3;
}

void Config::setShowPO2Cell3(bool show)
{
    if (m_showPO2Cell3 != show) {
        m_showPO2Cell3 = show;
        emit showPO2Cell3Changed();
    }
}

bool Config::showCompositePO2() const
{
    return m_showCompositePO2;
}

void Config::setShowCompositePO2(bool show)
{
    if (m_showCompositePO2 != show) {
        m_showCompositePO2 = show;
        emit showCompositePO2Changed();
    }
}

// ---- Profile settings -----------------------------------------------------

QColor Config::profileBackgroundColor() const { return m_profileBackgroundColor; }
void Config::setProfileBackgroundColor(const QColor& color)
{
    if (m_profileBackgroundColor != color) {
        m_profileBackgroundColor = color;
        emit profileBackgroundColorChanged();
    }
}

double Config::profileBackgroundOpacity() const { return m_profileBackgroundOpacity; }
void Config::setProfileBackgroundOpacity(double opacity)
{
    opacity = qBound(0.0, opacity, 1.0);
    if (qAbs(m_profileBackgroundOpacity - opacity) > 0.001) {
        m_profileBackgroundOpacity = opacity;
        emit profileBackgroundOpacityChanged();
    }
}

QColor Config::profileCurveColor() const { return m_profileCurveColor; }
void Config::setProfileCurveColor(const QColor& color)
{
    if (m_profileCurveColor != color) {
        m_profileCurveColor = color;
        emit profileCurveColorChanged();
    }
}

QColor Config::profileIndicatorColor() const { return m_profileIndicatorColor; }
void Config::setProfileIndicatorColor(const QColor& color)
{
    if (m_profileIndicatorColor != color) {
        m_profileIndicatorColor = color;
        emit profileIndicatorColorChanged();
    }
}

int Config::profileIndicatorMode() const { return m_profileIndicatorMode; }
void Config::setProfileIndicatorMode(int mode)
{
    if (m_profileIndicatorMode != mode) {
        m_profileIndicatorMode = mode;
        emit profileIndicatorModeChanged();
    }
}

int Config::profileIndicatorRadius() const { return m_profileIndicatorRadius; }
void Config::setProfileIndicatorRadius(int radius)
{
    radius = qMax(1, radius);
    if (m_profileIndicatorRadius != radius) {
        m_profileIndicatorRadius = radius;
        emit profileIndicatorRadiusChanged();
    }
}

int Config::profilePulsePeriodMs() const { return m_profilePulsePeriodMs; }
void Config::setProfilePulsePeriodMs(int periodMs)
{
    periodMs = qMax(100, periodMs);
    if (m_profilePulsePeriodMs != periodMs) {
        m_profilePulsePeriodMs = periodMs;
        emit profilePulsePeriodMsChanged();
    }
}

int Config::profileOutputWidth() const { return m_profileOutputWidth; }
void Config::setProfileOutputWidth(int w)
{
    w = qMax(16, w);
    if (m_profileOutputWidth != w) {
        m_profileOutputWidth = w;
        emit profileOutputWidthChanged();
    }
}

int Config::profileOutputHeight() const { return m_profileOutputHeight; }
void Config::setProfileOutputHeight(int h)
{
    h = qMax(16, h);
    if (m_profileOutputHeight != h) {
        m_profileOutputHeight = h;
        emit profileOutputHeightChanged();
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
    m_templatePath = m_settings.value("overlay/template", ":/images/DC_Faces/unabara_round_ocean.png").toString();
    
    // Load font
    QString fontFamily = m_settings.value("overlay/fontFamily", "Sans Serif").toString();
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

    // Load background opacity
    m_backgroundOpacity = m_settings.value("overlay/backgroundOpacity", 1.0).toDouble();

    // Load display flags
    m_showDepth = m_settings.value("overlay/showDepth", true).toBool();
    m_showTemperature = m_settings.value("overlay/showTemperature", true).toBool();
    m_showNDL = m_settings.value("overlay/showNDL", true).toBool();
    m_showPressure = m_settings.value("overlay/showPressure", true).toBool();
    m_showTime = m_settings.value("overlay/showTime", true).toBool();
    
    // Load CCR settings
    m_showPO2Cell1 = m_settings.value("overlay/showPO2Cell1", false).toBool();
    m_showPO2Cell2 = m_settings.value("overlay/showPO2Cell2", false).toBool();
    m_showPO2Cell3 = m_settings.value("overlay/showPO2Cell3", false).toBool();
    m_showCompositePO2 = m_settings.value("overlay/showCompositePO2", false).toBool();
    
    // Load unit system
    int unitSystemValue = m_settings.value("overlay/unitSystem", static_cast<int>(Units::UnitSystem::Metric)).toInt();
    m_unitSystem = static_cast<Units::UnitSystem>(unitSystemValue);
    
    // Load template directory
    QString defaultTemplateDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/templates";
    m_templateDirectory = m_settings.value("paths/templateDirectory", defaultTemplateDir).toString();
    // Create the directory if it doesn't exist
    QDir templateDir(m_templateDirectory);
    if (!templateDir.exists()) {
        templateDir.mkpath(".");
    }

    // Load active template path
    m_activeTemplatePath = m_settings.value("overlay/activeTemplate", "").toString();

    // Load export settings
    m_frameRate = m_settings.value("export/frameRate", 10.0).toDouble();

    // Load profile settings
    {
        const int bgR = m_settings.value("profile/backgroundColorR", 0).toInt();
        const int bgG = m_settings.value("profile/backgroundColorG", 0).toInt();
        const int bgB = m_settings.value("profile/backgroundColorB", 0).toInt();
        m_profileBackgroundColor = QColor(bgR, bgG, bgB);
    }
    m_profileBackgroundOpacity = m_settings.value("profile/backgroundOpacity", 0.0).toDouble();
    {
        const int cR = m_settings.value("profile/curveColorR", 0x9C).toInt();
        const int cG = m_settings.value("profile/curveColorG", 0x27).toInt();
        const int cB = m_settings.value("profile/curveColorB", 0xB0).toInt();
        m_profileCurveColor = QColor(cR, cG, cB);
    }
    {
        const int iR = m_settings.value("profile/indicatorColorR", 0).toInt();
        const int iG = m_settings.value("profile/indicatorColorG", 255).toInt();
        const int iB = m_settings.value("profile/indicatorColorB", 0).toInt();
        m_profileIndicatorColor = QColor(iR, iG, iB);
    }
    m_profileIndicatorMode = m_settings.value("profile/indicatorMode", 0).toInt();
    m_profileIndicatorRadius = m_settings.value("profile/indicatorRadius", 8).toInt();
    m_profilePulsePeriodMs = m_settings.value("profile/pulsePeriodMs", 2000).toInt();
    m_profileOutputWidth = m_settings.value("profile/outputWidth", 1920).toInt();
    m_profileOutputHeight = m_settings.value("profile/outputHeight", 400).toInt();
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

    // Save background opacity
    m_settings.setValue("overlay/backgroundOpacity", m_backgroundOpacity);

    // Save display flags
    m_settings.setValue("overlay/showDepth", m_showDepth);
    m_settings.setValue("overlay/showTemperature", m_showTemperature);
    m_settings.setValue("overlay/showNDL", m_showNDL);
    m_settings.setValue("overlay/showPressure", m_showPressure);
    m_settings.setValue("overlay/showTime", m_showTime);
    
    // Save CCR settings
    m_settings.setValue("overlay/showPO2Cell1", m_showPO2Cell1);
    m_settings.setValue("overlay/showPO2Cell2", m_showPO2Cell2);
    m_settings.setValue("overlay/showPO2Cell3", m_showPO2Cell3);
    m_settings.setValue("overlay/showCompositePO2", m_showCompositePO2);
    
    // Save unit system
    m_settings.setValue("overlay/unitSystem", static_cast<int>(m_unitSystem));
    
    // Save template directory
    m_settings.setValue("paths/templateDirectory", m_templateDirectory);

    // Save active template path
    m_settings.setValue("overlay/activeTemplate", m_activeTemplatePath);

    // Save export settings
    m_settings.setValue("export/frameRate", m_frameRate);

    // Save profile settings
    m_settings.setValue("profile/backgroundColorR", m_profileBackgroundColor.red());
    m_settings.setValue("profile/backgroundColorG", m_profileBackgroundColor.green());
    m_settings.setValue("profile/backgroundColorB", m_profileBackgroundColor.blue());
    m_settings.setValue("profile/backgroundOpacity", m_profileBackgroundOpacity);
    m_settings.setValue("profile/curveColorR", m_profileCurveColor.red());
    m_settings.setValue("profile/curveColorG", m_profileCurveColor.green());
    m_settings.setValue("profile/curveColorB", m_profileCurveColor.blue());
    m_settings.setValue("profile/indicatorColorR", m_profileIndicatorColor.red());
    m_settings.setValue("profile/indicatorColorG", m_profileIndicatorColor.green());
    m_settings.setValue("profile/indicatorColorB", m_profileIndicatorColor.blue());
    m_settings.setValue("profile/indicatorMode", m_profileIndicatorMode);
    m_settings.setValue("profile/indicatorRadius", m_profileIndicatorRadius);
    m_settings.setValue("profile/pulsePeriodMs", m_profilePulsePeriodMs);
    m_settings.setValue("profile/outputWidth", m_profileOutputWidth);
    m_settings.setValue("profile/outputHeight", m_profileOutputHeight);

    // Force write to disk
    m_settings.sync();
}
