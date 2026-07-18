#include "include/core/config.h"
#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtGlobal>
#include <cmath>

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
    , m_showCNS(false)
    , m_unitSystem(Units::UnitSystem::Metric)
    , m_frameRate(10.0)
    , m_showPO2Cell1(false)
    , m_showPO2Cell2(false)
    , m_showPO2Cell3(false)
    , m_showCompositePO2(false)
    , m_profileBackgroundColor(Qt::black)
    , m_profileBackgroundOpacity(0.0)
    , m_profileCurveColor(QColor("#9C27B0"))
    , m_profileCurveWidth(2)
    , m_profileIndicatorColor(QColor(0, 255, 0))
    , m_profileIndicatorMode(0)   // 0 = Static, 1 = Pulsing
    , m_profileIndicatorRadius(8)
    , m_profilePulsePeriodMs(2000)
    , m_profileOutputWidth(1920)
    , m_profileOutputHeight(400)
    , m_profileDecoZoneColor(QColor(255, 0, 0))
    , m_profileDecoZoneOpacity(0.35)
    , m_profileGridEnabled(false)
    , m_profileGridDepthInterval(10)
    , m_profileGridTimeInterval(600)
    , m_profileGridColor(QColor(180, 180, 180))
    , m_profileGridOpacity(0.5)
    , m_profileGridLineWidth(1)
    , m_profileGridShowLabels(true)
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

bool Config::showCNS() const
{
    return m_showCNS;
}

