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
#include "include/generators/template_undo_stack.h"
#include "include/export/image_export.h"
#include "include/export/video_export.h"
#include "include/generators/overlay_image_provider.h"
#include "include/generators/profile_gen.h"
#include "include/generators/profile_image_provider.h"
#include "include/generators/frame_cache.h"
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

    // Undo/redo for the template editor. Constructed after the generator (which
    // loads its active template during construction) so it baselines a valid
    // state; tracked signals are connected below.
    TemplateUndoStack* undoManager = new TemplateUndoStack(overlayGenerator);

    // Register the image provider
    engine.addImageProvider("overlay", imageProvider);

    g_imageProvider = imageProvider;  // Set the global variable

    // Create the profile generator and image provider (mirrors the overlay pair)
    ProfileGenerator* profileGenerator = new ProfileGenerator();
    ProfileImageProvider* profileImageProvider = new ProfileImageProvider(profileGenerator);
    engine.addImageProvider("profile", profileImageProvider);
    g_profileImageProvider = profileImageProvider;

    // Frame caches for the video-preview compositor. Tighter bound on the
    // profile cache since profile frames are larger (1920×400 by default).
    FrameCache* overlayFrameCache = new FrameCache(overlayGenerator, 0.5, 64);
    FrameCache* profileFrameCache = new FrameCache(profileGenerator, 0.5, 32);
    imageProvider->setFrameCache(overlayFrameCache);
    profileImageProvider->setFrameCache(profileFrameCache);

    // Wire every generator and Config signal that could affect rendered output
    // to cache invalidation. Conservative — invalidating on the wrong signal
    // costs a single re-render; missing one would show stale frames.
    auto invalidateOverlay = [overlayFrameCache]() { overlayFrameCache->invalidate(); };
    auto invalidateProfile = [profileFrameCache]() { profileFrameCache->invalidate(); };

    QObject::connect(overlayGenerator, &OverlayGenerator::fontChanged,              invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::textColorChanged,         invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::templateChanged,          invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showDepthChanged,         invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showTemperatureChanged,   invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showNDLChanged,           invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showPressureChanged,      invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showTimeChanged,          invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::backgroundOpacityChanged, invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showLabelChanged,         invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showPO2Cell1Changed,      invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showPO2Cell2Changed,      invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showPO2Cell3Changed,      invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showCompositePO2Changed,  invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::cellsChanged,             invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::cellLayoutChanged,        invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showCellBackgroundsChanged, invalidateOverlay);
    QObject::connect(Config::instance(), &Config::unitSystemChanged,                invalidateOverlay);

    // Undo/redo: record a snapshot when template *content* changes. This tracks
    // only the signals that map to what exportTemplate() serializes — not the
    // full invalidate set above, which also fires on editor-only / display state
    // (showCellBackgrounds, unit system, show* toggles whose real cell mutation
    // already arrives via cellsChanged/cellLayoutChanged) and would otherwise
    // produce no-op undo entries.
    undoManager->trackSignal(SIGNAL(cellsChanged()));
    undoManager->trackSignal(SIGNAL(cellLayoutChanged()));
    undoManager->trackSignal(SIGNAL(fontChanged()));
    undoManager->trackSignal(SIGNAL(textColorChanged()));
    undoManager->trackSignal(SIGNAL(backgroundOpacityChanged()));
    undoManager->trackSignal(SIGNAL(templateChanged()));
    undoManager->trackSignal(SIGNAL(showLabelChanged()));

    // History boundaries: a template file load (combobox / Load button) and a
    // dive import both reset undo history so it never crosses them. templateLoaded
    // fires from loadTemplateFromFile but NOT from loadTemplate(), so an undo
    // restore never triggers this.
    QObject::connect(overlayGenerator, &OverlayGenerator::templateLoaded,
                     undoManager, &TemplateUndoStack::resetHistory);
    // Queued: a dive change triggers synchronous QML cell-layout regeneration
    // (adjustTankCellVisibility, etc.) that emits cellsChanged. Running the reset
    // at the end of the event-loop turn lets it cancel the coalesce timer that
    // regeneration started, so the import itself produces no undo entry.
    QObject::connect(&mainWindow, &MainWindow::currentDiveChanged,
                     undoManager, &TemplateUndoStack::resetHistory,
                     Qt::QueuedConnection);

    QObject::connect(profileGenerator, &ProfileGenerator::backgroundColorChanged,    invalidateProfile);
    QObject::connect(profileGenerator, &ProfileGenerator::backgroundOpacityChanged,  invalidateProfile);
    QObject::connect(profileGenerator, &ProfileGenerator::curveColorChanged,         invalidateProfile);
    QObject::connect(profileGenerator, &ProfileGenerator::curveWidthChanged,         invalidateProfile);
    QObject::connect(profileGenerator, &ProfileGenerator::indicatorColorChanged,     invalidateProfile);
    QObject::connect(profileGenerator, &ProfileGenerator::indicatorModeChanged,      invalidateProfile);
    QObject::connect(profileGenerator, &ProfileGenerator::indicatorRadiusChanged,    invalidateProfile);
    QObject::connect(profileGenerator, &ProfileGenerator::pulsePeriodMsChanged,      invalidateProfile);
    QObject::connect(profileGenerator, &ProfileGenerator::outputWidthChanged,        invalidateProfile);
    QObject::connect(profileGenerator, &ProfileGenerator::outputHeightChanged,       invalidateProfile);
    QObject::connect(profileGenerator, &ProfileGenerator::decoZoneColorChanged,      invalidateProfile);
    QObject::connect(profileGenerator, &ProfileGenerator::decoZoneOpacityChanged,    invalidateProfile);
    QObject::connect(profileGenerator, &ProfileGenerator::gridEnabledChanged,        invalidateProfile);
    QObject::connect(profileGenerator, &ProfileGenerator::gridDepthIntervalChanged,  invalidateProfile);
    QObject::connect(profileGenerator, &ProfileGenerator::gridTimeIntervalChanged,   invalidateProfile);
    QObject::connect(profileGenerator, &ProfileGenerator::gridColorChanged,          invalidateProfile);
    QObject::connect(profileGenerator, &ProfileGenerator::gridOpacityChanged,        invalidateProfile);
    QObject::connect(profileGenerator, &ProfileGenerator::gridLineWidthChanged,      invalidateProfile);
    QObject::connect(profileGenerator, &ProfileGenerator::gridShowLabelsChanged,     invalidateProfile);

    // Connect the LogParser signals to MainWindow slots
    QObject::connect(&logParser, &LogParser::diveImported,
                     &mainWindow, &MainWindow::onDiveImported);
    QObject::connect(&logParser, &LogParser::multipleImported,
                     &mainWindow, &MainWindow::onMultipleDivesImported);

    // Connect signals to update the image providers when the active dive changes
    QObject::connect(&mainWindow, &MainWindow::currentDiveChanged,
                    [imageProvider, profileImageProvider, overlayFrameCache,
                     profileFrameCache, &mainWindow]() {
                        DiveData* dive = mainWindow.currentDive();
                        imageProvider->setCurrentDive(dive);
                        profileImageProvider->setCurrentDive(dive);
                        // Drop cached frames keyed on the previous dive pointer;
                        // the pointer might be reused for a fresh DiveData and
                        // would otherwise return stale images.
                        overlayFrameCache->invalidate();
                        profileFrameCache->invalidate();
                    });
    
    qDebug() << "Connected LogParser::diveImported to MainWindow::onDiveImported";
    
    // Expose C++ objects to QML
    engine.rootContext()->setContextProperty("mainWindow", &mainWindow);
    engine.rootContext()->setContextProperty("logParser", &logParser);
    engine.rootContext()->setContextProperty("overlayGenerator", overlayGenerator);
    engine.rootContext()->setContextProperty("undoManager", undoManager);
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