import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Unabara.Core 1.0

// Sync workflow controls (PR A5): the former "Sync with Dive Log" GroupBox
// content, moved out of VideoSyncPlayer into the video inspector's Sync
// section, plus the two bulk actions: "Apply Profile to All Clips"
// (overwrites every enabled clip's sync) and "Apply Profile to All Unsynced
// Clips" (fills in only clips that carry no sync yet).
//
// Attribution contract (PR A2): manual gestures (dive-time field, nudge
// buttons) emit syncOffsetComputed, which main.qml routes to the Timeline
// facade — the facade write is what attributes the sync as Manual. Camera
// profile applies write the clip directly (syncOffset + syncSource =
// CameraProfile) so they are NOT attributed as manual.
ColumnLayout {
    id: root

    // Active clip (clipList.activeClip) and current dive, bound upstream.
    property var videoClip: null
    property var dive: null
    // Mirror of the Timeline facade's videoOffset (dive time at clip t=0).
    property double videoOffset: 0
    // Player position in seconds (VideoSyncPlayer.videoPositionSeconds).
    property double videoPositionSeconds: 0

    signal syncOffsetComputed(double offset)
    // A bulk profile apply finished (main.qml toasts the summary).
    signal profileApplied(string profileName, int clipCount)

    readonly property bool clipLoaded: videoClip !== null
    // UTC seconds since epoch from the clip's probed metadata; -1 if unavailable.
    readonly property double videoCreationTime: videoClip ? videoClip.creationTimeSecs : -1
    readonly property bool metadataAvailable: videoCreationTime >= 0
    readonly property bool diveAvailable: dive !== null

    // Precise dive-time of the current video frame; drives the editable
    // "Dive time at this frame" field. While paused it is static, so typing
    // in the field isn't overwritten.
    readonly property double exactDiveTime: videoPositionSeconds + videoOffset

    // Reactive mirror of config.cameraPairingNames().
    property var profileNames: []

    // Bulk-apply counts, all refreshed together by refreshBulkCounts() off
    // clipsChanged + countChanged: the model emits clipsChanged for
    // syncSource flips, enable toggles and probe completion too (sync paths
    // write the offset first, flip the source second — without the second
    // emission these counts would latch the pre-flip state).
    //   unsyncedCount   — clips the unsynced-only apply touches
    //   applicableCount — clips the apply-to-all touches (enabled + dated)
    //   datedCount      — clips with a usable creation time, enabled or not
    //                     (only used to explain a disabled button)
    property int unsyncedCount: 0
    property int applicableCount: 0
    property int datedCount: 0

    spacing: 8

    function formatTime(totalSeconds) {
        var s = Math.max(0, Math.floor(totalSeconds))
        var m = Math.floor(s / 60)
        var ss = s % 60
        return m + ":" + (ss < 10 ? "0" : "") + ss
    }

    function parseDiveTime(text) {
        text = text.trim()
        if (text === "") return -1
        if (/^\d+$/.test(text)) return parseInt(text, 10)
        var parts = text.split(":")
        if (parts.length === 2) {
            var m = parseInt(parts[0], 10), s = parseInt(parts[1], 10)
            if (isNaN(m) || isNaN(s) || s >= 60) return -1
            return m * 60 + s
        }
        if (parts.length === 3) {
            var h = parseInt(parts[0], 10), m2 = parseInt(parts[1], 10), s2 = parseInt(parts[2], 10)
            if (isNaN(h) || isNaN(m2) || isNaN(s2) || m2 >= 60 || s2 >= 60) return -1
            return h * 3600 + m2 * 60 + s2
        }
        return -1
    }

    // Camera-profile math, shared by the single apply, the suggestion label
    // and the bulk apply — one function so the three sites cannot drift.
    // diveStartSecs here is EPOCH seconds (camera pairing), not the
    // seconds-of-day used by the timecode auto-sync — don't mix them.
    function profileSuggestedOffset(creationTimeSecs, calibrationConstant) {
        var diveStartSecs = root.dive.startTime.getTime() / 1000.0
        return (creationTimeSecs - diveStartSecs) + calibrationConstant
    }

    // Clips the bulk apply targets: not yet synced, with a usable creation
    // time (the profile math needs it).
    function unsyncedClips() {
        var result = []
        // The clipList guard matters at teardown: context properties die
        // before the QML tree, and the clip model's countChanged fires a
        // last refresh into this function.
        if (!root.diveAvailable || !clipList)
            return result
        var clips = clipList.clipsInDiveOrder(false)
        for (var i = 0; i < clips.length; i++) {
            if (clips[i].syncSource === VideoClip.Unsynced
                    && clips[i].creationTimeSecs >= 0)
                result.push(clips[i])
        }
        return result
    }

    // Clips the apply-to-all targets: every ENABLED clip with a usable
    // creation time, whatever its current sync (timecode, restored, manual
    // or an earlier profile) — those syncs are overwritten.
    function profileApplicableClips() {
        var result = []
        if (!root.diveAvailable || !clipList)  // teardown guard, see above
            return result
        var clips = clipList.clipsInDiveOrder(true)
        for (var i = 0; i < clips.length; i++) {
            if (clips[i].creationTimeSecs >= 0)
                result.push(clips[i])
        }
        return result
    }

    function refreshBulkCounts() {
        unsyncedCount = unsyncedClips().length
        applicableCount = profileApplicableClips().length
        var dated = 0
        if (root.diveAvailable && clipList) {
            var clips = clipList.clipsInDiveOrder(false)
            for (var i = 0; i < clips.length; i++) {
                if (clips[i].creationTimeSecs >= 0)
                    dated++
            }
        }
        datedCount = dated
    }

    // Shared by both bulk buttons: same math and same clip-direct
    // CameraProfile attribution as the single apply. The target list is
    // snapshotted before the loop — each syncOffset write re-sorts the
    // model's dive order.
    function applyProfileToClips(clips) {
        var name = profileComboBox.currentText
        var constant = config.cameraCalibrationConstant(name)
        for (var i = 0; i < clips.length; i++) {
            clips[i].syncOffset = root.profileSuggestedOffset(
                                      clips[i].creationTimeSecs, constant)
            clips[i].syncSource = VideoClip.CameraProfile
        }
        root.refreshBulkCounts()
        if (clips.length > 0)
            root.profileApplied(name, clips.length)
    }

    // Why a bulk button is disabled — a grey button with no explanation is
    // the bug this exists for: timecode-auto-synced and restored clips are
    // not Unsynced, so the unsynced-only apply greys out silently.
    function noProfileReason() {
        return qsTr("No saved camera profile — sync a clip by hand, then save its camera below.")
    }
    function noDatedClipReason() {
        return clipList && clipList.count === 0
                ? qsTr("No video clips imported.")
                : qsTr("No clip has a usable creation time (the camera metadata is missing, or the clip is still being probed).")
    }
    function applyAllDisabledReason() {
        if (profileComboBox.count === 0) return noProfileReason()
        if (root.datedCount === 0) return noDatedClipReason()
        return qsTr("Every clip with a usable creation time is disabled — enable a clip to apply the profile to it.")
    }
    function applyUnsyncedDisabledReason() {
        if (profileComboBox.count === 0) return noProfileReason()
        if (root.datedCount === 0) return noDatedClipReason()
        return qsTr("Every clip with a usable creation time already carries a sync (timecode, restored session, camera profile or manual) — use \"Apply Profile to All Clips\" to overwrite them.")
    }

    Component.onCompleted: {
        root.profileNames = config.cameraPairingNames()
        refreshBulkCounts()
    }

    onDiveChanged: refreshBulkCounts()

    Connections {
        target: config
        function onCameraPairingsChanged() {
            root.profileNames = config.cameraPairingNames()
        }
    }

    Connections {
        target: clipList
        function onClipsChanged() { root.refreshBulkCounts() }
        function onCountChanged() { root.refreshBulkCounts() }
    }

    // ── Section A: Use Saved Profile ──────────────────────────────────────
    ColumnLayout {
        Layout.fillWidth: true
        // Visible as soon as profiles + a dive exist: the bulk apply is
        // useful even while the ACTIVE clip has no creation time (other
        // clips may have one) — the single apply gates on it instead.
        visible: root.profileNames.length > 0 && root.diveAvailable
        spacing: 4

        Label { text: qsTr("Apply Saved Camera Profile"); font.bold: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ComboBox {
                id: profileComboBox
                Layout.fillWidth: true
                model: root.profileNames
            }

            Button {
                text: qsTr("Apply Profile")
                enabled: profileComboBox.count > 0 && root.videoClip !== null
                         && root.metadataAvailable
                onClicked: {
                    var constant = config.cameraCalibrationConstant(
                                       profileComboBox.currentText)
                    // Through the clip, not the Timeline facade: a facade
                    // videoOffset write means "manual sync", this one is
                    // attributed to the camera profile.
                    root.videoClip.syncOffset =
                            root.profileSuggestedOffset(root.videoCreationTime, constant)
                    root.videoClip.syncSource = VideoClip.CameraProfile
                }
            }
        }

        Label {
            visible: profileComboBox.count > 0 && root.metadataAvailable
                     && root.diveAvailable
            color: palette.placeholderText
            font.pixelSize: 11
            text: {
                if (profileComboBox.count === 0 || !root.metadataAvailable || !root.diveAvailable)
                    return ""
                var constant = config.cameraCalibrationConstant(profileComboBox.currentText)
                var suggested = root.profileSuggestedOffset(root.videoCreationTime, constant)
                return qsTr("Suggested offset: %1 s").arg(suggested.toFixed(1))
            }
        }

        // Bulk applies. Stacked full-width (the panel can be 280 px narrow).
        // Each Button sits in a wrapper Item whose HoverHandler drives the
        // tooltip: a disabled Control receives no hover events, so its own
        // hovered/ToolTip would never show — and the disabled state is
        // exactly where the explanation is needed.

        // Apply to ALL enabled dated clips, overwriting existing syncs
        // (follow-up B). No confirmation by design; the toast reports it.
        Item {
            Layout.fillWidth: true
            implicitHeight: applyAllButton.implicitHeight
            implicitWidth: applyAllButton.implicitWidth

            Button {
                id: applyAllButton
                anchors.fill: parent
                text: root.applicableCount > 0
                      ? qsTr("Apply Profile to All Clips (%1)").arg(root.applicableCount)
                      : qsTr("Apply Profile to All Clips")
                enabled: profileComboBox.count > 0 && root.applicableCount > 0
                onClicked: root.applyProfileToClips(root.profileApplicableClips())
            }

            HoverHandler { id: applyAllHover }
            ToolTip.visible: applyAllHover.hovered
            ToolTip.delay: 500
            ToolTip.text: applyAllButton.enabled
                          ? qsTr("Syncs every enabled clip that has a creation time, overwriting existing syncs (including manual and timecode ones).")
                          : root.applyAllDisabledReason()
        }

        // Unsynced-only apply (v0.4 plan): fills in clips that are still
        // unsynced and carry a creation time; existing syncs are kept.
        Item {
            Layout.fillWidth: true
            implicitHeight: applyUnsyncedButton.implicitHeight
            implicitWidth: applyUnsyncedButton.implicitWidth

            Button {
                id: applyUnsyncedButton
                anchors.fill: parent
                text: root.unsyncedCount > 0
                      ? qsTr("Apply Profile to All Unsynced Clips (%1)").arg(root.unsyncedCount)
                      : qsTr("Apply Profile to All Unsynced Clips")
                enabled: profileComboBox.count > 0 && root.unsyncedCount > 0
                onClicked: root.applyProfileToClips(root.unsyncedClips())
            }

            HoverHandler { id: applyUnsyncedHover }
            ToolTip.visible: applyUnsyncedHover.hovered
            ToolTip.delay: 500
            ToolTip.text: applyUnsyncedButton.enabled
                          ? qsTr("Syncs only clips that have no sync yet; existing syncs are kept.")
                          : root.applyUnsyncedDisabledReason()
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: palette.mid }
    }

    // ── No-metadata notice ────────────────────────────────────────────────
    Label {
        visible: !root.metadataAvailable && root.clipLoaded
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: palette.placeholderText
        font.pixelSize: 11
        text: qsTr("Video creation time not available — camera profiles cannot be used or saved for this file.")
    }

    // ── Graphical sync ────────────────────────────────────────────────────
    Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: !root.clipLoaded
              ? qsTr("No video loaded.")
              : qsTr("Pause when your dive computer is clearly visible, then drag the video band on the timeline (or nudge below) until the overlay's data matches the display on your computer. The red cursor follows the video.")
    }

    // Two rows, not one: the panel can be as narrow as 280 px, and a single
    // label+field+nudge row's fixed minimum (~450 px) would inflate the
    // whole section's layout width past the panel, clipping its right edge.
    RowLayout {
        Layout.fillWidth: true
        visible: root.clipLoaded
        spacing: 8

        Label { text: qsTr("Dive time at this frame:") }

        TextField {
            id: diveTimeField
            placeholderText: qsTr("MM:SS  (e.g. 12:15)")
            Layout.fillWidth: true
            implicitWidth: 120
            // Live readout of the current frame's dive-time. Editing
            // breaks this binding (user value wins); committing restores
            // it. While paused exactDiveTime is static, so typing isn't
            // overwritten.
            text: root.formatTime(root.exactDiveTime)
            onEditingFinished: {
                // Only commit when the user actually changed the value —
                // a bare focus-loss must not re-apply the floored readout
                // (which would shave the sub-second fraction off the offset).
                if (text !== root.formatTime(root.exactDiveTime)) {
                    var secs = root.parseDiveTime(text)
                    if (secs >= 0)
                        root.syncOffsetComputed(secs - root.videoPositionSeconds)
                }
                // Re-establish the live readout binding.
                text = Qt.binding(function() { return root.formatTime(root.exactDiveTime) })
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: root.clipLoaded
        spacing: 8

        Label { text: qsTr("Nudge:") }

        Button {
            text: qsTr("-1s")
            enabled: root.clipLoaded
            onClicked: root.syncOffsetComputed(root.videoOffset - 1)
        }

        Button {
            text: qsTr("+1s")
            enabled: root.clipLoaded
            onClicked: root.syncOffsetComputed(root.videoOffset + 1)
        }

        Item { Layout.fillWidth: true }
    }

    // ── Section B: Save Profile ───────────────────────────────────────────
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.metadataAvailable && root.diveAvailable && root.clipLoaded
        spacing: 4

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: palette.mid }

        Label { text: qsTr("Save Camera Profile"); font.bold: true }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: 11
            color: palette.placeholderText
            text: qsTr("Give this camera a name to remember its clock offset. Future dives with the same camera will sync automatically.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                id: cameraNameField
                Layout.fillWidth: true
                placeholderText: qsTr("Camera name (e.g. GoPro Hero 12)")
            }

            Button {
                text: qsTr("Save Profile")
                enabled: cameraNameField.text.trim().length > 0
                onClicked: {
                    var savedName = cameraNameField.text.trim()
                    var diveStartSecs = root.dive.startTime.getTime() / 1000.0
                    // The current live offset is the sync result.
                    var constant = root.videoOffset
                                   - (root.videoCreationTime - diveStartSecs)
                    // addOrUpdateCameraPairing emits cameraPairingsChanged
                    // synchronously, so root.profileNames and profileComboBox.model
                    // are already updated on the next line
                    config.addOrUpdateCameraPairing(savedName, constant)
                    cameraNameField.text = ""
                    // Select the saved profile in the ComboBox
                    var idx = root.profileNames.indexOf(savedName)
                    if (idx >= 0)
                        profileComboBox.currentIndex = idx
                }
            }
        }
    }
}