void Config::setShowCNS(bool show)
{
    if (m_showCNS != show) {
        m_showCNS = show;
        emit showCNSChanged();
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

int Config::profileCurveWidth() const { return m_profileCurveWidth; }
void Config::setProfileCurveWidth(int width)
{
    if (m_profileCurveWidth != width) {
        m_profileCurveWidth = width;
        emit profileCurveWidthChanged();
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

QColor Config::profileDecoZoneColor() const { return m_profileDecoZoneColor; }
void Config::setProfileDecoZoneColor(const QColor& color)
{
    if (m_profileDecoZoneColor != color) {
        m_profileDecoZoneColor = color;
        emit profileDecoZoneColorChanged();
    }
}

double Config::profileDecoZoneOpacity() const { return m_profileDecoZoneOpacity; }
void Config::setProfileDecoZoneOpacity(double opacity)
{
    opacity = qBound(0.0, opacity, 1.0);
    if (qAbs(m_profileDecoZoneOpacity - opacity) > 0.001) {
        m_profileDecoZoneOpacity = opacity;
        emit profileDecoZoneOpacityChanged();
    }
}

bool Config::profileGridEnabled() const { return m_profileGridEnabled; }
void Config::setProfileGridEnabled(bool enabled)
{
    if (m_profileGridEnabled != enabled) {
        m_profileGridEnabled = enabled;
        emit profileGridEnabledChanged();
    }
}

int Config::profileGridDepthInterval() const { return m_profileGridDepthInterval; }
void Config::setProfileGridDepthInterval(int interval)
{
    interval = qMax(1, interval);
    if (m_profileGridDepthInterval != interval) {
        m_profileGridDepthInterval = interval;
        emit profileGridDepthIntervalChanged();
    }
}

int Config::profileGridTimeInterval() const { return m_profileGridTimeInterval; }
void Config::setProfileGridTimeInterval(int seconds)
{
    seconds = qMax(1, seconds);
    if (m_profileGridTimeInterval != seconds) {
        m_profileGridTimeInterval = seconds;
        emit profileGridTimeIntervalChanged();
    }
}

QColor Config::profileGridColor() const { return m_profileGridColor; }
void Config::setProfileGridColor(const QColor& color)
{
    if (m_profileGridColor != color) {
        m_profileGridColor = color;
        emit profileGridColorChanged();
    }
}

double Config::profileGridOpacity() const { return m_profileGridOpacity; }
void Config::setProfileGridOpacity(double opacity)
{
    opacity = qBound(0.0, opacity, 1.0);
    if (qAbs(m_profileGridOpacity - opacity) > 0.001) {
        m_profileGridOpacity = opacity;
        emit profileGridOpacityChanged();
    }
}

int Config::profileGridLineWidth() const { return m_profileGridLineWidth; }
void Config::setProfileGridLineWidth(int width)
{
    width = qMax(1, width);
    if (m_profileGridLineWidth != width) {
        m_profileGridLineWidth = width;
        emit profileGridLineWidthChanged();
    }
}

bool Config::profileGridShowLabels() const { return m_profileGridShowLabels; }
void Config::setProfileGridShowLabels(bool show)
{
    if (m_profileGridShowLabels != show) {
        m_profileGridShowLabels = show;
        emit profileGridShowLabelsChanged();
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
    m_showCNS = m_settings.value("overlay/showCNS", false).toBool();

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
    m_profileCurveWidth = m_settings.value("profile/curveWidth", 2).toInt();
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
    {
        const int dR = m_settings.value("profile/decoZoneColorR", 255).toInt();
        const int dG = m_settings.value("profile/decoZoneColorG", 0).toInt();
        const int dB = m_settings.value("profile/decoZoneColorB", 0).toInt();
        m_profileDecoZoneColor = QColor(dR, dG, dB);
    }
    m_profileDecoZoneOpacity = m_settings.value("profile/decoZoneOpacity", 0.35).toDouble();
    m_profileGridEnabled = m_settings.value("profile/gridEnabled", false).toBool();
    m_profileGridDepthInterval = m_settings.value("profile/gridDepthInterval", 10).toInt();
    m_profileGridTimeInterval = m_settings.value("profile/gridTimeInterval", 600).toInt();
    {
        const int gR = m_settings.value("profile/gridColorR", 180).toInt();
        const int gG = m_settings.value("profile/gridColorG", 180).toInt();
        const int gB = m_settings.value("profile/gridColorB", 180).toInt();
        m_profileGridColor = QColor(gR, gG, gB);
    }
    m_profileGridOpacity = m_settings.value("profile/gridOpacity", 0.5).toDouble();
    m_profileGridLineWidth = m_settings.value("profile/gridLineWidth", 1).toInt();
    m_profileGridShowLabels = m_settings.value("profile/gridShowLabels", true).toBool();

    // Load per-video overlay layouts
    {
        m_videoOverlayLayouts.clear();
        const QString json = m_settings.value("video/overlayLayouts", "{}").toString();
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject root = doc.object();
            for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
                const QString path = it.key();
                if (it.value().isObject()) {
                    QVariantMap m = it.value().toObject().toVariantMap();
                    m_videoOverlayLayouts.insert(path, VideoOverlayLayout::fromVariantMap(m));
                }
            }
        }
        const QString lastJson = m_settings.value("video/lastOverlayLayout", "").toString();
        if (!lastJson.isEmpty()) {
            const QJsonDocument lastDoc = QJsonDocument::fromJson(lastJson.toUtf8());
            if (lastDoc.isObject()) {
                m_lastUsedVideoOverlayLayout = VideoOverlayLayout::fromVariantMap(
                    lastDoc.object().toVariantMap());
            }
        }
    }

    // Load camera pairings from JSON
    m_cameraPairings.clear();
    QString pairingsJson = m_settings.value("video/cameraPairings", "[]").toString();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(pairingsJson.toUtf8(), &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isArray()) {
        const QJsonArray arr = doc.array();
        for (const QJsonValue &val : arr) {
            QJsonObject obj = val.toObject();
            QString name = obj.value("name").toString();
            double constant = obj.value("calibrationConstant").toDouble(qQNaN());
            if (!name.isEmpty() && !std::isnan(constant))
                m_cameraPairings.insert(name, constant);
        }
    } else if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Config: failed to parse cameraPairings JSON:" << parseError.errorString();
    }
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
    m_settings.setValue("overlay/showCNS", m_showCNS);

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
    m_settings.setValue("profile/curveWidth", m_profileCurveWidth);
    m_settings.setValue("profile/indicatorColorR", m_profileIndicatorColor.red());
    m_settings.setValue("profile/indicatorColorG", m_profileIndicatorColor.green());
    m_settings.setValue("profile/indicatorColorB", m_profileIndicatorColor.blue());
    m_settings.setValue("profile/indicatorMode", m_profileIndicatorMode);
    m_settings.setValue("profile/indicatorRadius", m_profileIndicatorRadius);
    m_settings.setValue("profile/pulsePeriodMs", m_profilePulsePeriodMs);
    m_settings.setValue("profile/outputWidth", m_profileOutputWidth);
    m_settings.setValue("profile/outputHeight", m_profileOutputHeight);
    m_settings.setValue("profile/decoZoneColorR", m_profileDecoZoneColor.red());
    m_settings.setValue("profile/decoZoneColorG", m_profileDecoZoneColor.green());
    m_settings.setValue("profile/decoZoneColorB", m_profileDecoZoneColor.blue());
    m_settings.setValue("profile/decoZoneOpacity", m_profileDecoZoneOpacity);
    m_settings.setValue("profile/gridEnabled", m_profileGridEnabled);
    m_settings.setValue("profile/gridDepthInterval", m_profileGridDepthInterval);
    m_settings.setValue("profile/gridTimeInterval", m_profileGridTimeInterval);
    m_settings.setValue("profile/gridColorR", m_profileGridColor.red());
    m_settings.setValue("profile/gridColorG", m_profileGridColor.green());
    m_settings.setValue("profile/gridColorB", m_profileGridColor.blue());
    m_settings.setValue("profile/gridOpacity", m_profileGridOpacity);
    m_settings.setValue("profile/gridLineWidth", m_profileGridLineWidth);
    m_settings.setValue("profile/gridShowLabels", m_profileGridShowLabels);

    // Save per-video overlay layouts
    {
        QJsonObject root;
        for (auto it = m_videoOverlayLayouts.constBegin();
             it != m_videoOverlayLayouts.constEnd(); ++it) {
            root.insert(it.key(), QJsonObject::fromVariantMap(it.value().toVariantMap()));
        }
        m_settings.setValue("video/overlayLayouts",
                            QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
        const QJsonObject lastObj =
            QJsonObject::fromVariantMap(m_lastUsedVideoOverlayLayout.toVariantMap());
        m_settings.setValue("video/lastOverlayLayout",
                            QString::fromUtf8(QJsonDocument(lastObj).toJson(QJsonDocument::Compact)));
    }

    // Save camera pairings as compact JSON
    QJsonArray pairingsArray;
    for (auto it = m_cameraPairings.constBegin(); it != m_cameraPairings.constEnd(); ++it) {
        QJsonObject obj;
        obj["name"] = it.key();
        obj["calibrationConstant"] = it.value();
        pairingsArray.append(obj);
    }
    QJsonDocument pairingsDoc(pairingsArray);
    m_settings.setValue("video/cameraPairings",
                        QString::fromUtf8(pairingsDoc.toJson(QJsonDocument::Compact)));

    // Force write to disk
    m_settings.sync();
}

QStringList Config::cameraPairingNames() const
{
    return m_cameraPairings.keys();
}

void Config::addOrUpdateCameraPairing(const QString &name, double calibrationConstant)
{
    if (name.trimmed().isEmpty()) {
        qWarning() << "Config::addOrUpdateCameraPairing: empty name rejected";
        return;
    }
    m_cameraPairings.insert(name, calibrationConstant);
    saveConfig();
    emit cameraPairingsChanged();
}

void Config::removeCameraPairing(const QString &name)
{
    if (m_cameraPairings.remove(name) > 0) {
        saveConfig();
        emit cameraPairingsChanged();
    }
}

double Config::cameraCalibrationConstant(const QString &name) const
{
    return m_cameraPairings.value(name, qQNaN());
}

// ---- Per-video overlay layouts -------------------------------------------

QVariantMap Config::videoOverlayLayout(const QString &videoPath) const
{
    auto it = m_videoOverlayLayouts.constFind(videoPath);
    if (it != m_videoOverlayLayouts.constEnd())
        return it.value().toVariantMap();
    return m_lastUsedVideoOverlayLayout.toVariantMap();
}

void Config::setVideoOverlayLayout(const QString &videoPath, const QVariantMap &layout)
{
    if (videoPath.isEmpty())
        return;
    VideoOverlayLayout parsed = VideoOverlayLayout::fromVariantMap(layout);
    m_videoOverlayLayouts.insert(videoPath, parsed);
    m_lastUsedVideoOverlayLayout = parsed;
    saveConfig();
    emit videoOverlayLayoutChanged(videoPath);
}
