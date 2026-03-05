#ifndef CONFIG_H
#define CONFIG_H

#include <QObject>
#include <QSettings>
#include <QString>
#include <QFont>
#include <QColor>
#include "include/core/units.h"

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
    Q_PROPERTY(Units::UnitSystem unitSystem READ unitSystem WRITE setUnitSystem NOTIFY unitSystemChanged)
    
    // CCR settings
    Q_PROPERTY(bool showPO2Cell1 READ showPO2Cell1 WRITE setShowPO2Cell1 NOTIFY showPO2Cell1Changed)
    Q_PROPERTY(bool showPO2Cell2 READ showPO2Cell2 WRITE setShowPO2Cell2 NOTIFY showPO2Cell2Changed)
    Q_PROPERTY(bool showPO2Cell3 READ showPO2Cell3 WRITE setShowPO2Cell3 NOTIFY showPO2Cell3Changed)
    Q_PROPERTY(bool showCompositePO2 READ showCompositePO2 WRITE setShowCompositePO2 NOTIFY showCompositePO2Changed)
    
    // Template directory
    Q_PROPERTY(QString templateDirectory READ templateDirectory WRITE setTemplateDirectory NOTIFY templateDirectoryChanged)

    // Export settings
    Q_PROPERTY(double frameRate READ frameRate WRITE setFrameRate NOTIFY frameRateChanged)
    
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

    // Export settings
    double frameRate() const;
    void setFrameRate(double fps);
    
    // Save configuration to disk
    void saveConfig();
    
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
    void unitSystemChanged();
    void frameRateChanged();
    void templateDirectoryChanged();

    // CCR signals
    void showPO2Cell1Changed();
    void showPO2Cell2Changed();
    void showPO2Cell3Changed();
    void showCompositePO2Changed();
    
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
    Units::UnitSystem m_unitSystem;
    double m_frameRate;
    QString m_templateDirectory;
    
    // CCR settings
    bool m_showPO2Cell1;
    bool m_showPO2Cell2;
    bool m_showPO2Cell3;
    bool m_showCompositePO2;
    
    // Load configuration from disk
    void loadConfig();
};

#endif // CONFIG_H
