#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QDebug>

// Include the generated version header
#include "version.h"

// Include all the necessary headers
#include "include/ui/main_window.h"
#include "include/core/dive_data.h"
#include "include/core/log_parser.h"
#include "include/ui/timeline.h"
#include "include/generators/overlay_gen.h"
#include "include/export/image_export.h"
#include "include/export/video_export.h"
#include "include/generators/overlay_image_provider.h"
#include "include/core/config.h"
#include "include/core/units.h"

// Global image provider
OverlayImageProvider* g_imageProvider = nullptr;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    // Set application info
    app.setApplicationName("Unabara");
    app.setApplicationVersion(UNABARA_VERSION_STR);
    app.setOrganizationName("UnabaraProject");

    qInfo() << "Starting Unabara version" << UNABARA_VERSION_STR;
    
    // Register C++ types with QML
    qmlRegisterType<Timeline>("Unabara.UI", 1, 0, "Timeline");
    qmlRegisterType<OverlayGenerator>("Unabara.Generators", 1, 0, "OverlayGenerator");
    qmlRegisterType<ImageExporter>("Unabara.Export", 1, 0, "ImageExporter");
    qmlRegisterType<VideoExporter>("Unabara.Export", 1, 0, "VideoExporter");
    qmlRegisterUncreatableMetaObject(Units::staticMetaObject, "Unabara.Core", 1, 0, "Units", "Units is a utility class");
    
    // Create the QML engine
    QQmlApplicationEngine engine;
    
    // Debug resource paths
    qDebug() << "Resource file exists:" << QFile::exists(":/main.qml");
    
    // Create the main objects
    MainWindow mainWindow;
    LogParser logParser;
    
    // Create the overlay generator and image provider
    OverlayGenerator* overlayGenerator = new OverlayGenerator();
    OverlayImageProvider* imageProvider = new OverlayImageProvider(overlayGenerator);

    // Register the image provider
    engine.addImageProvider("overlay", imageProvider);

    g_imageProvider = imageProvider;  // Set the global variable
    
    // Connect the LogParser signals to MainWindow slots
    QObject::connect(&logParser, &LogParser::diveImported, 
                     &mainWindow, &MainWindow::onDiveImported);
    
    // Connect signals to update the image provider
    QObject::connect(&mainWindow, &MainWindow::currentDiveChanged, 
                    [imageProvider, &mainWindow]() {
                        imageProvider->setCurrentDive(mainWindow.currentDive());
                    });
    
    qDebug() << "Connected LogParser::diveImported to MainWindow::onDiveImported";
    
    // Expose C++ objects to QML
    engine.rootContext()->setContextProperty("mainWindow", &mainWindow);
    engine.rootContext()->setContextProperty("logParser", &logParser);
    engine.rootContext()->setContextProperty("overlayGenerator", overlayGenerator);
    engine.rootContext()->setContextProperty("config", Config::instance());

    // Expose version to QML
    engine.rootContext()->setContextProperty("appVersion", UNABARA_VERSION_STR);
    
    // Load the main QML file
    const QUrl url(QStringLiteral("qrc:/main.qml"));
    qDebug() << "Loading QML file from:" << url.toString();
    qDebug() << "File exists:" << QFile::exists(":/main.qml");
    
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl) {
                qDebug() << "Failed to load QML file:" << objUrl;
                QCoreApplication::exit(-1);
            }
        }, Qt::QueuedConnection
    );
    engine.load(url);
    
    return app.exec();
}