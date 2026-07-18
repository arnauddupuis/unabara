#ifndef CONFIG_H
#define CONFIG_H

#include <QObject>
#include <QSettings>
#include <QString>
#include <QFont>
#include <QColor>
#include <QMap>
#include <QHash>
#include <QStringList>
#include <QVariantMap>
#include "include/core/units.h"
#include "include/core/video_overlay_layout.h"

class Config : public QObject
{
    Q_OBJECT
    
    // General settings
    Q_PROPERTY(QString lastImportPath READ lastImportPath WRITE setLastImportPath NOTIFY lastImportPathChanged)
    Q_PROPERTY(QString lastExportPath READ lastExportPath WRITE setLastExportPath NOTIFY lastExportPathChanged)
    
    // Overlay settings
    Q_PROPERTY(QString templatePath READ templatePath WRITE setTemplatePath NOTIFY templatePathChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY textColorChanged)
    Q_PROPERTY(double backgroundOpacity READ backgroundOpacity WRITE setBackgroundOpacity NOTIFY backgroundOpacityChanged)
    Q_PROPERTY(bool showDepth READ showDepth WRITE setShowDepth NOTIFY showDepthChanged)
    Q_PROPERTY(bool showTemperature READ showTemperature WRITE setShowTemperature NOTIFY showTemperatureChanged)
    Q_PROPERTY(bool showNDL READ showNDL WRITE setShowNDL NOTIFY showNDLChanged)
    Q_PROPERTY(bool showPressure READ showPressure WRITE setShowPressure NOTIFY showPressureChanged)
    Q_PROPERTY(bool showTime READ showTime WRITE setShowTime NOTIFY showTimeChanged)
    Q_PROPERTY(bool showCNS READ showCNS WRITE setShowCNS NOTIFY showCNSChanged)
    Q_PROPERTY(Units::UnitSystem unitSystem READ unitSystem WRITE setUnitSystem NOTIFY unitSystemChanged)
    
    // CCR settings
    Q_PROPERTY(bool showPO2Cell1 READ showPO2Cell1 WRITE setShowPO2Cell1 NOTIFY showPO2Cell1Changed)
    Q_PROPERTY(bool showPO2Cell2 READ showPO2Cell2 WRITE setShowPO2Cell2 NOTIFY showPO2Cell2Changed)
    Q_PROPERTY(bool showPO2Cell3 READ showPO2Cell3 WRITE setShowPO2Cell3 NOTIFY showPO2Cell3Changed)
    Q_PROPERTY(bool showCompositePO2 READ showCompositePO2 WRITE setShowCompositePO2 NOTIFY showCompositePO2Changed)
    
    // Template directory
    Q_PROPERTY(QString templateDirectory READ templateDirectory WRITE setTemplateDirectory NOTIFY templateDirectoryChanged)

    // Active template (.utp file path)
    Q_PROPERTY(QString activeTemplatePath READ activeTemplatePath WRITE setActiveTemplatePath NOTIFY activeTemplatePathChanged)

    // Export settings
    Q_PROPERTY(double frameRate READ frameRate WRITE setFrameRate NOTIFY frameRateChanged)

    // Dive profile settings
    Q_PROPERTY(QColor profileBackgroundColor READ profileBackgroundColor WRITE setProfileBackgroundColor NOTIFY profileBackgroundColorChanged)
    Q_PROPERTY(double profileBackgroundOpacity READ profileBackgroundOpacity WRITE setProfileBackgroundOpacity NOTIFY profileBackgroundOpacityChanged)
    Q_PROPERTY(QColor profileCurveColor READ profileCurveColor WRITE setProfileCurveColor NOTIFY profileCurveColorChanged)
    Q_PROPERTY(int profileCurveWidth READ profileCurveWidth WRITE setProfileCurveWidth NOTIFY profileCurveWidthChanged)
    Q_PROPERTY(QColor profileIndicatorColor READ profileIndicatorColor WRITE setProfileIndicatorColor NOTIFY profileIndicatorColorChanged)
    Q_PROPERTY(int profileIndicatorMode READ profileIndicatorMode WRITE setProfileIndicatorMode NOTIFY profileIndicatorModeChanged)
    Q_PROPERTY(int profileIndicatorRadius READ profileIndicatorRadius WRITE setProfileIndicatorRadius NOTIFY profileIndicatorRadiusChanged)
    Q_PROPERTY(int profilePulsePeriodMs READ profilePulsePeriodMs WRITE setProfilePulsePeriodMs NOTIFY profilePulsePeriodMsChanged)
    Q_PROPERTY(int profileOutputWidth READ profileOutputWidth WRITE setProfileOutputWidth NOTIFY profileOutputWidthChanged)
    Q_PROPERTY(int profileOutputHeight READ profileOutputHeight WRITE setProfileOutputHeight NOTIFY profileOutputHeightChanged)
    Q_PROPERTY(QColor profileDecoZoneColor READ profileDecoZoneColor WRITE setProfileDecoZoneColor NOTIFY profileDecoZoneColorChanged)
    Q_PROPERTY(double profileDecoZoneOpacity READ profileDecoZoneOpacity WRITE setProfileDecoZoneOpacity NOTIFY profileDecoZoneOpacityChanged)
    Q_PROPERTY(bool profileGridEnabled READ profileGridEnabled WRITE setProfileGridEnabled NOTIFY profileGridEnabledChanged)
    Q_PROPERTY(int profileGridDepthInterval READ profileGridDepthInterval WRITE setProfileGridDepthInterval NOTIFY profileGridDepthIntervalChanged)
    Q_PROPERTY(int profileGridTimeInterval READ profileGridTimeInterval WRITE setProfileGridTimeInterval NOTIFY profileGridTimeIntervalChanged)
    Q_PROPERTY(QColor profileGridColor READ profileGridColor WRITE setProfileGridColor NOTIFY profileGridColorChanged)
    Q_PROPERTY(double profileGridOpacity READ profileGridOpacity WRITE setProfileGridOpacity NOTIFY profileGridOpacityChanged)
    Q_PROPERTY(int profileGridLineWidth READ profileGridLineWidth WRITE setProfileGridLineWidth NOTIFY profileGridLineWidthChanged)
    Q_PROPERTY(bool profileGridShowLabels READ profileGridShowLabels WRITE setProfileGridShowLabels NOTIFY profileGridShowLabelsChanged)

public:
    static Config* instance();
    
