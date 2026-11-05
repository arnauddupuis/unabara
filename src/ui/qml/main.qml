import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Unabara.UI 1.0
import Unabara.Generators 1.0
import Unabara.Export 1.0
import Unabara.Core 1.0

ApplicationWindow {
    id: window
    width: 1280
    height: 1024
    visible: true
    title: qsTr("Unabara - Dive Telemetry Overlay")
    
    // Release notes that were unseen when this session started. Non-empty
    // only on the first launch after an update; keeps the toolbar's
    // "What's New" recall button alive for the whole session even though
    // closing the dialog stamps the notes as seen.
    property var whatsNewStartupNotes: []

    // File-type routing shared by the video import dialog and the
    // window-level drop target. Generous on video extensions: anything the
    // demuxer might handle is worth trying — a probe/playback failure
    // surfaces gracefully on the clip card.
    readonly property var videoExtensions: ["mp4", "mov", "avi", "mkv", "m4v", "webm", "mts", "m2ts"]
    readonly property var diveLogExtensions: ["ssrf", "xml", "uddf", "fit"]
    readonly property string videoNameFilterGlobs: videoExtensions.map(function(e) { return "*." + e }).join(" ")

    // Shared by the import dialog and drag-and-drop: adds every URL as a
    // clip and toasts the net result. Duplicates are re-activated by the
    // model and toast separately through onDuplicateRequested, so they are
    // simply absent from the `added` count here.
    function importVideoUrls(urls) {
        var countBefore = clipList.count
        clipList.addClips(urls)
        var added = clipList.count - countBefore
        if (added > 0) {
            // Notify without interrupting: the user's next step is on the
            // timeline or the Video tab, not in a dialog.
            toast.show(qsTr("%n video(s) imported — drag the orange band on the timeline, or use the Video tab, to sync with the dive.", "", added),
                       { duration: 8000 })
        }
        return added
    }

    // Template-editor undo/redo.
    // StandardKey resolves the platform-native binding.
    // Disabled while an export runs: Shortcut is not blocked by popup
    // modality, and the image export loop pumps events, so an undo mid-export
    // would change the layout partway through the frame sequence.
    Shortcut {
        sequences: [StandardKey.Undo]
        enabled: !imageExporter.busy && !videoExporter.busy
        onActivated: undoManager.undo()
    }
    Shortcut {
        sequences: [StandardKey.Redo]
        enabled: !imageExporter.busy && !videoExporter.busy
        onActivated: undoManager.redo()
    }

    // Models and objects
    ImageExporter {
        id: imageExporter
        // Where per-dive export subfolders are created. The exporter itself
        // never reads the settings store — this binding is its only source.
        baseDirectory: config ? config.lastExportPath : ""

        onExportStarted: {
            // The generators are not thread-safe: playback keeps the image
            // provider rendering on its worker thread while the export
            // renders on the GUI thread. Export is reachable from the
            // Video tab, so stop the player first.
            videoSyncPlayer.pause()
            exportProgressDialog.open()
        }
        
        onProgressChanged: {
            exportProgressDialog.value = progress
        }
        
        onExportFinished: function(success, path) {
            exportProgressDialog.close()
            if (success) {
                toast.show(qsTr("Image sequence exported to %1").arg(path.split("/").pop()), {
                    actionText: qsTr("Open folder"),
                    onAction: function() { mainWindow.revealInFileManager(path) }
                })
            }
        }
        
        onExportError: function(errorMessage) {
            exportProgressDialog.close()
            messageDialog.title = qsTr("Export Error")
            messageDialog.message = errorMessage
            messageDialog.open()
        }

        onExportCancelled: {
            exportProgressDialog.close()
            toast.show(qsTr("Export cancelled — partial frames removed."))
        }
    }
    
    // Video exporter
    VideoExporter {
        id: videoExporter
        // Same contract as ImageExporter.baseDirectory
        baseDirectory: config ? config.lastExportPath : ""

        onExportStarted: {
            videoSyncPlayer.pause()  // see ImageExporter.onExportStarted
            videoExportProgressDialog.open()
        }
        
        onProgressChanged: {
            videoExportProgressDialog.value = progress
        }
        
        onStatusUpdate: function(message) {
            videoExportProgressDialog.statusText = message
        }

        // Non-fatal setup advisories (PR C5: colour-grade plan notes, e.g.
        // a missing user LUT — the export proceeds with the parametric
        // grade alone). Long duration: these fire right as the progress
        // dialog opens over them.
        onExportWarning: function(message) {
            toast.show(message, { duration: 8000 })
        }

        onExportFinished: function(success, path) {
            // Batch mode (PR A6): keep the progress dialog up between clips
            // and chain the next one. exportFinished is emitted after the
            // streaming loop fully unwound and busy flipped back to false,
            // so starting the next clip from here can never hit the
            // "Already exporting" guard; callLater keeps the chain out of
            // the signal emission anyway.
            if (clipExportQueue.active) {
                if (success)
                    clipExportQueue.exported++
                Qt.callLater(function() { clipExportQueue.startNext() })
                return
            }
            videoExportProgressDialog.close()
            if (success) {
                toast.show(qsTr("Video exported to %1").arg(path.split("/").pop()), {
                    actionText: qsTr("Open folder"),
                    onAction: function() { mainWindow.revealInFileManager(path) }
                })
            }
        }

        onExportError: function(errorMessage) {
            if (clipExportQueue.active) {
                // An error aborts the remaining queue (plan): report the
                // clip error together with how far the batch got.
                var exported = clipExportQueue.exported
                var total = clipExportQueue.paths.length
                clipExportQueue.reset()
                videoExportProgressDialog.close()
                messageDialog.title = qsTr("Export Error")
                messageDialog.message = qsTr("%1\n\nClip export stopped — %2 of %3 clips exported.")
                        .arg(errorMessage).arg(exported).arg(total)
                messageDialog.open()
                return
            }
            videoExportProgressDialog.close()
            messageDialog.title = qsTr("Export Error")
            messageDialog.message = errorMessage
            messageDialog.open()
        }

        onExportCancelled: {
            if (clipExportQueue.active) {
                // The C++ cancel path already removed the partial file of
                // the clip that was running; just stop the queue quietly.
                var exported = clipExportQueue.exported
                var total = clipExportQueue.paths.length
                clipExportQueue.reset()
                videoExportProgressDialog.close()
                toast.show(qsTr("Export cancelled after %1 of %2 clips — partial file removed.")
                               .arg(exported).arg(total))
                return
            }
            videoExportProgressDialog.close()
            toast.show(qsTr("Export cancelled."))
        }
    }

    // "Export every enabled clip separately" (PR A6): a QML-side queue over
    // clipsInDiveOrder(true), chaining one exportComposite() per clip on
    // exportFinished. Clips are held by PATH, never by VideoClip reference —
    // a clip can be removed between accept and its turn, so each step
    // re-resolves through clipList.indexOfPath() and skips (counting) any
    // clip that vanished, was disabled, or lost usable metadata meanwhile.
    QtObject {
        id: clipExportQueue

        property bool active: false
        property var paths: []          // enabled+probed clip paths, dive order
        property int index: 0           // next queue slot to start
        property int position: 0        // 1-based slot of the running clip (UI)
        property int exported: 0        // clips that finished successfully
        property int skipped: 0         // vanished/disabled/unprobed at their turn
        property bool overlaysOnly: false
        property string currentName: ""

        // Called from the export dialog's accept handler with the codec /
        // quality already applied to videoExporter (same ordering contract
        // as the single-clip path). Unprobed clips are dropped up front and
        // reported; they cannot export (exportComposite would error out and
        // abort the whole batch on them).
        function start(overlaysOnlyMode) {
            var clips = clipList.clipsInDiveOrder(true)
            var queued = []
            var notReady = 0
            for (var i = 0; i < clips.length; i++) {
                var c = clips[i]
                if (c.width > 0 && c.height > 0 && c.fpsNum > 0
                        && c.fpsDen > 0 && c.durationSeconds > 0)
                    queued.push(c.path)
                else
                    notReady++
            }
            if (queued.length === 0) {
                messageDialog.title = qsTr("Export Error")
                messageDialog.message = qsTr("None of the enabled clips has usable metadata yet — wait for probing to finish.")
                messageDialog.open()
                return
            }
            if (notReady > 0)
                toast.show(qsTr("%n clip(s) skipped — no usable metadata yet.", "", notReady))
            paths = queued
            index = 0
            position = 0
            exported = 0
            skipped = 0
            overlaysOnly = overlaysOnlyMode
            currentName = ""
            active = true
            startNext()
        }

        function startNext() {
            if (!active)
                return
            // (overlaysOnly skip helper) Absent keys default to visible —
            // only an explicit false on BOTH overlays means nothing would
            // be exported for this clip.
            function clipHasVisibleOverlay(clip) {
                var layout = clip.overlayLayoutMap
                if (!layout)
                    return true
                var dc = layout["diveComputer"]
                var prof = layout["diveProfile"]
                var dcVisible = !dc || dc["visible"] !== false
                var profVisible = !prof || prof["visible"] !== false
                return dcVisible || profVisible
            }
            while (index < paths.length) {
                var modelIndex = clipList.indexOfPath(paths[index])
                var clip = modelIndex >= 0 ? clipList.clipAt(modelIndex) : null
                if (!clip || !clip.enabled || clip.width <= 0 || clip.height <= 0
                        || clip.fpsNum <= 0 || clip.fpsDen <= 0
                        || clip.durationSeconds <= 0) {
                    // Removed, disabled or de-probed since the dialog was
                    // accepted: skip it, keep going.
                    skipped++
                    index++
                    continue
                }
                if (overlaysOnly && !clipHasVisibleOverlay(clip)) {
                    // Both overlays hidden in this clip's layout: a
                    // placed-overlays export of it would error out
                    // ("nothing to export") and abort the whole batch.
                    // Same skip class as disabled/unprobed.
                    skipped++
                    index++
                    continue
                }
                position = index + 1
                currentName = clip.displayName
                index++
                // Name THIS clip's file explicitly (same formula the
                // exporter's own fallback uses — dive + clip stem + content
                // tag). Doing it per clip also overwrites any pending path
                // a previously failed single export left armed, which
                // would otherwise hijack the first clip's file name.
                videoExporter.createDefaultExportFile(
                            mainWindow.currentDive, clip.path,
                            overlaysOnly ? "overlays_placed" : "composited")
                // Whole-clip range per clip (plan): the clip's own
                // syncOffset and overlay layout travel with it in C++.
                if (!videoExporter.exportComposite(mainWindow.currentDive, clip,
                                                   overlayGenerator, profileGenerator,
                                                   0, clip.durationSeconds,
                                                   overlaysOnly)) {
                    // exportComposite emitted exportError synchronously and
                    // the handler above has already shut the queue down.
                    return
                }
                return
            }
            finishQueue()
        }

        function finishQueue() {
            var exportedCount = exported
            var skippedCount = skipped
            var total = paths.length
            var dir = config ? config.lastExportPath : ""
            reset()
            videoExportProgressDialog.close()
            if (skippedCount > 0) {
                toast.show(qsTr("Exported %1 of %2 clips to %3 — %4 skipped.")
                               .arg(exportedCount).arg(total)
                               .arg(dir.split("/").pop()).arg(skippedCount), {
                               actionText: qsTr("Open folder"),
                               onAction: function() { mainWindow.revealInFileManager(dir) }
                           })
            } else {
                toast.show(qsTr("Exported %n clip(s) to %1.", "", exportedCount)
                               .arg(dir.split("/").pop()), {
                               actionText: qsTr("Open folder"),
                               onAction: function() { mainWindow.revealInFileManager(dir) }
                           })
            }
        }

        function reset() {
            active = false
            paths = []
            index = 0
            position = 0
            exported = 0
            skipped = 0
            currentName = ""
        }
    }

    // Encode-phase progress arrives through videoExporter's progressChanged
    // (parsed from FFmpeg's `-progress pipe:1` stream in C++) — no polling
    // timer and no forced-refresh hacks needed on top of the handler above.

    // Video metadata (duration, creation time, embedded timecode) comes from
    // the clip model's async ffprobe pass; the timecode auto-sync runs in
    // ClipListModel after the probe. When ffprobe is unavailable,
    // VideoSyncPlayer feeds the real player's duration back into the clip.
    Connections {
        target: clipList
        function onDuplicateRequested(path) {
            toast.show(qsTr("Video already imported — switched to the existing clip."))
        }
        // One toast per session restore (dive switch / re-import), however
        // many stored clip files have gone missing since last time.
        function onClipsMissingOnRestore(count) {
            toast.show(qsTr("%n video(s) from this dive's last session could not be found.", "", count),
                       { duration: 8000 })
        }
        // Follow-up K: a sync adjustment (any path — drag, SpinBox, nudge,
        // timecode, camera profile) or a session restore left clips outside
        // the dive. Wording stays path-neutral: after a restore the user
        // adjusted nothing.
        // The model emits once per settled burst and only on an
        // inside→outside transition, so a drag or a bulk apply is one toast.
        function onClipsOutsideDive(count, stackedCount, firstName) {
            var message
            if (count === 1) {
                message = stackedCount === 1
                        ? qsTr("\"%1\" is now entirely outside the dive. It is shown hatched at the start of the timeline; its sync offset is kept — drag the band or re-sync to place it.").arg(firstName)
                        : qsTr("\"%1\" is now partly outside the dive.").arg(firstName)
            } else {
                message = qsTr("%n clip(s) now sit outside the dive.", "", count)
                if (stackedCount > 0)
                    message += " " + qsTr("Entirely outside, shown hatched at the start of the timeline (sync offsets kept): %n.", "", stackedCount)
            }
            toast.show(message, { duration: 8000 })
        }
        // A dive switch swaps the whole clip list (A3), so a running batch
        // export loses its subject wholesale — abort the remaining queue.
        // diveChanged is the one authoritative boundary for this; per-clip
        // removals are already handled by the path re-resolution in
        // startNext() and must NOT abort the batch, which is why this does
        // not hang off the model's row signals. A clip export that is
        // mid-flight keeps the old DiveData pointer (MainWindow keeps all
        // imported dives alive) and finishes as a normal single export.
        function onDiveChanged() {
            if (clipExportQueue.active) {
                clipExportQueue.reset()
                // In the inter-clip gap nothing is running, so no terminal
                // export signal will ever close the (NoAutoClose) progress
                // dialog — close it here. A mid-flight clip keeps it open
                // and its own terminal signal closes it as usual.
                if (!videoExporter.busy)
                    videoExportProgressDialog.close()
                toast.show(qsTr("Dive changed — remaining clip exports were cancelled."))
            }
        }
    }

    // Connection to handle multiple dives
    Connections {
        target: mainWindow
        function onMultipleDivesFound(dives) {
            console.log("Multiple dives found, showing selection dialog")
            diveSelectionDialog.show(dives)
        }
    }
    
    // Update info arriving while the What's New notes are open is held back
    // so the two startup modals never stack; the notes' onClosed releases it.
    property var pendingUpdateInfo: null

    Connections {
        target: updateChecker
        function onUpdateAvailable(latestVersion, releaseUrl) {
            if (whatsNewDialog.visible) {
                window.pendingUpdateInfo = { version: latestVersion, url: releaseUrl }
                return
            }
            updateDialog.latestVersion = latestVersion
            updateDialog.releaseUrl = releaseUrl
            updateDialog.open()
        }
    }

    // Offer to theme the dive profile with the template's color scheme.
    // templateLoaded only fires on explicit file loads (never on undo
    // restores; the startup load happens before QML exists).
    Connections {
        target: overlayGenerator
        function onTemplateLoaded(filePath) {
            if (!overlayGenerator.hasColorScheme)
                return
            if (profileGenerator.colorSchemeApplied(overlayGenerator.primaryColor,
                                                    overlayGenerator.secondaryColor))
                return
            var policy = config.profileColorSchemePolicy
            if (policy === "never")
                return
            if (policy === "always") {
                profileGenerator.applyColorScheme(overlayGenerator.primaryColor,
                                                  overlayGenerator.secondaryColor)
                return
            }
            var name = filePath.split("/").pop().replace(/\.utp$/, "").replace(/_/g, " ")
            templateColorsDialog.templateName = name
            templateColorsDialog.open()
        }
    }

    Component.onCompleted: {
        // Check if FFmpeg is available and show a notification if not
        if (!videoExporter.isFFmpegAvailable()) {
            toast.show(qsTr("FFmpeg was not found — video export is disabled. Install FFmpeg and restart Unabara to enable it."),
                       { duration: 12000 })
        }

        // Check for updates (user-controllable from the Settings tab)
        if (config.checkUpdatesOnStartup) {
            updateChecker.checkForUpdates()
        }

        // "What's New": a brand-new install gets silently stamped (nothing
        // to diff against — the whole app is new to them); an existing
        // config sees the notes for versions newer than the last dismissal.
        if (config.firstRun) {
            config.whatsNewSeenVersion = appVersion
        } else {
            var pendingNotes = whatsNew.pendingReleases(config.whatsNewSeenVersion)
            if (pendingNotes.length > 0) {
                window.whatsNewStartupNotes = pendingNotes
                whatsNewDialog.releases = pendingNotes
                whatsNewDialog.open()
            }
        }
    }
    
    // Shared cell model: drives both the canvas editor (tab 0) and the
    // overlay editor sidebar. Owned here so one refresh serves both.
    CellModel {
        id: overlayCellModel
    }

    // Main UI layout
    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            ToolButton {
                id: importButton
                text: qsTr("Import")
                icon.name: "document-open"
                onClicked: importMenu.open()
                
                Menu {
                    id: importMenu
                    MenuItem {
                        text: qsTr("Import Dive Log")
                        onTriggered: importDiveLogFileDialog.open()
                    }
                    MenuItem {
                        text: qsTr("Import Video")
                        enabled: mainWindow.hasActiveDive
                        onTriggered: importVideoFileDialog.open()
                    }
                }
            }
            
            ToolButton {
                // Exports whichever overlay the currently active tab represents.
                // Tab 0 = dive computer overlay, Tab 1 = dive profile. Tab 2
                // (video) opens the dialog with a content chooser and
                // defaults the range to the imported video. Only the Settings
                // tab has nothing to export.
                text: {
                    if (contentTabs.currentIndex === 1) return qsTr("Export Profile")
                    if (contentTabs.currentIndex === 2) return qsTr("Export...")
                    return qsTr("Export Overlay")
                }
                icon.name: "document-save"
                enabled: mainWindow.hasActiveDive && contentTabs.currentIndex !== 3
                ToolTip.visible: hovered && contentTabs.currentIndex === 2
                ToolTip.delay: 500
                ToolTip.text: qsTr("Overlays export with transparency — composite them over your footage in your video editor.")
                onClicked: {
                    exportImagesDialog.chooseContent = contentTabs.currentIndex === 2
                    if (contentTabs.currentIndex === 2) {
                        // Composited export needs a probed active clip
                        // (geometry + fps drive the whole pipeline).
                        exportImagesDialog.compositeCapable =
                                clipList.activeClip !== null
                                && clipList.activeClip.width > 0
                                && clipList.activeClip.durationSeconds > 0
                        // Set the generator parameters explicitly too — a
                        // radio's change handler doesn't fire when it is
                        // already checked.
                        exportImagesDialog.targetGenerator = overlayGenerator
                        exportImagesDialog.contentType = "dive_computer"
                        exportImagesDialog.title = qsTr("Export for Video")
                        // Batch export (PR A6): snapshot how many enabled
                        // clips exist at open time (the dialog is modal, so
                        // the list can't change underneath it) and start
                        // every session unchecked — stale batch state must
                        // never silently fan a single export out to every
                        // clip.
                        exportImagesDialog.enabledClipCount =
                                clipList.clipsInDiveOrder(true).length
                        exportEveryClipCheckbox.checked = false
                        if (exportImagesDialog.compositeCapable) {
                            exportModeComposited.checked = true
                            compositeRangeWhole.checked = true
                        } else {
                            exportContentComputer.checked = true
                            if (exportVideoRangeOnly.enabled)
                                exportVideoRangeOnly.checked = true
                        }
                    } else if (contentTabs.currentIndex === 1) {
                        exportImagesDialog.targetGenerator = profileGenerator
                        exportImagesDialog.contentType = "dive_profile"
                        exportImagesDialog.title = qsTr("Export Dive Profile")
                        // Don't inherit a "video range" left over from a
                        // Video-tab export — each entry point picks its
                        // natural default range.
                        exportFullDive.checked = true
                    } else {
                        exportImagesDialog.targetGenerator = overlayGenerator
                        exportImagesDialog.contentType = "dive_computer"
                        exportImagesDialog.title = qsTr("Export Dive Computer Overlay")
                        exportFullDive.checked = true
                    }
                    exportImagesDialog.open()
                }
            }
            
            ToolButton {
                text: qsTr("Edit")
                icon.name: "edit-undo"
                onClicked: editMenu.open()

                Menu {
                    id: editMenu
                    MenuItem {
                        text: qsTr("Undo")
                        enabled: undoManager.canUndo
                        onTriggered: undoManager.undo()
                    }
                    MenuItem {
                        text: qsTr("Redo")
                        enabled: undoManager.canRedo
                        onTriggered: undoManager.redo()
                    }
                }
            }

            ToolButton {
                // Session-only recall of the update notes: visible only on
                // the first launch after an update, so a user who left the
                // dialog via "Show me" can get back to it. The permanent
                // entry point is on the Settings tab.
                text: qsTr("What's New")
                icon.name: "help-about"
                visible: window.whatsNewStartupNotes.length > 0
                onClicked: {
                    whatsNewDialog.releases = window.whatsNewStartupNotes
                    whatsNewDialog.open()
                }
            }

            Item { Layout.fillWidth: true }
            
            Label {
                text: mainWindow.hasActiveDive ? mainWindow.currentDive.diveName : qsTr("No dive loaded")
                elide: Text.ElideRight
                horizontalAlignment: Qt.AlignHCenter
                Layout.fillWidth: true
            }
            
            Item { Layout.fillWidth: true }
        }
    }
    
    // Split view with timeline at bottom, main area at top
    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical
        
        // Tab container: overlay (Tab 1), dive profile (Tab 2), video (Tab 3).
        // Timeline below stays global so all tabs share the same temporal state.
        Item {
            id: tabContainer
            SplitView.fillHeight: true
            SplitView.minimumHeight: 300

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                TabBar {
                    id: contentTabs
                    Layout.fillWidth: true
                    TabButton { text: qsTr("Dive Computer Overlay") }
                    TabButton { text: qsTr("Dive Profile") }
                    TabButton { text: qsTr("Video") }
                    TabButton { text: qsTr("Settings") }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: contentTabs.currentIndex

                    // Tab 1: Overlay — unified canvas (Edit/Render modes) + editor sidebar
                    SplitView {
                        id: mainContentArea
                        orientation: Qt.Horizontal

                        OverlayCanvas {
                            id: overlayCanvas
                            SplitView.fillWidth: true
                            SplitView.minimumWidth: 400
                            generator: overlayGenerator
                            dive: mainWindow.currentDive
                            timeline: timelineView.timeline
                            cellModel: overlayCellModel
                            tabActive: contentTabs.currentIndex === 0
                            onImportRequested: importDiveLogFileDialog.open()
                        }

                        // Overlay Editor Panel. Collapsible from its own
                        // header (replaces the old toolbar-wide toggle);
                        // collapsed it shrinks to a slim strip so the expand
                        // chevron stays discoverable.
                        Rectangle {
                            id: overlayEditorPanel
                            SplitView.preferredWidth: 420
                            SplitView.minimumWidth: collapsed ? collapsedWidth : 300
                            SplitView.maximumWidth: collapsed ? collapsedWidth : Infinity
                            color: palette.window
                            border.color: palette.mid
                            border.width: 1

                            property bool collapsed: false
                            readonly property int collapsedWidth: 40
                            property real expandedWidth: 420
                            onCollapsedChanged: {
                                // SplitView writes preferredWidth when the user
                                // drags the handle, so restore it imperatively.
                                if (collapsed) {
                                    expandedWidth = width
                                    overlayEditorPanel.SplitView.preferredWidth = collapsedWidth
                                } else {
                                    overlayEditorPanel.SplitView.preferredWidth = expandedWidth
                                }
                            }

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 1
                                spacing: 0

                                // Clickable header bar — same affordance as the
                                // inspector's collapsible sections (visible bar,
                                // hover highlight, full-row hit area). The beta
                                // panel found the bare chevron button invisible
                                // while the section idiom was understood by all.
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 32
                                    radius: 4
                                    color: overlayEditorHeaderMouse.containsMouse ? palette.midlight : palette.mid

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        spacing: 6

                                        Label {
                                            text: overlayEditorPanel.collapsed ? "«" : "»"
                                            font.bold: true
                                        }

                                        Label {
                                            text: qsTr("Overlay Editor")
                                            font.bold: true
                                            elide: Text.ElideRight
                                            visible: !overlayEditorPanel.collapsed
                                            Layout.fillWidth: true
                                        }
                                    }

                                    MouseArea {
                                        id: overlayEditorHeaderMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: overlayEditorPanel.collapsed = !overlayEditorPanel.collapsed
                                    }

                                    ToolTip.visible: overlayEditorHeaderMouse.containsMouse
                                    ToolTip.delay: 500
                                    ToolTip.text: overlayEditorPanel.collapsed
                                                  ? qsTr("Show the overlay editor")
                                                  : qsTr("Hide the overlay editor")
                                }

                                ScrollView {
                                    id: overlayEditorScroll
                                    visible: !overlayEditorPanel.collapsed
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Layout.margins: 4
                                    clip: true
                                    contentWidth: availableWidth
                                    contentHeight: overlayEditor.implicitHeight

                                    // Scroll an inspector control into view (What's
                                    // New "Show me" targets can sit below the fold).
                                    function ensureItemVisible(item) {
                                        var flickable = contentItem as Flickable
                                        if (!flickable)
                                            return
                                        var y = item.mapToItem(overlayEditor, 0, 0).y
                                        flickable.contentY = Math.max(0, Math.min(
                                            y - 8, flickable.contentHeight - flickable.height))
                                    }

                                    OverlayEditor {
                                        id: overlayEditor
                                        width: overlayEditorScroll.availableWidth
                                        generator: overlayGenerator
                                        onTemplateLoadFailed: function(path) {
                                            messageDialog.title = qsTr("Template Error")
                                            messageDialog.message = qsTr("Could not load the template:\n%1\n\nThe file may be corrupt or unreadable. The previous template is still active.").arg(path)
                                            messageDialog.open()
                                        }
                                        timeline: timelineView.timeline
                                        dive: mainWindow.currentDive
                                        cellModel: overlayCellModel
                                    }
                                }

                                // Keeps the header pinned to the top while the
                                // ScrollView is hidden (a lone layout child
                                // gets vertically centered otherwise).
                                Item {
                                    visible: overlayEditorPanel.collapsed
                                    Layout.fillHeight: true
                                }
                            }
                        }
                    } // end Tab 1 SplitView (mainContentArea)

                    // Tab 2: Dive Profile — preview (depth curve + indicator) + editor sidebar
                    SplitView {
                        id: profileTab
                        orientation: Qt.Horizontal

                        // Preview area
                        Item {
                            id: profilePreviewArea
                            SplitView.fillWidth: true
                            SplitView.minimumWidth: 400

                            Rectangle {
                                id: profilePreviewFrame
                                anchors.centerIn: parent
                                width: parent.width * 0.95
                                height: parent.height * 0.95
                                color: "transparent"
                                border.color: palette.mid
                                border.width: 1
                                visible: mainWindow.hasActiveDive

                                // Double-buffered preview: two Image elements share the
                                // same anchored rect. We always load the next frame into
                                // the BACK buffer (the hidden one); once it reports Ready
                                // we flip `useA` so the freshly-loaded buffer becomes the
                                // visible one. The previously-visible buffer stays on
                                // screen until the next frame's load completes, so the
                                // user never sees a blank gap between frames.
                                Item {
                                    id: profilePreviewImage
                                    anchors.fill: parent
                                    anchors.margins: 1

                                    property bool useA: true
                                    property real pulseAnchorMs: Date.now()

                                    function setSource(url) {
                                        if (useA) {
                                            bufferB.source = url
                                        } else {
                                            bufferA.source = url
                                        }
                                    }

                                    Image {
                                        id: bufferA
                                        anchors.fill: parent
                                        fillMode: Image.PreserveAspectFit
                                        cache: false
                                        asynchronous: true
                                        visible: profilePreviewImage.useA
                                        onStatusChanged: {
                                            if (status === Image.Ready && !profilePreviewImage.useA) {
                                                profilePreviewImage.useA = true
                                            }
                                        }
                                    }
                                    Image {
                                        id: bufferB
                                        anchors.fill: parent
                                        fillMode: Image.PreserveAspectFit
                                        cache: false
                                        asynchronous: true
                                        visible: !profilePreviewImage.useA
                                        onStatusChanged: {
                                            if (status === Image.Ready && profilePreviewImage.useA) {
                                                profilePreviewImage.useA = false
                                            }
                                        }
                                    }

                                    // 100 ms debounce — used for property-change triggered
                                    // refreshes (e.g. color change). The pulse-mode tick
                                    // skips this debounce to keep its own cadence.
                                    Timer {
                                        id: profileUpdateTimer
                                        interval: 100
                                        repeat: false
                                        onTriggered: {
                                            profilePreviewImage.setSource("image://profile/preview/" + Date.now())
                                        }
                                    }

                                    property var updatePreview: function() {
                                        if (mainWindow.hasActiveDive && timelineView.visible) {
                                            profileUpdateTimer.restart()
                                        }
                                    }

                                    Component.onCompleted: {
                                        Qt.callLater(function() {
                                            profilePreviewImage.setSource("image://profile/preview/" + Date.now())
                                        })
                                    }

                                    Connections {
                                        target: profileGenerator
                                        function onBackgroundColorChanged()   { profilePreviewImage.updatePreview() }
                                        function onBackgroundOpacityChanged() { profilePreviewImage.updatePreview() }
                                        function onCurveColorChanged()        { profilePreviewImage.updatePreview() }
                                        function onCurveWidthChanged()        { profilePreviewImage.updatePreview() }
                                        function onIndicatorColorChanged()    { profilePreviewImage.updatePreview() }
                                        function onIndicatorModeChanged()     { profilePreviewImage.updatePreview() }
                                        function onIndicatorRadiusChanged()   { profilePreviewImage.updatePreview() }
                                        function onPulsePeriodMsChanged()     { profilePreviewImage.updatePreview() }
                                        function onOutputWidthChanged()       { profilePreviewImage.updatePreview() }
                                        function onOutputHeightChanged()      { profilePreviewImage.updatePreview() }
                                        function onDecoZoneColorChanged()     { profilePreviewImage.updatePreview() }
                                        function onDecoZoneOpacityChanged()   { profilePreviewImage.updatePreview() }
                                        function onGridEnabledChanged()       { profilePreviewImage.updatePreview() }
                                        function onGridDepthIntervalChanged() { profilePreviewImage.updatePreview() }
                                        function onGridTimeIntervalChanged()  { profilePreviewImage.updatePreview() }
                                        function onGridColorChanged()         { profilePreviewImage.updatePreview() }
                                        function onGridOpacityChanged()       { profilePreviewImage.updatePreview() }
                                        function onGridLineWidthChanged()     { profilePreviewImage.updatePreview() }
                                        function onGridShowLabelsChanged()    { profilePreviewImage.updatePreview() }
                                    }

                                    Connections {
                                        target: timelineView.timeline
                                        function onCurrentTimeChanged() {
                                            // currentTime now also moves while the video plays
                                            // (Video tab). Skip regenerating this hidden
                                            // tab's preview unless it's the active tab.
                                            if (contentTabs.currentIndex === 1)
                                                profilePreviewImage.updatePreview()
                                        }
                                    }

                                    // Pulse-mode animation tick. Wall-clock phase passes
                                    // through the URL so each tick produces a distinct
                                    // frame even when the dive cursor is idle. Goes
                                    // straight through setSource — the double-buffer
                                    // handles smooth swap-on-Ready.
                                    Timer {
                                        interval: 80
                                        repeat: true
                                        // Runs only while this tab is actually visible: each tick
                                        // re-renders the full profile (O(sample count)), and on
                                        // long dives that pegs a core and starves the image
                                        // providers if left running behind another tab. Also
                                        // suppressed while the video is playing — the
                                        // indicator's position is already moving with dive time
                                        // there, and the wall-clock pulse adds visual noise.
                                        running: profileGenerator
                                                 && profileGenerator.indicatorMode === 1
                                                 && mainWindow.hasActiveDive
                                                 && contentTabs.currentIndex === 1
                                                 && !videoSyncPlayer.playing
                                        onTriggered: {
                                            var period = profileGenerator ? profileGenerator.pulsePeriodMs : 2000
                                            if (period < 100) period = 100
                                            var elapsed = Date.now() - profilePreviewImage.pulseAnchorMs
                                            var phase = (elapsed % period) / period
                                            profilePreviewImage.setSource(
                                                "image://profile/preview/phase=" + phase.toFixed(4) +
                                                "/" + Date.now())
                                        }
                                    }
                                }
                            }

                            // Placeholder when no dive is loaded
                            Label {
                                anchors.centerIn: parent
                                visible: !mainWindow.hasActiveDive
                                text: qsTr("Import a dive log to see the profile")
                                font.pixelSize: 16
                                color: palette.placeholderText
                            }
                        }

                        // Editor sidebar — same collapse behavior as the
                        // overlay editor panel on tab 0.
                        Rectangle {
                            id: profileEditorPanel
                            SplitView.preferredWidth: 400
                            SplitView.minimumWidth: collapsed ? collapsedWidth : 280
                            SplitView.maximumWidth: collapsed ? collapsedWidth : Infinity
                            color: palette.window
                            border.color: palette.mid
                            border.width: 1

                            property bool collapsed: false
                            readonly property int collapsedWidth: 40
                            property real expandedWidth: 400
                            onCollapsedChanged: {
                                if (collapsed) {
                                    expandedWidth = width
                                    profileEditorPanel.SplitView.preferredWidth = collapsedWidth
                                } else {
                                    profileEditorPanel.SplitView.preferredWidth = expandedWidth
                                }
                            }

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 1
                                spacing: 0

                                // Clickable header bar — see the overlay editor
                                // panel header above for the rationale.
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 32
                                    radius: 4
                                    color: profileEditorHeaderMouse.containsMouse ? palette.midlight : palette.mid

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        spacing: 6

                                        Label {
                                            text: profileEditorPanel.collapsed ? "«" : "»"
                                            font.bold: true
                                        }

                                        Label {
                                            text: qsTr("Profile Editor")
                                            font.bold: true
                                            elide: Text.ElideRight
                                            visible: !profileEditorPanel.collapsed
                                            Layout.fillWidth: true
                                        }
                                    }

                                    MouseArea {
                                        id: profileEditorHeaderMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: profileEditorPanel.collapsed = !profileEditorPanel.collapsed
                                    }

                                    ToolTip.visible: profileEditorHeaderMouse.containsMouse
                                    ToolTip.delay: 500
                                    ToolTip.text: profileEditorPanel.collapsed
                                                  ? qsTr("Show the profile editor")
                                                  : qsTr("Hide the profile editor")
                                }

                                ScrollView {
                                    id: profileEditorScroll
                                    visible: !profileEditorPanel.collapsed
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Layout.margins: 4
                                    clip: true
                                    contentWidth: availableWidth
                                    contentHeight: profileEditor.implicitHeight

                                    ProfileEditor {
                                        id: profileEditor
                                        width: profileEditorScroll.availableWidth
                                        generator: profileGenerator
                                    }
                                }

                                Item {
                                    visible: profileEditorPanel.collapsed
                                    Layout.fillHeight: true
                                }
                            }
                        }
                    }

                    // Tab 2: Video tab and sync workflow (PR A5) —
                    // clip strip above the player on the left, collapsible
                    // video inspector (clip metadata / sync / colour) on
                    // the right, same SplitView idiom as tabs 0/1.
                    SplitView {
                        id: videoTab
                        orientation: Qt.Horizontal

                        ColumnLayout {
                            spacing: 0
                            SplitView.fillWidth: true
                            SplitView.minimumWidth: 400

                            ClipStrip {
                                Layout.fillWidth: true
                                // Clips follow the dive (the model's sessions are
                                // keyed by it), and video import is gated on a
                                // loaded dive everywhere else too.
                                visible: mainWindow.hasActiveDive
                                onAddRequested: importVideoFileDialog.open()
                            }

                            VideoSyncPlayer {
                                id: videoSyncPlayer
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                videoClip: clipList.activeClip
                                dive: mainWindow.currentDive
                                videoOffset: timelineView.timeline ? timelineView.timeline.videoOffset : 0
                                tabActive: contentTabs.currentIndex === 2

                                // Inverse of videoOffset: the player drives the red cursor to the
                                // dive-time of the current video frame while the tab is active.
                                onCursorTimeRequested: function(t) {
                                    timelineView.timeline.currentTime = t
                                }
                            }
                        }

                        // Video inspector panel — same collapse behavior as
                        // the editor sidebars on tabs 0/1. Gated on a loaded
                        // dive like the clip strip.
                        Rectangle {
                            id: videoInspectorPanel
                            SplitView.preferredWidth: 400
                            SplitView.minimumWidth: collapsed ? collapsedWidth : 280
                            SplitView.maximumWidth: collapsed ? collapsedWidth : Infinity
                            visible: mainWindow.hasActiveDive
                            color: palette.window
                            border.color: palette.mid
                            border.width: 1

                            property bool collapsed: false
                            readonly property int collapsedWidth: 40
                            property real expandedWidth: 400
                            onCollapsedChanged: {
                                // SplitView writes preferredWidth when the user
                                // drags the handle, so restore it imperatively.
                                if (collapsed) {
                                    expandedWidth = width
                                    videoInspectorPanel.SplitView.preferredWidth = collapsedWidth
                                } else {
                                    videoInspectorPanel.SplitView.preferredWidth = expandedWidth
                                }
                            }

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 1
                                spacing: 0

                                // Clickable header bar — see the overlay editor
                                // panel header above for the rationale.
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 32
                                    radius: 4
                                    color: videoInspectorHeaderMouse.containsMouse ? palette.midlight : palette.mid

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        spacing: 6

                                        Label {
                                            text: videoInspectorPanel.collapsed ? "«" : "»"
                                            font.bold: true
                                        }

                                        Label {
                                            text: qsTr("Video Inspector")
                                            font.bold: true
                                            elide: Text.ElideRight
                                            visible: !videoInspectorPanel.collapsed
                                            Layout.fillWidth: true
                                        }
                                    }

                                    MouseArea {
                                        id: videoInspectorHeaderMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: videoInspectorPanel.collapsed = !videoInspectorPanel.collapsed
                                    }

                                    ToolTip.visible: videoInspectorHeaderMouse.containsMouse
                                    ToolTip.delay: 500
                                    ToolTip.text: videoInspectorPanel.collapsed
                                                  ? qsTr("Show the video inspector")
                                                  : qsTr("Hide the video inspector")
                                }

                                ScrollView {
                                    id: videoInspectorScroll
                                    visible: !videoInspectorPanel.collapsed
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Layout.margins: 4
                                    clip: true
                                    contentWidth: availableWidth
                                    contentHeight: videoInspector.implicitHeight

                                    VideoInspector {
                                        id: videoInspector
                                        width: videoInspectorScroll.availableWidth
                                        videoClip: clipList.activeClip
                                        dive: mainWindow.currentDive
                                        videoOffset: timelineView.timeline ? timelineView.timeline.videoOffset : 0
                                        videoPositionSeconds: videoSyncPlayer.videoPositionSeconds

                                        // Manual sync gestures route through the
                                        // Timeline facade, which attributes the
                                        // write as SyncSource.Manual (PR A2).
                                        onSyncOffsetComputed: function(offset) {
                                            timelineView.timeline.videoOffset = offset
                                        }
                                        onProfileApplied: function(profileName, clipCount) {
                                            toast.show(qsTr("Applied camera profile \"%1\" to %n clip(s).", "", clipCount)
                                                       .arg(profileName))
                                        }
                                    }
                                }

                                Item {
                                    visible: videoInspectorPanel.collapsed
                                    Layout.fillHeight: true
                                }
                            }
                        }
                    }

                    // Tab 3: application settings
                    SettingsPanel {
                        id: settingsPanel
                        onChooseExportDirectory: exportDestinationDialog.open()
                        onShowWhatsNew: {
                            whatsNewDialog.releases = whatsNew.allReleases()
                            whatsNewDialog.open()
                        }
                    }
                } // end StackLayout
            } // end ColumnLayout
        } // end tabContainer Item

        // Timeline area. Collapse state lives on the TimelineView (its header
        // chevron); this pane follows it so the SplitView actually gives the
        // space back. Session-only by design — never persisted, never
        // triggered programmatically.
        Rectangle {
            id: timelineArea
            SplitView.preferredHeight: 200
            SplitView.minimumHeight: timelineView.collapsed ? collapsedHeight : 150
            SplitView.maximumHeight: timelineView.collapsed ? collapsedHeight : Infinity
            color: palette.mid

            readonly property int collapsedHeight: 32
            property real expandedHeight: 200

            Connections {
                target: timelineView
                function onCollapsedChanged() {
                    // SplitView writes preferredHeight when the user drags the
                    // handle, so restore it imperatively (same pattern as the
                    // editor sidebars).
                    if (timelineView.collapsed) {
                        timelineArea.expandedHeight = timelineArea.height
                        timelineArea.SplitView.preferredHeight = timelineArea.collapsedHeight
                    } else {
                        timelineArea.SplitView.preferredHeight = timelineArea.expandedHeight
                    }
                }
            }

            TimelineView {
                id: timelineView
                anchors.fill: parent
                visible: mainWindow.hasActiveDive
                videoSyncMode: contentTabs.currentIndex === 2 && timeline.videoPath !== ""
            }
            
            // Placeholder when no dive is loaded
            Label {
                anchors.centerIn: parent
                text: qsTr("Import a dive log to view timeline data")
                visible: !mainWindow.hasActiveDive
                font.pixelSize: 16
                color: palette.placeholderText
            }
        }
    }
    
    // Non-blocking notifications. Informational messages (export done, video
    // imported, FFmpeg missing) surface here; errors and decisions stay modal.
    ToastNotification {
        id: toast
        // In the popup overlay, above modal dialogs and their dim — the
        // startup FFmpeg toast and the What's New dialog open in the same
        // tick, and in contentItem the toast would expire unseen behind it.
        parent: Overlay.overlay
        anchors.fill: parent
        z: 1000
    }

    // Colour-grade feedback (C4): eyedropper frame failures, LUT/preset
    // file errors and preset-saved confirmations. main.qml owns the toast,
    // so the controller's signals surface here, not in ColorPanel.
    Connections {
        target: colorGrade
        function onPickFailed(reason) { toast.show(reason) }
        function onErrorOccurred(message) { toast.show(message) }
        function onPresetSaved(name) { toast.show(qsTr("Preset \"%1\" saved.").arg(name)) }
    }

    // Window-wide external file drop target: dive logs import exactly like
    // the Import menu, video files become clips. This only ever sees
    // OS-level drags — the in-app gestures (overlay cell dragging, timeline
    // band dragging) are plain MouseArea moves, not QML drag-and-drop, so
    // nothing in the UI competes with it.
    DropArea {
        id: fileDropArea
        anchors.fill: parent
        z: 900

        // Same guard expression as the undo/redo Shortcuts above, for the
        // same reason: drag-and-drop delivery is not blocked by popup
        // modality, and the image export loop pumps events — a drop
        // mid-export would swap the dive/clips under the running frame
        // sequence.
        onEntered: function(drag) {
            if (!drag.hasUrls || imageExporter.busy || videoExporter.busy)
                drag.accepted = false
        }

        onDropped: function(drop) {
            if (!drop.hasUrls || imageExporter.busy || videoExporter.busy)
                return
            var logPaths = []
            var videoUrls = []
            var skipped = 0
            for (var i = 0; i < drop.urls.length; i++) {
                var path = mainWindow.urlToLocalFile(drop.urls[i].toString())
                if (path === "") {
                    skipped++
                    continue
                }
                var dotAt = path.lastIndexOf(".")
                var ext = dotAt >= 0 ? path.substring(dotAt + 1).toLowerCase() : ""
                if (window.diveLogExtensions.indexOf(ext) !== -1)
                    logPaths.push(path)
                else if (window.videoExtensions.indexOf(ext) !== -1)
                    videoUrls.push(drop.urls[i])
                else
                    skipped++
            }

            // Logs first, so a combined drop can attach its videos to the
            // freshly imported dive (importFile dispatches synchronously
            // for single-dive files). A multi-dive file only opens the
            // async selection dialog: with a dive already loaded the
            // videos land on THAT dive; with none, the hasActiveDive gate
            // below rejects them and the user re-drops after choosing.
            for (var j = 0; j < logPaths.length; j++) {
                if (!logParser.importFile(logPaths[j])) {
                    messageDialog.title = qsTr("Import Error")
                    messageDialog.message = logParser.lastError
                    messageDialog.open()
                }
            }

            if (videoUrls.length > 0) {
                // Same gate as the Import Video menu item: clips belong to
                // a dive (the model keys its sessions by it).
                if (!mainWindow.hasActiveDive)
                    toast.show(qsTr("Import a dive log before adding videos."))
                else
                    window.importVideoUrls(videoUrls)
            }

            if (skipped > 0)
                toast.show(qsTr("%n dropped file(s) were not recognized as dive logs or videos.", "", skipped))

            drop.accepted = true
        }

        // Simple full-window indicator while a drag hovers the window.
        Rectangle {
            anchors.fill: parent
            visible: fileDropArea.containsDrag
            color: "#66000000"
            border.color: "#ff8c00"
            border.width: 3

            Label {
                anchors.centerIn: parent
                text: qsTr("Drop dive logs or videos to import")
                color: "white"
                font.pixelSize: 22
                font.bold: true
            }
        }
    }

    // Dialogs
    FileDialog {
        id: importDiveLogFileDialog
        title: qsTr("Import Dive Log")
        nameFilters: ["Dive log files (*.xml *.ssrf *.uddf *.fit)", "All files (*)"]
        onAccepted: {
            console.log("Selected file path:", selectedFile.toString());
            // Use our C++ helper to convert the URL to a local file path
            let filePath = mainWindow.urlToLocalFile(selectedFile.toString());
            console.log("Converted file path:", filePath);
            
            if (logParser.importFile(filePath)) {
                // File import will be handled through the LogParser::diveImported signal
                // which is connected to MainWindow::onDiveImported
            } else {
                messageDialog.title = qsTr("Import Error");
                messageDialog.message = logParser.lastError;
                messageDialog.open();
            }
        }
    }
    
    FolderDialog {
        id: exportDestinationDialog
        title: qsTr("Select Export Directory")
        currentFolder: config ? mainWindow.localFileToUrl(config.lastExportPath) : ""
        onAccepted: {
            // Persisted immediately; the exporters read it at export time and
            // the Settings tab shows the same value.
            config.lastExportPath = mainWindow.urlToLocalFile(selectedFolder.toString())
        }
    }

    FileDialog {
        id: importVideoFileDialog
        title: qsTr("Import Videos")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Video files (%1)").arg(window.videoNameFilterGlobs),
                      qsTr("All files (*)")]

        onAccepted: window.importVideoUrls(selectedFiles)
    }
    
    Dialog {
        id: exportImagesDialog
        title: qsTr("Export")
        modal: true
        // Wider on Video-tab invocations (follow-up A): the composite rows
        // carry clip + dive time spans and per-clip advisories. Clamped to
        // the window so a small window can never push the dialog off
        // screen — every label below wraps or elides, so any width works.
        width: Math.min(chooseContent ? 640 : 500, window.width - 40)
        // Height follows the content but never exceeds the window; past
        // that the content column scrolls (exportContentFlick) instead of
        // losing its top rows off-screen.
        height: Math.min(implicitHeight, window.height - 40)

        // Parameters set by the caller (the per-tab Export button) before open():
        //   targetGenerator: the QObject* generator to render frames from
        //     (overlayGenerator or profileGenerator). Both implement
        //     IFrameGenerator; the exporter does the cross-cast internally.
        //   contentType: a short tag ("dive_computer" / "dive_profile")
        //     appended to the export filename/directory so exports of
        //     different overlays for the same dive don't collide.
        property var targetGenerator: overlayGenerator
        property string contentType: "dive_computer"
        //   chooseContent: true when invoked from the Video tab, where
        //     no single overlay is implied — the dialog then shows a
        //     "What to Export" radio group that drives the two properties
        //     above. False for the per-tab invocations, which set them
        //     directly.
        property bool chooseContent: false
        //   compositeCapable: tab-2 invocation with a probed active clip —
        //     unlocks the Composited / Overlays-only-placed modes (PR B3).
        property bool compositeCapable: false
        //   enabledClipCount: snapshot of clipsInDiveOrder(true).length taken
        //     by the tab-2 Export button at open time; drives the batch
        //     checkbox's enabled state and hint (PR A6).
        property int enabledClipCount: 0

        // True when accepting the dialog should run the per-clip batch
        // queue instead of a single export. Gated on compositeMode: the
        // batch is only offered for the two clip-anchored modes, where each
        // clip fully determines its own range, geometry and layout.
        readonly property bool batchMode:
            compositeMode && exportEveryClipCheckbox.checked

        // True while one of the two compositor-driven modes is selected.
        // Gated on chooseContent so stale radio state can never flip the
        // dialog into composite mode when opened from tabs 0/1.
        readonly property bool compositeMode:
            chooseContent && compositeCapable
            && (exportModeComposited.checked || exportModePlaced.checked)

        // Export range in CLIP seconds for the composite modes: the whole
        // clip, or the visible timeline range clipped to the clip.
        function compositeRange() {
            var clip = clipList.activeClip
            if (!clip)
                return { start: 0, end: 0 }
            if (compositeRangeVisible.checked) {
                var s = Math.max(0, timelineView.visibleStartTime - clip.syncOffset)
                var e = Math.min(clip.durationSeconds,
                                 timelineView.visibleEndTime - clip.syncOffset)
                return { start: s, end: Math.max(s, e) }
            }
            return { start: 0, end: clip.durationSeconds }
        }

        // The range options show the actual times they cover ("12:30 – 28:00")
        // so the link between the timeline state and the export is explicit
        // (beta panel: nobody connected "visible time range" to the timeline).
        function formatRangeTime(seconds) {
            // Signed: a clip synced before the dive start (camera rolling
            // at the surface) has a negative dive-time start — clamping
            // it to 0:00 would also make the displayed span length wrong.
            var rounded = Math.round(seconds)
            var s = Math.abs(rounded)
            var h = Math.floor(s / 3600)
            var m = Math.floor((s % 3600) / 60)
            var ss = s % 60
            var mmss = m + ":" + (ss < 10 ? "0" : "") + ss
            var text = h > 0 ? h + ":" + (m < 10 ? "0" : "") + mmss : mmss
            return (rounded < 0 ? "−" : "") + text
        }
        function formatRange(start, end) {
            return formatRangeTime(start) + " – " + formatRangeTime(end)
        }

        // x offset of a RadioButton/CheckBox's text column, so the wrapping
        // detail/hint Labels under a control line up with its text whatever
        // the style's indicator size (follow-up A).
        function radioTextIndent(button) {
            return button.leftPadding
                   + (button.indicator ? button.indicator.width + button.spacing : 0)
        }

        // The content's natural height (column + its 20 px margins). The
        // style then derives implicitHeight from it plus its OWN header,
        // footer and padding — the old hand-tuned "+ 140" undercounted
        // those, which went unnoticed while the column could spill past the
        // dialog edge unclipped; inside the clipping Flickable it would
        // force a needless scrollbar.
        contentHeight: contentColumn.implicitHeight + 40
        
        // Move standard buttons to the footer
        footer: DialogButtonBox {
            standardButtons: Dialog.Ok | Dialog.Cancel
            // Define a function to handle video export
            function handleExport() {
                console.log("Export dialog accepted - this should only print once");
                // Get the video file path (if any) for unique export naming
                let videoFile = timelineView.videoPath !== "" ? mainWindow.urlToLocalFile(timelineView.videoPath) : ""
                // Pull dialog-level parameters set by the caller.
                let target = exportImagesDialog.targetGenerator
                let contentType = exportImagesDialog.contentType
                if (exportImagesDialog.compositeMode) {
                    // Composited / placed-overlays export (PR B3): driven by
                    // the active clip — geometry, frame rate, placement
                    // layout and (for composited) the footage itself come
                    // from it. Codec/quality must be set BEFORE
                    // createDefaultExportFile for the same extension-
                    // derivation reason as below.
                    let clip = clipList.activeClip
                    if (!clip) {
                        messageDialog.title = qsTr("Export Error");
                        messageDialog.message = qsTr("No active video clip to export.");
                        messageDialog.open();
                        return;
                    }
                    let overlaysOnly = exportModePlaced.checked
                    videoExporter.videoQuality = config.videoQuality;
                    videoExporter.videoCodec = codecComboBox.currentText;
                    videoExporter.customResolution = Qt.size(0, 0);
                    if (exportImagesDialog.batchMode) {
                        // PR A6: one export per enabled clip, chained by
                        // the queue. No pre-computed output path here —
                        // the queue names each clip's file at its turn.
                        clipExportQueue.start(overlaysOnly);
                        return;
                    }
                    let compositeContent = overlaysOnly ? "overlays_placed" : "composited"
                    let outputFile = videoExporter.createDefaultExportFile(
                            mainWindow.currentDive, clip.path, compositeContent);
                    if (outputFile) {
                        let range = exportImagesDialog.compositeRange()
                        console.log("Composite export (", compositeContent, ") clip range",
                                    range.start, "to", range.end);
                        videoExporter.exportComposite(mainWindow.currentDive, clip,
                                                      overlayGenerator, profileGenerator,
                                                      range.start, range.end, overlaysOnly);
                    } else {
                        messageDialog.title = qsTr("Export Error");
                        messageDialog.message = qsTr("Failed to create export file");
                        messageDialog.open();
                    }
                } else if (exportTypeImages.checked) {
                    // Images export mode
                    let path = imageExporter.createDefaultExportDir(mainWindow.currentDive, videoFile, contentType);
                    if (path) {
                        imageExporter.exportPath = path;

                        // Check if we have a video and should export only the video portion
                        if (exportVideoRangeOnly.checked && timelineView.videoPath !== "") {
                            // Export only the video range - use the methods
                            let videoStart = timelineView.timeline.getVideoStartTime();
                            let videoEnd = timelineView.timeline.getVideoEndTime();

                            console.log("Exporting video range from", videoStart, "to", videoEnd);

                            imageExporter.exportImageRange(
                                mainWindow.currentDive,
                                target,
                                videoStart,
                                videoEnd
                            );
                        }
                        // Check if we should export only the visible range
                        else if (exportRangeOnly.checked) {
                            // Export only the visible range from the timeline
                            imageExporter.exportImageRange(
                                mainWindow.currentDive,
                                target,
                                timelineView.visibleStartTime,
                                timelineView.visibleEndTime
                            );
                        } else {
                            // Export the full dive
                            imageExporter.exportImages(mainWindow.currentDive, target);
                        }
                    } else {
                        messageDialog.title = qsTr("Export Error");
                        messageDialog.message = qsTr("Failed to create export directory");
                        messageDialog.open();
                    }
                } else {
                    // Video export mode
                    // Apply codec/quality/framerate BEFORE generating the output
                    // filename: createDefaultExportFile() derives the file
                    // extension from the selected codec, so the codec must be set
                    // first. Otherwise the extension lags behind the selection
                    // (e.g. ProRes lands in a .webm container) and FFmpeg's muxer
                    // rejects the codec/container mismatch.
                    videoExporter.frameRate = videoFrameRateSpinBox.value;
                    videoExporter.videoQuality = config.videoQuality;
                    videoExporter.videoCodec = codecComboBox.currentText;

                    let outputFile = videoExporter.createDefaultExportFile(mainWindow.currentDive, videoFile, contentType);
                    if (outputFile) {
                        // Handle resolution matching
                        if (matchVideoResolutionCheckbox.checked && timelineView.videoPath !== "") {
                            // Prefer the active clip's already-probed,
                            // rotation-applied size (same semantics as
                            // detectVideoResolution) — that call runs ffprobe
                            // synchronously on the GUI thread (up to 10 s).
                            // Fall back to it only for a not-yet-probed clip.
                            let clip = clipList.activeClip;
                            let videoRes = (clip && clip.width > 0 && clip.height > 0)
                                ? Qt.size(clip.width, clip.height)
                                : videoExporter.detectVideoResolution(timelineView.videoPath);
                            if (videoRes.width > 0 && videoRes.height > 0) {
                                videoExporter.customResolution = videoRes;
                            }
                        } else {
                            // Use default resolution from the template
                            videoExporter.customResolution = Qt.size(0, 0);
                        }
                        
                        // Determine the time range to export
                        let startTime, endTime;
                        
                        if (exportVideoRangeOnly.checked && timelineView.videoPath !== "") {
                            // Export the imported video's time range
                            startTime = timelineView.timeline.getVideoStartTime();
                            endTime = timelineView.timeline.getVideoEndTime();
                        } else if (exportRangeOnly.checked) {
                            // Export the visible range from the timeline
                            startTime = timelineView.visibleStartTime;
                            endTime = timelineView.visibleEndTime;
                        } else {
                            // Export the full dive
                            startTime = 0;
                            endTime = mainWindow.currentDive.durationSeconds;
                        }
                        
                        console.log("Exporting video from", startTime, "to", endTime);
                        videoExporter.exportVideo(mainWindow.currentDive, target, startTime, endTime);
                    } else {
                        messageDialog.title = qsTr("Export Error");
                        messageDialog.message = qsTr("Failed to create export file");
                        messageDialog.open();
                    }
                }
            }
            onAccepted: {
                // Close first: the image export runs synchronously inside
                // handleExport(), and the progress dialog (and the success
                // toast) would otherwise stack on this still-open modal.
                exportImagesDialog.accept();
                handleExport();
            }
            onRejected: exportImagesDialog.reject()
            padding: 10
        }

        // The dialog height is a declarative binding (see `height` above):
        // it tracks implicitHeight — which follows contentColumn as rows
        // show/hide and wrapped labels reflow — clamped to the window. This
        // replaced an imperative onImplicitHeightChanged write plus a 10 ms
        // resize timer driven by the image/video radios.

        Flickable {
            id: exportContentFlick
            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: exportImagesDialog.contentHeight
            // Only scrollable when the content really overflows (small
            // window); otherwise behaves like the old static column.
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: exportContentFlick.contentHeight > exportContentFlick.height
                        ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }

            // Keyboard navigation: when the content overflows (small
            // window), Tab can move focus to a control below the fold —
            // Flickable never follows focus on its own, so scroll the
            // focused control into view. Ignores focus items outside the
            // dialog's content column.
            property Item dialogFocusItem: Window.activeFocusItem
            onDialogFocusItemChanged: {
                if (!dialogFocusItem || contentHeight <= height)
                    return
                var p = dialogFocusItem
                var inside = false
                while (p) {
                    if (p === contentColumn) { inside = true; break }
                    p = p.parent
                }
                if (!inside)
                    return
                var r = dialogFocusItem.mapToItem(
                            contentItem, 0, 0,
                            dialogFocusItem.width, dialogFocusItem.height)
                if (r.y < contentY)
                    contentY = Math.max(0, r.y - 8)
                else if (r.y + r.height > contentY + height)
                    contentY = Math.min(contentHeight - height,
                                        r.y + r.height - height + 8)
            }

            ColumnLayout {
                id: contentColumn
                x: 20
                y: 20
                // Explicit width (not implicit): this is what makes every
                // Layout.fillWidth child — and the wrapping/eliding labels
                // inside the GroupBoxes — actually constrain to the dialog.
                width: exportContentFlick.width - 40
                spacing: 20

                // Destination: the base directory exports land in. Changing it
                // here writes config.lastExportPath, which the exporters read at
                // export time — the per-dive sub-folder/file naming is unchanged.
                GroupBox {
                    title: qsTr("Destination")
                    Layout.fillWidth: true

                    RowLayout {
                        anchors.fill: parent

                        TextField {
                            Layout.fillWidth: true
                            text: config ? config.lastExportPath : ""
                            readOnly: true

                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            ToolTip.text: qsTr("Each export creates an automatically named sub-folder or file inside this directory.")
                        }

                        Button {
                            text: qsTr("Change...")
                            onClicked: exportDestinationDialog.open()
                        }
                    }
                }

                // Which overlay to export — only offered from the Video
                // tab. The change handlers are gated on chooseContent so a stale
                // radio state can't clobber the parameters set by the tab-0/1
                // Export buttons.
                GroupBox {
                    title: qsTr("What to Export")
                    Layout.fillWidth: true
                    visible: exportImagesDialog.chooseContent

                    ColumnLayout {
                        anchors.fill: parent

                        // Compositor-driven modes (PR B3), offered only with a
                        // probed active clip. Both force video output and the
                        // clip's own geometry/frame rate.
                        // Every control here takes Layout.fillWidth: without it a
                        // ColumnLayout child keeps its text's implicitWidth and
                        // spills past the GroupBox (follow-up A). With it, the
                        // styles' own labels elide when squeezed.
                        RadioButton {
                            id: exportModeComposited
                            Layout.fillWidth: true
                            visible: exportImagesDialog.compositeCapable
                            text: qsTr("Composited video (footage + overlays)")
                            onCheckedChanged: {
                                if (checked && exportImagesDialog.chooseContent)
                                    compositeRangeWhole.checked = true
                            }
                        }

                        RadioButton {
                            id: exportModePlaced
                            Layout.fillWidth: true
                            visible: exportImagesDialog.compositeCapable
                            text: qsTr("Overlays only, placed for this video (transparent)")
                            onCheckedChanged: {
                                if (checked && exportImagesDialog.chooseContent)
                                    compositeRangeWhole.checked = true
                            }
                        }

                        RadioButton {
                            id: exportContentComputer
                            Layout.fillWidth: true
                            text: qsTr("Dive computer overlay only")
                            checked: true
                            onCheckedChanged: {
                                if (checked && exportImagesDialog.chooseContent) {
                                    exportImagesDialog.targetGenerator = overlayGenerator
                                    exportImagesDialog.contentType = "dive_computer"
                                }
                            }
                        }

                        RadioButton {
                            id: exportContentProfile
                            Layout.fillWidth: true
                            text: qsTr("Dive profile only")
                            onCheckedChanged: {
                                if (checked && exportImagesDialog.chooseContent) {
                                    exportImagesDialog.targetGenerator = profileGenerator
                                    exportImagesDialog.contentType = "dive_profile"
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            color: palette.placeholderText
                            text: {
                                if (exportModeComposited.checked && exportImagesDialog.compositeCapable)
                                    return qsTr("The overlays are burned into your footage at their preview positions — one ready-to-share video file.")
                                if (exportModePlaced.checked && exportImagesDialog.compositeCapable)
                                    return qsTr("A transparent video at the clip's size with the overlays already in position — drop it over your footage in your video editor.")
                                return qsTr("Overlays export with transparency — composite them over your footage in your video editor.")
                            }
                        }

                        // Batch export (PR A6): offered for BOTH clip-anchored
                        // modes — DC-only/profile-only exports are
                        // dive-time-ranged and would just re-export overlapping
                        // dive windows per clip. It sits below the whole mode
                        // list, un-indented, after a rule (follow-up G): it used
                        // to sit indented directly under "Overlays only,
                        // placed…", which read as a sub-option of that radio
                        // alone and made it look missing under Composited.
                        Rectangle {
                            visible: exportImagesDialog.compositeMode
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            implicitHeight: 1
                            color: palette.mid
                        }

                        CheckBox {
                            id: exportEveryClipCheckbox
                            visible: exportImagesDialog.compositeMode
                            enabled: exportImagesDialog.enabledClipCount > 0
                            Layout.fillWidth: true
                            text: qsTr("Export every enabled clip separately")
                        }

                        Label {
                            visible: exportImagesDialog.compositeMode
                                     && (exportEveryClipCheckbox.checked
                                         || exportImagesDialog.enabledClipCount === 0)
                            Layout.fillWidth: true
                            // Aligned with the checkbox's text column.
                            leftPadding: exportImagesDialog.radioTextIndent(exportEveryClipCheckbox)
                            wrapMode: Text.Wrap
                            font.pixelSize: 11
                            color: palette.placeholderText
                            text: exportImagesDialog.enabledClipCount === 0
                                  ? qsTr("No clips are enabled — enable at least one clip to export a batch.")
                                  : qsTr("Each of the %n enabled clip(s) exports its whole range as its own file, with its own sync offset and overlay layout.",
                                         "", exportImagesDialog.enabledClipCount)
                        }
                    }
                }

                // Export type selection. Composite modes are video-only by
                // construction (the source footage is a video input), so the
                // image/video choice disappears there.
                GroupBox {
                    title: qsTr("Export Format")
                    Layout.fillWidth: true
                    visible: !exportImagesDialog.compositeMode

                    ColumnLayout {
                        anchors.fill: parent
                        
                        RadioButton {
                            id: exportTypeImages
                            Layout.fillWidth: true
                            text: qsTr("Export as Image Sequence")
                            checked: true
                            ToolTip.text: qsTr("Export individual PNG images with transparency")
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                        }
                        
                        RadioButton {
                            id: exportTypeVideo
                            Layout.fillWidth: true
                            text: qsTr("Export as Video File")
                            enabled: videoExporter.isFFmpegAvailable()
                            ToolTip.text: videoExporter.isFFmpegAvailable() ? 
                                qsTr("Export a single video file with the overlay") : 
                                qsTr("FFmpeg not found - install FFmpeg to enable video export")
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                        }
                    }
                }
                
                // Common export range options. Collapsed entirely in batch mode
                // (PR A6): every queued clip exports its whole range — the
                // checkbox hint above says so in its place.
                GroupBox {
                    title: qsTr("Export Range")
                    Layout.fillWidth: true
                    visible: !exportImagesDialog.batchMode

                    ColumnLayout {
                        anchors.fill: parent

                        // Composite-mode ranges: clip-anchored, labelled in both
                        // clip time and dive time. "Full dive" makes no sense
                        // against a single clip and is hidden here (plan).
                        // Follow-up A: the radio text stays short and static;
                        // the variable spans (clip time, dive time, clip name —
                        // hours-long, signed, or arbitrarily long) live in a
                        // wrapping detail Label under each radio, aligned with
                        // its text column. RadioButton text can't wrap, and a
                        // contentItem override is rejected by the native
                        // Windows/macOS styles ("does not support
                        // customization"), so this is the portable pattern.
                        RadioButton {
                            id: compositeRangeWhole
                            Layout.fillWidth: true
                            visible: exportImagesDialog.compositeMode
                            text: qsTr("Whole clip")
                        }

                        Label {
                            visible: compositeRangeWhole.visible
                                     && clipList.activeClip !== null
                                     && clipList.activeClip.durationSeconds > 0
                            Layout.fillWidth: true
                            leftPadding: exportImagesDialog.radioTextIndent(compositeRangeWhole)
                            topPadding: -4
                            wrapMode: Text.Wrap
                            font.pixelSize: 11
                            color: palette.placeholderText
                            text: {
                                var clip = clipList.activeClip
                                if (!clip || clip.durationSeconds <= 0)
                                    return ""
                                return qsTr("Clip %1 · dive %2")
                                    .arg(exportImagesDialog.formatRange(0, clip.durationSeconds))
                                    .arg(exportImagesDialog.formatRange(
                                             clip.syncOffset,
                                             clip.syncOffset + clip.durationSeconds))
                            }
                        }

                        RadioButton {
                            id: compositeRangeVisible
                            Layout.fillWidth: true
                            visible: exportImagesDialog.compositeMode
                            enabled: {
                                var clip = clipList.activeClip
                                if (!clip || clip.durationSeconds <= 0)
                                    return false
                                return Math.min(clip.durationSeconds,
                                                timelineView.visibleEndTime - clip.syncOffset)
                                        > Math.max(0, timelineView.visibleStartTime - clip.syncOffset)
                            }
                            text: enabled ? qsTr("Visible timeline range within the clip")
                                          : qsTr("Visible timeline range (outside this clip)")
                        }

                        Label {
                            visible: compositeRangeVisible.visible && compositeRangeVisible.enabled
                            Layout.fillWidth: true
                            leftPadding: exportImagesDialog.radioTextIndent(compositeRangeVisible)
                            topPadding: -4
                            wrapMode: Text.Wrap
                            font.pixelSize: 11
                            color: palette.placeholderText
                            text: {
                                var clip = clipList.activeClip
                                if (!clip || clip.durationSeconds <= 0)
                                    return ""
                                var s = Math.max(0, timelineView.visibleStartTime - clip.syncOffset)
                                var e = Math.min(clip.durationSeconds,
                                                 timelineView.visibleEndTime - clip.syncOffset)
                                return qsTr("Clip %1 · dive %2")
                                    .arg(exportImagesDialog.formatRange(s, e))
                                    .arg(exportImagesDialog.formatRange(s + clip.syncOffset,
                                                                        e + clip.syncOffset))
                            }
                        }

                        RadioButton {
                            id: exportFullDive
                            Layout.fillWidth: true
                            visible: !exportImagesDialog.compositeMode
                            text: mainWindow.hasActiveDive
                                  ? qsTr("Export full dive (%1)").arg(
                                        exportImagesDialog.formatRange(0, mainWindow.currentDive.durationSeconds))
                                  : qsTr("Export full dive")
                            checked: true
                        }

                        // The three range radios share this ColumnLayout, so
                        // autoExclusive already keeps them mutually exclusive —
                        // no manual enabled/checked cross-wiring (it locked the
                        // visible-range option out whenever the video range was
                        // pre-selected, e.g. exporting from the Video tab).
                        // Bounded text (two signed h:mm:ss times): fits the
                        // 500 px dialog; fillWidth lets the style elide it in
                        // a window narrower than that instead of spilling.
                        RadioButton {
                            Layout.fillWidth: true
                            visible: !exportImagesDialog.compositeMode
                            text: qsTr("Export only visible time range (%1)").arg(
                                      exportImagesDialog.formatRange(timelineView.visibleStartTime,
                                                                     timelineView.visibleEndTime))
                            id: exportRangeOnly
                        }

                        // Since A2 the "video range" IS the active clip's dive
                        // window (the timeline facade reads through it), so say
                        // so (plan wording): name the clip and show its dive
                        // range, consistent with the composite radios above
                        // (which add the clip-time range too — meaningless
                        // here, where output frames are dive-time overlays).
                        // The clip name is unbounded (user-renamable), so it
                        // goes in the wrapping detail line, not the radio text.
                        RadioButton {
                            id: exportVideoRangeOnly
                            Layout.fillWidth: true
                            visible: !exportImagesDialog.compositeMode
                            text: qsTr("Export only the active clip's range")
                            enabled: timelineView.videoPath !== "" && timelineView.timeline.videoDuration > 0
                        }

                        Label {
                            visible: exportVideoRangeOnly.visible && exportVideoRangeOnly.enabled
                                     && clipList.activeClip !== null
                            Layout.fillWidth: true
                            leftPadding: exportImagesDialog.radioTextIndent(exportVideoRangeOnly)
                            topPadding: -4
                            wrapMode: Text.Wrap
                            font.pixelSize: 11
                            color: palette.placeholderText
                            text: {
                                var clip = clipList.activeClip
                                if (!clip)
                                    return ""
                                return qsTr("%1 · dive %2")
                                    .arg(clip.displayName)
                                    .arg(exportImagesDialog.formatRange(
                                             clip.syncOffset,
                                             clip.syncOffset + clip.durationSeconds))
                            }
                        }
                    }
                }
                
                // Image-specific options (visible when exporting images; the
                // composite modes are always video, whatever the hidden
                // format radios still hold)
                GroupBox {
                    title: qsTr("Image Export Settings")
                    Layout.fillWidth: true
                    visible: exportTypeImages.checked && !exportImagesDialog.compositeMode
                    
                    RowLayout {
                        Layout.fillWidth: true
                        
                        Label {
                            text: qsTr("Frame rate:")
                        }
                        
                        SpinBox {
                            id: frameRateSpinBox
                            value: config ? Math.round(config.frameRate) : 10
                            from: 1
                            to: 60
                            
                            onValueChanged: {
                                if (exportTypeImages.checked) {
                                    imageExporter.frameRate = value
                                }
                            }
                            
                            Component.onCompleted: {
                                imageExporter.frameRate = value
                            }
                        }
                    }
                }
                
                // Video-specific options (visible when exporting video; the
                // composite modes are always video)
                GroupBox {
                    title: qsTr("Video Export Settings")
                    Layout.fillWidth: true
                    visible: exportTypeVideo.checked || exportImagesDialog.compositeMode
                    enabled: exportTypeVideo.checked || exportImagesDialog.compositeMode

                    // Set a minimum height to ensure proper layout calculations
                    Layout.minimumHeight: videoSettingsColumn.implicitHeight + 30

                    ColumnLayout {
                        id: videoSettingsColumn
                        anchors.fill: parent
                        spacing: 8 // Reduce spacing for a more compact layout

                        RowLayout {
                            Layout.fillWidth: true
                            // Composite modes always run at the clip's native
                            // rate — the dialog rate only drives overlay-only
                            // video exports (config.videoFrameRate default).
                            visible: !exportImagesDialog.compositeMode

                            Label {
                                text: qsTr("Frame rate:")
                                Layout.preferredWidth: 100
                            }

                            SpinBox {
                                id: videoFrameRateSpinBox
                                value: config ? Math.round(config.videoFrameRate) : 30
                                from: 1
                                to: 60
                                Layout.fillWidth: true
                                onValueModified: {
                                    if (config)
                                        config.videoFrameRate = value
                                }
                            }
                        }

                        Label {
                            visible: exportImagesDialog.compositeMode
                                     && clipList.activeClip !== null
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            color: palette.placeholderText
                            // Batch mode exports many clips, each at its own
                            // geometry — the active clip's numbers would lie.
                            text: {
                                if (exportImagesDialog.batchMode)
                                    return qsTr("Resolution and frame rate follow each clip.")
                                return clipList.activeClip !== null
                                       ? qsTr("Resolution and frame rate follow the clip (%1×%2, %3 fps).")
                                             .arg(clipList.activeClip.width)
                                             .arg(clipList.activeClip.height)
                                             .arg(clipList.activeClip.fps.toFixed(2))
                                       : ""
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                text: qsTr("Codec:")
                                Layout.preferredWidth: 100
                            }

                            ComboBox {
                                id: codecComboBox
                                // Probe-driven: only codecs whose encoder the
                                // detected FFmpeg actually has (ffmpegInfo falls
                                // back to all four while probing or when FFmpeg
                                // is missing — the export path is already gated
                                // on isFFmpegAvailable), narrowed per mode:
                                // composited = opaque delivery codecs
                                // (h264/hevc mp4, prores 422 mov); placed =
                                // the alpha-capable pair; overlay-only = all.
                                model: {
                                    var all = ffmpegInfo.availableCodecs
                                    // Mode lists carry their own preference
                                    // order (first = fallback when the saved
                                    // codec isn't offered): delivery-friendly
                                    // h264 leads the composited list.
                                    if (exportModeComposited.checked && exportImagesDialog.compositeMode)
                                        return ["h264", "hevc", "prores"].filter(function(c) {
                                            return all.indexOf(c) >= 0
                                        })
                                    if (exportModePlaced.checked && exportImagesDialog.compositeMode)
                                        return ["vp9", "prores"].filter(function(c) {
                                            return all.indexOf(c) >= 0
                                        })
                                    return all
                                }
                                currentIndex: Math.max(0, model.indexOf(config ? config.videoCodec : "vp9"))
                                // An interactive selection breaks the
                                // currentIndex binding (standard ComboBox
                                // behavior), so a probe snapshot or mode switch
                                // landing while the dialog is open could
                                // silently remap the stale index to a different
                                // codec. Re-clamp against the new list
                                // explicitly.
                                onModelChanged: {
                                    var idx = model.indexOf(config ? config.videoCodec : "vp9")
                                    currentIndex = idx >= 0 ? idx : 0
                                }
                                // Persist the preference eagerly (dialog
                                // setting); the exporter is fed at export time.
                                onActivated: {
                                    if (config)
                                        config.videoCodec = currentText
                                }
                                Layout.fillWidth: true

                                ToolTip {
                                    visible: codecComboBox.hovered
                                    text: {
                                        if (codecComboBox.currentText === "h264")
                                            return qsTr("H.264 - most compatible, no transparency support")
                                        else if (codecComboBox.currentText === "prores")
                                            return exportImagesDialog.compositeMode && exportModeComposited.checked
                                                   ? qsTr("ProRes 422 - high quality editing format (no transparency needed when composited)")
                                                   : qsTr("ProRes 4444 - high quality with transparent background")
                                        else if (codecComboBox.currentText === "vp9")
                                            return qsTr("VP9 - compact format with transparent background (recommended)")
                                        else if (codecComboBox.currentText === "hevc")
                                            return qsTr("HEVC - better compression, no transparency support")
                                        else
                                            return ""
                                    }
                                    delay: 500
                                }
                            }
                        }

                        Label {
                            // Alpha only matters for the transparent-output
                            // modes; a composited file is opaque footage by
                            // definition.
                            visible: !exportImagesDialog.compositeMode
                                     && !videoExporter.codecSupportsAlpha(codecComboBox.currentText)
                            text: qsTr("This codec does not support transparency. The overlay background will be opaque.")
                            color: "#FF8800"
                            font.italic: true
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                text: qsTr("Quality:")
                                Layout.preferredWidth: 100
                            }

                            // Quality tier combo (PR B3) — replaced the bitrate
                            // slider, which never worked for h264/hevc (the
                            // fixed CRF always won over -b:v).
                            ComboBox {
                                id: qualityCombo
                                Layout.fillWidth: true
                                model: [qsTr("Best"), qsTr("High"), qsTr("Standard"), qsTr("Small file")]
                                readonly property var tiers: ["best", "high", "standard", "small"]
                                currentIndex: {
                                    var idx = tiers.indexOf(config ? config.videoQuality : "standard")
                                    return idx >= 0 ? idx : 2
                                }
                                onActivated: {
                                    if (config)
                                        config.videoQuality = tiers[currentIndex]
                                }
                            }
                        }

                        // Composite-only information rows: what happens to the
                        // clip's audio (copy vs AAC re-encode — decided in C++),
                        // and the 8-bit SDR warning for 10-bit/HDR sources
                        // (same detection as the video inspector's badge).
                        // The two rows below describe the ACTIVE clip (audio
                        // handling, HDR warning) — hidden in batch mode, where
                        // each clip gets its own treatment in C++ and a single
                        // clip's facts would mislead.
                        Label {
                            visible: exportImagesDialog.compositeMode
                                     && exportModeComposited.checked
                                     && !exportImagesDialog.batchMode
                                     && clipList.activeClip !== null
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            color: palette.placeholderText
                            // Keyed on the dialog's visibility like the grade
                            // row below: the invokable reads the clip's probed
                            // audio fields, which carry no dependency of their
                            // own in this binding — re-evaluating on open
                            // keeps the row current (pre-C5 it only refreshed
                            // by luck of the codec-combo dependency).
                            // Value args (not the clip pointer): the binding
                            // depends on the probe-NOTIFY'd properties, so the
                            // row updates when the async probe lands mid-open.
                            text: exportImagesDialog.visible
                                  && clipList.activeClip !== null
                                  ? videoExporter.compositeAudioText(clipList.activeClip.hasAudio,
                                                                     clipList.activeClip.audioCodec,
                                                                     codecComboBox.currentText)
                                  : ""
                        }

                        Label {
                            visible: exportImagesDialog.compositeMode
                                     && exportModeComposited.checked
                                     && !exportImagesDialog.batchMode
                                     && clipList.activeClip !== null
                                     && (clipList.activeClip.bitDepth > 8
                                         || clipList.activeClip.colorTransfer === "smpte2084"
                                         || clipList.activeClip.colorTransfer === "arib-std-b67")
                            text: qsTr("10-bit/HDR source — exported as 8-bit SDR in v0.4")
                            color: "#FF8800"
                            font.italic: true
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        // Colour-tag advisory (PR C5): an untagged source gets
                        // a decode matrix forced by resolution convention —
                        // the text (built in C++) matches EXACTLY what
                        // FFmpegArgs::inColorMatrixFor decides for the export.
                        // Empty text = tagged source, row hidden. Passing the
                        // colorSpace/height VALUES makes the binding depend on
                        // the probe-notified properties, so the row refreshes
                        // when the async probe lands (same pattern as the HDR
                        // row above).
                        Label {
                            visible: exportImagesDialog.compositeMode
                                     && exportModeComposited.checked
                                     && !exportImagesDialog.batchMode
                                     && clipList.activeClip !== null
                                     && text !== ""
                            text: clipList.activeClip !== null
                                  ? videoExporter.compositeColorTagText(
                                        clipList.activeClip.colorSpace,
                                        clipList.activeClip.height)
                                  : ""
                            color: "#FF8800"
                            font.italic: true
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        // Colour-grade row (PR C5): what the composited export
                        // will do with the active clip's grade — "applied
                        // (static)" / "applied (depth-linked)", plus a
                        // missing-LUT advisory. Hidden (empty text) when the
                        // clip has no enabled grade.
                        Label {
                            // Dependency channel: colorGradeMap's NOTIFY is
                            // colorGradeChanged, so referencing it below keeps
                            // this row fresh across colour-panel edits between
                            // dialog opens (the invokable itself is not
                            // reactive).
                            readonly property var gradeState:
                                clipList.activeClip !== null
                                ? clipList.activeClip.colorGradeMap : null
                            visible: exportImagesDialog.compositeMode
                                     && exportModeComposited.checked
                                     && !exportImagesDialog.batchMode
                                     && clipList.activeClip !== null
                                     && text !== ""
                            // Also keyed on the dialog's visibility: the
                            // missing-LUT advisory checks the file ON DISK,
                            // which can change (file deleted externally) with
                            // no QML signal — re-evaluating on every open
                            // keeps the row honest at the moment it is shown.
                            text: exportImagesDialog.visible
                                  && clipList.activeClip !== null && gradeState !== null
                                  ? videoExporter.compositeGradeText(clipList.activeClip,
                                                                     mainWindow.currentDive)
                                  : ""
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            color: palette.placeholderText
                        }

                        // Batch mode exports each clip with its OWN grade (the
                        // C5 arming lives inside exportComposite), so a single
                        // clip's grade facts would mislead here — say the
                        // per-clip rule instead.
                        Label {
                            visible: exportImagesDialog.batchMode
                                     && exportModeComposited.checked
                            text: qsTr("Colour grades: applied per clip where enabled")
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            color: palette.placeholderText
                        }

                        // Resolution options (overlay-only exports; composites
                        // always render at the clip's own resolution)
                        CheckBox {
                            id: matchVideoResolutionCheckbox
                            visible: !exportImagesDialog.compositeMode
                            text: qsTr("Match video resolution")
                            enabled: timelineView.videoPath !== ""
                            checked: false
                            Layout.fillWidth: true

                            ToolTip {
                                visible: parent.hovered
                                text: timelineView.videoPath !== "" ?
                                    qsTr("Output video will match the resolution of the imported video") :
                                    qsTr("Import a video first to enable this option")
                                delay: 500
                            }
                        }
                    }
                }
            }
        }
    }


    Dialog {
        id: diveSelectionDialog
        title: qsTr("Select Dive")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 500
        height: 400
        
        property var diveList: []
        property int selectedDiveIndex: -1
        
        function show(dives) {
            diveList = dives
            selectedDiveIndex = -1
            listView.currentIndex = -1
            open()
        }
        
        onAccepted: {
            if (selectedDiveIndex >= 0 && selectedDiveIndex < diveList.length) {
                // Get the selected dive data
                var selectedDiveData = diveList[selectedDiveIndex]
                console.log("User selected dive:", selectedDiveData.diveNumber, selectedDiveData.diveName)
                
                // Use the index to select the dive
                mainWindow.selectDiveByIndex(selectedDiveData.index)
            }
        }
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            
            Label {
                text: qsTr("This file contains multiple dives. Please select one:")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.bold: true
            }
            
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                
                ListView {
                    id: listView
                    model: diveSelectionDialog.diveList
                    
                    delegate: ItemDelegate {
                        width: listView.width
                        height: 60
                        
                        Rectangle {
                            anchors.fill: parent
                            color: parent.hovered ? palette.midlight : (listView.currentIndex === index ? palette.highlight : "transparent")
                            radius: 4
                            
                            ColumnLayout {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: 10
                                spacing: 2
                                
                                Label {
                                    text: qsTr("Dive #%1").arg(modelData.diveNumber)
                                    font.bold: true
                                    Layout.fillWidth: true
                                }
                                
                                Label {
                                    text: {
                                        var info = ""
                                        if (modelData.startTime && modelData.startTime.toString() !== "") {
                                            var date = new Date(modelData.startTime)
                                            info = date.toLocaleDateString() + " " + date.toLocaleTimeString()
                                        }
                                        if (modelData.diveSiteName && modelData.diveSiteName !== "") {
                                            if (info !== "") info += " - "
                                            info += modelData.diveSiteName
                                        } else if (modelData.location && modelData.location !== "") {
                                            if (info !== "") info += " - "
                                            info += modelData.location
                                        }
                                        return info
                                    }
                                    color: palette.windowText
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        
                        onClicked: {
                            listView.currentIndex = index
                            diveSelectionDialog.selectedDiveIndex = index
                        }
                        
                        onDoubleClicked: {
                            listView.currentIndex = index
                            diveSelectionDialog.selectedDiveIndex = index
                            diveSelectionDialog.accept()
                        }
                    }
                }
            }
            
            Label {
                text: qsTr("Double-click a dive to select it, or select and click OK.")
                Layout.fillWidth: true
                color: palette.placeholderText
                font.italic: true
            }
        }
    }
    
    Dialog {
        id: exportProgressDialog
        title: qsTr("Exporting Images")
        modal: true
        closePolicy: Popup.NoAutoClose
        standardButtons: Dialog.Cancel
        width: 400
        height: 200
        
        property int value: 0
        
        onRejected: {
            imageExporter.cancelExport()
        }
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 20
            
            Label {
                text: qsTr("Generating overlay images...")
                Layout.fillWidth: true
            }
            
            ProgressBar {
                value: exportProgressDialog.value / 100
                Layout.fillWidth: true
            }
        }
    }
    
    Dialog {
        id: videoExportProgressDialog
        title: qsTr("Exporting Video")
        modal: true
        closePolicy: Popup.NoAutoClose
        standardButtons: Dialog.Cancel
        width: 500
        height: 280
        
        property int value: 0
        property string statusText: qsTr("Preparing...")
        
        onRejected: {
            // Inter-clip gap of a batch: no export is in flight, so
            // cancelExport() below would be a no-op and the pending
            // startNext would reopen the dialog — the user's Cancel
            // would be silently eaten. Stop the queue here instead.
            if (clipExportQueue.active && !videoExporter.busy) {
                var done = clipExportQueue.exported
                var total = clipExportQueue.paths.length
                clipExportQueue.reset()
                toast.show(qsTr("Export cancelled after %1 of %2 clips.")
                               .arg(done).arg(total))
                return
            }
            // Try to cancel the FFmpeg process
            videoExporter.cancelExport();
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 20

            // Batch position line (PR A6). The bar/percentage below stay
            // per-clip — each clip is its own full 0-100% export run.
            Label {
                visible: clipExportQueue.active
                text: qsTr("Clip %1 of %2 — %3")
                          .arg(clipExportQueue.position)
                          .arg(clipExportQueue.paths.length)
                          .arg(clipExportQueue.currentName)
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            Label {
                text: videoExportProgressDialog.statusText
                Layout.fillWidth: true
                font.bold: true
            }
            
            // Wrap the ProgressBar in a container
            Item {
                id: progressBarContainer
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                
                ProgressBar {
                    id: exportProgressBar
                    anchors.fill: parent
                    value: videoExportProgressDialog.value / 100
                    
                    // Add a pulse animation to indicate activity
                    Rectangle {
                        id: pulseAnimation
                        width: Math.min(30, exportProgressBar.width / 10) // Make width proportional but not too wide
                        height: Math.min(4, parent.height * 0.5)
                        radius: height / 2 // Rounded corners to match progress bar
                        color: Qt.rgba(1, 1, 1, 0.25) // Subtle white with low opacity
                        visible: parent.value > 0 && parent.value < 1.0
                        y: (parent.height - height) / 2
                        
                        SequentialAnimation on x {
                            loops: Animation.Infinite
                            running: pulseAnimation.visible
                            // Start from the left side
                            PropertyAnimation {
                                from: 0
                                to: exportProgressBar.width - pulseAnimation.width
                                duration: 1500
                                easing.type: Easing.InOutQuad // Smoother easing
                            }
                            // Go back to the start
                            PropertyAnimation {
                                from: exportProgressBar.width - pulseAnimation.width
                                to: 0
                                duration: 1500
                                easing.type: Easing.InOutQuad
                            }
                        }
                    }
                    
                    onValueChanged: {
                        // console.log("Progress bar value updated to:", Math.round(value * 100), "%");
                    }
                }
            }
            
            Label {
                text: qsTr("Progress: %1%").arg(videoExportProgressDialog.value)
                Layout.alignment: Qt.AlignHCenter
            }

            Item {
                Layout.fillHeight: true
            }

            Label {
                text: qsTr("This process can take several minutes depending on video length and settings.\nDo not close the application during export.")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.italic: true
            }
        }
    }
    
    Dialog {
        id: messageDialog
        title: ""
        property string message: ""
        modal: true
        standardButtons: Dialog.Ok
        width: 400
        height: 200

        ColumnLayout {
            anchors.fill: parent

            Label {
                text: messageDialog.message
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
        }
    }

    Dialog {
        id: updateDialog
        title: qsTr("Update Available")
        property string latestVersion: ""
        property string releaseUrl: ""
        modal: true
        width: 450
        height: 200

        ColumnLayout {
            anchors.fill: parent
            spacing: 15

            Label {
                text: qsTr("A new version of Unabara (%1) is available!\n\nYou are currently running version %2.").arg(updateDialog.latestVersion).arg(appVersion)
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 10

                Button {
                    text: qsTr("Dismiss")
                    onClicked: updateDialog.close()
                }

                Button {
                    text: qsTr("Open Downloads")
                    highlighted: true
                    onClicked: {
                        Qt.openUrlExternally(updateDialog.releaseUrl)
                        updateDialog.close()
                    }
                }
            }
        }
    }

    Dialog {
        id: templateColorsDialog
        title: qsTr("Apply Template Colors?")
        property string templateName: ""
        modal: true
        width: 450
        // No explicit height: the dialog sizes to its content. A fixed height
        // overflows when the wrapped text needs more lines (theme/font/locale
        // dependent), pushing the buttons outside the popup.

        onAboutToShow: rememberColorChoiceCheck.checked = false

        ColumnLayout {
            width: parent.width
            spacing: 15

            Label {
                text: qsTr("The template \"%1\" comes with a color scheme.\n\nApply it to the dive profile (curve, indicator, grid and deco zone colors)?").arg(templateColorsDialog.templateName)
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            CheckBox {
                id: rememberColorChoiceCheck
                text: qsTr("Remember my choice for all templates")
                checked: false
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 10

                Button {
                    text: qsTr("No")
                    onClicked: {
                        if (rememberColorChoiceCheck.checked)
                            config.profileColorSchemePolicy = "never"
                        templateColorsDialog.close()
                    }
                }

                Button {
                    text: qsTr("Apply")
                    highlighted: true
                    onClicked: {
                        if (rememberColorChoiceCheck.checked)
                            config.profileColorSchemePolicy = "always"
                        profileGenerator.applyColorScheme(overlayGenerator.primaryColor,
                                                          overlayGenerator.secondaryColor)
                        templateColorsDialog.close()
                    }
                }
            }
        }
    }

    WhatsNewDialog {
        id: whatsNewDialog

        // "Show me" closes the notes, lands the user in the area the entry
        // talks about, and pulses a highlight around it. Zone tours
        // (planned) will hook in here.
        onShowMeRequested: function(target) {
            close()
            switch (target) {
            case "canvas":
                contentTabs.currentIndex = 0
                showMeFlash.flash(overlayCanvas)
                break
            case "timeline":
                contentTabs.currentIndex = 0
                showMeFlash.flash(timelineView)
                break
            case "profile":
                contentTabs.currentIndex = 1
                showMeFlash.flash(profileEditorPanel)
                break
            case "video":
                contentTabs.currentIndex = 2
                showMeFlash.flash(videoSyncPlayer)
                break
            case "settings":
                contentTabs.currentIndex = 3
                showMeFlash.flash(settingsPanel)
                break
            case "import_button":
                showMeFlash.flash(importButton)   // toolbar is visible on every tab — no tab switch
                break
            case "template_selector":
            case "cells_section":
            case "text_section": {
                // Inspector targets: the control sits inside a collapsible
                // section of the overlay editor sidebar. Reveal the whole
                // chain (tab, sidebar, section), then scroll + flash once
                // the geometry has settled.
                contentTabs.currentIndex = 0
                overlayEditorPanel.collapsed = false
                var item = overlayEditor.showMeTarget(target)
                if (item) {
                    Qt.callLater(function() {
                        overlayEditorScroll.ensureItemVisible(item)
                        showMeFlash.flash(item)
                    })
                }
                break
            }
            }
        }

        // Any dismissal counts as "seen" for the running version — including
        // manual opens from the Settings tab, where it's a no-op re-stamp.
        onClosed: {
            config.whatsNewSeenVersion = appVersion
            // Release an update notice that arrived while the notes were open
            if (window.pendingUpdateInfo) {
                updateDialog.latestVersion = window.pendingUpdateInfo.version
                updateDialog.releaseUrl = window.pendingUpdateInfo.url
                window.pendingUpdateInfo = null
                updateDialog.open()
            }
        }
    }

    // "Show me" target highlight: an Unabara-blue border pulsing slowly
    // twice over ~2 seconds, then gone. Lives in the window overlay layer
    // (not the content item) so it can also draw over toolbar targets —
    // the header stacks above regular content children.
    Rectangle {
        id: showMeFlash
        parent: Overlay.overlay
        visible: false
        opacity: 0
        color: "transparent"
        border.color: "#3498db"
        border.width: 4
        radius: 4
        z: 1000

        property Item target: null

        function flash(item) {
            target = item
            // Measure only after the tab switch has propagated visibility
            // and geometry.
            Qt.callLater(begin)
        }

        function begin() {
            if (!target || !target.visible)
                return
            var pos = target.mapToItem(showMeFlash.parent, 0, 0)
            x = pos.x - 3
            y = pos.y - 3
            width = target.width + 6
            height = target.height + 6
            visible = true
            flashAnimation.restart()
        }

        SequentialAnimation {
            id: flashAnimation
            loops: 2

            NumberAnimation {
                target: showMeFlash
                property: "opacity"
                from: 0.0; to: 1.0
                duration: 500
                easing.type: Easing.InOutQuad
            }
            NumberAnimation {
                target: showMeFlash
                property: "opacity"
                from: 1.0; to: 0.0
                duration: 500
                easing.type: Easing.InOutQuad
            }

            onFinished: showMeFlash.visible = false
        }
    }
}