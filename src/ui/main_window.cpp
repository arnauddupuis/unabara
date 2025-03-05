#include "include/ui/main_window.h"
#include "include/generators/overlay_image_provider.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QCoreApplication>
#include <QStandardPaths>

MainWindow::MainWindow(QObject *parent)
    : QObject(parent)
    , m_currentDive(nullptr)
{
}

MainWindow::~MainWindow()
{
    // Clean up any dive data objects we've created
    qDeleteAll(m_availableDives);
}

void MainWindow::setCurrentDive(DiveData* dive)
{
    if (m_currentDive != dive) {
        m_currentDive = dive;
        
        // Update the global image provider with the new dive
        extern OverlayImageProvider* g_imageProvider;
        if (g_imageProvider) {
            g_imageProvider->setCurrentDive(dive);
        }
        
        emit currentDiveChanged();
    }
}

QString MainWindow::openFileDialog(const QString &title, const QString &filter)
{
    return QFileDialog::getOpenFileName(nullptr, title, QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), filter);
}

QString MainWindow::saveFileDialog(const QString &title, const QString &filter)
{
    return QFileDialog::getSaveFileName(nullptr, title, QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), filter);
}

QString MainWindow::selectDirectoryDialog(const QString &title)
{
    return QFileDialog::getExistingDirectory(nullptr, title, QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
}

void MainWindow::showAboutDialog()
{
    QMessageBox::about(nullptr, tr("About Unabara"),
                       tr("Unabara - Dive Telemetry Overlay\n"
                          "Version 0.1\n\n"
                          "A tool for creating telemetry overlays for scuba diving videos."));
}

void MainWindow::showPreferencesDialog()
{
    // This would be implemented to show a preferences dialog
    // For now it's just a placeholder
}

void MainWindow::exitApplication()
{
    QCoreApplication::quit();
}

QString MainWindow::urlToLocalFile(const QString &urlString)
{
    QUrl url(urlString);
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }
    return urlString;
}

void MainWindow::onDiveImported(DiveData* dive)
{
    if (!dive) {
        qDebug() << "MainWindow::onDiveImported - Received nullptr dive";
        return;
    }
    
    qDebug() << "MainWindow::onDiveImported - Received dive:" << dive->diveName();
    
    // Add the dive to our list of available dives
    m_availableDives.append(dive);
    
    // Set it as the current dive
    setCurrentDive(dive);
    
    qDebug() << "Dive set as current, name:" << m_currentDive->diveName()
             << "duration:" << m_currentDive->durationSeconds() << "seconds"
             << "max depth:" << m_currentDive->maxDepth() << "meters";
}