    // General settings
    QString lastImportPath() const;
    void setLastImportPath(const QString &path);
    
    QString lastExportPath() const;
    void setLastExportPath(const QString &path);
    
    // Overlay settings
    QString templatePath() const;
    void setTemplatePath(const QString &path);
    
    QFont font() const;
    void setFont(const QFont &font);
    
    QColor textColor() const;
    void setTextColor(const QColor &color);

    double backgroundOpacity() const;
    void setBackgroundOpacity(double opacity);

    bool showDepth() const;
    void setShowDepth(bool show);
    
    bool showTemperature() const;
    void setShowTemperature(bool show);
    
    bool showNDL() const;
    void setShowNDL(bool show);
    
    bool showPressure() const;
    void setShowPressure(bool show);
    
    bool showTime() const;
    void setShowTime(bool show);

    bool showCNS() const;
    void setShowCNS(bool show);

    Units::UnitSystem unitSystem() const;
    void setUnitSystem(Units::UnitSystem system);
    
    // CCR settings
    bool showPO2Cell1() const;
    void setShowPO2Cell1(bool show);
    
    bool showPO2Cell2() const;
    void setShowPO2Cell2(bool show);
    
    bool showPO2Cell3() const;
    void setShowPO2Cell3(bool show);
    
    bool showCompositePO2() const;
    void setShowCompositePO2(bool show);
    
    // Template directory
    QString templateDirectory() const;
    void setTemplateDirectory(const QString &path);

    // Active template
    QString activeTemplatePath() const;
    void setActiveTemplatePath(const QString &path);

    // Export settings
    double frameRate() const;
    void setFrameRate(double fps);

    // Dive profile settings
    QColor profileBackgroundColor() const;
    void setProfileBackgroundColor(const QColor& color);

    double profileBackgroundOpacity() const;
    void setProfileBackgroundOpacity(double opacity);

    QColor profileCurveColor() const;
    void setProfileCurveColor(const QColor& color);

    int profileCurveWidth() const;
    void setProfileCurveWidth(int width);

    QColor profileIndicatorColor() const;
    void setProfileIndicatorColor(const QColor& color);

    int profileIndicatorMode() const;
    void setProfileIndicatorMode(int mode);

    int profileIndicatorRadius() const;
    void setProfileIndicatorRadius(int radius);

    int profilePulsePeriodMs() const;
    void setProfilePulsePeriodMs(int periodMs);

    int profileOutputWidth() const;
    void setProfileOutputWidth(int w);

    int profileOutputHeight() const;
    void setProfileOutputHeight(int h);

    QColor profileDecoZoneColor() const;
    void setProfileDecoZoneColor(const QColor& color);

    double profileDecoZoneOpacity() const;
    void setProfileDecoZoneOpacity(double opacity);

    bool profileGridEnabled() const;
    void setProfileGridEnabled(bool enabled);

    int profileGridDepthInterval() const;
    void setProfileGridDepthInterval(int interval);

    int profileGridTimeInterval() const;
    void setProfileGridTimeInterval(int seconds);

    QColor profileGridColor() const;
    void setProfileGridColor(const QColor& color);

    double profileGridOpacity() const;
    void setProfileGridOpacity(double opacity);

