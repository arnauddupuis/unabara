#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QIcon>
#include <QFontDatabase>
#include <QDebug>

// Include the generated version header
#include "version.h"

// Include all the necessary headers
#include "include/ui/main_window.h"
#include "include/ui/color_grade_controller.h"
#include "include/core/dive_data.h"
#include "include/core/log_parser.h"
#include "include/ui/timeline.h"
#include "include/ui/cell_model.h"
#include "include/generators/overlay_gen.h"
#include "include/generators/template_undo_stack.h"
#include "include/export/image_export.h"
#include "include/export/video_export.h"
#include "include/generators/overlay_image_provider.h"
#include "include/generators/lut_strip_image_provider.h"
#include "include/generators/profile_gen.h"
#include "include/generators/profile_image_provider.h"
#include "include/generators/frame_cache.h"
#include "include/core/config.h"
#include "include/core/units.h"
#include "include/core/update_checker.h"
#include "include/core/whatsnew.h"
#include "include/core/clip_list_model.h"
#include "include/core/video_clip.h"
#include "include/core/ffmpeg_info.h"
#include "include/generators/clip_thumb_image_provider.h"

// Global image provider
OverlayImageProvider* g_imageProvider = nullptr;

// Config-backed persistence for the clip model (video/clipStates +
// video/diveSessions + video/overlayLayouts). Lives here so ClipListModel
// stays Config-free and unit-testable with an in-memory store; Config's setters write through
// (targeted key + sync, same discipline as setVideoOverlayLayout), and the
// exit-hook saveConfig() re-serializes the same in-memory caches.
class ConfigClipSessionStore : public ClipSessionStore
{
public:
    QVariantMap loadClipState(const QString& path) const override
    {
        return Config::instance()->clipState(path);
    }
    void saveClipState(const QString& path, const QVariantMap& state) override
    {
        Config::instance()->setClipState(path, state);
    }
    QVariantMap loadSession(const QString& diveKey) const override
    {
        return Config::instance()->diveSession(diveKey);
    }
    void saveSession(const QString& diveKey, const QVariantMap& session) override
    {
        Config::instance()->setDiveSession(diveKey, session);
    }
    // Follow-up F: path + dive x resolution bucket keys in the one
    // video/overlayLayouts blob; Config round-trips, the model interprets.
    QVariantMap loadOverlayLayout(const QString& key) const override
    {
        return Config::instance()->storedVideoOverlayLayout(key);
    }
    QVariantMap loadLastOverlayLayout() const override
    {
        return Config::instance()->lastVideoOverlayLayout();
    }
    void saveOverlayLayout(const QStringList& keys, const QVariantMap& layout,
                           bool updateLastUsed) override
    {
        Config::instance()->setVideoOverlayLayoutEntries(keys, layout, updateLastUsed);
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    // Set application info
    app.setApplicationName("Unabara");
    app.setApplicationVersion(UNABARA_VERSION_STR);
    app.setOrganizationName("UnabaraProject");
    app.setWindowIcon(QIcon(":/images/unabara-icon.png"));

    // Bundled fonts. Must be registered before the OverlayGenerator is
    // constructed — it loads the active template (which may reference these
    // families) in its constructor. Orbitron/Share Tech Mono (OFL) serve the
    // HUD/Social template family; DejaVu Sans is the default overlay font —
    // "Sans Serif" is a fontconfig alias that only exists on Linux, so the
    // default must be a real bundled family to render identically everywhere.
    for (const char* fontPath : {":/fonts/Orbitron.ttf", ":/fonts/ShareTechMono-Regular.ttf",
                                 ":/fonts/DejaVuSans.ttf", ":/fonts/DejaVuSans-Bold.ttf",
                                 ":/fonts/DejaVuSans-Oblique.ttf", ":/fonts/DejaVuSans-BoldOblique.ttf"}) {
        if (QFontDatabase::addApplicationFont(QLatin1String(fontPath)) == -1)
            qWarning() << "Failed to register bundled font" << fontPath;
    }

    // Legacy "Sans Serif" families are rewritten to the bundled default when
    // templates/settings load (CellData::normalizedFontFamily) — on Linux
    // fontconfig resolves the alias before this substitution would run, so
    // load-time rewriting is what makes rendering distro-independent. The
    // substitution stays as a safety net for paths that bypass the loaders.
    QFont::insertSubstitution(QStringLiteral("Sans Serif"), QStringLiteral("DejaVu Sans"));

    qInfo() << "Starting Unabara version" << UNABARA_VERSION_STR;
    
    // Register C++ types with QML
    qmlRegisterType<Timeline>("Unabara.UI", 1, 0, "Timeline");
    qmlRegisterType<CellModel>("Unabara.UI", 1, 0, "CellModel");
    qmlRegisterType<OverlayGenerator>("Unabara.Generators", 1, 0, "OverlayGenerator");
    qmlRegisterType<ProfileGenerator>("Unabara.Generators", 1, 0, "ProfileGenerator");
    qmlRegisterType<ImageExporter>("Unabara.Export", 1, 0, "ImageExporter");
    qmlRegisterType<VideoExporter>("Unabara.Export", 1, 0, "VideoExporter");
    qmlRegisterUncreatableMetaObject(Units::staticMetaObject, "Unabara.Core", 1, 0, "Units", "Units is a utility class");
    // Uncreatable: instances come from ClipListModel; registration makes the
    // SyncSource/ProbeState enums visible to QML (PR A2+ UI work).
    qmlRegisterUncreatableType<VideoClip>("Unabara.Core", 1, 0, "VideoClip",
                                          "VideoClip instances are created by ClipListModel");
    
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

    // FFmpeg status bridge for the Settings tab and the export dialog's
    // codec list. Its constructor pushes Config::ffmpegPath into FFmpegProbe
    // (and re-pushes on change) and starts the async capability snapshot —
    // construct it before anything that could resolve ffmpeg/ffprobe paths,
    // so the persisted override is honored from the first lookup.
    FfmpegInfo ffmpegInfo;

    // Clip model for the video tab. The Timeline facade (instantiated in
    // TimelineView.qml with clipModel: clipList) and VideoSyncPlayer follow
    // its active clip. Follows the active dive so timecode auto-sync can
    // resolve offsets against the dive start.
    // Declared before clipList so it outlives the model: the model's
    // destructor flushes debounce-pending clip states into the store.
    ConfigClipSessionStore clipSessionStore;
    ClipListModel clipList;
    // Installed before the dive connection below so the very first import
    // already restores that dive's stored session.
    clipList.setSessionStore(&clipSessionStore);
    QObject::connect(&mainWindow, &MainWindow::currentDiveChanged,
                     &clipList, [&clipList, &mainWindow]() {
                         clipList.setDive(mainWindow.currentDive());
                     });

    // Colour-grade controller (track C): follows the active clip and owns
    // the deserialized grade. countChanged is tracked as well as
    // activeIndexChanged because removing/restoring clips can swap the
    // active clip OBJECT without moving the index (setClip no-ops on the
    // same pointer, so the double subscription is cheap).
    ColorGradeController colorGradeController;
    auto syncGradeClip = [&colorGradeController, &clipList]() {
        colorGradeController.setClip(clipList.activeClip());
    };
    QObject::connect(&clipList, &ClipListModel::activeIndexChanged,
                     &colorGradeController, syncGradeClip);
    QObject::connect(&clipList, &ClipListModel::countChanged,
                     &colorGradeController, syncGradeClip);
    syncGradeClip();
    // C3 depth link: the controller resolves the grade at the dive's
    // depth, so it follows the active dive (same pattern as clipList).
    QObject::connect(&mainWindow, &MainWindow::currentDiveChanged,
                     &colorGradeController, [&colorGradeController, &mainWindow]() {
                         colorGradeController.setDive(mainWindow.currentDive());
                     });
    colorGradeController.setDive(mainWindow.currentDive());
    // Anchor/readout depth strings are formatted in C++ with the
    // configured unit system; refresh them when it flips. Connected here
    // (not in the controller's constructor) so the controller only touches
    // Config inside its text getters.
    QObject::connect(Config::instance(), &Config::unitSystemChanged,
                     &colorGradeController, &ColorGradeController::refreshDepthTexts);


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

    // Hald-strip LUT provider for the colour-grade shader (track C).
    // Serves the identity strip until the controller bakes an imported
    // .cube into it (C4); the engine takes ownership. The sink seam keeps
    // the controller (and the testlib) Quick-free; a null image resets the
    // provider to identity, and installing the sink immediately pushes the
    // current strip state, so this wiring is order-independent against the
    // first clip load above.
    LutStripImageProvider* lutStripProvider = new LutStripImageProvider();
    engine.addImageProvider("lutstrip", lutStripProvider);
    colorGradeController.setLutStripSink([lutStripProvider](const QImage& strip) {
        lutStripProvider->setStrip(strip);
    });

    // First-frame thumbnails for the ClipStrip cards (follow-up C): async
    // provider on its own small thread pool, so extractions never block the
    // GUI thread, the image-loader thread or the clip probers. The engine
    // takes ownership (its destructor kills/joins in-flight extractions).
    engine.addImageProvider("clipthumb", new ClipThumbImageProvider());

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
    QObject::connect(overlayGenerator, &OverlayGenerator::labelColorChanged,        invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::valueColorChanged,        invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::templateChanged,          invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showDepthChanged,         invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showTemperatureChanged,   invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showNDLChanged,           invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showPressureChanged,      invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showTimeChanged,          invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::backgroundOpacityChanged, invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showLabelChanged,         invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::shadowChanged,            invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showPO2Cell1Changed,      invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showPO2Cell2Changed,      invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showPO2Cell3Changed,      invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showCompositePO2Changed,  invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::showCircuitModeChanged,    invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::cellsChanged,             invalidateOverlay);
    QObject::connect(overlayGenerator, &OverlayGenerator::cellLayoutChanged,        invalidateOverlay);
    QObject::connect(Config::instance(), &Config::unitSystemChanged,                invalidateOverlay);

    // Undo/redo: record a snapshot when template *content* changes. This tracks
    // only the signals that map to what exportTemplate() serializes — not the
    // full invalidate set above, which also fires on editor-only / display state
    // (unit system, show* toggles whose real cell mutation
    // already arrives via cellsChanged/cellLayoutChanged) and would otherwise
    // produce no-op undo entries.
    undoManager->trackSignal(SIGNAL(cellsChanged()));
    undoManager->trackSignal(SIGNAL(cellLayoutChanged()));
    undoManager->trackSignal(SIGNAL(fontChanged()));
    undoManager->trackSignal(SIGNAL(labelColorChanged()));
    undoManager->trackSignal(SIGNAL(valueColorChanged()));
    undoManager->trackSignal(SIGNAL(backgroundOpacityChanged()));
    undoManager->trackSignal(SIGNAL(templateChanged()));
    undoManager->trackSignal(SIGNAL(showLabelChanged()));
    undoManager->trackSignal(SIGNAL(shadowChanged()));
    undoManager->trackSignal(SIGNAL(colorSchemeChanged()));

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
    engine.rootContext()->setContextProperty("clipList", &clipList);
    engine.rootContext()->setContextProperty("colorGrade", &colorGradeController);
    engine.rootContext()->setContextProperty("ffmpegInfo", &ffmpegInfo);

    // Expose version to QML
    engine.rootContext()->setContextProperty("appVersion", UNABARA_VERSION_STR);

    // Create update checker and expose to QML
    UpdateChecker updateChecker;
    engine.rootContext()->setContextProperty("updateChecker", &updateChecker);

    // Release notes for the "What's New" dialog
    WhatsNew whatsNew(QStringLiteral(UNABARA_VERSION_STR));
    whatsNew.loadFromFile(QStringLiteral(":/whatsnew.json"));
    engine.rootContext()->setContextProperty("whatsNew", &whatsNew);
    
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