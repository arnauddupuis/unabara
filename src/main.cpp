#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QIcon>
#include <QDebug>

// Include the generated version header
#include "version.h"

// Include all the necessary headers
#include "include/ui/main_window.h"
#include "include/core/dive_data.h"
#include "include/core/log_parser.h"
#include "include/ui/timeline.h"
#include "include/ui/cell_model.h"
#include "include/generators/overlay_gen.h"
#include "include/export/image_export.h"
#include "include/export/video_export.h"
#include "include/generators/overlay_image_provider.h"
#include "include/generators/profile_gen.h"
#include "include/generators/profile_image_provider.h"
#include "include/core/config.h"
#include "include/core/units.h"
#include "include/core/update_checker.h"

// Global image provider
OverlayImageProvider* g_imageProvider = nullptr;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    // Set application info
    app.setApplicationName("Unabara");
    app.setApplicationVersion(UNABARA_VERSION_STR);
    app.setOrganizationName("UnabaraProject");
    app.setWindowIcon(QIcon(":/images/unabara-icon.png"));

    qInfo() << "Starting Unabara version" << UNABARA_VERSION_STR;
    
    // Register C++ types with QML
    qmlRegisterType<Timeline>("Unabara.UI", 1, 0, "Timeline");
    qmlRegisterType<CellModel>("Unabara.UI", 1, 0, "CellModel");
    qmlRegisterType<OverlayGenerator>("Unabara.Generators", 1, 0, "OverlayGenerator");
    qmlRegisterType<ProfileGenerator>("Unabara.Generators", 1, 0, "ProfileGenerator");
    qmlRegisterType<ImageExporter>("Unabara.Export", 1, 0, "ImageExporter");
    qmlRegisterType<VideoExporter>("Unabara.Export", 1, 0, "VideoExporter");
    qmlRegisterUncreatableMetaObject(Units::staticMetaObject, "Unabara.Core", 1, 0, "Units", "Units is a utility class");
    
    // Persist configuration on application quit. Config's destructor calls
    // saveConfig(), but the singleton is never explicitly deleted, so without
    // this hook only the few setters that explicitly call saveConfig() persist
    // their values across runs.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        Config::instance()->saveConfig();
    });

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

    // Create the profile generator and image provider (mirrors the overlay pair)
    ProfileGenerator* profileGenerator = new ProfileGenerator();
    ProfileImageProvider* profileImageProvider = new ProfileImageProvider(profileGenerator);
    engine.addImageProvider("profile", profileImageProvider);
    g_profileImageProvider = profileImageProvider;

    // Connect the LogParser signals to MainWindow slots
    QObject::connect(&logParser, &LogParser::diveImported,
                     &mainWindow, &MainWindow::onDiveImported);
    QObject::connect(&logParser, &LogParser::multipleImported,
                     &mainWindow, &MainWindow::onMultipleDivesImported);

    // Connect signals to update the image providers when the active dive changes
    QObject::connect(&mainWindow, &MainWindow::currentDiveChanged,
                    [imageProvider, profileImageProvider, &mainWindow]() {
                        DiveData* dive = mainWindow.currentDive();
                        imageProvider->setCurrentDive(dive);
                        profileImageProvider->setCurrentDive(dive);
                    });
    
    qDebug() << "Connected LogParser::diveImported to MainWindow::onDiveImported";
    
    // Expose C++ objects to QML
    engine.rootContext()->setContextProperty("mainWindow", &mainWindow);
    engine.rootContext()->setContextProperty("logParser", &logParser);
    engine.rootContext()->setContextProperty("overlayGenerator", overlayGenerator);
    engine.rootContext()->setContextProperty("profileGenerator", profileGenerator);
    engine.rootContext()->setContextProperty("config", Config::instance());

    // Expose version to QML
    engine.rootContext()->setContextProperty("appVersion", UNABARA_VERSION_STR);

    // Create update checker and expose to QML
    UpdateChecker updateChecker;
    engine.rootContext()->setContextProperty("updateChecker", &updateChecker);
    
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