    int profileGridLineWidth() const;
    void setProfileGridLineWidth(int width);

    bool profileGridShowLabels() const;
    void setProfileGridShowLabels(bool show);

    // Save configuration to disk
    void saveConfig();

    // Camera pairing persistence
    Q_INVOKABLE QStringList cameraPairingNames() const;
    Q_INVOKABLE void addOrUpdateCameraPairing(const QString &name, double calibrationConstant);
    Q_INVOKABLE void removeCameraPairing(const QString &name);
    Q_INVOKABLE double cameraCalibrationConstant(const QString &name) const;

    // Per-video overlay layout persistence. Returns the saved layout for
    // `videoPath`, falling back to the last-used layout if none is saved
    // (so a fresh video inherits the user's most recent placement).
    Q_INVOKABLE QVariantMap videoOverlayLayout(const QString &videoPath) const;
    // Saves the layout for `videoPath` AND replaces the last-used layout
    // so the next opened video without a saved layout inherits it.
    Q_INVOKABLE void setVideoOverlayLayout(const QString &videoPath,
                                           const QVariantMap &layout);

signals:
    void lastImportPathChanged();
    void lastExportPathChanged();
    void templatePathChanged();
    void fontChanged();
    void textColorChanged();
    void backgroundOpacityChanged();
    void showDepthChanged();
    void showTemperatureChanged();
    void showNDLChanged();
    void showPressureChanged();
    void showTimeChanged();
    void showCNSChanged();
    void unitSystemChanged();
    void frameRateChanged();
    void templateDirectoryChanged();
    void activeTemplatePathChanged();

    // CCR signals
    void showPO2Cell1Changed();
    void showPO2Cell2Changed();
    void showPO2Cell3Changed();
    void showCompositePO2Changed();

    void cameraPairingsChanged();

    void videoOverlayLayoutChanged(const QString &videoPath);

    // Profile signals
    void profileBackgroundColorChanged();
    void profileBackgroundOpacityChanged();
    void profileCurveColorChanged();
    void profileCurveWidthChanged();
    void profileIndicatorColorChanged();
    void profileIndicatorModeChanged();
    void profileIndicatorRadiusChanged();
    void profilePulsePeriodMsChanged();
    void profileOutputWidthChanged();
    void profileOutputHeightChanged();
    void profileDecoZoneColorChanged();
    void profileDecoZoneOpacityChanged();
    void profileGridEnabledChanged();
    void profileGridDepthIntervalChanged();
    void profileGridTimeIntervalChanged();
    void profileGridColorChanged();
    void profileGridOpacityChanged();
    void profileGridLineWidthChanged();
    void profileGridShowLabelsChanged();

private:
    explicit Config(QObject *parent = nullptr);
    ~Config();
    
    // Singleton instance
    static Config* s_instance;
    
    // Settings storage
    QSettings m_settings;
    
    // Cache for settings
    QString m_lastImportPath;
    QString m_lastExportPath;
    QString m_templatePath;
    QFont m_font;
    QColor m_textColor;
    double m_backgroundOpacity;
    bool m_showDepth;
    bool m_showTemperature;
    bool m_showNDL;
    bool m_showPressure;
    bool m_showTime;
    bool m_showCNS;
    Units::UnitSystem m_unitSystem;
    double m_frameRate;
    QString m_templateDirectory;
    QString m_activeTemplatePath;
    
    // CCR settings
    bool m_showPO2Cell1;
    bool m_showPO2Cell2;
    bool m_showPO2Cell3;
    bool m_showCompositePO2;

    // Profile settings
    QColor m_profileBackgroundColor;
    double m_profileBackgroundOpacity;
    QColor m_profileCurveColor;
    int m_profileCurveWidth;
    QColor m_profileIndicatorColor;
    int m_profileIndicatorMode;
    int m_profileIndicatorRadius;
    int m_profilePulsePeriodMs;
    int m_profileOutputWidth;
    int m_profileOutputHeight;
    QColor m_profileDecoZoneColor;
    double m_profileDecoZoneOpacity;
    bool m_profileGridEnabled;
    int m_profileGridDepthInterval;
    int m_profileGridTimeInterval;
    QColor m_profileGridColor;
    double m_profileGridOpacity;
    int m_profileGridLineWidth;
    bool m_profileGridShowLabels;

    // Camera pairings: maps camera name -> calibration constant (seconds)
    QMap<QString, double> m_cameraPairings;

    // Per-video overlay layouts, keyed by absolute video path.
    QHash<QString, VideoOverlayLayout> m_videoOverlayLayouts;
    // Layout to seed a new video that has no saved entry yet.
    VideoOverlayLayout m_lastUsedVideoOverlayLayout;

    // Load configuration from disk
    void loadConfig();
};

#endif // CONFIG_H
