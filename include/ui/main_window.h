#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QObject>
#include <QString>
#include <QList>
#include "include/core/dive_data.h"

class MainWindow : public QObject
{
    Q_OBJECT
    
    Q_PROPERTY(DiveData* currentDive READ currentDive WRITE setCurrentDive NOTIFY currentDiveChanged)
    Q_PROPERTY(bool hasActiveDive READ hasActiveDive NOTIFY currentDiveChanged)
    
public:
    explicit MainWindow(QObject *parent = nullptr);
    ~MainWindow();
    
    // Dive management
    DiveData* currentDive() const { return m_currentDive; }
    void setCurrentDive(DiveData* dive);
    bool hasActiveDive() const { return m_currentDive != nullptr; }
    
    // File dialogs
    Q_INVOKABLE QString openFileDialog(const QString &title, const QString &filter);
    Q_INVOKABLE QString saveFileDialog(const QString &title, const QString &filter);
    Q_INVOKABLE QString selectDirectoryDialog(const QString &title);
    
    // UI actions
    Q_INVOKABLE void showAboutDialog();
    Q_INVOKABLE void showPreferencesDialog();
    Q_INVOKABLE void exitApplication();
    
    // Helper functions
    Q_INVOKABLE QString urlToLocalFile(const QString &urlString);

    
public slots:
    void onDiveImported(DiveData* dive);
    void onMultipleDivesImported(QList<DiveData*> dives);
    void onDiveSelected(DiveData* dive);
    
    Q_INVOKABLE void selectDiveByIndex(int index);
    
signals:
    void currentDiveChanged();
    void exportRequested(const QString &path);
    void multipleDivesFound(QVariantList dives);
    
private:
    DiveData* m_currentDive;
    QList<DiveData*> m_availableDives;
    QList<DiveData*> m_pendingDiveSelection;
};

#endif // MAIN_WINDOW_H