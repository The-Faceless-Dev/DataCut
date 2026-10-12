import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtMultimedia
import QtQuick.Effects

ApplicationWindow {
    id: window
    width: 1672
    height: 941
    minimumWidth: 1150
    minimumHeight: 700
    visible: false
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowSystemMenuHint
        | Qt.WindowMinMaxButtonsHint
    title: "DataCut — " + editor.projectName + (editor.dirty ? " *" : "")
    color: backgroundColor
    readonly property bool maximized: windowControls.maximized
    readonly property bool normalWindow: !windowControls.maximized
        && !windowControls.minimized && visibility !== Window.FullScreen

    readonly property color backgroundColor: "#0c141d"
    readonly property color panelColor: "#121e2a"
    readonly property color panelRaised: "#182635"
    readonly property color controlColor: "#1b2a39"
    readonly property color borderColor: "#304355"
    readonly property color textColor: "#e8eff7"
    readonly property color mutedColor: "#a0b3c7"
    readonly property color dimColor: "#70869c"
    readonly property color accentColor: "#2489f5"
    readonly property color accentSoft: "#173653"
    property string exportRequestKind: ""
    property int exportRequestFrame: 0
    function chooseExportFile(kind, frame) {
        exportRequestKind = kind
        exportRequestFrame = frame === undefined ? editor.playheadFrame : frame
        exportFileDialog.open()
    }
    function jobTime(seconds) {
        const total = Math.max(0, Math.floor(seconds))
        const hours = Math.floor(total / 3600)
        const minutes = Math.floor(total / 60) % 60
        const rest = total % 60
        return hours > 0 ? hours + ":" + String(minutes).padStart(2, "0")
            + ":" + String(rest).padStart(2, "0")
            : minutes + ":" + String(rest).padStart(2, "0")
    }
    property real pixelsPerFrame: 0.5
    property real previewZoom: 1
    property real previewPanX: 0
    property real previewPanY: 0
    property bool previewFullscreen: false
    property bool fullscreenControlsVisible: true
    property real previewPreviousZoom: 1
    property real previewPreviousPanX: 0
    property real previewPreviousPanY: 0
    function enterPreviewFullscreen() {
        if (previewFullscreen) return
        previewPreviousZoom = previewZoom
        previewPreviousPanX = previewPanX
        previewPreviousPanY = previewPanY
        previewZoom = 1
        previewPanX = 0
        previewPanY = 0
        previewFullscreen = true
        fullscreenControlsVisible = true
        fullScreenViewerWindow.showFullScreen()
        fullScreenViewerWindow.requestActivate()
        fullscreenIdleTimer.restart()
    }
    function exitPreviewFullscreen() {
        if (!previewFullscreen) return
        fullscreenIdleTimer.stop()
        previewFullscreen = false
        fullScreenViewerWindow.hide()
        window.requestActivate()
        previewZoom = previewPreviousZoom
        previewPanX = previewPreviousPanX
        previewPanY = previewPreviousPanY
        Qt.callLater(clampPreviewPan)
    }
    function togglePreviewPlayback() {
        if (editor.timelineDurationFrames > 0) editor.toggleTimelinePlayback()
        else if (player.playbackState === MediaPlayer.PlayingState) player.pause()
        else player.play()
    }
    property int libraryMode: 0
    property int generationTab: 0
    property int settingsPage: 0
    property int generativeSection: 0
    property bool captionGenerationExpanded: true
    property string captionStyleSearch: ""
    property string captionStyleCategory: "All"
    property int captionSubtab: 0
    property bool voiceGenerationExpanded: false
    function openGenerationSettings(section) {
        settingsPage = 1
        generativeSection = section
        generativeSettingsDialog.open()
    }
    property string generationReferenceImage: ""
    property var generationResults: []
    property string filesGenerationKind: ""
    property var filesGenerationResults: []
    property string selectedVoiceId: ""
    readonly property var selectedVoice: voiceGenerations.voices.find(function(v) { return v.id === selectedVoiceId })
    function previewSelectedVoice() {
        if (!selectedVoice) return
        if (voiceReferencePlayer.playbackState === MediaPlayer.PlayingState) {
            voiceReferencePlayer.stop()
            return
        }
        voiceReferencePlayer.previewStartMs = Math.max(0, Number(voiceReferenceStart.text) * 1000)
        voiceReferencePlayer.previewEndMs = voiceReferencePlayer.previewStartMs
            + Math.max(1, Number(voiceReferenceDuration.text)) * 1000
        voiceReferencePlayer.previewPending = true
        voiceReferencePlayer.source = selectedVoice.audioUrl
        voiceReferencePlayer.startPreviewIfReady()
    }
    MediaPlayer {
        id: voiceReferencePlayer
        property bool previewPending: false
        property real previewStartMs: 0
        property real previewEndMs: 10000
        audioOutput: AudioOutput { volume: 0.7 }
        function startPreviewIfReady() {
            if (!previewPending) return
            if (mediaStatus === MediaPlayer.LoadedMedia || mediaStatus === MediaPlayer.BufferedMedia
                || mediaStatus === MediaPlayer.EndOfMedia) {
                previewPending = false
                position = previewStartMs
                play()
            }
        }
        onMediaStatusChanged: startPreviewIfReady()
        onPositionChanged: if (playbackState === MediaPlayer.PlayingState && position >= previewEndMs) stop()
    }
    property url newVoiceAudio: ""
    property url newVoiceImage: ""
    onGenerationTabChanged: generationResults = projectGenerationResults(generationKind)
    readonly property string generationKind: ["image", "video", "music", "sound"][generationTab]
    readonly property var generationProvider: mediaGenerations.providers[generationTab]
    property var selectedTimelineKeyframe: null
    function hasVisualKeyframe(points, frame) {
        return (points || []).some(function(point) { return Number(point.frame) === Number(frame) })
    }
    function toggleVisualKeyframe(kind, id, property, points, startFrame) {
        const frame = editor.playheadFrame
        if (hasVisualKeyframe(points, kind === "caption" ? frame : frame - startFrame))
            editor.removeVisualPropertyKeyframe(kind, id, property, frame)
        else editor.setVisualPropertyKeyframe(kind, id, property, frame)
    }
    function deleteSelectedTimelineKeyframe() {
        const point = selectedTimelineKeyframe
        if (!point) return false
        if (point.kind === "effect") {
            if (point.parameterId)
                editor.removeTrackEffectParameterKeyframe(point.track, point.effectId, point.parameterId, point.frame)
            else editor.removeTrackEffectKeyframe(point.track, point.effectId, point.frame)
        } else if (point.kind === "gain")
            editor.removeAudioGainPoint(point.id, point.frame)
        else editor.removeVisualPropertyKeyframe(point.kind, point.id, point.property, point.frame)
        selectedTimelineKeyframe = null
        return true
    }
    onLibraryModeChanged: {
        editor.cancelEffectPreview()
        if (libraryMode !== 1) {
            if (collectionInspectorActive) collectionInspector.commitDetails()
            collectionInspectorActive = false
        } else if (selectedCollectionTileId !== "" || openedCollectionId !== "" || selectedSegmentIds.length > 0)
            collectionInspectorActive = true
    }
    property string pendingProjectAction: ""
    property string searchQuery: ""
    property string openedCollectionId: ""
    property string selectedCollectionTileId: ""
    property var selectedSegmentIds: []
    property bool collectionInspectorActive: false
    property var segmentClipboard: []
    property bool segmentClipboardCut: false
    property int segmentSelectionAnchor: -1
    readonly property string activeCollectionId: editor.collections.some(function(c) { return c.id === openedCollectionId })
        ? openedCollectionId : editor.collections.some(function(c) { return c.id === selectedCollectionTileId })
            ? selectedCollectionTileId : (editor.collections.length ? editor.collections[0].id : "")
    readonly property var inspectedCollection: editor.collections.find(function(c) {
        return c.id === (openedCollectionId !== "" ? openedCollectionId : selectedCollectionTileId)
    })
    readonly property var inspectedSegment: selectedSegmentIds.length === 1
        ? editor.segments.find(function(s) { return s.id === selectedSegmentIds[0] }) : null
    readonly property bool collectionTextEditing: collectionDetailName.activeFocus
        || segmentDetailLabel.activeFocus || segmentDetailTags.activeFocus
        || segmentDetailNotes.activeFocus || segmentDetailCaption.activeFocus
    property int inspectorTab: 8
    onInspectorTabChanged: Qt.callLater(revealInspectorTab)
    onInspectorTabOptionsChanged: Qt.callLater(revealInspectorTab)
    function revealInspectorTab() {
        const position = inspectorTabOptions.findIndex(function(tab) { return tab.id === inspectorTab })
        const tab = inspectorTabRepeater.itemAt(position)
        if (!tab || inspectorTabViewport.width <= 0) return
        const left = inspectorTabViewport.contentX
        const right = left + inspectorTabViewport.width
        const maxX = Math.max(0, inspectorTabViewport.contentWidth - inspectorTabViewport.width)
        if (tab.x < left)
            inspectorTabViewport.contentX = Math.max(0, tab.x - 8)
        else if (tab.x + tab.width > right)
            inspectorTabViewport.contentX = Math.min(maxX, tab.x + tab.width - inspectorTabViewport.width + 8)
    }
    property int selectedTrack: 0
    readonly property var visibleTracks: editor.tracks
    property bool trackSelectionExplicit: false
    property int selectedEffectTrack: activeClip && activeClip.hasVideo ? activeClip.track
        : activeTitle ? activeTitle.track : activeCaptionRow ? activeCaptionRow.trackId : 0
    property string selectedEffectId: ""
    property string selectedTransitionDefinitionId: ""
    readonly property int trackRowHeight: 52
    readonly property int newTrackDropHeight: 24
    readonly property int trackRowsY: 36 + newTrackDropHeight
    property int draggedTrackId: -1
    property int draggedTrackTargetRow: -1
    property real draggedTrackPointerY: -1
    property bool snappingEnabled: true
    property real viewportSnapGuideX: -1
    property real viewportSnapGuideY: -1
    property int snapGuideFrame: -1
    property real previewVolume: 0.5
    property bool previewMuted: false
    readonly property int snapToleranceFrames: Math.max(1, Math.round(8 / pixelsPerFrame))
    property string activeVideoClipId: ""
    property string activeAudio1ClipId: ""
    property string activeAudio2ClipId: ""
    property string timelineVisualMode: "Filmstrip"
    property string draggedAssetId: ""
    property string draggedSegmentId: ""
    property string draggedEffectId: ""
    property string draggedTransitionId: ""
    property bool draggedTextObject: false
    property string draggedAssetName: ""
    property real draggedAssetX: 0
    property real draggedAssetY: 0
    readonly property var draggedTimelineTarget: draggedAssetId !== "" || draggedSegmentId !== "" || draggedTextObject
        || draggedEffectId !== "" || draggedTransitionId !== ""
        ? timelineDropPosition(window.contentItem, draggedAssetX, draggedAssetY)
        : { valid: false }
    readonly property var draggedTransitionTarget: draggedTransitionId !== ""
        ? transitionDropPosition(window.contentItem, draggedAssetX, draggedAssetY) : ({})
    readonly property var draggedClipPlacement: {
        if (!draggedTimelineTarget.valid || draggedEffectId !== "" || draggedTransitionId !== "") return { start: 0, guide: -1 }
        if (draggedTextObject) return editor.previewNewTitleSnap(draggedTimelineTarget.frame,
            draggedTimelineTarget.track, snappingEnabled, snapToleranceFrames)
        if (!snappingEnabled) return { start: draggedTimelineTarget.frame, guide: -1 }
        return editor.previewNewClipSnap(draggedSegmentId !== "" ? draggedSegmentId : draggedAssetId,
            draggedSegmentId !== "", draggedTimelineTarget.frame, draggedTimelineTarget.track,
            snapToleranceFrames)
    }
    readonly property bool draggedTargetCompatible: {
        if (draggedTextObject) return draggedTimelineTarget.valid &&
            (editor.isVideoTrack(draggedTimelineTarget.track) || draggedTimelineTarget.track === -1)
        if (draggedTransitionId !== "") return !!draggedTransitionTarget.valid
        if (!draggedTimelineTarget.valid) return false
        if (draggedEffectId !== "") return (draggedTimelineTarget.track === -1
            && editor.tracks.filter(function(row) { return row.kind === "video" }).length < 30)
            || editor.isVideoTrack(draggedTimelineTarget.track)
        const source = draggedSegmentId !== ""
            ? editor.segments.find(function(segment) { return segment.id === draggedSegmentId })
            : editor.assets.find(function(asset) { return asset.id === draggedAssetId })
        if (!source) return false
        return source.hasVideo
            ? ((draggedTimelineTarget.track === -1
                && editor.tracks.filter(function(row) { return row.kind === "video" }).length < 30)
                || editor.isVideoTrack(draggedTimelineTarget.track))
            : ((draggedTimelineTarget.track === -2
                && editor.tracks.filter(function(row) { return row.kind === "audio" }).length < 30)
                || editor.isAudioTrack(draggedTimelineTarget.track))
    }
    readonly property var activeClip: editor.clips.find(function(clip) { return clip.id === editor.selectedClipId })
    readonly property var activeClipVisual: activeClip ? editor.clipVisualAt(activeClip.id, editor.playheadFrame) : ({})
    readonly property int selectedClipGain: activeClip && activeClip.hasAudio
        ? editor.clipGainAt(activeClip.id, editor.playheadFrame) : 100
    readonly property var activeTitle: editor.titles.find(function(title) { return title.id === editor.selectedTitleId })
    readonly property var activeTitleVisual: activeTitle ? editor.titleVisualAt(activeTitle.id, editor.playheadFrame) : ({})
    readonly property var activeCaptionRow: editor.captionRows.find(function(row) { return row.id === editor.selectedCaptionRowId })
    readonly property var activeCaptionVisual: activeCaptionRow ? editor.captionVisualAt(activeCaptionRow.id, editor.playheadFrame) : ({})
    readonly property var activeCaptionCue: editor.captionCues.find(function(cue) { return cue.id === editor.selectedCaptionId })
    onActiveCaptionCueChanged: {
        if (!captionEditor) return
        if (!activeCaptionCue || !activeCaptionCue.words.length) {
            captionEditor.insertAfterWordId = ""
        } else if (!activeCaptionCue.words.some(function(word) { return word.id === captionEditor.insertAfterWordId })) {
            captionEditor.insertAfterWordId = activeCaptionCue.words[activeCaptionCue.words.length - 1].id
        }
    }
    readonly property var activeTransition: editor.transitions.find(function(item) {
        return item.id === editor.selectedTransitionId
    })
    readonly property var activeTransitionDefinition: activeTransition
        ? editor.transitionDefinitions.find(function(item) { return item.id === activeTransition.definitionId }) : null
    readonly property var inspectorTabOptions: {
        if (activeCaptionRow) return [{ id: 6, label: "Captions" }, { id: 2, label: "Effects" }]
        if (activeTitle) return [{ id: 7, label: "Text" }, { id: 2, label: "Effects" }]
        if (activeClip) {
            if (!activeClip.hasVideo) return [{ id: 1, label: "Audio" }, { id: 5, label: "Generate" }]
            const tabs = [{ id: 0, label: "Video" }]
            if (activeClip.hasAudio) tabs.push({ id: 1, label: "Audio" })
            tabs.push({ id: 2, label: "Effects" })
            if (activeClip.hasAudio) tabs.push({ id: 5, label: "Generate" })
            return tabs
        }
        if (activeTransition) return [{ id: 4, label: "Transition" }]
        return [{ id: 8, label: "Project" }, { id: 2, label: "Effects" }]
    }
    readonly property var selectedAsset: editor.assets.find(function(asset) { return asset.id === editor.selectedAssetId })
    readonly property var activeTimelineTitle: editor.timelinePreviewEnabled
        ? editor.titles.find(function(title) {
            return editor.playheadFrame >= title.start && editor.playheadFrame < title.start + title.duration
        }) : null
    Component.onCompleted: {
        if (crashReporter.previousExitUnexpected) unexpectedExitDialog.open()
        else if (editor.recoveryAvailable && !editor.dirty) recoveryDialog.open()
    }
    Connections {
        target: editor
        function onProjectChanged() {
            generationResults = projectGenerationResults(generationKind)
            if (filesGenerationKind !== "") filesGenerationResults = projectGenerationResults(filesGenerationKind)
            if (openedCollectionId !== "" && !editor.collections.some(function(c) { return c.id === openedCollectionId }))
                openedCollectionId = ""
            if (selectedCollectionTileId !== "" && !editor.collections.some(function(c) { return c.id === selectedCollectionTileId }))
                selectedCollectionTileId = ""
            const remaining = selectedSegmentIds.filter(function(id) {
                return editor.segments.some(function(s) { return s.id === id })
            })
            if (remaining.length !== selectedSegmentIds.length) selectedSegmentIds = remaining
            const clip = editor.clips.find(function(item) { return item.id === editor.selectedClipId })
            if (clip && !trackSelectionExplicit) selectedTrack = clip.track
            else if (!editor.tracks.some(function(row) { return row.id === selectedTrack })) {
                selectedTrack = editor.tracks.length ? editor.tracks[0].id : 0
                trackSelectionExplicit = false
            }
            if (!editor.isVideoTrack(selectedEffectTrack)
                && !editor.captionRows.some(function(row) { return row.trackId === selectedEffectTrack })) {
                const row = editor.tracks.find(function(item) { return item.kind === "video" })
                selectedEffectTrack = row ? row.id : 0
            }
        }
        function onRecoveryChanged() {
            if (editor.recoveryAvailable && !editor.dirty) recoveryDialog.open()
        }
        function onSelectionChanged() {
            selectedTimelineKeyframe = null
            const clip = editor.clips.find(function(item) { return item.id === editor.selectedClipId })
            if (clip || editor.selectedTitleId !== "" || editor.selectedCaptionRowId !== "") {
                if (collectionInspectorActive) collectionInspector.commitDetails()
                collectionInspectorActive = false
                if (selectedSegmentIds.length > 0) selectedSegmentIds = []
            }
            if (clip) { selectedTrack = clip.track; trackSelectionExplicit = false }
            const title = editor.titles.find(function(item) { return item.id === editor.selectedTitleId })
            if (title) { selectedTrack = title.track; selectedEffectTrack = title.track; trackSelectionExplicit = false }
            if (clip && clip.hasVideo) selectedEffectTrack = clip.track
            if (editor.selectedCaptionRowId !== "") {
                const captionTrack = editor.captionRows.find(function(row) { return row.id === editor.selectedCaptionRowId })
                if (captionTrack) {
                    selectedTrack = captionTrack.trackId
                    selectedEffectTrack = captionTrack.trackId
                    trackSelectionExplicit = true
                }
                inspectorTab = 6
            }
            else if (clip && !clip.hasVideo && clip.hasAudio) inspectorTab = 1
            else if (!inspectorTabOptions.some(function(tab) { return tab.id === inspectorTab }))
                inspectorTab = inspectorTabOptions[0].id
        }
    }

    function saveCaptionStyle(changes) {
        if (!activeCaptionRow) return
        const style = Object.assign({}, activeCaptionRow, changes || {})
        editor.updateCaptionStyle(style.id, style.fontFamily, Number(style.fontPixels),
            style.bold, style.italic, style.color, style.highlightColor,
            style.backgroundColor, Number(style.backgroundOpacity),
            Number(style.positionX), Number(style.positionY))
    }
    ColorDialog {
        id: captionColorDialog
        property string role: "color"
        title: role === "color" ? "Caption text color" : role === "highlightColor"
            ? "Highlight color" : "Caption background color"
        onAccepted: {
            const value = selectedColor.toString()
            const change = {}
            change[role] = value
            saveCaptionStyle(change)
        }
    }
    function chooseCaptionColor(role, currentColor) {
        captionColorDialog.role = role
        captionColorDialog.selectedColor = currentColor
        captionColorDialog.open()
    }
    function saveTitleStyle() {
        if (!activeTitle) return
        editor.updateTitleStyle(activeTitle.id, titleFont.currentText, titleBold.checked,
            titleItalic.checked, titleBackground.text, Number(titleBackgroundOpacity.text),
            activeTitle.positionX, activeTitle.positionY,
            Number(titleAnchorX.text), Number(titleAnchorY.text))
    }
    function setTitlePosition(x, y) {
        if (!activeTitle) return
        const id = activeTitle.id
        const frame = editor.playheadFrame
        if (x !== activeTitleVisual.x) editor.setVisualPropertyValue("title", id, "x", frame, x)
        if (y !== activeTitleVisual.y) editor.setVisualPropertyValue("title", id, "y", frame, y)
    }
    function saveTitleContent() {
        if (!activeTitle) return
        editor.updateTitle(activeTitle.id, titleInspectorText.text, Number(titleStart.text),
            Number(titleDuration.text), Number(titleSize.text), titleColor.text)
    }
    ColorDialog {
        id: titleColorDialog
        property string role: "text"
        title: role === "text" ? "Text color" : "Text background color"
        onAccepted: {
            if (!activeTitle) return
            if (role === "text") editor.updateTitle(activeTitle.id, activeTitle.text,
                activeTitle.start, activeTitle.duration, activeTitle.fontPixels,
                selectedColor.toString())
            else editor.updateTitleStyle(activeTitle.id, activeTitle.fontFamily,
                activeTitle.bold, activeTitle.italic, selectedColor.toString(),
                activeTitle.backgroundOpacity, activeTitle.positionX, activeTitle.positionY,
                activeTitle.anchorX, activeTitle.anchorY)
        }
    }
    function chooseTitleColor(role, current) {
        titleColorDialog.role = role
        titleColorDialog.selectedColor = current
        titleColorDialog.open()
    }

    palette.window: panelColor
    palette.windowText: textColor
    palette.button: panelRaised
    palette.buttonText: textColor
    palette.base: panelRaised
    palette.text: textColor
    palette.highlight: accentColor
    palette.highlightedText: "white"

    function clockTime(milliseconds) {
        const total = Math.max(0, Math.floor(milliseconds / 1000))
        const hours = Math.floor(total / 3600)
        const minutes = Math.floor((total % 3600) / 60)
        const seconds = total % 60
        return (hours ? String(hours).padStart(2, "0") + ":" : "")
            + String(minutes).padStart(2, "0") + ":" + String(seconds).padStart(2, "0")
    }

    function requestProjectAction(action) {
        if (editor.dirty) {
            pendingProjectAction = action
            discardDialog.open()
        } else if (action === "new") editor.newProject()
        else if (action === "demo") editor.openDemoProject()
        else if (action === "legacy") openLegacyProjectDialog.open()
        else openDialog.open()
    }
    function openRecentProject(path) {
        if (editor.dirty) {
            pendingProjectAction = "recent:" + path
            discardDialog.open()
        } else editor.openProject(mediaGenerations.fileUrl(path))
    }

    function seekProjectFrame(frame) {
        editor.seekTimelineFrame(frame)
        syncTimelinePlayers(true)
    }
    function syncTimelinePlayers(forceSeek) {
        if (!editor.timelinePreviewEnabled) return
        for (let index = 0; index < videoLayerRepeater.count; ++index) {
            const layer = videoLayerRepeater.itemAt(index)
            if (layer) layer.sync(forceSeek)
        }
        syncTimelineAudio(audioPlayer1, editor.timelineAudio1, 1, forceSeek)
        syncTimelineAudio(audioPlayer2, editor.timelineAudio2, 2, forceSeek)
        for (let index = 0; index < extraAudioPlayers.count; ++index) {
            const target = extraAudioPlayers.objectAt(index)
            if (!target) continue
            syncExtraTimelineAudio(target, editor.timelineMediaForTrack(target.trackId), forceSeek)
        }
    }
    function syncExtraTimelineAudio(target, state, forceSeek) {
        if (target.activeClipId !== (state.id || "")) {
            target.activeClipId = state.id || ""
            forceSeek = true
        }
        if (state.id && target.seekable) {
            // Audio must run continuously. QMediaPlayer's reported position can lag
            // behind its actual audio output, so correcting small drift every frame
            // repeatedly discards decoded samples and makes the sound stutter.
            if (forceSeek || (!editor.timelinePlaying && Math.abs(target.position - state.sourceMs) > 30))
                target.position = state.sourceMs
            if (editor.timelinePlaying && target.playbackState !== MediaPlayer.PlayingState)
                target.play()
        } else if (target.playbackState === MediaPlayer.PlayingState) target.pause()
    }
    function syncTimelineAudio(target, state, track, forceSeek) {
        const previousId = track === 1 ? activeAudio1ClipId : activeAudio2ClipId
        if (state.id !== previousId) {
            if (track === 1) activeAudio1ClipId = state.id || ""
            else activeAudio2ClipId = state.id || ""
            forceSeek = true
        }
        if (state.id && target.seekable) {
            if (forceSeek || (!editor.timelinePlaying && Math.abs(target.position - state.sourceMs) > 30))
                target.position = state.sourceMs
            if (editor.timelinePlaying && target.playbackState !== MediaPlayer.PlayingState)
                target.play()
        } else if (target.playbackState === MediaPlayer.PlayingState) target.pause()
    }
    function timelineDropPosition(sourceItem, x, y) {
        const point = timeline.mapFromItem(sourceItem, x, y)
        if (point.x < 0 || point.x >= timeline.width || point.y < 36 || point.y >= timeline.height)
            return { valid: false }
        const contentY = point.y + timeline.contentY
        let track = -99
        if (contentY < trackRowsY) track = -1
        else if (contentY >= trackRowsY + editor.tracks.length * trackRowHeight)
            track = -2
        else {
            const row = Math.floor((contentY - trackRowsY) / trackRowHeight)
            track = editor.tracks[row].id
        }
        return {
            valid: true,
            frame: Math.max(0, Math.round((point.x + timeline.contentX) / pixelsPerFrame)),
            track: track
        }
    }
    function transitionDropPosition(sourceItem, x, y) {
        const viewport = timeline.mapFromItem(sourceItem, x, y)
        if (viewport.x < 0 || viewport.x >= timeline.width
            || viewport.y < 0 || viewport.y >= timeline.height) return {valid: false}
        const content = timelineClips.mapFromItem(sourceItem, x, y)
        const row = Math.floor(content.y / trackRowHeight)
        if (row < 0 || row >= editor.tracks.length) return {valid: false}
        const track = editor.tracks[row]
        if (track.kind !== "video") return {valid: false}
        return editor.transitionTargetAt(track.id,
            Math.max(0, Math.round(content.x / pixelsPerFrame)),
            Math.max(snapToleranceFrames, Math.round(14 / pixelsPerFrame)))
    }
    function rowForClipY(y) {
        if (y < 0) return -1
        const row = Math.floor(y / trackRowHeight)
        return row >= editor.tracks.length ? -2 : editor.tracks[row].id
    }
    function trackName(track) {
        const row = visibleTracks.find(function(item) { return item.id === track })
        return row ? row.name : "V1"
    }
    function visibleTrackRow(track) {
        return visibleTracks.findIndex(function(item) { return item.id === track })
    }
    function updateDraggedTrackTarget(pointerY) {
        draggedTrackPointerY = pointerY
        draggedTrackTargetRow = Math.max(0, Math.min(visibleTracks.length - 1,
            Math.floor((pointerY + timeline.contentY - trackRowsY) / trackRowHeight)))
    }
    function openCollection(id) {
        collectionInspector.commitDetails()
        openedCollectionId = id
        selectedCollectionTileId = id
        selectedSegmentIds = []
        segmentSelectionAnchor = -1
        collectionInspectorActive = true
    }
    function selectSegmentTile(id, index, modifiers) {
        collectionInspector.commitDetails()
        const rows = editor.segments.filter(function(s) { return s.collectionId === openedCollectionId })
        if (modifiers & Qt.ShiftModifier && segmentSelectionAnchor >= 0) {
            const first = Math.min(segmentSelectionAnchor, index)
            const last = Math.max(segmentSelectionAnchor, index)
            const range = rows.slice(first, last + 1).map(function(s) { return s.id })
            selectedSegmentIds = modifiers & Qt.ControlModifier
                ? Array.from(new Set(selectedSegmentIds.concat(range))) : range
        } else if (modifiers & Qt.ControlModifier) {
            selectedSegmentIds = selectedSegmentIds.includes(id)
                ? selectedSegmentIds.filter(function(item) { return item !== id })
                : selectedSegmentIds.concat([id])
            segmentSelectionAnchor = index
        } else {
            selectedSegmentIds = [id]
            segmentSelectionAnchor = index
        }
        if (selectedSegmentIds.includes(id)) editor.selectSegment(id)
        collectionInspectorActive = true
    }
    function pasteSegmentsInto(collectionId) {
        if (segmentClipboard.length === 0 || collectionId === "") return
        collectionInspector.commitDetails()
        if (segmentClipboardCut) {
            editor.moveSegments(segmentClipboard, collectionId)
            segmentClipboard = []
            segmentClipboardCut = false
        } else editor.copySegments(segmentClipboard, collectionId)
        openedCollectionId = collectionId
        selectedCollectionTileId = collectionId
        selectedSegmentIds = []
    }
    function deleteCollectionSelection() {
        if (openedCollectionId !== "" && selectedSegmentIds.length > 0) {
            editor.removeSegments(selectedSegmentIds)
            selectedSegmentIds = []
        } else if (selectedCollectionTileId !== "") deleteCollectionDialog.open()
    }
    function moveAssetDrag(sourceItem, x, y) {
        const point = window.contentItem.mapFromItem(sourceItem, x, y)
        draggedAssetX = point.x
        draggedAssetY = point.y
        const timelinePoint = timeline.mapFromItem(sourceItem, x, y)
        autoScrollTimeline(timelinePoint.y)
    }
    function snapViewportMove(x, y, width, height) {
        const result = { x: x, y: y, guideX: -1, guideY: -1 }
        if (!snappingEnabled) return result
        const tolerance = 9
        const horizontal = [
            { distance: Math.abs(x), position: 0, guide: 0 },
            { distance: Math.abs(x + width / 2 - previewVisual.width / 2),
              position: (previewVisual.width - width) / 2, guide: previewVisual.width / 2 },
            { distance: Math.abs(x + width - previewVisual.width),
              position: previewVisual.width - width, guide: previewVisual.width }
        ].sort(function(a, b) { return a.distance - b.distance })[0]
        const vertical = [
            { distance: Math.abs(y), position: 0, guide: 0 },
            { distance: Math.abs(y + height / 2 - previewVisual.height / 2),
              position: (previewVisual.height - height) / 2, guide: previewVisual.height / 2 },
            { distance: Math.abs(y + height - previewVisual.height),
              position: previewVisual.height - height, guide: previewVisual.height }
        ].sort(function(a, b) { return a.distance - b.distance })[0]
        if (horizontal.distance <= tolerance) {
            result.x = horizontal.position
            result.guideX = horizontal.guide
        }
        if (vertical.distance <= tolerance) {
            result.y = vertical.position
            result.guideY = vertical.guide
        }
        return result
    }
    function snapViewportScale(anchorX, anchorY, width, height, fractionX, fractionY, factor) {
        const result = { factor: factor, guideX: -1, guideY: -1 }
        if (!snappingEnabled) return result
        const tolerance = 9
        // A centered object should fit both opposing canvas edges at once.
        // Prefer this over a single-edge snap when the two are nearly coincident.
        const fitHeight = previewVisual.height / height
        const fitCenterY = anchorY + height * fitHeight * (0.5 - fractionY)
        if (Math.abs(fitCenterY - previewVisual.height / 2) <= tolerance
                && Math.abs((factor - fitHeight) * height) <= tolerance) {
            result.factor = fitHeight
            result.guideY = previewVisual.height / 2
            return result
        }
        const fitWidth = previewVisual.width / width
        const fitCenterX = anchorX + width * fitWidth * (0.5 - fractionX)
        if (Math.abs(fitCenterX - previewVisual.width / 2) <= tolerance
                && Math.abs((factor - fitWidth) * width) <= tolerance) {
            result.factor = fitWidth
            result.guideX = previewVisual.width / 2
            return result
        }
        const candidates = []
        const add = function(target, divisor, guide, axis) {
            if (Math.abs(divisor) < 0.001) return
            const snapped = target / divisor
            if (snapped <= 0) return
            const distance = Math.abs((factor - snapped) * divisor)
            if (distance <= 9) candidates.push({factor: snapped, distance: distance, guide: guide, axis: axis})
        }
        add(anchorX, width * fractionX, 0, "x")
        add(previewVisual.width - anchorX, width * (1 - fractionX), previewVisual.width, "x")
        add(anchorY, height * fractionY, 0, "y")
        add(previewVisual.height - anchorY, height * (1 - fractionY), previewVisual.height, "y")
        candidates.sort(function(a, b) { return a.distance - b.distance })
        if (candidates.length) {
            result.factor = candidates[0].factor
            if (candidates[0].axis === "x") result.guideX = candidates[0].guide
            else result.guideY = candidates[0].guide
        }
        return result
    }
    function visibleHandleX(x, width) {
        return Math.max(8 - previewVisual.x,
            Math.min(previewVisual.parent.width - previewVisual.x - width - 8, x))
    }
    function visibleHandleY(y, height) {
        return Math.max(8 - previewVisual.y,
            Math.min(previewVisual.parent.height - previewVisual.y - height - 8, y))
    }
    function rotatedViewportBounds(x, y, width, height, degrees) {
        const radians = degrees * Math.PI / 180
        const rotatedWidth = Math.abs(width * Math.cos(radians)) + Math.abs(height * Math.sin(radians))
        const rotatedHeight = Math.abs(width * Math.sin(radians)) + Math.abs(height * Math.cos(radians))
        return { x: x + (width - rotatedWidth) / 2, y: y + (height - rotatedHeight) / 2,
            width: rotatedWidth, height: rotatedHeight }
    }
    function rotatedViewportCorner(x, y, width, height, degrees) {
        const radians = degrees * Math.PI / 180
        return { x: x + width / 2 + width * Math.cos(radians) / 2 - height * Math.sin(radians) / 2,
            y: y + height / 2 + width * Math.sin(radians) / 2 + height * Math.cos(radians) / 2 }
    }
    function autoScrollTimeline(viewportY) {
        if (viewportY > timeline.height - 20 && viewportY < timeline.height + 36)
            timeline.contentY = Math.min(timeline.contentHeight - timeline.height, timeline.contentY + 9)
        else if (viewportY >= 0 && viewportY < 54)
            timeline.contentY = Math.max(0, timeline.contentY - 9)
    }
    function setTimelineZoom(value) {
        pixelsPerFrame = Math.max(0.02, Math.min(24, value))
        if (timelineVisualMode === "Frame" && pixelsPerFrame < 6)
            timelineVisualMode = "Filmstrip"
    }
    function zoomTimelineAt(value, viewportX) {
        const frameAtPointer = (timeline.contentX + viewportX) / pixelsPerFrame
        setTimelineZoom(value)
        timeline.contentX = Math.max(0, Math.min(timeline.contentWidth - timeline.width,
            frameAtPointer * pixelsPerFrame - viewportX))
    }
    function clampPreviewPan() {
        const maxX = Math.max(0, (previewVisual.width * previewZoom - previewViewport.width) / 2 + 24)
        const maxY = Math.max(0, (previewVisual.height * previewZoom - previewViewport.height) / 2 + 24)
        previewPanX = Math.max(-maxX, Math.min(maxX, previewPanX))
        previewPanY = Math.max(-maxY, Math.min(maxY, previewPanY))
    }
    function canPanPreviewFrom(viewportX, viewportY) {
        if (previewZoom <= 1) return false
        const scaledWidth = previewVisual.width * previewZoom
        const scaledHeight = previewVisual.height * previewZoom
        const left = previewVisual.x + (previewVisual.width - scaledWidth) / 2
        const top = previewVisual.y + (previewVisual.height - scaledHeight) / 2
        return viewportX < left || viewportX > left + scaledWidth
            || viewportY < top || viewportY > top + scaledHeight
    }
    function zoomPreviewAt(value, viewportX, viewportY) {
        const next = Math.max(1, Math.min(8, value))
        const ratio = next / previewZoom
        previewPanX += (viewportX - previewViewport.width / 2 - previewPanX) * (1 - ratio)
        previewPanY += (viewportY - previewViewport.height / 2 - previewPanY) * (1 - ratio)
        previewZoom = next
        clampPreviewPan()
    }
    function resetPreviewZoom() {
        previewZoom = 1
        previewPanX = 0
        previewPanY = 0
    }
    function clearLibrarySelection() {
        if (libraryMode === 1) {
            collectionInspector.commitDetails()
            selectedSegmentIds = []
            selectedCollectionTileId = ""
            collectionInspectorActive = false
        } else if (libraryMode === 2) {
            selectedEffectId = ""
            editor.cancelEffectPreview()
        } else if (libraryMode === 3) {
            selectedTransitionDefinitionId = ""
        } else {
            editor.clearSelection()
        }
    }
    function emptyGridPoint(grid, x, y) {
        const point = grid.contentItem.mapFromItem(grid, x, y)
        return grid.indexAt(point.x, point.y) < 0
    }
    function previewClipPlacement(clip, x, y, cursorX) {
        const track = rowForClipY(y)
        const rawStart = Math.max(0, Math.round(x / pixelsPerFrame))
        return editor.previewClipSnap(clip.id, rawStart, track,
                                      snappingEnabled, snapToleranceFrames,
                                      Math.max(0, Math.round(cursorX / pixelsPerFrame)))
    }
    function saveCurrentProject() {
        if (editor.hasProjectDirectory) editor.saveProject("")
        else saveDialog.open()
    }
    function projectGenerationResults(kind) {
        const imported = new Set(editor.assets.map(function(asset) { return asset.url.toString() }))
        return mediaGenerations.results(kind).filter(function(result) {
            return imported.has(result.url.toString())
        })
    }
    function toggleMaximized() {
        windowControls.toggleMaximized()
    }
    function openProjectMenu() {
        const point = projectButton.mapToItem(window.contentItem, 0, projectButton.height)
        projectMenu.x = point.x
        projectMenu.y = point.y + 5
        projectMenu.open()
    }
    function openResolutionMenu() {
        const point = resolutionButton.mapToItem(window.contentItem, 0, resolutionButton.height)
        resolutionMenu.x = point.x
        resolutionMenu.y = point.y + 5
        resolutionMenu.open()
    }
    function openAspectMenu() {
        const point = aspectButton.mapToItem(window.contentItem, 0, aspectButton.height)
        aspectMenu.x = point.x
        aspectMenu.y = point.y + 5
        aspectMenu.open()
    }
    function openFrameRateMenu() {
        const point = frameRateButton.mapToItem(window.contentItem, 0, frameRateButton.height)
        frameRateMenu.x = point.x
        frameRateMenu.y = point.y + 5
        frameRateMenu.open()
    }
    function projectRateLabel() {
        const rate = editor.projectRate.replace(" FPS", "")
        if (rate === "24000/1001") return "23.976 FPS"
        if (rate === "30000/1001") return "29.97 FPS"
        return rate.split("/")[0] + " FPS"
    }

    component VideoEffectShader: ShaderEffect {
        property var source
        property var settings: ({})
        property var transitionMask: ({})
        property size sourceSize: Qt.size(width, height)
        property real transition_kind: transitionMask.kind || 0
        property real transition_progress: transitionMask.progress || 0
        property real transition_softness: transitionMask.softness || 0
        property real transition_complexity: transitionMask.complexity || 6
        property real transition_reveal: transitionMask.reveal ? 1 : 0
        property real saturation: settings.saturation || 0
        property real brightness: settings.brightness || 0
        property real blur: settings.blur || 0
        property real vignette: settings.vignette || 0
        property real contrast_enabled: settings.contrast_enabled || 0
        property real contrast_amount: settings.contrast_amount || 0
        property real contrast_pivot: settings.contrast_pivot || 0
        property real hue_shift_enabled: settings.hue_shift_enabled || 0
        property real hue_shift_angle: settings.hue_shift_angle || 0
        property real hue_shift_saturation: settings.hue_shift_saturation || 0
        property real temperature_enabled: settings.temperature_enabled || 0
        property real temperature_temperature: settings.temperature_temperature || 0
        property real temperature_tint: settings.temperature_tint || 0
        property real gamma_enabled: settings.gamma_enabled || 0
        property real gamma_gamma: settings.gamma_gamma || 0
        property real gamma_weight: settings.gamma_weight || 0
        property real sepia_enabled: settings.sepia_enabled || 0
        property real sepia_amount: settings.sepia_amount || 0
        property real sepia_warmth: settings.sepia_warmth || 0
        property real invert_enabled: settings.invert_enabled || 0
        property real invert_amount: settings.invert_amount || 0
        property real posterize_enabled: settings.posterize_enabled || 0
        property real posterize_levels: settings.posterize_levels || 0
        property real posterize_amount: settings.posterize_amount || 0
        property real threshold_enabled: settings.threshold_enabled || 0
        property real threshold_threshold: settings.threshold_threshold || 0
        property real threshold_softness: settings.threshold_softness || 0
        property real pixelate_enabled: settings.pixelate_enabled || 0
        property real pixelate_size: settings.pixelate_size || 0
        property real pixelate_amount: settings.pixelate_amount || 0
        property real sharpen_enabled: settings.sharpen_enabled || 0
        property real sharpen_amount: settings.sharpen_amount || 0
        property real sharpen_radius: settings.sharpen_radius || 0
        fragmentShader: "qrc:/shaders/render/shaders/video_effects.frag.qsb"
    }

    component TransitionEdgeLayer: Item {
        id: transitionEdgeLayer
        property var sourceState: ({})
        property var effectSettings: ({})
        property var transitionMask: ({})
        property real opacityOverride: -1
        property real canvasWidth: 1
        property real canvasHeight: 1
        readonly property real sourceRatio: sourceState.sourceWidth > 0 && sourceState.sourceHeight > 0
            ? sourceState.sourceWidth / sourceState.sourceHeight : canvasWidth / canvasHeight
        readonly property real fitWidth: Math.min(canvasWidth, canvasHeight * sourceRatio)
        readonly property real fitHeight: fitWidth / sourceRatio
        visible: !!sourceState.url
        width: sourceState.textObject
            ? (sourceState.imageWidth || 1) * canvasHeight / 1080 * ((sourceState.scalePercent || 100) / 100)
            : fitWidth * ((sourceState.scalePercent || 100) / 100)
        height: sourceState.textObject
            ? (sourceState.imageHeight || 1) * canvasHeight / 1080 * ((sourceState.scalePercent || 100) / 100)
            : fitHeight * ((sourceState.scalePercent || 100) / 100)
        x: sourceState.textObject
            ? canvasWidth * (sourceState.positionX || 0) / 100 - width * (sourceState.anchorX || 0) / 100
            : (canvasWidth - width) / 2 + (sourceState.positionX || 0) * canvasWidth / editor.canvasWidth
        y: sourceState.textObject
            ? canvasHeight * (sourceState.positionY || 0) / 100 - height * (sourceState.anchorY || 0) / 100
            : (canvasHeight - height) / 2 + (sourceState.positionY || 0) * canvasHeight / editor.canvasHeight
        rotation: sourceState.rotationDegrees || 0
        opacity: (opacityOverride >= 0 ? opacityOverride : (sourceState.opacity || 0))
            * (sourceState.clipOpacity === undefined ? 1 : sourceState.clipOpacity)
        layer.enabled: !!effectSettings.active || !!transitionMask.kind
        layer.effect: VideoEffectShader { settings: effectSettings; transitionMask: transitionEdgeLayer.transitionMask }
        MediaPlayer {
            id: heldEdgePlayer
            source: transitionEdgeLayer.sourceState.imageUrl ? ""
                : (transitionEdgeLayer.sourceState.url || "")
            videoOutput: heldEdgeOutput
            onMediaStatusChanged: {
                if (mediaStatus === MediaPlayer.LoadedMedia) {
                    position = transitionEdgeLayer.sourceState.sourceMs || 0
                    play(); pause()
                }
            }
            onSourceChanged: if (source.toString() !== "") position = transitionEdgeLayer.sourceState.sourceMs || 0
        }
        Connections {
            target: editor
            function onPlayheadChanged() {
                if (!heldEdgePlayer.source.toString() || !heldEdgePlayer.seekable) return
                const target = transitionEdgeLayer.sourceState.sourceMs || 0
                if (Math.abs(heldEdgePlayer.position - target) > 50)
                    heldEdgePlayer.position = target
            }
        }
        VideoOutput {
            id: heldEdgeOutput
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectFit
            visible: !transitionEdgeLayer.sourceState.imageUrl
        }
        Image {
            anchors.fill: parent
            source: transitionEdgeLayer.sourceState.imageUrl || ""
            fillMode: Image.PreserveAspectFit
            visible: !!transitionEdgeLayer.sourceState.imageUrl
            asynchronous: true
        }
    }

    component AppButton: Button {
        id: control
        property bool primary: false
        property bool subtle: false
        implicitHeight: 34
        leftPadding: 12
        rightPadding: 12
        font.pixelSize: 12
        contentItem: Text {
            text: control.text
            color: control.enabled ? textColor : dimColor
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 7
            border.width: control.primary ? 0 : 1
            border.color: control.activeFocus ? accentColor : control.subtle ? "transparent" : borderColor
            color: !control.enabled ? panelRaised
                : control.primary ? (control.down ? "#146fcd" : control.hovered ? "#3999ff" : accentColor)
                : control.down ? "#2a4359" : control.hovered ? "#24394c" : control.subtle ? "transparent" : controlColor
            Behavior on color { ColorAnimation { duration: 110 } }
        }
    }

    component IconButton: Button {
        id: iconControl
        property string symbol: ""
        property bool selected: false
        implicitWidth: window.width < 1360 ? 28 : 32
        implicitHeight: 32
        contentItem: Text {
            text: iconControl.symbol
            color: iconControl.enabled ? (iconControl.selected ? "#b8dcff" : textColor) : dimColor
            font.pixelSize: 17
            font.family: "Segoe UI Symbol"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 6
            color: iconControl.selected ? accentSoft : iconControl.down ? "#294159" : iconControl.hovered ? controlColor : "transparent"
            border.color: iconControl.activeFocus || iconControl.selected ? accentColor : "transparent"
            Behavior on color { ColorAnimation { duration: 100 } }
        }
    }
    component KeyframeControl: IconButton {
        property bool atKeyframe: false
        property bool animated: false
        symbol: atKeyframe ? "◆" : "◇"
        selected: atKeyframe
        contentItem: Text {
            text: symbol
            color: !enabled ? dimColor : selected ? accentColor : animated ? "#7ca9d0" : mutedColor
            font.family: "Segoe UI Symbol"; font.pixelSize: 17
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
        ToolTip.visible: hovered
        ToolTip.text: atKeyframe ? "Remove keyframe at cursor" : animated
            ? "Add keyframe at cursor · property is animated" : "Add keyframe at cursor"
    }

    component AppSlider: Slider {
        id: appSlider
        implicitHeight: 24
        background: Rectangle {
            x: appSlider.leftPadding
            y: appSlider.topPadding + appSlider.availableHeight / 2 - 2
            width: appSlider.availableWidth
            height: 4
            radius: 2
            color: borderColor
            Rectangle { width: appSlider.visualPosition * parent.width; height: parent.height; radius: 2; color: accentColor }
        }
        handle: Rectangle {
            x: appSlider.leftPadding + appSlider.visualPosition * (appSlider.availableWidth - width)
            y: appSlider.topPadding + appSlider.availableHeight / 2 - height / 2
            width: appSlider.hovered || appSlider.pressed ? 16 : 14
            height: width; radius: width / 2
            color: appSlider.enabled ? "#b5dbff" : dimColor
            border.color: appSlider.activeFocus ? accentColor : appSlider.enabled ? "#e4f4ff" : borderColor
            Behavior on width { NumberAnimation { duration: 100 } }
        }
    }
    component AppField: TextField {
        id: appField
        property string unit: ""
        property bool pending: false
        property bool invalid: activeFocus && validator && text.length > 0 && !acceptableInput
        implicitHeight: 34
        leftPadding: 11
        rightPadding: unit === "" ? 11 : Math.max(28, unitLabel.implicitWidth + 16)
        color: enabled ? textColor : dimColor
        selectionColor: accentColor
        selectedTextColor: "white"
        placeholderTextColor: dimColor
        font.pixelSize: 12
        background: Rectangle {
            radius: 6
            color: appField.enabled ? (appField.activeFocus ? "#20354a" : controlColor) : panelRaised
            border.width: 1
            border.color: appField.invalid ? "#e38d80" : appField.activeFocus ? accentColor
                : appField.pending ? "#c6a86c" : appField.hovered ? "#52708a" : borderColor
            Behavior on border.color { ColorAnimation { duration: 120 } }
            Text {
                id: unitLabel
                anchors.right: parent.right; anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: appField.unit
                visible: text !== ""
                color: dimColor
                font.pixelSize: 11
            }
        }
    }
    component CaptionField: AppField { }
    component AppTextArea: TextArea {
        id: appArea
        leftPadding: 11; rightPadding: 11; topPadding: 9; bottomPadding: 9
        color: textColor; selectionColor: accentColor; selectedTextColor: "white"
        placeholderTextColor: dimColor; font.pixelSize: 12
        background: Rectangle {
            radius: 7
            color: appArea.enabled ? controlColor : panelRaised
            border.width: 1
            border.color: appArea.activeFocus ? accentColor : appArea.hovered ? "#52708a" : borderColor
            Behavior on border.color { ColorAnimation { duration: 120 } }
        }
    }

    component CaptionSelect: ComboBox {
        id: captionSelect
        implicitHeight: 34
        leftPadding: 10
        rightPadding: 28
        font.pixelSize: 12
        contentItem: Text {
            text: captionSelect.displayText
            color: captionSelect.enabled ? textColor : dimColor
            font: captionSelect.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: captionSelect.width - width - 10
            anchors.verticalCenter: parent.verticalCenter
            text: "⌄"
            color: mutedColor
            font.pixelSize: 16
        }
        background: Rectangle {
            radius: 6
            color: captionSelect.pressed ? "#284057" : controlColor
            border.color: captionSelect.activeFocus ? accentColor : captionSelect.hovered ? "#52708a" : borderColor
        }
        delegate: ItemDelegate {
            required property int index
            width: captionSelect.width
            height: 32
            contentItem: Text {
                text: captionSelect.textAt(index)
                color: "#eaf2fa"
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle { radius: 4; color: highlighted ? accentSoft : hovered ? "#263b4e" : panelRaised }
        }
        popup: Popup {
            y: captionSelect.height + 2
            width: captionSelect.width
            implicitHeight: Math.min(248, contentItem.implicitHeight + 8)
            padding: 4
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: captionSelect.popup.visible ? captionSelect.delegateModel : null
                currentIndex: captionSelect.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator { }
            }
            background: Rectangle { radius: 7; color: panelColor; border.color: borderColor }
        }
    }

    component CaptionColor: Button {
        id: captionColor
        property string colorValue: "#ffffff"
        implicitHeight: 34
        leftPadding: 8
        rightPadding: 8
        contentItem: RowLayout {
            spacing: 8
            Rectangle { width: 18; height: 18; radius: 4; color: captionColor.colorValue; border.color: "#8090a0" }
            Text { Layout.fillWidth: true; text: captionColor.text; color: "#eaf2fa"; font.pixelSize: 11; elide: Text.ElideRight }
            Text { text: "⌄"; color: "#9fb2c7"; font.pixelSize: 15 }
        }
        background: Rectangle {
            radius: 5
            color: captionColor.pressed ? "#293b4d" : captionColor.hovered ? "#263748" : controlColor
            border.color: captionColor.hovered ? "#52667a" : "#344657"
        }
    }

    component AppMenuItem: MenuItem {
        id: menuAction
        property string shortcutLabel: ""
        implicitWidth: 252
        implicitHeight: 35
        width: menuAction.menu ? menuAction.menu.availableWidth : implicitWidth
        leftPadding: 12
        rightPadding: 12
        contentItem: RowLayout {
            spacing: 12
            Text {
                text: menuAction.text
                color: menuAction.enabled ? textColor : "#647384"
                font.pixelSize: 12
                Layout.fillWidth: true
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                text: menuAction.shortcutLabel
                color: mutedColor
                font.pixelSize: 11
                visible: text !== ""
                verticalAlignment: Text.AlignVCenter
            }
        }
        background: Rectangle {
            radius: 4
            color: menuAction.highlighted ? "#25466b" : "transparent"
        }
    }

    component WindowControl: Button {
        id: frameControl
        property string frameAction: ""
        implicitWidth: 44
        implicitHeight: 49
        focusPolicy: Qt.NoFocus
        background: Rectangle {
            color: frameControl.hovered
                ? (frameControl.frameAction === "close" ? "#b63d47" : "#263544")
                : frameControl.down ? "#31485f" : "transparent"
        }
        contentItem: Item {
            Rectangle {
                visible: frameControl.frameAction === "minimize"
                width: 12; height: 1
                anchors.centerIn: parent
                color: textColor
            }
            Rectangle {
                visible: frameControl.frameAction === "maximize"
                width: 11; height: 10
                anchors.centerIn: parent
                color: "transparent"
                border.width: 1
                border.color: textColor
            }
            Item {
                visible: frameControl.frameAction === "restore"
                width: 14; height: 13
                anchors.centerIn: parent
                Rectangle { x: 4; y: 0; width: 10; height: 9; color: panelColor; border.color: textColor }
                Rectangle { x: 0; y: 4; width: 10; height: 9; color: panelColor; border.color: textColor }
            }
            Text {
                visible: frameControl.frameAction === "close"
                anchors.centerIn: parent
                text: "×"
                color: "#edf3fa"
                font.family: "Segoe UI"
                font.pixelSize: 20
            }
        }
    }

    FileDialog {
        id: importDialog
        title: "Import media"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Media files (*.mp4 *.mov *.mkv *.webm *.avi *.mp3 *.wav *.flac *.m4a *.png *.jpg *.jpeg *.webp *.bmp)", "All files (*)"]
        onAccepted: {
            for (const file of selectedFiles) editor.importMedia(file)
        }
    }
    Connections {
        target: mediaGenerations
        function onChanged() { if (!mediaGenerations.running) { generationResults = projectGenerationResults(generationKind); if (filesGenerationKind !== "") filesGenerationResults = projectGenerationResults(filesGenerationKind) } }
        function onResultReady(path) { editor.importMedia(mediaGenerations.fileUrl(path)) }
    }
    FolderDialog {
        id: generationRuntimeFolder
        title: "Choose local runtime folder"
        onAccepted: mediaGenerations.setRuntimePath(generationKind, mediaGenerations.localPath(selectedFolder))
    }
    FolderDialog {
        id: generationOutputFolder
        title: "Choose generated results folder"
        onAccepted: mediaGenerations.setOutputPath(generationKind, mediaGenerations.localPath(selectedFolder))
    }
    FileDialog {
        id: generationImageFile
        title: "Choose reference image"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp)"]
        onAccepted: generationReferenceImage = mediaGenerations.localPath(selectedFile)
    }
    FileDialog {
        id: voiceAudioFile
        title: "Choose a target voice recording"
        nameFilters: ["Audio files (*.wav *.mp3 *.flac *.ogg *.m4a)", "All files (*)"]
        onAccepted: newVoiceAudio = selectedFile
    }
    FileDialog {
        id: voiceImageFile
        title: "Choose a voice thumbnail"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp)", "All files (*)"]
        onAccepted: newVoiceImage = selectedFile
    }
    Dialog {
        id: addVoiceDialog
        title: "Add custom voice"
        width: Math.min(window.width - 40, 480)
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        background: Rectangle { color: panelColor; radius: 9; border.color: "#344456" }
        contentItem: ColumnLayout {
            spacing: 11
            Label { text: "Create a voice"; color: textColor; font.pixelSize: 17; font.bold: true }
            Label { text: "Choose a recording of one speaker, then set the excerpt SeedVC will use by default."; color: mutedColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: "NAME"; color: "#8fa8bf"; font.pixelSize: 10; font.bold: true; Layout.topMargin: 5 }
            CaptionField { id: newVoiceTitle; Layout.fillWidth: true; placeholderText: "Voice name" }
            Label { text: "TARGET RECORDING"; color: "#8fa8bf"; font.pixelSize: 10; font.bold: true }
            RowLayout { Layout.fillWidth: true
                Label { text: newVoiceAudio.toString() === "" ? "No audio selected" : decodeURIComponent(newVoiceAudio.toString().split("/").pop()); color: mutedColor; Layout.fillWidth: true; elide: Text.ElideMiddle; font.pixelSize: 11 }
                AppButton { text: "Choose audio…"; onClicked: voiceAudioFile.open() }
            }
            Label { text: "THUMBNAIL · OPTIONAL"; color: "#8fa8bf"; font.pixelSize: 10; font.bold: true }
            RowLayout { Layout.fillWidth: true
                Label { text: newVoiceImage.toString() === "" ? "Default silhouette" : decodeURIComponent(newVoiceImage.toString().split("/").pop()); color: mutedColor; Layout.fillWidth: true; elide: Text.ElideMiddle; font.pixelSize: 11 }
                AppButton { text: "Choose image…"; onClicked: voiceImageFile.open() }
                AppButton { text: "Clear"; visible: newVoiceImage.toString() !== ""; subtle: true; onClicked: newVoiceImage = "" }
            }
            Label { text: "DEFAULT REFERENCE SPAN"; color: "#8fa8bf"; font.pixelSize: 10; font.bold: true; Layout.topMargin: 5 }
            RowLayout { Layout.fillWidth: true; spacing: 8
                Label { text: "Start"; color: mutedColor; font.pixelSize: 11 }
                CaptionField { id: newVoiceReferenceStart; Layout.preferredWidth: 80; text: "0"; inputMethodHints: Qt.ImhFormattedNumbersOnly
                    validator: DoubleValidator { bottom: 0; top: 36000; decimals: 2 } }
                Label { text: "s"; color: mutedColor; font.pixelSize: 11 }
                Item { Layout.fillWidth: true }
                Label { text: "Length"; color: mutedColor; font.pixelSize: 11 }
                CaptionField { id: newVoiceReferenceDuration; Layout.preferredWidth: 80; text: "10"; inputMethodHints: Qt.ImhFormattedNumbersOnly
                    validator: DoubleValidator { bottom: 1; top: voiceGenerations.maxReferenceSeconds; decimals: 2 } }
                Label { text: "s"; color: mutedColor; font.pixelSize: 11 }
            }
            Label { text: "SeedVC uses up to " + voiceGenerations.maxReferenceSeconds + " seconds. The excerpt must fit inside the recording."; color: mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: voiceGenerations.error; visible: voiceGenerations.error !== ""; color: "#ffb899"; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 11 }
            RowLayout { Layout.fillWidth: true; Layout.topMargin: 10
                Item { Layout.fillWidth: true }
                AppButton { text: "Cancel"; onClicked: addVoiceDialog.close() }
                AppButton { text: "Save voice"; primary: true; enabled: newVoiceTitle.text.trim().length > 0 && newVoiceAudio.toString() !== ""
                    onClicked: {
                        voiceGenerations.addVoice(newVoiceTitle.text, newVoiceAudio, newVoiceImage,
                            Number(newVoiceReferenceStart.text), Number(newVoiceReferenceDuration.text))
                        if (voiceGenerations.error === "") {
                            newVoiceTitle.text = ""; newVoiceAudio = ""; newVoiceImage = ""
                            newVoiceReferenceStart.text = "0"; newVoiceReferenceDuration.text = "10"
                            addVoiceDialog.close()
                        }
                    }
                }
            }
        }
    }
    FolderDialog {
        id: seedvcRuntimeFolder
        title: "Choose SeedVC runtime folder"
        onAccepted: voiceGenerations.setSeedvcRuntimePath(mediaGenerations.localPath(selectedFolder))
    }
    FolderDialog {
        id: voiceOutputFolder
        title: "Choose voice results folder"
        onAccepted: voiceGenerations.setOutputPath(mediaGenerations.localPath(selectedFolder))
    }
    FileDialog {
        id: relinkFileDialog
        title: "Choose replacement media"
        onAccepted: editor.relinkAsset(editor.selectedAssetId, selectedFile)
    }
    FolderDialog {
        id: relinkFolderDialog
        title: "Find missing media in folder"
        onAccepted: editor.relinkFolder(selectedFolder)
    }
    FolderDialog {
        id: generationsFolderDialog
        title: "Choose generations directory"
        onAccepted: generations.setRootUrl(selectedFolder)
    }
    Dialog {
        id: generativeSettingsDialog
        title: "Settings"
        width: Math.min(window.width - 56, 900)
        height: Math.min(window.height - 56, 710)
        modal: true
        anchors.centerIn: parent
        padding: 0
        standardButtons: Dialog.NoButton
        background: Rectangle { color: panelColor; radius: 10; border.color: "#344456" }
        contentItem: ColumnLayout {
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 62
                Layout.leftMargin: 24
                Layout.rightMargin: 18
                Label { text: "Settings"; color: textColor; font.pixelSize: 19; font.bold: true; Layout.fillWidth: true }
                IconButton { symbol: "×"; onClicked: generativeSettingsDialog.close() }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: borderColor }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                Rectangle {
                    Layout.preferredWidth: 172
                    Layout.fillHeight: true
                    color: panelColor
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 5
                        Label { text: "PREFERENCES"; color: "#8297ac"; font.pixelSize: 10; font.bold: true; Layout.leftMargin: 9; Layout.topMargin: 9; Layout.bottomMargin: 9 }
                        AppButton { text: "General"; Layout.fillWidth: true; primary: settingsPage === 0; onClicked: settingsPage = 0 }
                        AppButton { text: "Generative"; Layout.fillWidth: true; primary: settingsPage === 1; onClicked: settingsPage = 1 }
                        Item { Layout.fillHeight: true }
                        Label { text: "DataCut"; color: "#60768b"; font.pixelSize: 11; Layout.leftMargin: 9; Layout.bottomMargin: 5 }
                    }
                }
                Rectangle { width: 1; Layout.fillHeight: true; color: borderColor }
                ScrollView {
                    id: settingsScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: settingsScroll.availableWidth
                        spacing: 0
                        Item { Layout.preferredHeight: 24 }
                        ColumnLayout {
                            visible: settingsPage === 0
                            Layout.fillWidth: true
                            Layout.leftMargin: 28
                            Layout.rightMargin: 28
                            spacing: 13
                            Label { text: "General"; color: textColor; font.pixelSize: 22; font.bold: true }
                            Label { text: "Editor information and diagnostics"; color: mutedColor; font.pixelSize: 12 }
                            Rectangle {
                                Layout.fillWidth: true; implicitHeight: generalCard.implicitHeight + 36
                                radius: 8; color: panelRaised; border.color: borderColor
                                ColumnLayout {
                                    id: generalCard; anchors.fill: parent; anchors.margins: 18; spacing: 11
                                    Label { text: "APPLICATION"; color: "#83b9ef"; font.pixelSize: 10; font.bold: true }
                                    Label { text: "DataCut native editor"; color: textColor; font.pixelSize: 14; font.bold: true }
                                    Label { text: "Your project and media stay local. Generation runtimes are optional and managed separately."; color: mutedColor; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 11 }
                                    RowLayout { Layout.fillWidth: true
                                        AppButton { text: "Open diagnostics folder"; onClicked: crashReporter.openReportDirectory() }
                                        Item { Layout.fillWidth: true }
                                    }
                                }
                            }
                        }
                        ColumnLayout {
                            visible: settingsPage === 1
                            Layout.fillWidth: true
                            Layout.leftMargin: 28
                            Layout.rightMargin: 28
                            spacing: 16
                            Label { text: "Generative"; color: textColor; font.pixelSize: 22; font.bold: true }
                            Label { text: "Manage isolated local runtimes and output folders. Remote providers are coming later."; color: mutedColor; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            RowLayout { Layout.fillWidth: true; spacing: 7
                                Repeater { model: ["Media", "Auto Caption", "Voice Change"]
                                    delegate: AppButton { required property string modelData; required property int index; text: modelData; primary: generativeSection === index; onClicked: generativeSection = index }
                                }
                                Item { Layout.fillWidth: true }
                            }
                            Rectangle {
                                visible: generativeSection === 0
                                Layout.fillWidth: true; implicitHeight: mediaSettingsCard.implicitHeight + 36
                                radius: 8; color: panelRaised; border.color: borderColor
                                ColumnLayout {
                                    id: mediaSettingsCard; anchors.fill: parent; anchors.margins: 18; spacing: 10
                                    Label { text: "IMAGE, VIDEO, MUSIC & SOUND EFFECTS"; color: "#83b9ef"; font.pixelSize: 10; font.bold: true }
                                    Label { text: mediaGenerations.gpuMiB < 0 ? "Checking GPU…" : "NVIDIA VRAM: " + (mediaGenerations.gpuMiB / 1024).toFixed(2) + " GiB · 10 GB class GPU required"; color: mediaGenerations.gpuMiB >= 10000 ? "#88dfb8" : "#ffb899"; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                    RowLayout { Layout.fillWidth: true; spacing: 6
                                        Repeater { model: ["Image", "Video", "Music", "Sound FX"]
                                            delegate: AppButton { required property string modelData; required property int index; text: modelData; primary: generationTab === index; onClicked: generationTab = index }
                                        }
                                        Item { Layout.fillWidth: true }
                                    }
                                    Label { text: generationProvider ? generationProvider.name : ""; color: textColor; font.pixelSize: 14; font.bold: true }
                                    Label { text: generationProvider ? generationProvider.status : ""; color: generationProvider && generationProvider.ready ? "#88dfb8" : "#ffb899"; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 11 }
                                    Label { text: "Runtime folder"; color: mutedColor; font.pixelSize: 11; Layout.topMargin: 5 }
                                    RowLayout { Layout.fillWidth: true
                                        CaptionField { Layout.fillWidth: true; text: generationProvider ? generationProvider.runtimePath : ""; onEditingFinished: mediaGenerations.setRuntimePath(generationKind, text) }
                                        AppButton { text: "Browse…"; onClicked: generationRuntimeFolder.open() }
                                    }
                                    Label { text: "Python and models install inside this runtime folder."; color: mutedColor; font.pixelSize: 11; Layout.fillWidth: true }
                                    Label { visible: generationKind === "music" && generationProvider && generationProvider.freeGiB >= 0; text: "Music setup: allow about 25 GiB free · " + (generationProvider ? generationProvider.freeGiB.toFixed(1) : "0") + " GiB available on this drive"; color: generationProvider && generationProvider.freeGiB >= 25 ? mutedColor : "#ffb899"; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                    AppButton { visible: generationKind === "music" && generationProvider && generationProvider.suggestedRoot !== ""; text: "Use " + (generationProvider ? generationProvider.suggestedRoot : "") + " for music"; onClicked: mediaGenerations.useSuggestedStorage(generationKind) }
                                    Label { text: "Results folder"; color: mutedColor; font.pixelSize: 11 }
                                    RowLayout { Layout.fillWidth: true
                                        CaptionField { Layout.fillWidth: true; text: generationProvider ? generationProvider.outputPath : ""; onEditingFinished: mediaGenerations.setOutputPath(generationKind, text) }
                                        AppButton { text: "Browse…"; onClicked: generationOutputFolder.open() }
                                    }
                                    RowLayout { Layout.fillWidth: true; Layout.topMargin: 7
                                        AppButton { text: "Set up / repair"; primary: true; enabled: !mediaGenerations.running; onClicked: mediaGenerations.setup(generationKind) }
                                        AppButton { text: "Refresh status"; enabled: !mediaGenerations.running; onClicked: mediaGenerations.refresh() }
                                        Item { Layout.fillWidth: true }
                                        AppButton { text: "Cancel"; visible: mediaGenerations.running; onClicked: mediaGenerations.cancel() }
                                    }
                                    Label { text: mediaGenerations.stage; visible: mediaGenerations.running; color: mutedColor; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                                    JobProgressBar { visible: mediaGenerations.running; Layout.fillWidth: true; indeterminate: !mediaGenerations.progressKnown; value: mediaGenerations.progress }
                                    Label { text: mediaGenerations.error; visible: mediaGenerations.error !== ""; color: "#ffb899"; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 11 }
                                    RowLayout { Layout.fillWidth: true; spacing: 6
                                        AppButton { text: "Copy error"; visible: mediaGenerations.error !== ""; subtle: true; onClicked: mediaGenerations.copyError() }
                                        AppButton { text: "Open logs"; subtle: true; onClicked: mediaGenerations.openLogFolder(generationKind) }
                                        Item { Layout.fillWidth: true }
                                    }
                                    Label { text: generationKind === "sound" ? "Sound FX downloads about 7.2 GB of TangoFlux and text-encoder weights, plus its private Python packages. The worker closes when setup finishes." : "Some model downloads are several gigabytes. Each runtime closes when its job finishes."; color: mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                }
                            }
                            Rectangle {
                                visible: generativeSection === 1
                                Layout.fillWidth: true; implicitHeight: captionSettingsCard.implicitHeight + 36
                                radius: 8; color: panelRaised; border.color: borderColor
                                ColumnLayout {
                                    id: captionSettingsCard; anchors.fill: parent; anchors.margins: 18; spacing: 10
                                    Label { text: "AUTO CAPTION"; color: "#83b9ef"; font.pixelSize: 10; font.bold: true }
                                    Label { text: "NVIDIA Parakeet TDT 0.6B v3"; color: textColor; font.pixelSize: 15; font.bold: true }
                                    Label { text: "Windows 10/11 x64 · CPU runtime · 2 GB free RAM minimum · 8 GB system RAM recommended. Remote provider coming later."; color: mutedColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                    Label { text: "Generations directory"; color: mutedColor; font.pixelSize: 11; Layout.topMargin: 5 }
                                    RowLayout { Layout.fillWidth: true
                                        CaptionField { Layout.fillWidth: true; text: generations.rootPath; onEditingFinished: generations.setRootPath(text) }
                                        AppButton { text: "Browse…"; onClicked: generationsFolderDialog.open() }
                                    }
                                    Label { text: generations.localReady ? "Runtime ready" : "Runtime not set up"; color: generations.localReady ? "#88dfb8" : "#ffb899"; font.pixelSize: 11 }
                                    RowLayout { Layout.fillWidth: true; Layout.topMargin: 5
                                        AppButton { text: generations.localReady ? "Repair setup" : "Set up Parakeet"; primary: true; enabled: !generations.setupRunning && !generations.captionRunning; onClicked: generations.setupLocal() }
                                        Item { Layout.fillWidth: true }
                                        AppButton { text: "Cancel"; visible: generations.setupRunning; onClicked: generations.cancelJob() }
                                    }
                                    Label { text: generations.stage + " · " + jobTime(generations.elapsedSeconds); visible: generations.setupRunning; color: mutedColor; font.pixelSize: 11 }
                                    JobProgressBar { Layout.fillWidth: true; visible: generations.setupRunning; indeterminate: !generations.progressKnown; value: generations.progress }
                                    Label { text: generations.error; visible: generations.error !== ""; color: "#ffb899"; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 11 }
                                }
                            }
                            Rectangle {
                                visible: generativeSection === 2
                                Layout.fillWidth: true; implicitHeight: seedvcSettingsCard.implicitHeight + 36
                                radius: 8; color: panelRaised; border.color: borderColor
                                ColumnLayout {
                                    id: seedvcSettingsCard; anchors.fill: parent; anchors.margins: 18; spacing: 10
                                    Label { text: "VOICE CHANGE"; color: "#83b9ef"; font.pixelSize: 10; font.bold: true }
                                    Label { text: "SeedVC speech"; color: textColor; font.pixelSize: 15; font.bold: true }
                                    Label { text: "Reference based speech conversion without transcripts. Uses an isolated Python process and the speech checkpoint (no singing pitch conditioning). NVIDIA CUDA GPU with 10 GB class VRAM · roughly 10 GB free disk for runtime and models."; color: mutedColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                    Label { text: voiceGenerations.seedvcStatus; color: voiceGenerations.seedvcReady ? "#88dfb8" : "#ffb899"; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                    Label { text: "Runtime folder"; color: mutedColor; font.pixelSize: 11; Layout.topMargin: 5 }
                                    RowLayout { Layout.fillWidth: true
                                        CaptionField { Layout.fillWidth: true; text: voiceGenerations.seedvcRuntimePath; onEditingFinished: voiceGenerations.setSeedvcRuntimePath(text) }
                                        AppButton { text: "Browse…"; onClicked: seedvcRuntimeFolder.open() }
                                    }
                                    Label { text: "Results folder"; color: mutedColor; font.pixelSize: 11 }
                                    RowLayout { Layout.fillWidth: true
                                        CaptionField { Layout.fillWidth: true; text: voiceGenerations.outputPath; onEditingFinished: voiceGenerations.setOutputPath(text) }
                                        AppButton { text: "Browse…"; onClicked: voiceOutputFolder.open() }
                                    }
                                    RowLayout { Layout.fillWidth: true; Layout.topMargin: 7
                                        AppButton { text: "Set up / verify SeedVC"; primary: true; enabled: !voiceGenerations.running; onClicked: voiceGenerations.setupSeedvc() }
                                        Item { Layout.fillWidth: true }
                                        AppButton { text: "Cancel"; visible: voiceGenerations.running; onClicked: voiceGenerations.cancel() }
                                    }
                                    Label { text: voiceGenerations.stage + " · " + jobTime(voiceGenerations.elapsedSeconds); visible: voiceGenerations.running; color: mutedColor; font.pixelSize: 11 }
                                    JobProgressBar { Layout.fillWidth: true; visible: voiceGenerations.running; value: Math.max(0, voiceGenerations.progress); indeterminate: voiceGenerations.progress < 0 }
                                    Label { text: voiceGenerations.setupMessage; visible: voiceGenerations.setupMessage !== ""; color: voiceGenerations.seedvcReady && voiceGenerations.error === "" ? "#88dfb8" : "#ffb899"; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 11 }
                                    Label { text: voiceGenerations.error; visible: voiceGenerations.error !== "" && voiceGenerations.setupMessage === ""; color: "#ffb899"; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 11 }
                                }
                            }
                        }
                        Item { Layout.preferredHeight: 28 }
                    }
                }
            }
        }
    }
    Dialog {
        id: autoCaptionProgressDialog
        title: "Auto Caption"
        width: 400
        modal: false
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        background: Rectangle { color: panelColor; radius: 8; border.color: borderColor }
        contentItem: ColumnLayout {
            spacing: 12
            RowLayout { Layout.fillWidth: true
                Label { text: generations.stage; color: textColor; font.pixelSize: 12; Layout.fillWidth: true }
                Label { text: generations.progressKnown ? Math.round(generations.progress * 100) + "%" : "Working"; color: mutedColor; font.pixelSize: 11 }
            }
            JobProgressBar { Layout.fillWidth: true; indeterminate: !generations.progressKnown; value: generations.progress }
            Label { text: "Elapsed " + jobTime(generations.elapsedSeconds); color: mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout { Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton { text: "Cancel"; onClicked: generations.cancelJob() }
            }
        }
    }
    Connections {
        target: generations
        function onJobChanged() {
            if (generations.captionRunning && !autoCaptionProgressDialog.opened)
                autoCaptionProgressDialog.open()
            else if (!generations.captionRunning && autoCaptionProgressDialog.opened)
                autoCaptionProgressDialog.close()
        }
    }
    Dialog {
        id: voiceGenerationProgressDialog
        title: "Change voice"
        width: Math.min(window.width - 40, 430)
        modal: false
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        background: Rectangle { color: panelColor; radius: 9; border.color: "#344456" }
        contentItem: ColumnLayout {
            spacing: 12
            Label { text: voiceGenerations.stage; color: textColor; font.pixelSize: 13; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            JobProgressBar { Layout.fillWidth: true; value: Math.max(0, voiceGenerations.progress); indeterminate: voiceGenerations.progress < 0 }
            RowLayout { Layout.fillWidth: true
                Label { text: voiceGenerations.progress < 0 ? "Preparing model" : Math.round(voiceGenerations.progress * 100) + "%"; color: mutedColor; font.pixelSize: 11 }
                Item { Layout.fillWidth: true }
                Label { text: "Elapsed " + jobTime(voiceGenerations.elapsedSeconds); color: mutedColor; font.pixelSize: 11 }
            }
            RowLayout { Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton { text: "Cancel"; onClicked: voiceGenerations.cancel() }
            }
        }
    }
    Connections {
        target: voiceGenerations
        function onChanged() {
            if (!voiceGenerations.running && voiceGenerationProgressDialog.opened)
                voiceGenerationProgressDialog.close()
        }
    }
    FileDialog {
        id: openDialog
        title: "Open a DataCut project"
        fileMode: FileDialog.OpenFile
        nameFilters: ["DataCut project (*.datacutproject *.json)", "All files (*)"]
        onAccepted: editor.openProject(selectedFile)
    }
    FolderDialog {
        id: openLegacyProjectDialog
        title: "Open a legacy DataCut project folder"
        onAccepted: editor.openProject(selectedFolder)
    }
    FileDialog {
        id: saveDialog
        title: "Save DataCut project"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "datacutproject"
        nameFilters: ["DataCut project (*.datacutproject)"]
        onAccepted: editor.saveProject(selectedFile)
    }
    FileDialog {
        id: exportFileDialog
        title: "Choose export output"
        fileMode: FileDialog.SaveFile
        nameFilters: exportRequestKind === "stillFrame" || exportRequestKind === "projectFrame"
            ? ["PNG image (*.png)"]
            : exportRequestKind === "rangeAudio" || exportRequestKind === "clipAudio" || exportRequestKind === "projectAudio"
            ? ["M4A audio (*.m4a)", "WAV audio (*.wav)", "MP3 audio (*.mp3)", "FLAC audio (*.flac)"]
            : ["MP4 video (*.mp4)"]
        onAccepted: {
            if (exportRequestKind === "projectFrame")
                editor.startFrameExport(exportRequestFrame, selectedFile)
            else editor.startExport(exportRequestKind, selectedFile)
            exportDialog.startError = editor.exportRunning ? "" : editor.status
            exportDialog.open()
        }
    }
    FolderDialog {
        id: exportSequenceFolderDialog
        title: "Choose a folder for the image sequence"
        onAccepted: {
            editor.startExport("imageSequence", selectedFolder)
            exportDialog.startError = editor.exportRunning ? "" : editor.status
            exportDialog.open()
        }
    }
    FolderDialog {
        id: exportCollectionFolderDialog
        title: "Choose a folder for the collection export"
        onAccepted: {
            editor.exportCollection(activeCollectionId, selectedFolder)
            exportDialog.startError = editor.exportRunning ? "" : editor.status
            exportDialog.open()
        }
    }
    Dialog {
        id: customRangeIncrementDialog
        title: "Add range increment"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        background: Rectangle { color: panelColor; radius: 8; border.color: borderColor }
        contentItem: ColumnLayout {
            spacing: 10
            Label { text: "Step or frame rule, such as 6, 6n+1, or n+4"; color: textColor; font.pixelSize: 12 }
            CaptionField {
                id: customRangeIncrementInput
                Layout.preferredWidth: 270
                color: textColor
                placeholderText: "6n+1"
                onAccepted: if (editor.addCustomRangeIncrement(text)) customRangeIncrementDialog.close()
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                AppButton { text: "Cancel"; subtle: true; onClicked: customRangeIncrementDialog.close() }
                AppButton { text: "Add"; primary: true; onClicked: if (editor.addCustomRangeIncrement(customRangeIncrementInput.text)) customRangeIncrementDialog.close() }
            }
        }
        onOpened: { customRangeIncrementInput.text = ""; customRangeIncrementInput.forceActiveFocus() }
    }
    Dialog {
        id: exportDialog
        property string startError: ""
        title: "Export"
        width: 400
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        background: Rectangle { color: panelColor; radius: 7; border.color: borderColor }
        ColumnLayout {
            width: parent.width
            spacing: 12
            Label { text: "Range or clip"; color: mutedColor; font.pixelSize: 11 }
            CaptionSelect {
                id: exportMode
                Layout.fillWidth: true
                textRole: "text"
                valueRole: "kind"
                model: [
                    { text: "Whole project timeline · MP4", kind: "projectVideo", available: editor.clips.length > 0 },
                    { text: "Marked timeline range · MP4", kind: "timelineRangeVideo", available: editor.hasTimelineRange && editor.clips.length > 0 },
                    { text: "Whole project timeline · audio", kind: "projectAudio", available: editor.clips.some(function(clip) { return clip.hasAudio }) },
                    { text: "Selected source range · video", kind: "rangeVideo", available: editor.assets.find(function(asset) { return asset.id === editor.selectedAssetId })?.hasVideo },
                    { text: "Selected source range · PNG sequence", kind: "imageSequence", available: editor.assets.find(function(asset) { return asset.id === editor.selectedAssetId })?.hasVideo },
                    { text: "First selected source frame · PNG", kind: "stillFrame", available: editor.assets.find(function(asset) { return asset.id === editor.selectedAssetId })?.hasVideo },
                    { text: "Project frame · PNG", kind: "projectFrame", available: editor.timelineDurationFrames > 0 },
                    { text: "Selected source range · audio", kind: "rangeAudio", available: editor.assets.find(function(asset) { return asset.id === editor.selectedAssetId })?.hasAudio },
                    { text: "Selected timeline clip · video", kind: "timelineClipVideo", available: activeClip?.hasVideo },
                    { text: "Selected timeline clip · audio", kind: "clipAudio", available: activeClip?.hasAudio }
                ].filter(function(option) { return option.available })
            }
            RowLayout {
                Layout.fillWidth: true
                visible: exportMode.currentValue === "projectFrame"
                Label { text: "Project frame"; color: mutedColor; font.pixelSize: 11 }
                Item { Layout.fillWidth: true }
                AppField {
                    id: exportFrameInput
                    Layout.preferredWidth: 120
                    text: String(editor.playheadFrame)
                    validator: RegularExpressionValidator { regularExpression: /\d+/ }
                    horizontalAlignment: TextInput.AlignRight
                }
            }
            RowLayout { Layout.fillWidth: true
                Label { text: "Resolution"; color: mutedColor; font.pixelSize: 11 }
                Item { Layout.fillWidth: true }
                CaptionSelect { Layout.preferredWidth: 120; model: ["720p", "1080p"]; currentIndex: model.indexOf(editor.exportResolution); onActivated: editor.setExportResolution(currentText) }
            }
            Label { text: editor.canvasWidth + " × " + editor.canvasHeight + " px"; color: mutedColor; font.pixelSize: 10 }
            Label { text: "Timeline range: project frames " + editor.timelineRangeStart + "–" + (Number(editor.timelineRangeEnd) - 1); color: mutedColor; font.pixelSize: 10; visible: exportMode.currentValue === "timelineRangeVideo" }
            Label { text: exportMode.currentValue === "imageSequence"
                    ? "Creates numbered PNG frames and a JSON manifest in a new folder."
                    : "Video: MP4 / H.264 · Audio: M4A, WAV, MP3, or FLAC · Frame: PNG"
                color: mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            AppButton { text: exportMode.currentValue === "imageSequence" ? "Choose destination folder…" : "Choose output file…"; primary: true; Layout.fillWidth: true; enabled: !editor.exportRunning && exportMode.count > 0 && (exportMode.currentValue !== "projectFrame" || (exportFrameInput.acceptableInput && Number(exportFrameInput.text) < editor.timelineDurationFrames)); onClicked: { exportDialog.close(); if (exportMode.currentValue === "imageSequence") exportSequenceFolderDialog.open(); else chooseExportFile(exportMode.currentValue, Number(exportFrameInput.text)) } }
            Rectangle { Layout.fillWidth: true; height: 1; color: borderColor }
            RowLayout { Layout.fillWidth: true; visible: editor.exportStage !== ""
                Label { text: editor.exportStage; color: textColor; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                Label { text: Math.round(editor.exportProgress * 100) + "%"; color: mutedColor; font.pixelSize: 11 }
            }
            Label {
                text: exportDialog.startError !== "" ? exportDialog.startError : editor.status
                visible: exportDialog.startError !== "" || editor.exportStage === "Failed"
                    || editor.exportStage === "Canceled"
                color: "#ffb899"; font.pixelSize: 11; wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            JobProgressBar {
                value: editor.exportProgress
                Layout.fillWidth: true
                visible: editor.exportStage !== ""
            }
            Label { text: "Elapsed " + jobTime(editor.exportElapsedSeconds)
                    + (editor.exportRemainingSeconds >= 0 ? " · About " + jobTime(editor.exportRemainingSeconds) + " remaining" : "")
                visible: editor.exportRunning; color: mutedColor; font.pixelSize: 10 }
            Label { text: editor.exportOutput; color: mutedColor; font.pixelSize: 10; elide: Text.ElideMiddle; Layout.fillWidth: true; visible: editor.exportStage !== "" }
            RowLayout { Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton { text: editor.exportRunning ? "Cancel export" : "Close"; onClicked: editor.exportRunning ? editor.cancelExport() : exportDialog.close() }
            }
        }
    }
    Dialog {
        id: discardDialog
        title: "Unsaved changes"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: Rectangle { color: panelRaised; radius: 6; border.color: borderColor }
        Label { text: "Continue and discard unsaved project changes?"; color: textColor }
        onAccepted: {
            if (pendingProjectAction === "new") editor.newProject()
            else if (pendingProjectAction === "demo") editor.openDemoProject()
            else if (pendingProjectAction === "legacy") openLegacyProjectDialog.open()
            else if (pendingProjectAction.startsWith("recent:"))
                editor.openProject(mediaGenerations.fileUrl(pendingProjectAction.substring(7)))
            else openDialog.open()
            pendingProjectAction = ""
        }
        onRejected: pendingProjectAction = ""
    }
    Dialog {
        id: unexpectedExitDialog
        title: "DataCut closed unexpectedly"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        background: Rectangle { color: panelColor; radius: 7; border.color: borderColor }
        ColumnLayout {
            width: 420; spacing: 12
            Label {
                text: "The previous session ended without a normal shutdown. A diagnostic log was kept"
                    + (crashReporter.previousDumpPath !== "" ? "; a Windows crash dump may also be available." : ".")
                color: textColor; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            Label {
                text: "Open your project folder to check for a newer recovery autosave."
                color: mutedColor; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            Label {
                text: crashReporter.reportDirectory
                color: mutedColor; font.pixelSize: 10; elide: Text.ElideMiddle; Layout.fillWidth: true
            }
            RowLayout { Layout.fillWidth: true
                AppButton { text: "Open diagnostics folder"; onClicked: crashReporter.openReportDirectory() }
                Item { Layout.fillWidth: true }
                AppButton { text: "Continue"; primary: true; onClicked: {
                    unexpectedExitDialog.close()
                    if (editor.recoveryAvailable && !editor.dirty) recoveryDialog.open()
                } }
            }
        }
    }
    Dialog {
        id: recoveryDialog
        title: "Recover unsaved work?"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        background: Rectangle { color: panelColor; radius: 7; border.color: borderColor }
        ColumnLayout {
            width: 350; spacing: 12
            Label { text: "A newer autosave was found for this project."; color: textColor; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout { Layout.fillWidth: true
                AppButton { text: "Discard autosave"; onClicked: { editor.discardRecovery(); recoveryDialog.close() } }
                Item { Layout.fillWidth: true }
                AppButton { text: "Recover"; primary: true; onClicked: { editor.recoverProject(); recoveryDialog.close() } }
            }
        }
    }
    Dialog {
        id: newCollectionDialog
        title: "New collection"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: Rectangle { color: panelColor; radius: 7; border.color: borderColor }
        CaptionField { id: newCollectionName; width: 290; placeholderText: "Collection name"; color: textColor; onAccepted: newCollectionDialog.accept() }
        onAccepted: {
            if (newCollectionName.text.trim() === "") return
            collectionInspector.commitDetails()
            editor.createCollection(newCollectionName.text)
            if (editor.collections.length) selectedCollectionTileId = editor.collections[editor.collections.length - 1].id
            openedCollectionId = ""
            selectedSegmentIds = []
            collectionInspectorActive = true
            newCollectionName.text = ""
        }
    }
    Dialog {
        id: deleteCollectionDialog
        title: "Delete collection?"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        background: Rectangle { color: panelColor; radius: 7; border.color: borderColor }
        ColumnLayout { width: 340; spacing: 12
            Label {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: textColor
                text: "Delete this collection and its saved segment references? Source media and timeline clips will stay in the project."
            }
            RowLayout { Layout.fillWidth: true
                AppButton { text: "Cancel"; onClicked: deleteCollectionDialog.close() }
                Item { Layout.fillWidth: true }
                AppButton { text: "Delete collection"; primary: true; onClicked: {
                    editor.removeCollectionWithSegments(selectedCollectionTileId)
                    selectedCollectionTileId = ""
                    openedCollectionId = ""
                    selectedSegmentIds = []
                    deleteCollectionDialog.close()
                } }
            }
        }
    }
    Menu {
        id: projectMenu
        popupType: Popup.Item
        width: 264
        padding: 6
        background: Rectangle { color: panelRaised; border.color: "#3a4c60"; border.width: 1; radius: 7 }
        AppMenuItem { text: "New project"; shortcutLabel: "Ctrl+N"; onTriggered: requestProjectAction("new") }
        AppMenuItem { text: "Open project…"; shortcutLabel: "Ctrl+O"; onTriggered: requestProjectAction("open") }
        AppMenuItem { text: "Open legacy project folder…"; onTriggered: requestProjectAction("legacy") }
        Menu {
            id: recentProjectsMenu
            title: "Recent projects"
            enabled: editor.recentProjects.length > 0
            Instantiator {
                model: editor.recentProjects
                delegate: AppMenuItem {
                    required property string modelData
                    text: modelData.split(/[\\/]/).pop()
                    ToolTip.visible: hovered
                    ToolTip.text: modelData
                    onTriggered: openRecentProject(modelData)
                }
                onObjectAdded: (index, object) => recentProjectsMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => recentProjectsMenu.removeItem(object)
            }
        }
        AppMenuItem { text: "Open demo project"; onTriggered: requestProjectAction("demo") }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: borderColor } }
        AppMenuItem { text: "Save project"; shortcutLabel: "Ctrl+S"; onTriggered: saveCurrentProject() }
        AppMenuItem { text: "Save project as…"; onTriggered: saveDialog.open() }
        AppMenuItem { text: "Open project location"; enabled: editor.hasProjectDirectory; onTriggered: editor.openProjectLocation() }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: borderColor } }
        AppMenuItem { text: "Relink selected media…"; enabled: !!selectedAsset && selectedAsset.missing; onTriggered: relinkFileDialog.open() }
        AppMenuItem { text: "Relink missing media folder…"; onTriggered: relinkFolderDialog.open() }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: borderColor } }
        AppMenuItem { text: "Settings…"; onTriggered: { settingsPage = 0; generativeSettingsDialog.open() } }
        AppMenuItem { text: "Open diagnostics folder"; onTriggered: crashReporter.openReportDirectory() }
    }
    Menu {
        id: windowMenu
        popupType: Popup.Item
        width: 200
        padding: 6
        background: Rectangle { color: panelRaised; border.color: "#3a4c60"; radius: 7 }
        AppMenuItem { text: "Minimize to taskbar"; onTriggered: windowControls.minimize() }
        AppMenuItem { text: window.maximized ? "Restore down" : "Maximize"; onTriggered: toggleMaximized() }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: borderColor } }
        AppMenuItem { text: "Close"; shortcutLabel: "Alt+F4"; onTriggered: window.close() }
    }
    Menu {
        id: clipContextMenu
        property var clipData: null
        popupType: Popup.Item
        width: 218
        padding: 6
        background: Rectangle { color: panelRaised; border.color: "#3a4c60"; radius: 7 }
        AppMenuItem { text: "Copy"; shortcutLabel: "Ctrl+C"; onTriggered: editor.copySelectedClip() }
        AppMenuItem { text: "Cut"; shortcutLabel: "Ctrl+X"; onTriggered: editor.cutSelectedClip() }
        AppMenuItem { text: "Delete"; shortcutLabel: "Del"; onTriggered: editor.removeSelectedClip() }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: borderColor } }
        AppMenuItem {
            text: "Export clip…"
            enabled: !!clipContextMenu.clipData && !editor.exportRunning
            onTriggered: chooseExportFile(clipContextMenu.clipData.hasVideo
                ? "timelineClipVideo" : "clipAudio")
        }
        AppMenuItem {
            text: "Export frame…"
            enabled: !!clipContextMenu.clipData && clipContextMenu.clipData.hasVideo
                && editor.playheadFrame >= clipContextMenu.clipData.start
                && editor.playheadFrame < clipContextMenu.clipData.start + clipContextMenu.clipData.duration
                && !editor.exportRunning
            onTriggered: chooseExportFile("projectFrame", editor.playheadFrame)
        }
    }
    Menu {
        id: trackContextMenu
        property int targetTrack: -1
        popupType: Popup.Item
        width: 224
        padding: 6
        background: Rectangle { color: panelRaised; border.color: "#3a4c60"; radius: 7 }
        AppMenuItem {
            text: "Paste at cursor"
            shortcutLabel: "Ctrl+V"
            enabled: editor.hasCopiedClip && editor.canPasteClipToTrack(trackContextMenu.targetTrack)
            onTriggered: editor.pasteClipToTrack(trackContextMenu.targetTrack)
        }
    }
    Menu {
        id: resolutionMenu
        popupType: Popup.Item
        width: 148
        padding: 5
        background: Rectangle { color: panelRaised; border.color: "#3a4c60"; radius: 7 }
        AppMenuItem { text: "720p"; onTriggered: editor.setExportResolution("720p") }
        AppMenuItem { text: "1080p"; onTriggered: editor.setExportResolution("1080p") }
    }
    Menu {
        id: aspectMenu
        popupType: Popup.Item
        width: 154
        padding: 5
        background: Rectangle { color: panelRaised; border.color: "#3a4c60"; radius: 7 }
        AppMenuItem { text: "16:9"; onTriggered: editor.setProjectAspectRatio("16:9") }
        AppMenuItem { text: "9:16"; onTriggered: editor.setProjectAspectRatio("9:16") }
        AppMenuItem { text: "1:1"; onTriggered: editor.setProjectAspectRatio("1:1") }
        AppMenuItem { text: "4:5"; onTriggered: editor.setProjectAspectRatio("4:5") }
        AppMenuItem { text: "3:4"; onTriggered: editor.setProjectAspectRatio("3:4") }
        AppMenuItem { text: "4:3"; onTriggered: editor.setProjectAspectRatio("4:3") }
        AppMenuItem { text: "2:1"; onTriggered: editor.setProjectAspectRatio("2:1") }
        AppMenuItem { text: "2:3"; onTriggered: editor.setProjectAspectRatio("2:3") }
        AppMenuItem { text: "21:9"; onTriggered: editor.setProjectAspectRatio("21:9") }
        AppMenuItem { text: "Custom…"; onTriggered: {
            const parts = editor.projectAspectRatio.split(":")
            customAspectWidth.text = parts[0]
            customAspectHeight.text = parts[1]
            customAspectDialog.open()
        } }
    }
    Dialog {
        id: customAspectDialog
        title: "Custom project aspect ratio"
        width: 320
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.NoButton
        background: Rectangle { color: panelColor; radius: 9; border.color: borderColor }
        contentItem: ColumnLayout {
            spacing: 12
            Label { text: "Aspect ratio"; color: textColor; font.pixelSize: 15; font.bold: true }
            Label { text: "Set the canvas width and height ratio."; color: mutedColor; font.pixelSize: 11 }
            RowLayout {
                Layout.fillWidth: true
                CaptionField { id: customAspectWidth; Layout.fillWidth: true
                    validator: IntValidator { bottom: 1; top: 100 }
                    horizontalAlignment: TextInput.AlignHCenter }
                Label { text: ":"; color: mutedColor; font.pixelSize: 15 }
                CaptionField { id: customAspectHeight; Layout.fillWidth: true
                    validator: IntValidator { bottom: 1; top: 100 }
                    horizontalAlignment: TextInput.AlignHCenter }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton { text: "Cancel"; onClicked: customAspectDialog.close() }
                AppButton { text: "Apply"; primary: true; enabled: customAspectWidth.acceptableInput && customAspectHeight.acceptableInput
                    onClicked: {
                        editor.setProjectAspectRatio(customAspectWidth.text + ":" + customAspectHeight.text)
                        customAspectDialog.close()
                    }
                }
            }
        }
    }
    Menu {
        id: frameRateMenu
        popupType: Popup.Item
        width: 225
        padding: 5
        background: Rectangle { color: panelRaised; border.color: "#3a4c60"; radius: 7 }
        AppMenuItem { text: "23.976 FPS  ·  24000/1001"; onTriggered: editor.setProjectFrameRate(24000, 1001) }
        AppMenuItem { text: "24 FPS  ·  24/1"; onTriggered: editor.setProjectFrameRate(24, 1) }
        AppMenuItem { text: "25 FPS  ·  25/1"; onTriggered: editor.setProjectFrameRate(25, 1) }
        AppMenuItem { text: "29.97 FPS  ·  30000/1001"; onTriggered: editor.setProjectFrameRate(30000, 1001) }
        AppMenuItem { text: "30 FPS  ·  30/1"; onTriggered: editor.setProjectFrameRate(30, 1) }
        AppMenuItem { text: "60 FPS  ·  60/1"; onTriggered: editor.setProjectFrameRate(60, 1) }
    }

    MediaPlayer {
        id: player
        source: editor.timelinePreviewEnabled || (selectedAsset && selectedAsset.isStillImage) ? "" : editor.previewUrl
        playbackRate: 1
        videoOutput: videoOutput
        audioOutput: AudioOutput {
            volume: window.previewVolume
            muted: window.previewMuted
        }
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia && source.toString() !== "") {
                position = Math.min(duration, editor.previewPositionMs)
            }
        }
        onErrorOccurred: function(error, errorString) {
            if (error !== MediaPlayer.NoError) playbackError.text = errorString
        }
    }
    MediaPlayer {
        id: audioPlayer1
        source: editor.timelinePreviewEnabled ? (editor.timelineAudio1.url || "") : ""
        playbackRate: editor.timelineAudio1.speed || 1
        audioOutput: AudioOutput { volume: Math.min(1, window.previewVolume * (editor.timelineAudio1.gain === undefined ? 1 : editor.timelineAudio1.gain)); muted: window.previewMuted || !!editor.timelineAudio1.muted }
        onMediaStatusChanged: if (mediaStatus === MediaPlayer.LoadedMedia) syncTimelineAudio(audioPlayer1, editor.timelineAudio1, 1, true)
    }
    MediaPlayer {
        id: audioPlayer2
        source: editor.timelinePreviewEnabled ? (editor.timelineAudio2.url || "") : ""
        playbackRate: editor.timelineAudio2.speed || 1
        audioOutput: AudioOutput { volume: Math.min(1, window.previewVolume * (editor.timelineAudio2.gain === undefined ? 1 : editor.timelineAudio2.gain)); muted: window.previewMuted || !!editor.timelineAudio2.muted }
        onMediaStatusChanged: if (mediaStatus === MediaPlayer.LoadedMedia) syncTimelineAudio(audioPlayer2, editor.timelineAudio2, 2, true)
    }
    Instantiator {
        id: extraAudioPlayers
        model: editor.tracks.filter(function(row) { return row.kind === "audio" && row.id !== 1 && row.id !== 2 })
        delegate: MediaPlayer {
            required property var modelData
            property int trackId: modelData.id
            property string activeClipId: ""
            source: editor.timelinePreviewEnabled && editor.playheadFrame >= 0
                ? (editor.timelineMediaForTrack(trackId).url || "") : ""
            playbackRate: editor.timelineMediaForTrack(trackId).speed || 1
            audioOutput: AudioOutput {
                volume: Math.min(1, window.previewVolume *
                    (editor.timelineMediaForTrack(trackId).gain === undefined ? 1 : editor.timelineMediaForTrack(trackId).gain))
                muted: window.previewMuted || !!editor.timelineMediaForTrack(trackId).muted
            }
            onMediaStatusChanged: if (mediaStatus === MediaPlayer.LoadedMedia)
                syncExtraTimelineAudio(this, editor.timelineMediaForTrack(trackId), true)
        }
    }
    Connections {
        target: editor
        function onPlayheadChanged() { syncTimelinePlayers(false) }
        function onPlaybackChanged() {
            if (!editor.timelinePlaying) {
                player.pause()
                for (let index = 0; index < videoLayerRepeater.count; ++index) {
                    const layer = videoLayerRepeater.itemAt(index)
                    if (layer) layer.sync(true)
                }
                audioPlayer1.pause()
                audioPlayer2.pause()
                for (let index = 0; index < extraAudioPlayers.count; ++index) {
                    const target = extraAudioPlayers.objectAt(index)
                    if (target) target.pause()
                }
            } else syncTimelinePlayers(true)
        }
    }

    Shortcut { sequence: "Ctrl+Z"; onActivated: editor.undo() }
    Shortcut { sequence: "Ctrl+N"; onActivated: requestProjectAction("new") }
    Shortcut { sequence: "Ctrl+O"; onActivated: requestProjectAction("open") }
    Shortcut { sequence: "Alt+F"; onActivated: openProjectMenu() }
    Shortcut { sequence: "Alt+Space"; onActivated: { windowMenu.x = 8; windowMenu.y = 2; windowMenu.open() } }
    Shortcut { sequence: "Ctrl+S"; onActivated: saveCurrentProject() }
    Shortcut { sequence: "Ctrl+Y"; onActivated: editor.redo() }
    Shortcut { sequence: "Ctrl+K"; onActivated: editor.splitSelectedClip() }
    Shortcut { sequence: "Delete"; enabled: !collectionTextEditing && !(window.activeFocusItem && window.activeFocusItem.cursorPosition !== undefined); onActivated: { if (libraryMode === 1) deleteCollectionSelection(); else if (!deleteSelectedTimelineKeyframe()) { if (editor.selectedCaptionRowId !== "") editor.removeSelectedCaption(); else if (editor.selectedTitleId !== "") editor.removeSelectedTitle(); else editor.removeSelectedClip() } } }
    Shortcut { sequence: "Ctrl+C"; enabled: libraryMode === 1 && selectedSegmentIds.length > 0 && !collectionTextEditing; onActivated: { segmentClipboard = selectedSegmentIds.slice(); segmentClipboardCut = false } }
    Shortcut { sequence: "Ctrl+X"; enabled: libraryMode === 1 && selectedSegmentIds.length > 0 && !collectionTextEditing; onActivated: { segmentClipboard = selectedSegmentIds.slice(); segmentClipboardCut = true } }
    Shortcut { sequence: "Ctrl+V"; enabled: libraryMode === 1 && segmentClipboard.length > 0 && openedCollectionId !== "" && !collectionTextEditing; onActivated: pasteSegmentsInto(openedCollectionId) }
    Shortcut { sequence: "Ctrl+C"; enabled: libraryMode !== 1 && editor.selectedClipId !== "" && !(window.activeFocusItem && window.activeFocusItem.cursorPosition !== undefined); onActivated: editor.copySelectedClip() }
    Shortcut { sequence: "Ctrl+X"; enabled: libraryMode !== 1 && editor.selectedClipId !== "" && !(window.activeFocusItem && window.activeFocusItem.cursorPosition !== undefined); onActivated: editor.cutSelectedClip() }
    Shortcut { sequence: "Ctrl+V"; enabled: libraryMode !== 1 && editor.hasCopiedClip && editor.canPasteClipToTrack(selectedTrack) && !(window.activeFocusItem && window.activeFocusItem.cursorPosition !== undefined); onActivated: editor.pasteClipToTrack(selectedTrack) }
    Shortcut { sequence: "Ctrl+A"; enabled: libraryMode === 1 && openedCollectionId !== "" && !collectionTextEditing; onActivated: selectedSegmentIds = editor.segments.filter(function(s) { return s.collectionId === openedCollectionId }).map(function(s) { return s.id }) }
    Shortcut { sequence: "["; onActivated: editor.trimClipStart(editor.selectedClipId, editor.playheadFrame) }
    Shortcut { sequence: "]"; onActivated: editor.trimClipEnd(editor.selectedClipId, editor.playheadFrame + 1) }

    header: Rectangle {
        id: headerBar
        height: 49
        color: panelColor
        border.color: borderColor
        Item {
            anchors.fill: parent
            DragHandler {
                target: null
                enabled: window.normalWindow
                acceptedButtons: Qt.LeftButton
                onActiveChanged: if (active && window.normalWindow) window.startSystemMove()
            }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onDoubleTapped: toggleMaximized()
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onPressed: (mouse) => {
                    const point = headerBar.mapToItem(window.contentItem, mouse.x, mouse.y)
                    windowMenu.x = point.x
                    windowMenu.y = point.y
                    windowMenu.open()
                }
            }
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 0
            spacing: 8
                    Item {
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        Layout.minimumWidth: 24
                        Layout.minimumHeight: 24

                        Image {
                            anchors.fill: parent
                            source: "qrc:/app/datacut.png"
                            sourceSize.width: 48
                            sourceSize.height: 48
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            mipmap: true
                        }
                    }
            Label { text: "DataCut"; color: textColor; font.pixelSize: 15; font.bold: true; Layout.rightMargin: 22 }
            AppButton { id: projectButton; text: "Project  ⌄"; subtle: true; onClicked: openProjectMenu() }
            AppButton { text: "Settings  ⌄"; subtle: true; onClicked: { settingsPage = 0; generativeSettingsDialog.open() } }
            Rectangle { width: 1; height: 21; color: borderColor; Layout.leftMargin: 2; Layout.rightMargin: 2 }
            IconButton { symbol: "▣"; ToolTip.visible: hovered; ToolTip.text: "Save project"; onClicked: saveCurrentProject() }
            IconButton { symbol: "↶"; enabled: editor.canUndo; ToolTip.visible: hovered; ToolTip.text: "Undo"; onClicked: editor.undo() }
            IconButton { symbol: "↷"; enabled: editor.canRedo; ToolTip.visible: hovered; ToolTip.text: "Redo"; onClicked: editor.redo() }
            Item { Layout.fillWidth: true }
            RowLayout {
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: window.width >= 1450 ? 215 : 106
                Layout.maximumWidth: window.width >= 1450 ? 260 : 106
                spacing: 7
                Label { text: editor.projectName; color: textColor; font.pixelSize: 12; Layout.maximumWidth: 108; elide: Text.ElideRight }
                Label { text: "•  " + (editor.dirty ? "Unsaved changes" : editor.status); visible: window.width >= 1450; color: mutedColor; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
            }
            Item { Layout.fillWidth: true }
            AppButton { id: resolutionButton; text: editor.exportResolution + "  ⌄"; Layout.preferredWidth: 84; onClicked: openResolutionMenu() }
            AppButton { id: aspectButton; text: editor.projectAspectRatio + "  ⌄"; Layout.preferredWidth: 74; ToolTip.visible: hovered; ToolTip.text: "Project aspect ratio"; onClicked: openAspectMenu() }
            AppButton { id: frameRateButton; text: projectRateLabel() + "  ⌄"; Layout.preferredWidth: 112; ToolTip.visible: hovered; ToolTip.text: "Project frame rate: " + editor.projectRate; onClicked: openFrameRateMenu() }
            AppButton { text: "↥  Export"; primary: true; enabled: editor.selectedAssetId !== "" || editor.clips.length > 0; Layout.preferredWidth: 96; onClicked: exportDialog.open() }
            Rectangle { width: 1; height: 24; color: borderColor; Layout.leftMargin: 5; Layout.rightMargin: 1 }
            WindowControl { frameAction: "minimize"; ToolTip.visible: hovered; ToolTip.text: "Minimize to taskbar"; onClicked: windowControls.minimize() }
            WindowControl { frameAction: window.maximized ? "restore" : "maximize"; ToolTip.visible: hovered; ToolTip.text: window.maximized ? "Restore down" : "Maximize"; onClicked: toggleMaximized() }
            WindowControl { frameAction: "close"; ToolTip.visible: hovered; ToolTip.text: "Close"; onClicked: window.close() }
        }
        Item {
            anchors.fill: parent
            z: 10
            visible: window.normalWindow
            MouseArea { x: 0; y: 0; width: 12; height: 12; cursorShape: Qt.SizeFDiagCursor; onPressed: window.startSystemResize(Qt.TopEdge | Qt.LeftEdge) }
            MouseArea { x: 12; y: 0; width: parent.width - 24; height: 6; cursorShape: Qt.SizeVerCursor; onPressed: window.startSystemResize(Qt.TopEdge) }
            MouseArea { x: parent.width - 12; y: 0; width: 12; height: 12; cursorShape: Qt.SizeBDiagCursor; onPressed: window.startSystemResize(Qt.TopEdge | Qt.RightEdge) }
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical
        handle: Rectangle { implicitHeight: 1; color: borderColor }

        SplitView {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 330
            orientation: Qt.Horizontal
            handle: Rectangle { implicitWidth: 1; color: borderColor }

            Rectangle {
                SplitView.preferredWidth: 120
                SplitView.minimumWidth: 110
                SplitView.maximumWidth: 132
                color: panelColor
                Column {
                    anchors.fill: parent
                    anchors.topMargin: 12
                    spacing: 2
                    Repeater {
                        model: [
                            { icon: "□", label: "Files", mode: 0, available: true },
                            { icon: "T", label: "Objects", mode: 4, available: true },
                            { icon: "✦", label: "Generate", mode: 7, available: true },
                            { icon: "▦", label: "Collections", mode: 1, available: true },
                            { icon: "☆", label: "Effects", mode: 2, available: true },
                            { icon: "⋈", label: "Transitions", mode: 3, available: true },
                            { icon: "♫", label: "Audio", mode: 5, available: true },
                            { icon: "↥", label: "Export", mode: 6, available: true }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            width: parent.width - 8
                            x: 4
                            height: 44
                            radius: 7
                            color: libraryMode === modelData.mode ? accentSoft : navMouse.containsMouse ? controlColor : "transparent"
                            Behavior on color { ColorAnimation { duration: 110 } }
                            Rectangle { width: 2; height: parent.height; color: accentColor; visible: libraryMode === modelData.mode }
                            Row {
                                x: 19; anchors.verticalCenter: parent.verticalCenter; spacing: 14
                                Text { text: modelData.icon; width: 22; color: modelData.available ? "#e0ecfa" : "#627386"; font.pixelSize: 23; font.family: "Segoe UI Symbol"; horizontalAlignment: Text.AlignHCenter }
                                Text { text: modelData.label; color: modelData.available ? textColor : "#627386"; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                            }
                            MouseArea { id: navMouse; anchors.fill: parent; hoverEnabled: true; enabled: modelData.available; onClicked: libraryMode = modelData.mode }
                        }
                    }
                }
            }

            Rectangle {
                SplitView.preferredWidth: 385
                SplitView.minimumWidth: 310
                color: panelColor
                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 12
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 43
                        Layout.minimumHeight: 43
                        Layout.maximumHeight: 43
                        spacing: 20
                        Repeater {
                            model: libraryMode === 0 ? ["Files"]
                                : libraryMode === 4 ? ["Objects"]
                                : libraryMode === 1 ? ["Collections", "Saved Segments"]
                                : libraryMode === 2 ? ["Effects"]
                                : libraryMode === 3 ? ["Transitions"]
                                : libraryMode === 6 ? ["Export"]
                                : libraryMode === 7 ? ["Generate"] : ["Library"]
                            delegate: Item {
                                required property string modelData
                                required property int index
                                Layout.preferredWidth: label.implicitWidth + 4
                                Layout.fillHeight: true
                                Label { id: label; anchors.centerIn: parent; text: modelData; color: index === 0 ? textColor : mutedColor; font.pixelSize: 12 }
                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: accentColor; visible: index === 0 }
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: borderColor }
                    Rectangle {
                        visible: libraryMode === 0
                        Layout.fillWidth: true
                        Layout.topMargin: 12
                        Layout.minimumHeight: 31
                        Layout.maximumHeight: 31
                        height: 31
                        color: controlColor
                        radius: 5
                        border.color: "#334154"
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 9; anchors.rightMargin: 9
                            Label { text: "⌕"; color: mutedColor; font.pixelSize: 18 }
                            CaptionField {
                                Layout.fillWidth: true
                                placeholderText: "Search project files…"
                                placeholderTextColor: mutedColor
                                color: textColor
                                font.pixelSize: 11
                                background: Item {}
                                onTextChanged: searchQuery = text
                            }
                            Label { text: "☷"; color: mutedColor; font.pixelSize: 15 }
                        }
                    }
                    RowLayout {
                        visible: libraryMode === 0
                        Layout.fillWidth: true
                        Layout.topMargin: 10
                        Layout.bottomMargin: 10
                        Layout.minimumHeight: 32
                        Layout.maximumHeight: 32
                        AppButton { text: "+  Import"; onClicked: importDialog.open() }
                        AppButton { text: "+ Playhead"; enabled: !!selectedAsset; onClicked: if (selectedAsset) editor.placeAssetOnTimeline(selectedAsset.id, editor.playheadFrame, selectedAsset.hasVideo ? 0 : 1, snappingEnabled, snapToleranceFrames) }
                        Item { Layout.fillWidth: true }
                        IconButton { symbol: "▦"; selected: true }
                        IconButton { symbol: "☷"; enabled: false }
                    }
                    RowLayout {
                        visible: libraryMode === 1
                        Layout.fillWidth: true
                        Layout.topMargin: 7
                        Layout.bottomMargin: 8
                        Layout.minimumHeight: 32
                        Layout.maximumHeight: 32
                        IconButton { symbol: "←"; visible: openedCollectionId !== ""; onClicked: { collectionInspector.commitDetails(); openedCollectionId = ""; selectedSegmentIds = []; collectionInspectorActive = true } }
                        Label { text: openedCollectionId === "" ? "Collections" : (inspectedCollection?.name || "Collection"); color: textColor; font.pixelSize: 13; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                        Item { Layout.fillWidth: true }
                        AppButton { text: "+ Folder"; subtle: true; onClicked: newCollectionDialog.open() }
                        AppButton { text: "Export…"; subtle: true; visible: openedCollectionId !== ""; enabled: !editor.exportRunning && editor.segments.some(function(segment) { return segment.collectionId === openedCollectionId }); onClicked: exportCollectionFolderDialog.open() }
                    }
                    RowLayout {
                        visible: libraryMode === 1
                        Layout.fillWidth: true
                        Layout.preferredHeight: 35
                        AppButton { text: "Copy"; subtle: true; enabled: selectedSegmentIds.length > 0; onClicked: { segmentClipboard = selectedSegmentIds.slice(); segmentClipboardCut = false } }
                        AppButton { text: "Cut"; subtle: true; enabled: selectedSegmentIds.length > 0; onClicked: { segmentClipboard = selectedSegmentIds.slice(); segmentClipboardCut = true } }
                        AppButton { text: "Paste"; subtle: true; enabled: openedCollectionId !== "" && segmentClipboard.length > 0; onClicked: pasteSegmentsInto(openedCollectionId) }
                        AppButton { text: "Delete"; subtle: true; enabled: selectedSegmentIds.length > 0 || selectedCollectionTileId !== ""; onClicked: deleteCollectionSelection() }
                        Item { Layout.fillWidth: true }
                        Label { text: openedCollectionId === "" ? editor.collections.length + " folders" : editor.segments.filter(function(segment) { return segment.collectionId === openedCollectionId }).length + " items"; color: mutedColor; font.pixelSize: 10 }
                    }
                    RowLayout {
                        visible: libraryMode === 0
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        Layout.bottomMargin: 8
                        Layout.minimumHeight: 24
                        Layout.maximumHeight: 24
                        Label { text: filesGenerationKind === "" ? "Files in Project" : "Generated " + filesGenerationKind; color: textColor; font.pixelSize: 12; font.bold: true }
                        Item { Layout.fillWidth: true }
                        Label { text: filesGenerationKind === "" ? editor.assets.length + " files" : filesGenerationResults.length + " files"; color: mutedColor; font.pixelSize: 11 }
                    }
                    RowLayout {
                        visible: libraryMode === 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: 27
                        spacing: 4
                        AppButton { text: "Project"; subtle: true; primary: filesGenerationKind === ""; onClicked: filesGenerationKind = "" }
                        Repeater { model: ["Image", "Video", "Music", "Sound FX"]
                            delegate: AppButton { required property string modelData; required property int index; text: modelData; subtle: true; primary: filesGenerationKind === ["image", "video", "music", "sound"][index]; onClicked: { filesGenerationKind = ["image", "video", "music", "sound"][index]; filesGenerationResults = projectGenerationResults(filesGenerationKind) } }
                        }
                    }
                    GridView {
                        id: mediaGrid
                        visible: libraryMode === 0 && filesGenerationKind === ""
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        TapHandler { target: null; onTapped: (point) => {
                            if (emptyGridPoint(mediaGrid, point.position.x, point.position.y)) clearLibrarySelection()
                        } }
                        cellWidth: Math.max(82, Math.floor(width / 4))
                        cellHeight: 101
                        model: editor.assets.filter(function(asset) { return asset.name.toLowerCase().includes(searchQuery.toLowerCase()) })
                        delegate: Item {
                            required property var modelData
                            width: mediaGrid.cellWidth; height: mediaGrid.cellHeight
                            Rectangle {
                                anchors.fill: parent; anchors.rightMargin: 5; anchors.bottomMargin: 2
                                radius: 6
                                color: editor.selectedAssetId === modelData.id ? "#1b3959" : "transparent"
                                border.width: editor.selectedAssetId === modelData.id ? 2 : 0
                                border.color: accentColor
                                Behavior on color { ColorAnimation { duration: 140 } }
                                Behavior on border.width { NumberAnimation { duration: 140 } }
                            }
                            Rectangle {
                                x: 1; y: 1; width: parent.width - 8; height: 66
                                radius: 5; clip: true
                                color: "#254054"
                                border.width: editor.selectedAssetId === modelData.id ? 2 : 1
                                border.color: editor.selectedAssetId === modelData.id ? accentColor : borderColor
                                Image { anchors.fill: parent; source: modelData.hasVideo ? modelData.thumbnail : modelData.waveform; fillMode: modelData.hasVideo ? Image.PreserveAspectCrop : Image.Stretch; asynchronous: true }
                                Label { anchors.centerIn: parent; text: modelData.hasVideo ? "▶" : "♫"; color: "#a1ceff"; font.pixelSize: 22; visible: modelData.thumbnail.toString() === "" }
                                Rectangle { anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 3; width: timeLabel.implicitWidth + 7; height: 16; radius: 2; color: "#bb0a1016"
                                    Label { id: timeLabel; anchors.centerIn: parent; text: clockTime(modelData.durationUs / 1000); color: "white"; font.pixelSize: 10 }
                                }
                            }
                            Label { x: 2; y: 72; width: parent.width - 8; text: modelData.name; color: textColor; font.pixelSize: 10; elide: Text.ElideRight }
                            MouseArea {
                                id: assetMouse
                                anchors.fill: parent
                                preventStealing: true
                                hoverEnabled: true
                                ToolTip.visible: containsMouse && draggedAssetId === ""
                                ToolTip.text: "Drag to a timeline track · Double-click to append"
                                property real pressX: 0
                                property real pressY: 0
                                onPressed: (mouse) => {
                                    pressX = mouse.x
                                    pressY = mouse.y
                                    editor.selectAsset(modelData.id)
                                }
                                onPositionChanged: (mouse) => {
                                    if (!(mouse.buttons & Qt.LeftButton)) return
                                    if (draggedAssetId === ""
                                        && Math.hypot(mouse.x - pressX, mouse.y - pressY) < 8) return
                                    draggedAssetId = modelData.id
                                    draggedAssetName = modelData.name
                                    moveAssetDrag(assetMouse, mouse.x, mouse.y)
                                }
                                onReleased: (mouse) => {
                                    if (draggedAssetId === modelData.id) {
                                        const target = timelineDropPosition(assetMouse, mouse.x, mouse.y)
                                        if (target.valid)
                                            editor.placeAssetOnTimeline(modelData.id, target.frame,
                                                                        target.track, snappingEnabled,
                                                                        snapToleranceFrames)
                                    }
                                    draggedAssetId = ""
                                }
                                onCanceled: draggedAssetId = ""
                                onDoubleClicked: editor.addToTimeline(modelData.id)
                            }
                        }
                        Label { anchors.centerIn: parent; visible: editor.assets.length === 0; text: "Import media or open the demo project"; color: mutedColor; font.pixelSize: 11 }
                    }
                    GridView {
                        id: collectionFolderGrid
                        visible: libraryMode === 1 && openedCollectionId === ""
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true
                        TapHandler { target: null; onTapped: (point) => {
                            if (emptyGridPoint(collectionFolderGrid, point.position.x, point.position.y)) clearLibrarySelection()
                        } }
                        cellWidth: Math.max(114, Math.floor(width / 3))
                        cellHeight: 127
                        model: editor.collections
                        delegate: Item {
                            required property var modelData
                            width: collectionFolderGrid.cellWidth; height: collectionFolderGrid.cellHeight
                            Rectangle {
                                anchors.fill: parent; anchors.rightMargin: 7; anchors.bottomMargin: 5
                                radius: 7
                                color: selectedCollectionTileId === modelData.id ? "#1b3959" : "#192635"
                                border.width: selectedCollectionTileId === modelData.id ? 2 : 1
                                border.color: selectedCollectionTileId === modelData.id ? accentColor : borderColor
                                Behavior on color { ColorAnimation { duration: 140 } }
                                Rectangle { x: 20; y: 19; width: 36; height: 11; radius: 3; color: "#8ab8ea" }
                                Rectangle { x: 19; y: 26; width: parent.width - 38; height: 48; radius: 5; color: "#6c9dd4"; border.color: "#a7cbf0" }
                                Label { x: 8; y: 84; width: parent.width - 16; text: modelData.name; color: textColor; font.pixelSize: 11; font.bold: true; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter }
                                Label { x: 8; y: 101; width: parent.width - 16; text: modelData.count + " segments"; color: mutedColor; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter }
                            }
                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                ToolTip.visible: containsMouse
                                ToolTip.text: "Double-click to open folder"
                                onClicked: { collectionInspector.commitDetails(); selectedCollectionTileId = modelData.id; selectedSegmentIds = []; collectionInspectorActive = true }
                                onDoubleClicked: openCollection(modelData.id)
                            }
                        }
                        Label { anchors.centerIn: parent; visible: editor.collections.length === 0; text: "Create a collection folder"; color: mutedColor; font.pixelSize: 11 }
                    }
                    GridView {
                        id: collectionSegmentGrid
                        visible: libraryMode === 1 && openedCollectionId !== ""
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true
                        TapHandler { target: null; onTapped: (point) => {
                            if (emptyGridPoint(collectionSegmentGrid, point.position.x, point.position.y)) clearLibrarySelection()
                        } }
                        cellWidth: Math.max(114, Math.floor(width / 3))
                        cellHeight: 130
                        model: editor.segments.filter(function(segment) { return segment.collectionId === openedCollectionId })
                        delegate: Item {
                            required property var modelData
                            required property int index
                            width: collectionSegmentGrid.cellWidth; height: collectionSegmentGrid.cellHeight
                            Rectangle {
                                anchors.fill: parent; anchors.rightMargin: 7; anchors.bottomMargin: 5
                                radius: 7
                                opacity: segmentClipboardCut && segmentClipboard.includes(modelData.id) ? 0.55 : 1
                                color: selectedSegmentIds.includes(modelData.id) ? "#1b3959" : controlColor
                                border.width: selectedSegmentIds.includes(modelData.id) ? 2 : 1
                                border.color: selectedSegmentIds.includes(modelData.id) ? accentColor : borderColor
                                Behavior on color { ColorAnimation { duration: 140 } }
                                Rectangle { x: 5; y: 5; width: parent.width - 10; height: 75; radius: 4; color: "#263b4c"; clip: true
                                    Image { anchors.fill: parent; source: modelData.hasVideo ? modelData.thumbnail : modelData.waveform; fillMode: modelData.hasVideo ? Image.PreserveAspectCrop : Image.Stretch; asynchronous: true }
                                    Label { anchors.centerIn: parent; text: modelData.hasVideo ? "▶" : "♫"; color: "#a6d4ff"; font.pixelSize: 25; visible: (modelData.hasVideo ? modelData.thumbnail : modelData.waveform).toString() === "" }
                                    Rectangle { anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 3; width: durationLabel.implicitWidth + 8; height: 17; radius: 3; color: "#b80a1119"
                                        Label { id: durationLabel; anchors.centerIn: parent; text: clockTime(modelData.durationMs); color: "white"; font.pixelSize: 10 }
                                    }
                                }
                                Label { x: 7; y: 85; width: parent.width - 14; text: modelData.label; color: textColor; font.pixelSize: 11; elide: Text.ElideRight }
                                Label { x: 7; y: 103; width: parent.width - 14; text: modelData.timelineFrames > 0
                                    ? modelData.timelineFrames + " project frames"
                                    : modelData.frameCount + (modelData.hasVideo ? " source frames" : " samples"); color: mutedColor; font.pixelSize: 10; elide: Text.ElideRight }
                            }
                            MouseArea {
                                id: segmentMouse
                                anchors.fill: parent
                                preventStealing: true
                                hoverEnabled: true
                                ToolTip.visible: containsMouse && draggedSegmentId === ""
                                ToolTip.text: "Ctrl/Shift to select · drag to timeline"
                                property real pressX: 0
                                property real pressY: 0
                                onPressed: (mouse) => {
                                    pressX = mouse.x
                                    pressY = mouse.y
                                    selectSegmentTile(modelData.id, index, mouse.modifiers)
                                }
                                onPositionChanged: (mouse) => {
                                    if (!(mouse.buttons & Qt.LeftButton)) return
                                    if (draggedSegmentId === ""
                                        && Math.hypot(mouse.x - pressX, mouse.y - pressY) < 8) return
                                    draggedSegmentId = modelData.id
                                    draggedAssetName = modelData.label
                                    moveAssetDrag(segmentMouse, mouse.x, mouse.y)
                                }
                                onReleased: (mouse) => {
                                    if (draggedSegmentId === modelData.id) {
                                        const target = timelineDropPosition(segmentMouse, mouse.x, mouse.y)
                                        if (target.valid)
                                            editor.placeSegmentOnTimeline(modelData.id, target.frame,
                                                                          target.track, snappingEnabled,
                                                                          snapToleranceFrames)
                                    }
                                    draggedSegmentId = ""
                                }
                                onCanceled: draggedSegmentId = ""
                            }
                        }
                        Label { anchors.centerIn: parent; visible: !editor.segments.some(function(segment) { return segment.collectionId === openedCollectionId }); text: "Save a timeline range or paste segments here"; color: mutedColor; font.pixelSize: 11 }
                    }
                    ColumnLayout {
                        id: generateLibraryPanel
                        visible: libraryMode === 7
                        Layout.fillWidth: true; Layout.fillHeight: true
                        spacing: 12
                        Rectangle {
                            Layout.fillWidth: true; implicitHeight: 38; radius: 7
                            color: controlColor; border.color: borderColor
                            RowLayout { anchors.fill: parent; anchors.margins: 3; spacing: 3
                                Repeater { model: ["Image", "Video", "Music", "Sound FX"]
                                    delegate: AppButton {
                                        required property string modelData; required property int index
                                        Layout.fillWidth: true; Layout.fillHeight: true
                                        text: modelData; primary: generationTab === index; subtle: generationTab !== index
                                        onClicked: generationTab = index
                                    }
                                }
                            }
                        }
                        ScrollView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                          ColumnLayout { width: Math.max(200, generateLibraryPanel.width - 24); spacing: 10
                            RowLayout { Layout.fillWidth: true; spacing: 8
                                Rectangle { width: 7; height: 7; radius: 4; color: generationProvider && generationProvider.ready ? "#72d5ad" : "#e8ad8c" }
                                Label { text: generationProvider ? generationProvider.name : ""; color: textColor; font.pixelSize: 12; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                            }
                            Label { text: generationProvider ? generationProvider.status : ""; color: generationProvider && generationProvider.ready ? "#8fd9b7" : "#e8ad8c"; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 11 }
                            AppButton { text: "Open generation settings…"; subtle: true; visible: !generationProvider || !generationProvider.ready; onClicked: openGenerationSettings(0) }
                            Label { text: "PROMPT"; color: mutedColor; font.pixelSize: 10; font.bold: true; Layout.topMargin: 7 }
                            AppTextArea { id: generationPrompt; Layout.fillWidth: true; Layout.preferredHeight: 112; wrapMode: TextEdit.Wrap; placeholderText: generationTab === 2 ? "Describe the music, style, instruments and mood" : generationTab === 3 ? "Describe the sound effect" : "Describe what to create" }
                            Label { text: "Avoid (optional)"; visible: generationTab === 1; color: textColor; font.pixelSize: 11 }
                            CaptionField { id: generationNegative; visible: generationTab === 1; Layout.fillWidth: true; placeholderText: "Blurry, distorted…" }
                            Label { text: "Lyrics (optional)"; visible: generationTab === 2; color: textColor; font.pixelSize: 11 }
                            AppTextArea { id: generationLyrics; visible: generationTab === 2; Layout.fillWidth: true; Layout.preferredHeight: 78; wrapMode: TextEdit.Wrap; placeholderText: "Leave blank for instrumental" }
                            Label { text: "REFERENCE IMAGE"; visible: generationTab < 2; color: mutedColor; font.pixelSize: 10; font.bold: true; Layout.topMargin: 8 }
                            Rectangle { visible: generationTab < 2; Layout.fillWidth: true; implicitHeight: 42; radius: 7; color: controlColor; border.color: borderColor
                                RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 5; spacing: 8
                                    Label { text: generationReferenceImage === "" ? "No reference selected" : generationReferenceImage.split(/[\\/]/).pop(); color: generationReferenceImage === "" ? dimColor : textColor; font.pixelSize: 11; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                    AppButton { text: generationReferenceImage === "" ? "Choose" : "Change"; implicitHeight: 30; onClicked: generationImageFile.open() }
                                    IconButton { symbol: "×"; visible: generationReferenceImage !== ""; implicitWidth: 26; onClicked: generationReferenceImage = ""; ToolTip.visible: hovered; ToolTip.text: "Clear reference image" }
                                }
                            }
                            Rectangle { Layout.fillWidth: true; height: 1; color: borderColor; Layout.topMargin: 6 }
                            Label { text: "OUTPUT SETTINGS"; color: mutedColor; font.pixelSize: 10; font.bold: true; Layout.topMargin: 3 }
                            RowLayout { visible: generationTab < 2; Layout.fillWidth: true; spacing: 10
                                ColumnLayout { Layout.fillWidth: true; spacing: 5
                                    Label { text: "Width"; color: mutedColor; font.pixelSize: 11 }
                                    CaptionField { id: generationWidth; Layout.fillWidth: true; text: generationTab === 0 ? "768" : "512"; unit: "px"; validator: IntValidator { bottom: 256; top: 1536 } }
                                }
                                ColumnLayout { Layout.fillWidth: true; spacing: 5
                                    Label { text: "Height"; color: mutedColor; font.pixelSize: 11 }
                                    CaptionField { id: generationHeight; Layout.fillWidth: true; text: generationTab === 0 ? "768" : "512"; unit: "px"; validator: IntValidator { bottom: 256; top: 1536 } }
                                }
                            }
                            RowLayout { Layout.fillWidth: true; spacing: 10
                                ColumnLayout { Layout.fillWidth: true; spacing: 5
                                    Label { text: generationTab === 0 ? "Steps" : "Duration"; color: mutedColor; font.pixelSize: 11 }
                                    CaptionField { id: generationDuration; Layout.fillWidth: true; text: generationTab === 0 ? "4" : generationTab === 2 ? "30" : "10"; unit: generationTab === 0 ? "" : "sec"; validator: IntValidator { bottom: 1; top: generationTab === 1 ? 10 : generationTab === 3 ? 30 : 300 } }
                                }
                                ColumnLayout { Layout.fillWidth: true; spacing: 5
                                    Label { text: "Seed"; color: mutedColor; font.pixelSize: 11 }
                                    RowLayout { Layout.fillWidth: true; spacing: 3
                                        CaptionField { id: generationSeed; Layout.fillWidth: true; text: "-1"; validator: IntValidator { bottom: -1; top: 2147483647 } ToolTip.visible: hovered; ToolTip.text: "-1 selects a random seed" }
                                        IconButton { symbol: "⚄"; implicitWidth: 28; ToolTip.visible: hovered; ToolTip.text: "Randomize seed"; onClicked: generationSeed.text = String(Math.floor(Math.random() * 2147483647)) }
                                    }
                                }
                            }
                            ColumnLayout { visible: generationTab > 0; Layout.fillWidth: true; spacing: 5
                                Label { text: "Inference steps"; color: mutedColor; font.pixelSize: 11 }
                                CaptionField { id: generationSteps; Layout.fillWidth: true; text: generationTab === 1 ? "8" : generationTab === 2 ? "8" : "50"; validator: IntValidator { bottom: generationTab === 3 ? 10 : 1; top: generationTab === 3 ? 100 : 200 } }
                                Label { text: "24 FPS · up to 10 s"; visible: generationTab === 1; color: dimColor; font.pixelSize: 10 }
                                Label { text: "50 recommended"; visible: generationTab === 3; color: dimColor; font.pixelSize: 10 }
                            }
                            AppButton { text: mediaGenerations.running && mediaGenerations.activeKind === generationKind ? "Generating…" : "Generate " + ["image", "video", "music", "sound effect"][generationTab]; primary: true; Layout.fillWidth: true; Layout.topMargin: 10; implicitHeight: 38; enabled: generationProvider && generationProvider.ready && !mediaGenerations.running && generationPrompt.text.trim().length > 0
                                onClicked: mediaGenerations.generate(generationKind, {"prompt": generationPrompt.text.trim(), "negative": generationNegative.text.trim(), "lyrics": generationLyrics.text.trim(), "reference_image": generationReferenceImage,
                                    "width": Number(generationWidth.text), "height": Number(generationHeight.text), "duration": Number(generationDuration.text), "steps": generationTab === 0 ? Number(generationDuration.text) : Number(generationSteps.text),
                                    "seed": Number(generationSeed.text), "frames": Math.max(9, 1 + Math.floor(Number(generationDuration.text) * 24 / 8) * 8), "fps": 24}) }
                            RowLayout { visible: mediaGenerations.running; Layout.fillWidth: true
                                Label { text: mediaGenerations.stage; color: textColor; Layout.fillWidth: true; elide: Text.ElideRight }
                                AppButton { text: "Cancel"; onClicked: mediaGenerations.cancel() }
                            }
                            JobProgressBar { visible: mediaGenerations.running; Layout.fillWidth: true; indeterminate: !mediaGenerations.progressKnown; value: mediaGenerations.progress }
                            Label { text: mediaGenerations.error; visible: mediaGenerations.error !== ""; color: "#ffb899"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                            RowLayout { visible: mediaGenerations.error !== ""; Layout.fillWidth: true; spacing: 6
                                AppButton { text: "Copy error"; subtle: true; onClicked: mediaGenerations.copyError() }
                                AppButton { text: "Open logs"; subtle: true; onClicked: mediaGenerations.openLogFolder(generationKind) }
                                Item { Layout.fillWidth: true }
                            }
                            Rectangle { Layout.fillWidth: true; height: 1; color: borderColor }
                            Label { text: "Results"; color: textColor; font.pixelSize: 12; font.bold: true }
                            ListView { Layout.fillWidth: true; Layout.preferredHeight: Math.min(220, generationResults.length * 36); clip: true; model: generationResults
                                delegate: AppButton { required property var modelData; width: ListView.view.width; text: modelData.name; subtle: true; onClicked: editor.importMedia(modelData.url) }
                            }
                            Label { visible: generationResults.length === 0; text: "Generated media in this project appears here and in Files."; color: mutedColor; font.pixelSize: 10 }
                          }
                        }
                    }
                    GridView {
                        id: generatedFilesGrid
                        visible: libraryMode === 0 && filesGenerationKind !== ""
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true
                        cellWidth: Math.max(82, Math.floor(width / 4))
                        cellHeight: 101
                        model: filesGenerationResults
                        delegate: Item { required property var modelData; width: generatedFilesGrid.cellWidth; height: generatedFilesGrid.cellHeight
                            Rectangle { anchors.fill: parent; anchors.rightMargin: 5; anchors.bottomMargin: 3; radius: 6; color: controlColor; border.color: borderColor }
                            Image { x: 3; y: 3; width: parent.width - 10; height: 65; source: filesGenerationKind === "image" ? modelData.url : ""; fillMode: Image.PreserveAspectCrop; asynchronous: true }
                            Label { anchors.centerIn: parent; text: filesGenerationKind === "video" ? "▶" : "♫"; font.pixelSize: 23; color: "#a1ceff"; visible: filesGenerationKind !== "image" }
                            Label { x: 3; y: 71; width: parent.width - 8; text: modelData.name; color: textColor; font.pixelSize: 10; elide: Text.ElideRight }
                            MouseArea { anchors.fill: parent; hoverEnabled: true; ToolTip.visible: containsMouse; ToolTip.text: "Double-click to add to project files"; onDoubleClicked: editor.importMedia(modelData.url) }
                        }
                        Label { anchors.centerIn: parent; visible: filesGenerationResults.length === 0; text: "No generated files yet"; color: mutedColor; font.pixelSize: 11 }
                    }
                    ColumnLayout {
                        id: objectsLibraryPanel
                        visible: libraryMode === 4
                        Layout.fillWidth: true; Layout.fillHeight: true
                        spacing: 9
                        TapHandler { target: null; onTapped: (point) => {
                            if (point.position.y < 132 && point.position.x > 108) clearLibrarySelection()
                        } }
                        Label { text: "Objects"; color: textColor; font.pixelSize: 12; font.bold: true }
                        Rectangle {
                            Layout.preferredWidth: 94; Layout.preferredHeight: 94
                            radius: 7; color: "#28354a"; border.color: "#5d75a3"
                            Label { anchors.centerIn: parent; text: "T"; font.pixelSize: 36; font.bold: true; color: "#e8dcff" }
                            MouseArea {
                                id: textObjectMouse; anchors.fill: parent; hoverEnabled: true
                                property real pressX: 0; property real pressY: 0
                                ToolTip.visible: containsMouse; ToolTip.text: "Drag onto a video row or double-click to add at the playhead"
                                onPressed: (mouse) => { pressX = mouse.x; pressY = mouse.y }
                                onPositionChanged: (mouse) => {
                                    if (!(mouse.buttons & Qt.LeftButton)) return
                                    if (!draggedTextObject && Math.hypot(mouse.x - pressX, mouse.y - pressY) < 8) return
                                    draggedTextObject = true
                                    draggedAssetName = "Text"
                                    moveAssetDrag(textObjectMouse, mouse.x, mouse.y)
                                }
                                onReleased: (mouse) => {
                                    if (draggedTextObject) {
                                        const target = timelineDropPosition(textObjectMouse, mouse.x, mouse.y)
                                        if (target.valid && (editor.isVideoTrack(target.track) || target.track === -1)) {
                                            editor.placeTitleAt(newTitleText.text.trim() || "Text",
                                                target.frame, target.track, snappingEnabled, snapToleranceFrames)
                                        }
                                    }
                                    draggedTextObject = false
                                }
                                onCanceled: draggedTextObject = false
                                onDoubleClicked: editor.addTitleAt(newTitleText.text.trim() || "Text", editor.playheadFrame, 3)
                            }
                        }
                        Label { text: "Text"; color: textColor; font.pixelSize: 11 }
                        CaptionField { id: newTitleText; Layout.fillWidth: true; placeholderText: "Title text"; color: textColor; onAccepted: { editor.addTitle(text); text = "" } }
                        AppButton { text: "+ Add at playhead"; primary: true; Layout.fillWidth: true; onClicked: { editor.addTitle(newTitleText.text.trim() || "Text"); newTitleText.text = "" } }
                        Rectangle { Layout.fillWidth: true; height: 1; color: borderColor }
                        Label { text: "Project titles"; color: mutedColor; font.pixelSize: 11 }
                        ListView { id: objectTitlesList; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: editor.titles; spacing: 5
                            TapHandler { target: null; onTapped: (point) => {
                                if (emptyGridPoint(objectTitlesList, point.position.x, point.position.y)) clearLibrarySelection()
                            } }
                            delegate: AppButton { required property var modelData; width: ListView.view.width; text: modelData.text; onClicked: editor.selectTitle(modelData.id) }
                        }
                    }
                    ColumnLayout {
                        visible: libraryMode === 2
                        Layout.fillWidth: true; Layout.fillHeight: true
                        spacing: 9
                        Label { text: "Built-in visual effects"; color: textColor; font.pixelSize: 12; font.bold: true }
                        Label { text: "Select a tile to preview at the playhead. Apply it to " + trackName(selectedEffectTrack) + " or drag it onto a visual row."; color: mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        AppButton { text: selectedEffectId === "" ? "Choose an effect" : "Apply to " + trackName(selectedEffectTrack); primary: true; Layout.fillWidth: true; enabled: selectedEffectId !== ""; onClicked: { editor.addTrackEffect(selectedEffectTrack, selectedEffectId); inspectorTab = 2 } }
                        GridView {
                            id: effectsGrid
                            Layout.fillWidth: true; Layout.fillHeight: true
                            clip: true
                            TapHandler { target: null; onTapped: (point) => {
                                if (emptyGridPoint(effectsGrid, point.position.x, point.position.y)) clearLibrarySelection()
                            } }
                            cellWidth: Math.max(130, Math.floor(width / 2))
                            cellHeight: 142
                            model: editor.effects
                            delegate: Rectangle {
                                required property var modelData
                                width: effectsGrid.cellWidth - 8; height: 132; radius: 6
                                color: selectedEffectId === modelData.id ? accentSoft : panelRaised
                                border.width: selectedEffectId === modelData.id ? 2 : 1
                                border.color: selectedEffectId === modelData.id ? accentColor : borderColor
                                Behavior on color { ColorAnimation { duration: 130 } }
                                Behavior on border.color { ColorAnimation { duration: 130 } }
                                Rectangle {
                                    x: 5; y: 5; width: parent.width - 10; height: 88; radius: 4
                                    color: "#254054"; clip: true
                                    Image {
                                        anchors.fill: parent
                                        source: {
                                            const asset = editor.assets.find(function(item) { return item.hasVideo && item.thumbnail.toString() !== "" })
                                            return asset ? asset.thumbnail : ""
                                        }
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        layer.enabled: true
                                        layer.effect: VideoEffectShader { settings: editor.effectTilePreviewSettings(modelData.id) }
                                    }
                                    Label { anchors.centerIn: parent; text: "✦"; color: "#a9cfff"; font.pixelSize: 27; visible: editor.assets.length === 0 }
                                }
                                Label { x: 9; y: 101; width: parent.width - 18; text: modelData.name; color: textColor; font.pixelSize: 11; font.bold: true; elide: Text.ElideRight }
                                MouseArea {
                                    id: effectMouse
                                    anchors.fill: parent
                                    preventStealing: true
                                    property real pressX: 0
                                    property real pressY: 0
                                    onPressed: (mouse) => { pressX = mouse.x; pressY = mouse.y }
                                    onPositionChanged: (mouse) => {
                                        if (!(mouse.buttons & Qt.LeftButton)) return
                                        if (draggedEffectId === "" && Math.hypot(mouse.x - pressX, mouse.y - pressY) < 8) return
                                        draggedEffectId = modelData.id
                                        draggedAssetName = modelData.name
                                        moveAssetDrag(effectMouse, mouse.x, mouse.y)
                                    }
                                    onReleased: (mouse) => {
                                        if (draggedEffectId === modelData.id) {
                                            const target = timelineDropPosition(effectMouse, mouse.x, mouse.y)
                                            if (target.valid && (target.track === -1
                                                || editor.isVideoTrack(target.track)
                                                || editor.captionRows.some(function(row) { return row.trackId === target.track }))) {
                                                const row = target.track === -1 ? editor.addTrack("video") : target.track
                                                if (row >= 0) {
                                                    editor.addTrackEffect(row, modelData.id)
                                                    selectedTrack = row
                                                    trackSelectionExplicit = true
                                                    selectedEffectTrack = row
                                                    inspectorTab = 2
                                                }
                                            }
                                        } else {
                                            selectedEffectId = modelData.id
                                            editor.previewEffect(modelData.id, selectedEffectTrack)
                                        }
                                        draggedEffectId = ""
                                    }
                                    onCanceled: draggedEffectId = ""
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        visible: libraryMode === 3
                        Layout.fillWidth: true; Layout.fillHeight: true
                        spacing: 9
                        Label { text: "Visual transitions"; color: textColor; font.pixelSize: 12; font.bold: true }
                        Label {
                            text: "Drag a transition to a clip edge or cut. Its duration occupies frames inside the clips."
                            color: mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        GridView {
                            id: transitionsGrid
                            Layout.fillWidth: true; Layout.fillHeight: true
                            clip: true; cellWidth: Math.max(130, Math.floor(width / 2)); cellHeight: 125
                            TapHandler { target: null; onTapped: (point) => {
                                if (emptyGridPoint(transitionsGrid, point.position.x, point.position.y)) clearLibrarySelection()
                            } }
                            model: editor.transitionDefinitions
                            delegate: Rectangle {
                                required property var modelData
                                width: transitionsGrid.cellWidth - 8; height: 115; radius: 6
                                color: selectedTransitionDefinitionId === modelData.id ? accentSoft : panelRaised
                                border.width: selectedTransitionDefinitionId === modelData.id ? 2 : 1
                                border.color: selectedTransitionDefinitionId === modelData.id ? accentColor : borderColor
                                Behavior on color { ColorAnimation { duration: 130 } }
                                Rectangle {
                                    x: 6; y: 6; width: parent.width - 12; height: 76; radius: 4
                                    color: "#0c1622"; clip: true
                                    Rectangle { x: 8; y: 8; width: (parent.width - 16) / 2; height: parent.height - 16; radius: 2; color: "#348bd0" }
                                    Rectangle {
                                        x: 8 + (parent.width - 16) * 0.47; y: 8
                                        width: (parent.width - 16) * 0.53; height: parent.height - 16
                                        radius: 2; color: modelData.renderer === "dip_black" ? "#080b10" : "#a55a79"
                                        opacity: modelData.renderer === "dip_black" ? 0.9 : 0.75
                                    }
                                    Label { anchors.centerIn: parent; text: modelData.renderer === "dip_black" ? "◀  ●  ▶" : "◀  ⋈  ▶"; color: "white"; font.pixelSize: 21 }
                                }
                                Label { x: 9; y: 89; width: parent.width - 18; text: modelData.name; color: textColor; font.pixelSize: 11; font.bold: true; elide: Text.ElideRight }
                                MouseArea {
                                    id: transitionTileMouse
                                    anchors.fill: parent; preventStealing: true
                                    property real pressX: 0
                                    property real pressY: 0
                                    onPressed: (mouse) => { pressX = mouse.x; pressY = mouse.y }
                                    onPositionChanged: (mouse) => {
                                        if (!(mouse.buttons & Qt.LeftButton)) return
                                        if (draggedTransitionId === "" && Math.hypot(mouse.x - pressX, mouse.y - pressY) < 8) return
                                        draggedTransitionId = modelData.id
                                        draggedAssetName = modelData.name
                                        moveAssetDrag(transitionTileMouse, mouse.x, mouse.y)
                                    }
                                    onReleased: (mouse) => {
                                        if (draggedTransitionId === modelData.id) {
                                            const target = transitionDropPosition(transitionTileMouse,
                                                mouse.x, mouse.y)
                                            if (target.valid) {
                                                editor.addTransition(modelData.id, target.track, target.cut)
                                                inspectorTab = 4
                                            }
                                        } else selectedTransitionDefinitionId = modelData.id
                                        draggedTransitionId = ""
                                    }
                                    onCanceled: draggedTransitionId = ""
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        visible: libraryMode === 5
                        Layout.fillWidth: true; Layout.fillHeight: true
                        spacing: 9
                        Label { text: "Audio in project"; color: textColor; font.pixelSize: 12; font.bold: true }
                        ListView { id: audioLibraryList; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 7
                            TapHandler { target: null; onTapped: (point) => {
                                if (emptyGridPoint(audioLibraryList, point.position.x, point.position.y)) clearLibrarySelection()
                            } }
                            model: editor.assets.filter(function(asset) { return asset.hasAudio })
                            delegate: AppButton { required property var modelData; width: ListView.view.width; text: "♫  " + modelData.name; onClicked: editor.selectAsset(modelData.id); onDoubleClicked: editor.addToTimeline(modelData.id) }
                        }
                    }
                    ColumnLayout {
                        visible: libraryMode === 6
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.topMargin: 14
                        spacing: 12
                        Label { text: "Export a frame"; color: textColor; font.pixelSize: 14; font.bold: true }
                        Label {
                            text: "Save the composed project frame exactly as it appears inside the camera view."
                            color: mutedColor; font.pixelSize: 11; wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: borderColor }
                        Label { text: "Project frame"; color: mutedColor; font.pixelSize: 11 }
                        RowLayout {
                            Layout.fillWidth: true
                            AppField {
                                id: libraryFrameInput
                                Layout.fillWidth: true
                                text: String(editor.playheadFrame)
                                validator: RegularExpressionValidator { regularExpression: /\d+/ }
                                horizontalAlignment: TextInput.AlignRight
                            }
                            AppButton { text: "Use cursor"; subtle: true; onClicked: libraryFrameInput.text = String(editor.playheadFrame) }
                        }
                        Label {
                            text: editor.timelineDurationFrames > 0
                                ? "Valid frames: 0–" + (editor.timelineDurationFrames - 1)
                                : "Add visual content to the timeline first"
                            color: mutedColor; font.pixelSize: 10
                        }
                        AppButton {
                            text: "Export frame as PNG…"
                            primary: true
                            Layout.fillWidth: true
                            enabled: !editor.exportRunning && editor.timelineDurationFrames > 0
                                && libraryFrameInput.acceptableInput
                                && Number(libraryFrameInput.text) < editor.timelineDurationFrames
                            onClicked: chooseExportFile("projectFrame", Number(libraryFrameInput.text))
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: borderColor; Layout.topMargin: 8 }
                        Label { text: "Other exports"; color: textColor; font.pixelSize: 13; font.bold: true }
                        AppButton { text: "Video, audio, range and sequence…"; subtle: true; Layout.fillWidth: true; onClicked: exportDialog.open() }
                        Item { Layout.fillHeight: true }
                    }
                }
            }

            Rectangle {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 420
                color: backgroundColor
                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    anchors.topMargin: 30
                    anchors.bottomMargin: 10
                    spacing: 0
                    Rectangle {
                        id: previewViewport
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 7
                        color: panelRaised
                        clip: true
                        onWidthChanged: Qt.callLater(clampPreviewPan)
                        onHeightChanged: Qt.callLater(clampPreviewPan)
                        Item {
                            id: previewVisual
                            objectName: "previewVisual"
                            parent: previewFullscreen ? fullscreenCanvas : previewViewport
                            x: (parent.width - width) / 2 + previewPanX
                            y: (parent.height - height) / 2 + previewPanY
                            width: Math.min(Math.max(1, parent.width - (previewFullscreen ? 0 : 48)),
                                Math.max(1, parent.height - (previewFullscreen ? 0 : 48)) * editor.canvasWidth / editor.canvasHeight)
                            height: Math.min(Math.max(1, parent.height - (previewFullscreen ? 0 : 48)),
                                Math.max(1, parent.width - (previewFullscreen ? 0 : 48)) * editor.canvasHeight / editor.canvasWidth)
                            scale: previewZoom
                            transformOrigin: Item.Center
                            clip: false
                            onWidthChanged: Qt.callLater(clampPreviewPan)
                            onHeightChanged: Qt.callLater(clampPreviewPan)
                            Item {
                                id: previewContent
                                anchors.fill: parent
                                clip: true
                                Rectangle { anchors.fill: parent; color: "#000000" }
                            Repeater {
                                id: videoLayerRepeater
                                model: editor.tracks.filter(function(row) { return row.kind === "video" }).reverse()
                                delegate: Item {
                                    id: videoLayer
                                    required property var modelData
                                    required property int index
                                    property int trackId: modelData.id
                                    property string activeClipId: ""
                                    property bool currentFrameReady: false
                                    property real dragPixelsX: 0
                                    property real dragPixelsY: 0
                                    property real resizeFactor: 1
                                    readonly property var mediaState: editor.timelinePreviewEnabled && editor.playheadFrame >= 0
                                        ? editor.timelineMediaForTrack(trackId) : ({})
                                    readonly property var effectState: editor.timelinePreviewEnabled && editor.playheadFrame >= 0
                                        ? editor.previewEffectsForTrack(trackId) : ({})
                                    readonly property real sourceRatio: mediaState.sourceWidth > 0 && mediaState.sourceHeight > 0
                                        ? mediaState.sourceWidth / mediaState.sourceHeight : editor.canvasWidth / editor.canvasHeight
                                    readonly property real fitWidth: Math.min(previewVisual.width,
                                        previewVisual.height * sourceRatio)
                                    readonly property real fitHeight: fitWidth / sourceRatio
                                    readonly property var transitionState: {
                                        const items = editor.transitions
                                        return editor.timelinePreviewEnabled && editor.playheadFrame >= 0
                                            ? editor.transitionPreviewForTrack(trackId) : ({})
                                    }
                                    visible: editor.timelinePreviewEnabled && !!mediaState.id && modelData.visible
                                    width: fitWidth * ((mediaState.scalePercent || 100) / 100) * resizeFactor
                                    height: fitHeight * ((mediaState.scalePercent || 100) / 100) * resizeFactor
                                    x: (previewVisual.width - width) / 2 + ((mediaState.positionX || 0) + dragPixelsX) * previewVisual.width / editor.canvasWidth
                                    y: (previewVisual.height - height) / 2 + ((mediaState.positionY || 0) + dragPixelsY) * previewVisual.height / editor.canvasHeight
                                    rotation: mediaState.rotationDegrees || 0
                                    opacity: mediaState.opacityPercent === undefined ? 1 : mediaState.opacityPercent / 100
                                    z: visibleTracks.length - 1 - visibleTrackRow(trackId)
                                    layer.enabled: !!effectState.active || !!transitionState.mainMask?.kind
                                    layer.effect: VideoEffectShader { settings: videoLayer.effectState; transitionMask: videoLayer.transitionState.mainMask || ({}) }
                                    function sync(forceSeek) {
                                        const state = mediaState
                                        if (activeClipId !== (state.id || "")) {
                                            activeClipId = state.id || ""
                                            currentFrameReady = !!state.still
                                            forceSeek = true
                                        }
                                        if (state.id && !state.still && videoTrackPlayer.seekable) {
                                            if (forceSeek || Math.abs(videoTrackPlayer.position - state.sourceMs) > 160)
                                                videoTrackPlayer.position = state.sourceMs
                                            if (editor.timelinePlaying && videoTrackPlayer.playbackState !== MediaPlayer.PlayingState)
                                                videoTrackPlayer.play()
                                            else if (!editor.timelinePlaying && videoTrackPlayer.playbackState === MediaPlayer.PlayingState)
                                                videoTrackPlayer.pause()
                                        } else if (videoTrackPlayer.playbackState === MediaPlayer.PlayingState)
                                            videoTrackPlayer.pause()
                                        if (state.voiceActive && voiceTrackPlayer.seekable) {
                                            if (forceSeek || Math.abs(voiceTrackPlayer.position - state.voiceMs) > 160)
                                                voiceTrackPlayer.position = state.voiceMs
                                            if (editor.timelinePlaying && voiceTrackPlayer.playbackState !== MediaPlayer.PlayingState)
                                                voiceTrackPlayer.play()
                                            else if (!editor.timelinePlaying && voiceTrackPlayer.playbackState === MediaPlayer.PlayingState)
                                                voiceTrackPlayer.pause()
                                        } else if (voiceTrackPlayer.playbackState === MediaPlayer.PlayingState)
                                            voiceTrackPlayer.pause()
                                    }
                                    MediaPlayer {
                                        id: videoTrackPlayer
                                        property string initializedSource: ""
                                        source: videoLayer.mediaState.id && !videoLayer.mediaState.still ? (videoLayer.mediaState.url || "") : ""
                                        playbackRate: videoLayer.mediaState.speed || 1
                                        videoOutput: layerVideoOutput
                                        audioOutput: AudioOutput {
                                            volume: Math.min(1, window.previewVolume * (videoLayer.mediaState.gain === undefined ? 1 : videoLayer.mediaState.gain))
                                            muted: window.previewMuted || !!videoLayer.mediaState.muted || !!videoLayer.mediaState.voiceActive
                                        }
                                        onMediaStatusChanged: {
                                            if (mediaStatus !== MediaPlayer.LoadedMedia || initializedSource === source.toString()) return
                                            initializedSource = source.toString()
                                            position = Math.min(duration, videoLayer.mediaState.sourceMs || 0)
                                            if (editor.timelinePlaying) play()
                                            else { play(); pause() }
                                            videoLayer.sync(true)
                                        }
                                        onSourceChanged: {
                                            initializedSource = ""
                                            videoLayer.currentFrameReady = false
                                        }
                                        onErrorOccurred: function(error, errorString) {
                                            if (error !== MediaPlayer.NoError) playbackError.text = errorString
                                        }
                                    }
                                    VideoOutput { id: layerVideoOutput; anchors.fill: parent; fillMode: VideoOutput.PreserveAspectFit; visible: !videoLayer.mediaState.still }
                                    Connections {
                                        target: layerVideoOutput.videoSink
                                        function onVideoFrameChanged(frame) {
                                            if (layerVideoOutput.videoSink.videoSize.width > 0
                                                && videoLayer.activeClipId === (videoLayer.mediaState.id || ""))
                                                videoLayer.currentFrameReady = true
                                        }
                                    }
                                    Image { anchors.fill: parent; source: videoLayer.mediaState.still ? videoLayer.mediaState.url : ""; fillMode: Image.PreserveAspectFit; visible: !!videoLayer.mediaState.still; asynchronous: true }
                                    Rectangle {
                                        anchors.fill: parent
                                        color: "black"
                                        opacity: videoLayer.transitionState.blackOpacity || 0
                                        visible: opacity > 0
                                    }
                                    MouseArea {
                                        id: clipMoveArea; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.SizeAllCursor
                                        property real originX: 0; property real originY: 0
                                        onPressed: (mouse) => {
                                            const p = previewVisual.mapFromItem(clipMoveArea, mouse.x, mouse.y)
                                            originX = p.x; originY = p.y
                                            editor.selectClipWithoutSeek(videoLayer.mediaState.id)
                                        }
                                        onPositionChanged: (mouse) => {
                                            if (!(mouse.buttons & Qt.LeftButton)) return
                                            const p = previewVisual.mapFromItem(clipMoveArea, mouse.x, mouse.y)
                                            videoLayer.dragPixelsX = (p.x - originX) * editor.canvasWidth / previewVisual.width
                                            videoLayer.dragPixelsY = (p.y - originY) * editor.canvasHeight / previewVisual.height
                                            const bounds = rotatedViewportBounds(videoLayer.x, videoLayer.y,
                                                videoLayer.width, videoLayer.height, videoLayer.rotation)
                                            const snapped = snapViewportMove(bounds.x, bounds.y,
                                                bounds.width, bounds.height)
                                            const adjustX = snapped.x - bounds.x
                                            const adjustY = snapped.y - bounds.y
                                            videoLayer.dragPixelsX += adjustX * editor.canvasWidth / previewVisual.width
                                            videoLayer.dragPixelsY += adjustY * editor.canvasHeight / previewVisual.height
                                            viewportSnapGuideX = snapped.guideX
                                            viewportSnapGuideY = snapped.guideY
                                        }
                                        onReleased: {
                                            if (Math.abs(videoLayer.dragPixelsX) + Math.abs(videoLayer.dragPixelsY) > 0.5) {
                                                const x = Math.round(videoLayer.mediaState.positionX + videoLayer.dragPixelsX)
                                                const y = Math.round(videoLayer.mediaState.positionY + videoLayer.dragPixelsY)
                                                const clip = editor.clips.find(function(item) { return item.id === videoLayer.mediaState.id })
                                                editor.setVisualPropertyValue("clip", videoLayer.mediaState.id, "x", editor.playheadFrame, x)
                                                editor.setVisualPropertyValue("clip", videoLayer.mediaState.id, "y", editor.playheadFrame, y)
                                            }
                                            videoLayer.dragPixelsX = 0; videoLayer.dragPixelsY = 0
                                            viewportSnapGuideX = -1; viewportSnapGuideY = -1
                                        }
                                        onCanceled: {
                                            videoLayer.dragPixelsX = 0; videoLayer.dragPixelsY = 0
                                            viewportSnapGuideX = -1; viewportSnapGuideY = -1
                                        }
                                    }
                                    Rectangle {
                                        parent: previewVisual
                                        x: videoLayer.x; y: videoLayer.y
                                        width: videoLayer.width; height: videoLayer.height
                                        rotation: videoLayer.rotation
                                        color: "transparent"; border.width: 2; border.color: "#79c7ff"
                                        visible: videoLayer.visible && editor.selectedClipId === videoLayer.mediaState.id
                                        z: 101
                                    }
                                    MediaPlayer {
                                        id: voiceTrackPlayer
                                        source: videoLayer.mediaState.voiceActive ? (videoLayer.mediaState.voiceUrl || "") : ""
                                        playbackRate: videoLayer.mediaState.speed || 1
                                        audioOutput: AudioOutput {
                                            volume: Math.min(1, window.previewVolume * (videoLayer.mediaState.gain === undefined ? 1 : videoLayer.mediaState.gain))
                                            muted: window.previewMuted || !!videoLayer.mediaState.muted
                                        }
                                        onMediaStatusChanged: if (mediaStatus === MediaPlayer.LoadedMedia) videoLayer.sync(true)
                                    }
                                    Rectangle {
                                        parent: previewVisual
                                        readonly property var corner: rotatedViewportCorner(videoLayer.x,
                                            videoLayer.y, videoLayer.width, videoLayer.height, videoLayer.rotation)
                                        width: 12; height: 12; radius: 2; color: "#eaf7ff"; border.color: "#448fca"
                                        x: visibleHandleX(corner.x - width / 2, width)
                                        y: visibleHandleY(corner.y - height / 2, height)
                                        visible: videoLayer.visible && editor.selectedClipId === videoLayer.mediaState.id
                                        z: 102
                                        MouseArea {
                                            id: clipResizeArea; anchors.fill: parent; cursorShape: Qt.SizeFDiagCursor
                                            property real originX: 0; property real originalWidth: 1
                                            onPressed: (mouse) => {
                                                originX = previewVisual.mapFromItem(clipResizeArea, mouse.x, mouse.y).x
                                                originalWidth = videoLayer.width
                                            }
                                            onPositionChanged: (mouse) => {
                                                if (!(mouse.buttons & Qt.LeftButton)) return
                                                const p = previewVisual.mapFromItem(clipResizeArea, mouse.x, mouse.y)
                                                videoLayer.resizeFactor = Math.max(0.25, Math.min(4,
                                                    1 + (p.x - originX) / Math.max(24, originalWidth)))
                                                const anchorX = previewVisual.width / 2
                                                    + videoLayer.mediaState.positionX * previewVisual.width / editor.canvasWidth
                                                const anchorY = previewVisual.height / 2
                                                    + videoLayer.mediaState.positionY * previewVisual.height / editor.canvasHeight
                                                const rotatedSize = rotatedViewportBounds(0, 0,
                                                    videoLayer.fitWidth * videoLayer.mediaState.scalePercent / 100,
                                                    videoLayer.fitHeight * videoLayer.mediaState.scalePercent / 100,
                                                    videoLayer.rotation)
                                                const snapped = snapViewportScale(anchorX, anchorY,
                                                    rotatedSize.width, rotatedSize.height,
                                                    0.5, 0.5, videoLayer.resizeFactor)
                                                videoLayer.resizeFactor = Math.max(0.25, Math.min(4, snapped.factor))
                                                viewportSnapGuideX = snapped.guideX
                                                viewportSnapGuideY = snapped.guideY
                                            }
                                            onReleased: {
                                                const state = videoLayer.mediaState
                                                const next = Math.max(25, Math.min(400, Math.round(state.scalePercent * videoLayer.resizeFactor)))
                                                const clip = editor.clips.find(function(item) { return item.id === state.id })
                                                editor.setVisualPropertyValue("clip", state.id, "scale", editor.playheadFrame, next)
                                                videoLayer.resizeFactor = 1
                                                viewportSnapGuideX = -1; viewportSnapGuideY = -1
                                            }
                                            onCanceled: {
                                                videoLayer.resizeFactor = 1
                                                viewportSnapGuideX = -1; viewportSnapGuideY = -1
                                            }
                                        }
                                    }
                                }
                            }
                            Repeater {
                                model: editor.tracks.filter(function(row) { return row.kind === "video" }).reverse()
                                delegate: Item {
                                    id: transitionOtherLayer
                                    required property var modelData
                                    required property int index
                                    readonly property var transitionState: {
                                        const items = editor.transitions
                                        return editor.timelinePreviewEnabled && editor.playheadFrame >= 0
                                            ? editor.transitionPreviewForTrack(modelData.id) : ({})
                                    }
                                    readonly property var effectState: editor.timelinePreviewEnabled && editor.playheadFrame >= 0
                                        ? editor.previewEffectsForTrack(modelData.id) : ({})
                                    readonly property var liveLayer: {
                                        const layerCount = videoLayerRepeater.count
                                        return index < layerCount ? videoLayerRepeater.itemAt(index) : null
                                    }
                                    readonly property bool liveFrameReady: !!liveLayer
                                        && liveLayer.activeClipId === (liveLayer.mediaState.id || "")
                                        && liveLayer.currentFrameReady
                                    visible: modelData.visible
                                    anchors.fill: parent
                                    z: visibleTracks.length - 1 - visibleTrackRow(modelData.id) + 0.75
                                    TransitionEdgeLayer {
                                        sourceState: transitionOtherLayer.transitionState.incoming || ({})
                                        transitionMask: transitionOtherLayer.transitionState.incomingMask || ({})
                                        opacityOverride: transitionOtherLayer.transitionState.paired
                                            && transitionOtherLayer.transitionState.usesHeldEdges
                                            && editor.playheadFrame >= transitionOtherLayer.transitionState.cut
                                            && !transitionOtherLayer.transitionState.incoming?.textObject
                                            && !transitionOtherLayer.liveFrameReady ? 1 : -1
                                        effectSettings: transitionOtherLayer.effectState
                                        canvasWidth: previewVisual.width
                                        canvasHeight: previewVisual.height
                                    }
                                    TransitionEdgeLayer {
                                        sourceState: transitionOtherLayer.transitionState.outgoing || ({})
                                        transitionMask: transitionOtherLayer.transitionState.outgoingMask || ({})
                                        effectSettings: transitionOtherLayer.effectState
                                        canvasWidth: previewVisual.width
                                        canvasHeight: previewVisual.height
                                    }
                                }
                            }
                            Repeater {
                                model: editor.timelinePreviewEnabled ? editor.titles : []
                                delegate: Rectangle {
                                    id: titlePreviewBox
                                    required property var modelData
                                    readonly property var visual: editor.titleVisualAt(modelData.id, editor.playheadFrame)
                                    readonly property bool selected: editor.selectedTitleId === modelData.id
                                    readonly property real visualScale: (visual.scale || 100) / 100
                                    property real dragX: 0
                                    property real dragY: 0
                                    property real resizeScale: 1
                                    width: Math.min(previewVisual.width * 0.9, titlePreviewText.implicitWidth + 28)
                                    height: titlePreviewText.implicitHeight + 16
                                    scale: visualScale * resizeScale
                                    transformOrigin: Item.TopLeft
                                    x: previewVisual.width * (visual.x + dragX) / 100 - width * scale * modelData.anchorX / 100
                                    y: previewVisual.height * (visual.y + dragY) / 100 - height * scale * modelData.anchorY / 100
                                    visible: editor.playheadFrame >= modelData.start && editor.playheadFrame < modelData.start + modelData.duration
                                        && editor.tracks.some(function(row) { return row.id === modelData.track && row.visible })
                                    z: visibleTracks.length - 1 - visibleTrackRow(modelData.track) + 0.5
                                    radius: 5
                                    color: Qt.rgba(parseInt(modelData.backgroundColor.slice(1, 3), 16) / 255,
                                        parseInt(modelData.backgroundColor.slice(3, 5), 16) / 255,
                                        parseInt(modelData.backgroundColor.slice(5, 7), 16) / 255,
                                        modelData.backgroundOpacity / 100)
                                    readonly property var effectState: {
                                        const frame = editor.playheadFrame
                                        const effects = editor.trackEffects
                                        return editor.previewEffectsForTrack(modelData.track)
                                    }
                                    readonly property var transitionState: {
                                        const frame = editor.playheadFrame
                                        const transitions = editor.transitions
                                        return editor.timelinePreviewEnabled
                                            ? editor.transitionPreviewForTrack(modelData.track) : ({})
                                    }
                                    opacity: transitionState.id
                                        && (transitionState.outgoingId === modelData.id
                                            || transitionState.incomingId === modelData.id)
                                        ? (transitionState.mainOpacity === undefined ? 1 : transitionState.mainOpacity) : 1
                                    layer.enabled: !!effectState.active || !!transitionState.mainMask?.kind
                                    layer.effect: VideoEffectShader { settings: titlePreviewBox.effectState; transitionMask: titlePreviewBox.transitionState.mainMask || ({}) }
                                    Text {
                                        id: titlePreviewText
                                        anchors.centerIn: parent
                                        width: Math.min(previewVisual.width * 0.86, implicitWidth)
                                        text: modelData.wrappedText || modelData.text
                                        color: modelData.color
                                        font.family: modelData.fontFamily
                                        font.bold: modelData.bold
                                        font.italic: modelData.italic
                                        font.pixelSize: Math.max(1, Math.round(modelData.fontPixels * previewVisual.height / 1080))
                                        horizontalAlignment: Text.AlignHCenter
                                        wrapMode: Text.NoWrap
                                    }
                                    MouseArea {
                                        id: titleMoveArea; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.SizeAllCursor
                                        property real originX: 0; property real originY: 0
                                        onPressed: (mouse) => { const p = previewVisual.mapFromItem(titleMoveArea, mouse.x, mouse.y); editor.selectTitle(modelData.id); originX = p.x; originY = p.y }
                                        onPositionChanged: (mouse) => {
                                            if (!(mouse.buttons & Qt.LeftButton)) return
                                            const p = previewVisual.mapFromItem(titleMoveArea, mouse.x, mouse.y)
                                            titlePreviewBox.dragX = (p.x - originX) * 100 / previewVisual.width
                                            titlePreviewBox.dragY = (p.y - originY) * 100 / previewVisual.height
                                            const snapped = snapViewportMove(titlePreviewBox.x, titlePreviewBox.y,
                                                titlePreviewBox.width * titlePreviewBox.scale,
                                                titlePreviewBox.height * titlePreviewBox.scale)
                                            const adjustX = snapped.x - titlePreviewBox.x
                                            const adjustY = snapped.y - titlePreviewBox.y
                                            titlePreviewBox.dragX += adjustX * 100 / previewVisual.width
                                            titlePreviewBox.dragY += adjustY * 100 / previewVisual.height
                                            viewportSnapGuideX = snapped.guideX
                                            viewportSnapGuideY = snapped.guideY
                                        }
                                        onReleased: {
                                            if (Math.abs(titlePreviewBox.dragX) + Math.abs(titlePreviewBox.dragY) > 0.01) {
                                                const x = Math.round(titlePreviewBox.visual.x + titlePreviewBox.dragX)
                                                const y = Math.round(titlePreviewBox.visual.y + titlePreviewBox.dragY)
                                                editor.setVisualPropertyValue("title", modelData.id, "x", editor.playheadFrame, x)
                                                editor.setVisualPropertyValue("title", modelData.id, "y", editor.playheadFrame, y)
                                            }
                                            titlePreviewBox.dragX = 0; titlePreviewBox.dragY = 0
                                            viewportSnapGuideX = -1; viewportSnapGuideY = -1
                                        }
                                        onCanceled: {
                                            titlePreviewBox.dragX = 0; titlePreviewBox.dragY = 0
                                            viewportSnapGuideX = -1; viewportSnapGuideY = -1
                                        }
                                    }
                                    Rectangle {
                                        parent: previewVisual
                                        x: titlePreviewBox.x; y: titlePreviewBox.y
                                        width: titlePreviewBox.width * titlePreviewBox.scale
                                        height: titlePreviewBox.height * titlePreviewBox.scale
                                        color: "transparent"; border.color: "#81c8ff"; border.width: 2
                                        visible: titlePreviewBox.visible && titlePreviewBox.selected; z: 101
                                    }
                                    Rectangle {
                                        parent: previewVisual
                                        width: 11; height: 11; radius: 2; color: "#eaf7ff"; border.color: "#448fca"
                                        x: visibleHandleX(titlePreviewBox.x + titlePreviewBox.width * titlePreviewBox.scale - width / 2, width)
                                        y: visibleHandleY(titlePreviewBox.y + titlePreviewBox.height * titlePreviewBox.scale - height / 2, height)
                                        visible: titlePreviewBox.visible && titlePreviewBox.selected; z: 102
                                        MouseArea {
                                            id: titleResizeArea; anchors.fill: parent; cursorShape: Qt.SizeFDiagCursor
                                            property real originX: 0; property real originalWidth: 1
                                            onPressed: (mouse) => {
                                                originX = previewVisual.mapFromItem(titleResizeArea, mouse.x, mouse.y).x
                                                originalWidth = titlePreviewBox.width
                                            }
                                            onPositionChanged: (mouse) => {
                                                if (!(mouse.buttons & Qt.LeftButton)) return
                                                const p = previewVisual.mapFromItem(titleResizeArea, mouse.x, mouse.y)
                                                titlePreviewBox.resizeScale = Math.max(0.25, Math.min(4,
                                                    1 + (p.x - originX) / Math.max(24, originalWidth)))
                                                const snapped = snapViewportScale(
                                                    previewVisual.width * titlePreviewBox.visual.x / 100,
                                                    previewVisual.height * titlePreviewBox.visual.y / 100,
                                                    titlePreviewBox.width * titlePreviewBox.visualScale,
                                                    titlePreviewBox.height * titlePreviewBox.visualScale,
                                                    modelData.anchorX / 100, modelData.anchorY / 100,
                                                    titlePreviewBox.resizeScale)
                                                titlePreviewBox.resizeScale = Math.max(0.25, Math.min(4, snapped.factor))
                                                viewportSnapGuideX = snapped.guideX
                                                viewportSnapGuideY = snapped.guideY
                                            }
                                            onReleased: {
                                                const next = Math.max(25, Math.min(400, Math.round(titlePreviewBox.visual.scale * titlePreviewBox.resizeScale)))
                                                editor.setVisualPropertyValue("title", modelData.id, "scale", editor.playheadFrame, next)
                                                titlePreviewBox.resizeScale = 1
                                                viewportSnapGuideX = -1; viewportSnapGuideY = -1
                                            }
                                            onCanceled: {
                                                titlePreviewBox.resizeScale = 1
                                                viewportSnapGuideX = -1; viewportSnapGuideY = -1
                                            }
                                        }
                                    }
                                }
                            }
                            Repeater {
                                model: editor.timelinePreviewEnabled ? editor.captionRows : []
                                delegate: Rectangle {
                                    id: captionPreviewBox
                                    required property var modelData
                                    readonly property var captionState: editor.captionDisplayForRow(modelData.id, editor.viewerFrame)
                                    readonly property var visual: editor.captionVisualAt(modelData.id, editor.viewerFrame)
                                    readonly property bool selected: editor.selectedCaptionRowId === modelData.id
                                    property real dragX: 0; property real dragY: 0; property real resizeFactor: 1
                                    visible: modelData.visible && !!captionState.text
                                    z: visibleTracks.length - 1 - visibleTrackRow(modelData.trackId) + 0.5
                                    x: previewVisual.width * (visual.x + dragX) / 100 - width * scale / 2
                                    y: previewVisual.height * (visual.y + dragY) / 100 - height * scale / 2
                                    width: Math.min(previewVisual.width * 0.9, captionText.implicitWidth + 28)
                                    height: captionText.implicitHeight + 16
                                    scale: ((visual.scale || 100) / 100) * resizeFactor
                                    opacity: captionState.opacity === undefined ? 1 : captionState.opacity / 100
                                    transformOrigin: Item.TopLeft
                                    radius: 5
                                    color: Qt.rgba(parseInt(modelData.backgroundColor.slice(1, 3), 16) / 255,
                                        parseInt(modelData.backgroundColor.slice(3, 5), 16) / 255,
                                        parseInt(modelData.backgroundColor.slice(5, 7), 16) / 255,
                                        captionState.treatment === "clear" ? 0
                                            : captionState.treatment === "card" ? 0.92
                                            : modelData.backgroundOpacity / 100)
                                    readonly property var effectState: {
                                        const frame = editor.playheadFrame
                                        const effects = editor.trackEffects
                                        return editor.previewEffectsForTrack(modelData.trackId)
                                    }
                                    layer.enabled: !!effectState.active
                                    layer.effect: VideoEffectShader { settings: captionPreviewBox.effectState }
                                    Text {
                                        id: captionText
                                        anchors.centerIn: parent
                                        width: Math.min(previewVisual.width * 0.86, implicitWidth)
                                        text: {
                                            const words = captionState.words || []
                                            if (captionState.highlight === undefined || captionState.highlight < 0)
                                                return captionState.wrappedText || captionState.text || ""
                                            const breaks = captionState.lineBreakBefore || []
                                            return words.map(function(word, index) {
                                                const safe = word.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                                                let colored = safe
                                                if (index === captionState.highlight) {
                                                    if (captionState.treatment === "block")
                                                        colored = "<span style='background-color:" + modelData.highlightColor + "; color:#101826'>" + safe + "</span>"
                                                    else if (captionState.treatment === "underline")
                                                        colored = "<u><font color='" + modelData.highlightColor + "'>" + safe + "</font></u>"
                                                    else colored = "<font color='" + modelData.highlightColor + "'>" + safe + "</font>"
                                                }
                                                return (index === 0 ? "" : breaks.indexOf(index) >= 0 ? "<br/>" : " ") + colored
                                            }).join("")
                                        }
                                        textFormat: captionState.highlight >= 0 ? Text.RichText : Text.PlainText
                                        color: modelData.color
                                        font.family: modelData.fontFamily
                                        font.pixelSize: Math.max(1, Math.round(modelData.fontPixels * previewVisual.height / 1080))
                                        font.bold: modelData.bold
                                        font.italic: modelData.italic
                                        horizontalAlignment: Text.AlignHCenter
                                        wrapMode: Text.NoWrap
                                    }
                                    MouseArea {
                                        id: captionMoveArea; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.SizeAllCursor
                                        property real originX: 0; property real originY: 0
                                        onPressed: (mouse) => {
                                            const p = previewVisual.mapFromItem(captionMoveArea, mouse.x, mouse.y)
                                            originX = p.x; originY = p.y
                                            const cue = editor.captionCues.find(function(item) {
                                                return item.rowId === modelData.id && editor.playheadFrame >= item.start
                                                    && editor.playheadFrame < item.start + item.duration
                                            })
                                            if (cue) editor.selectCaption(cue.id)
                                            else editor.selectCaptionRow(modelData.id)
                                        }
                                        onPositionChanged: (mouse) => {
                                            if (!(mouse.buttons & Qt.LeftButton)) return
                                            const p = previewVisual.mapFromItem(captionMoveArea, mouse.x, mouse.y)
                                            captionPreviewBox.dragX = (p.x - originX) * 100 / previewVisual.width
                                            captionPreviewBox.dragY = (p.y - originY) * 100 / previewVisual.height
                                            const snapped = snapViewportMove(captionPreviewBox.x, captionPreviewBox.y,
                                                captionPreviewBox.width * captionPreviewBox.scale,
                                                captionPreviewBox.height * captionPreviewBox.scale)
                                            const adjustX = snapped.x - captionPreviewBox.x
                                            const adjustY = snapped.y - captionPreviewBox.y
                                            captionPreviewBox.dragX += adjustX * 100 / previewVisual.width
                                            captionPreviewBox.dragY += adjustY * 100 / previewVisual.height
                                            viewportSnapGuideX = snapped.guideX
                                            viewportSnapGuideY = snapped.guideY
                                        }
                                        onReleased: {
                                            if (Math.abs(captionPreviewBox.dragX) + Math.abs(captionPreviewBox.dragY) > 0.01) {
                                                const x = Math.round(captionPreviewBox.visual.x + captionPreviewBox.dragX)
                                                const y = Math.round(captionPreviewBox.visual.y + captionPreviewBox.dragY)
                                                editor.setVisualPropertyValue("caption", modelData.id, "x", editor.playheadFrame, x)
                                                editor.setVisualPropertyValue("caption", modelData.id, "y", editor.playheadFrame, y)
                                            }
                                            captionPreviewBox.dragX = 0; captionPreviewBox.dragY = 0
                                            viewportSnapGuideX = -1; viewportSnapGuideY = -1
                                        }
                                        onCanceled: {
                                            captionPreviewBox.dragX = 0; captionPreviewBox.dragY = 0
                                            viewportSnapGuideX = -1; viewportSnapGuideY = -1
                                        }
                                    }
                                    Rectangle {
                                        parent: previewVisual
                                        x: captionPreviewBox.x; y: captionPreviewBox.y
                                        width: captionPreviewBox.width * captionPreviewBox.scale
                                        height: captionPreviewBox.height * captionPreviewBox.scale
                                        color: "transparent"; border.width: 2; border.color: "#81c8ff"
                                        visible: captionPreviewBox.visible && captionPreviewBox.selected; z: 101
                                    }
                                    Rectangle {
                                        parent: previewVisual
                                        width: 11; height: 11; radius: 2; color: "#eaf7ff"; border.color: "#448fca"
                                        x: visibleHandleX(captionPreviewBox.x + captionPreviewBox.width * captionPreviewBox.scale - width / 2, width)
                                        y: visibleHandleY(captionPreviewBox.y + captionPreviewBox.height * captionPreviewBox.scale - height / 2, height)
                                        visible: captionPreviewBox.visible && captionPreviewBox.selected; z: 102
                                        MouseArea {
                                            id: captionResizeArea; anchors.fill: parent; cursorShape: Qt.SizeFDiagCursor
                                            property real originX: 0; property real originalWidth: 1
                                            onPressed: (mouse) => {
                                                originX = previewVisual.mapFromItem(captionResizeArea, mouse.x, mouse.y).x
                                                originalWidth = captionPreviewBox.width
                                            }
                                            onPositionChanged: (mouse) => {
                                                if (!(mouse.buttons & Qt.LeftButton)) return
                                                const p = previewVisual.mapFromItem(captionResizeArea, mouse.x, mouse.y)
                                                captionPreviewBox.resizeFactor = Math.max(0.25, Math.min(4,
                                                    1 + (p.x - originX) / Math.max(24, originalWidth)))
                                                const snapped = snapViewportScale(
                                                    previewVisual.width * captionPreviewBox.visual.x / 100,
                                                    previewVisual.height * captionPreviewBox.visual.y / 100,
                                                    captionPreviewBox.width * (captionPreviewBox.visual.scale || 100) / 100,
                                                    captionPreviewBox.height * (captionPreviewBox.visual.scale || 100) / 100,
                                                    0.5, 0.5, captionPreviewBox.resizeFactor)
                                                captionPreviewBox.resizeFactor = Math.max(0.25, Math.min(4, snapped.factor))
                                                viewportSnapGuideX = snapped.guideX
                                                viewportSnapGuideY = snapped.guideY
                                            }
                                            onReleased: {
                                                const next = Math.max(25, Math.min(400, Math.round(captionPreviewBox.visual.scale * captionPreviewBox.resizeFactor)))
                                                editor.setVisualPropertyValue("caption", modelData.id, "scale", editor.playheadFrame, next)
                                                captionPreviewBox.resizeFactor = 1
                                                viewportSnapGuideX = -1; viewportSnapGuideY = -1
                                            }
                                            onCanceled: {
                                                captionPreviewBox.resizeFactor = 1
                                                viewportSnapGuideX = -1; viewportSnapGuideY = -1
                                            }
                                        }
                                    }
                                }
                            }
                            VideoOutput { id: videoOutput; anchors.fill: parent; fillMode: VideoOutput.PreserveAspectFit; visible: !editor.timelinePreviewEnabled && (!selectedAsset || !selectedAsset.isStillImage) }
                            Image { anchors.fill: parent; source: !editor.timelinePreviewEnabled && selectedAsset && selectedAsset.isStillImage ? editor.previewUrl : ""; fillMode: Image.PreserveAspectFit; visible: !editor.timelinePreviewEnabled && !!selectedAsset && selectedAsset.isStillImage; asynchronous: true }
                            }
                            Rectangle {
                                anchors.fill: parent; color: "transparent"
                                border.width: 1; border.color: "#7f91a5"
                                z: 100
                            }
                            Rectangle {
                                x: viewportSnapGuideX; y: 0; width: 1; height: previewVisual.height
                                color: "#58c9ff"; visible: viewportSnapGuideX >= 0; z: 103
                            }
                            Rectangle {
                                x: 0; y: viewportSnapGuideY; width: previewVisual.width; height: 1
                                color: "#58c9ff"; visible: viewportSnapGuideY >= 0; z: 103
                            }
                        }
                        WheelHandler {
                            target: null
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            acceptedModifiers: Qt.ControlModifier
                            onWheel: (event) => {
                                const steps = event.angleDelta.y !== 0 ? event.angleDelta.y / 120
                                    : event.pixelDelta.y / 100
                                if (steps !== 0) zoomPreviewAt(previewZoom * Math.pow(1.18, steps), event.x, event.y)
                                event.accepted = true
                            }
                        }
                        PinchHandler {
                            id: previewPinch
                            target: null
                            acceptedDevices: PointerDevice.TouchPad | PointerDevice.TouchScreen
                            rotationAxis.enabled: false
                            xAxis.enabled: false
                            yAxis.enabled: false
                            scaleAxis.onActiveValueChanged: (delta) =>
                                zoomPreviewAt(previewZoom * delta,
                                    previewPinch.centroid.position.x, previewPinch.centroid.position.y)
                        }
                        MouseArea {
                            anchors.fill: parent
                            z: 106
                            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                            hoverEnabled: true
                            cursorShape: pressed ? Qt.ClosedHandCursor
                                : canPanPreviewFrom(mouseX, mouseY) ? Qt.OpenHandCursor : Qt.ArrowCursor
                            property real startX: 0
                            property real startY: 0
                            property real startPanX: 0
                            property real startPanY: 0
                            onPressed: (mouse) => {
                                if (mouse.button === Qt.LeftButton
                                        && !canPanPreviewFrom(mouse.x, mouse.y)) {
                                    mouse.accepted = false
                                    return
                                }
                                startX = mouse.x; startY = mouse.y
                                startPanX = previewPanX; startPanY = previewPanY
                            }
                            onPositionChanged: (mouse) => {
                                if (!pressed) return
                                previewPanX = startPanX + mouse.x - startX
                                previewPanY = startPanY + mouse.y - startY
                                clampPreviewPan()
                            }
                        }
                        Rectangle {
                            x: 10; y: 10; z: 107
                            width: 142; height: 30; radius: 5
                            color: "#d0131b24"
                            Row {
                                anchors.centerIn: parent; spacing: 3
                                IconButton { symbol: "−"; implicitWidth: 26; implicitHeight: 26
                                    ToolTip.visible: hovered; ToolTip.text: "Zoom viewer out · Ctrl + wheel or pinch"
                                    onClicked: zoomPreviewAt(previewZoom / 1.25, previewViewport.width / 2, previewViewport.height / 2) }
                                Label { width: 42; height: 26; text: Math.round(previewZoom * 100) + "%"
                                    color: textColor; font.pixelSize: 10
                                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                IconButton { symbol: "+"; implicitWidth: 26; implicitHeight: 26
                                    ToolTip.visible: hovered; ToolTip.text: "Zoom viewer in · Ctrl + wheel or pinch"
                                    onClicked: zoomPreviewAt(previewZoom * 1.25, previewViewport.width / 2, previewViewport.height / 2) }
                                IconButton { symbol: "⤢"; implicitWidth: 26; implicitHeight: 26
                                    ToolTip.visible: hovered; ToolTip.text: "Fit canvas in viewer"
                                    onClicked: resetPreviewZoom() }
                            }
                        }
                        Label { anchors.centerIn: parent; visible: !editor.timelinePreviewEnabled && editor.previewUrl.toString() === ""; text: "Select media to preview"; color: mutedColor; font.pixelSize: 14 }
                        Label { id: playbackError; anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; color: "#ffb899"; font.pixelSize: 11 }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 78
                        Layout.topMargin: 16
                        color: "#141c26"
                        radius: 5
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10; anchors.rightMargin: 10
                            anchors.topMargin: 5; anchors.bottomMargin: 5
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Label { text: editor.timelinePreviewEnabled
                                            ? editor.projectFrameTimestamp(editor.playheadFrame) + " · F" + editor.playheadFrame
                                            : clockTime(player.position) + " / " + clockTime(player.duration)
                                    color: textColor; font.pixelSize: 10; elide: Text.ElideRight
                                    Layout.fillWidth: true; Layout.minimumWidth: 90; Layout.maximumWidth: 155 }
                                Item { Layout.fillWidth: true }
                                IconButton { symbol: "⏮"; enabled: editor.timelineDurationFrames > 0 || player.seekable; onClicked: { if (editor.timelineDurationFrames > 0) seekProjectFrame(0); else player.position = 0 } }
                                IconButton { symbol: "◀"; enabled: editor.timelineDurationFrames > 0; onClicked: seekProjectFrame(Math.max(0, editor.playheadFrame - 1)) }
                                IconButton {
                                    symbol: editor.timelinePlaying || (!editor.timelinePreviewEnabled && player.playbackState === MediaPlayer.PlayingState) ? "❚❚" : "▶"
                                    enabled: editor.timelineDurationFrames > 0 || (editor.previewUrl.toString() !== "" && (!selectedAsset || !selectedAsset.isStillImage))
                                    onClicked: {
                                        if (editor.timelineDurationFrames > 0) editor.toggleTimelinePlayback()
                                        else if (player.playbackState === MediaPlayer.PlayingState) player.pause()
                                        else player.play()
                                    }
                                }
                                IconButton { symbol: "▶"; enabled: editor.timelineDurationFrames > 0; onClicked: seekProjectFrame(editor.playheadFrame + 1) }
                                IconButton { symbol: "⏭"; enabled: editor.timelineDurationFrames > 0 || player.seekable; onClicked: { if (editor.timelineDurationFrames > 0) seekProjectFrame(editor.timelineDurationFrames - 1); else player.position = player.duration } }
                                Item { Layout.fillWidth: true }
                                IconButton { symbol: "⛶"; ToolTip.visible: hovered; ToolTip.text: "Fullscreen viewer"; onClicked: enterPreviewFullscreen() }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 5
                                Item { Layout.fillWidth: true }
                                IconButton {
                                    id: previewMuteButton
                                    onClicked: window.previewMuted = !window.previewMuted
                                    ToolTip.visible: hovered; ToolTip.text: window.previewMuted ? "Unmute preview" : "Mute preview"
                                    contentItem: Item {
                                        VolumeIcon { anchors.centerIn: parent; muted: window.previewMuted
                                            iconColor: previewMuteButton.enabled ? "#dbe7f5" : "#607083" }
                                    }
                                }
                                AppSlider { from: 0; to: 1; value: window.previewVolume; Layout.preferredWidth: 78; onValueChanged: window.previewVolume = value }
                                CaptionSelect { Layout.preferredWidth: 82; implicitHeight: 28; model: ["Full", "Proxy"]; currentIndex: model.indexOf(editor.previewQuality); onActivated: editor.setPreviewQuality(currentText) }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                    AppSlider {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        from: 0; to: Math.max(1, editor.timelinePreviewEnabled ? editor.projectFramesToMs(Math.max(0, editor.timelineDurationFrames - 1)) : player.duration)
                        value: editor.timelinePreviewEnabled ? editor.projectFramesToMs(editor.playheadFrame) : player.position
                        enabled: editor.timelineDurationFrames > 0 || player.seekable
                        onMoved: { if (editor.timelinePreviewEnabled) seekProjectFrame(editor.projectMsToFrames(value)); else player.position = value }
                    }
                }
            }

            Rectangle {
                SplitView.preferredWidth: 345
                SplitView.minimumWidth: 300
                color: panelColor
                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 15
                    spacing: 0
                    Item {
                        id: inspectorTabs
                        Layout.fillWidth: true
                        Layout.preferredHeight: 45
                        Rectangle { anchors.fill: parent; color: panelColor }
                        Flickable {
                            id: inspectorTabViewport
                            x: contentWidth > inspectorTabs.width ? 26 : 0
                            y: 0
                            width: Math.max(0, parent.width - (contentWidth > inspectorTabs.width ? 52 : 0))
                            height: 42
                            clip: true
                            contentWidth: inspectorTabContent.width
                            contentHeight: height
                            flickableDirection: Flickable.HorizontalFlick
                            boundsBehavior: Flickable.StopAtBounds
                            onWidthChanged: Qt.callLater(revealInspectorTab)
                            Row {
                                id: inspectorTabContent
                                width: implicitWidth
                                height: inspectorTabViewport.height
                                spacing: 4
                                Repeater {
                                    id: inspectorTabRepeater
                                    model: inspectorTabOptions
                                    delegate: Rectangle {
                                        required property var modelData
                                        required property int index
                                        width: Math.max(68, inspectorTabLabel.implicitWidth + 28)
                                        height: inspectorTabViewport.height - 2
                                         radius: 6
                                         color: modelData.id === inspectorTab ? accentSoft : tabPointer.containsMouse ? controlColor : "transparent"
                                         Behavior on color { ColorAnimation { duration: 110 } }
                                        Label {
                                            id: inspectorTabLabel
                                            anchors.centerIn: parent
                                            text: modelData.label
                                            color: modelData.id === inspectorTab ? textColor : mutedColor
                                            font.pixelSize: 11
                                            font.bold: modelData.id === inspectorTab
                                        }
                                        Rectangle {
                                            anchors.bottom: parent.bottom
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: parent.width - 14
                                            height: 2
                                            radius: 1
                                            color: accentColor
                                            visible: modelData.id === inspectorTab
                                        }
                                        MouseArea {
                                            id: tabPointer
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: inspectorTab = modelData.id
                                        }
                                    }
                                }
                            }
                            WheelHandler {
                                target: null
                                onWheel: (event) => {
                                    const maxX = Math.max(0, inspectorTabViewport.contentWidth - inspectorTabViewport.width)
                                    inspectorTabViewport.contentX = Math.max(0, Math.min(maxX,
                                        inspectorTabViewport.contentX - event.angleDelta.y / 120 * 72))
                                    event.accepted = true
                                }
                            }
                        }
                        Rectangle {
                            x: 0; y: 0; width: 24; height: 40; radius: 4
                            visible: inspectorTabViewport.contentWidth > inspectorTabs.width
                            color: backTabPointer.containsMouse ? "#233649" : "transparent"
                            opacity: inspectorTabViewport.contentX > 1 ? 1 : 0.38
                            Label { anchors.centerIn: parent; text: "‹"; color: textColor; font.pixelSize: 25 }
                            MouseArea { id: backTabPointer; anchors.fill: parent; hoverEnabled: true
                                onClicked: inspectorTabViewport.contentX = Math.max(0, inspectorTabViewport.contentX - 140) }
                        }
                        Rectangle {
                            x: parent.width - 24; y: 0; width: 24; height: 40; radius: 4
                            visible: inspectorTabViewport.contentWidth > inspectorTabs.width
                            color: forwardTabPointer.containsMouse ? "#233649" : "transparent"
                            opacity: inspectorTabViewport.contentX < inspectorTabViewport.contentWidth - inspectorTabViewport.width - 1 ? 1 : 0.38
                            Label { anchors.centerIn: parent; text: "›"; color: textColor; font.pixelSize: 25 }
                            MouseArea { id: forwardTabPointer; anchors.fill: parent; hoverEnabled: true
                                onClicked: inspectorTabViewport.contentX = Math.min(
                                    Math.max(0, inspectorTabViewport.contentWidth - inspectorTabViewport.width),
                                    inspectorTabViewport.contentX + 140) }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: borderColor }
                    RowLayout {
                        visible: inspectorTab === 6 && !!activeCaptionRow
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        spacing: 5
                        Repeater {
                            model: ["Appearance & Words", "Display Styles"]
                            delegate: Rectangle {
                                required property string modelData
                                required property int index
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                radius: 5
                                color: captionSubtab === index ? accentSoft : tabMouse.containsMouse ? controlColor : "transparent"
                                border.color: captionSubtab === index ? "#366b9f" : "transparent"
                                Behavior on color { ColorAnimation { duration: 120 } }
                                Label { anchors.centerIn: parent; text: modelData
                                    color: captionSubtab === index ? textColor : mutedColor
                                    font.pixelSize: 11; font.bold: captionSubtab === index }
                                MouseArea { id: tabMouse; anchors.fill: parent; hoverEnabled: true
                                    onClicked: captionSubtab = index }
                            }
                        }
                    }
                    ScrollView {
                        id: inspectorScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !(inspectorTab === 6 && !!activeCaptionRow && captionSubtab === 1)
                        clip: true
                        ColumnLayout {
                            width: inspectorScroll.availableWidth
                            spacing: 0
                            RowLayout {
                                visible: !!activeClip && !!selectedAsset && (inspectorTab === 0 || inspectorTab === 1
                                    || inspectorTab === 2 || inspectorTab === 5)
                                Layout.fillWidth: true; Layout.topMargin: 15; Layout.bottomMargin: 14
                                Rectangle {
                                    width: 78; height: 49; radius: 4; clip: true; color: "#20334a"
                                    Image { anchors.fill: parent; source: editor.assets.find(function(asset) { return asset.id === editor.selectedAssetId })?.thumbnail || ""; fillMode: Image.PreserveAspectCrop }
                                }
                                Column { Layout.fillWidth: true; spacing: 5
                                    Label { text: editor.assets.find(function(asset) { return asset.id === editor.selectedAssetId })?.name || "No media selected"; color: textColor; font.pixelSize: 12; elide: Text.ElideRight; width: parent.width }
                                    Label { text: editor.sourceDetails.split("\n")[0]; color: mutedColor; font.pixelSize: 10; elide: Text.ElideRight; width: parent.width }
                                }
                            }
                            Rectangle { Layout.fillWidth: true; height: 1; color: borderColor
                                visible: !!activeClip && !!selectedAsset && (inspectorTab === 0 || inspectorTab === 1
                                    || inspectorTab === 2 || inspectorTab === 5) }
                            ColumnLayout {
                                visible: inspectorTab === 5
                                Layout.fillWidth: true
                                Layout.topMargin: 14
                                spacing: 10
                                Label { text: "Generate"; color: textColor; font.pixelSize: 16; font.bold: true; Layout.bottomMargin: 3 }
                                Rectangle {
                                    Layout.fillWidth: true; implicitHeight: 40; radius: 6
                                    color: captionGenerationExpanded ? "#24364a" : panelRaised
                                    border.color: captionGenerationExpanded ? "#3f658a" : borderColor
                                    RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                                        Label { text: "Auto Caption"; color: textColor; font.pixelSize: 12; font.bold: true; Layout.fillWidth: true }
                                        Label { text: generations.captionRunning ? "Working" : generations.localReady ? "Ready" : "Setup needed"; color: generations.localReady ? "#88dfb8" : mutedColor; font.pixelSize: 10 }
                                        Label { text: captionGenerationExpanded ? "⌃" : "⌄"; color: mutedColor; font.pixelSize: 17 }
                                    }
                                    MouseArea { anchors.fill: parent; onClicked: captionGenerationExpanded = !captionGenerationExpanded }
                                }
                                ColumnLayout {
                                    visible: captionGenerationExpanded
                                    Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 10
                                    spacing: 9
                                    Label { text: "Create timed captions from the selected clip's audio."; color: mutedColor; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 11 }
                                    Label { text: "Select an audio clip or video clip with audio."; visible: !activeClip || !activeClip.hasAudio; color: mutedColor; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 11 }
                                    RowLayout { Layout.fillWidth: true; visible: !!activeClip && activeClip.hasAudio
                                        AppButton { text: "Open settings…"; visible: !generations.localReady; onClicked: openGenerationSettings(1) }
                                        AppButton { text: "Create captions"; primary: true; visible: generations.localReady; enabled: !generations.captionRunning; onClicked: editor.startAutoCaption() }
                                        Item { Layout.fillWidth: true }
                                        AppButton { text: "Cancel"; visible: generations.captionRunning; onClicked: generations.cancelJob() }
                                    }
                                    Label { text: generations.stage + " · " + jobTime(generations.elapsedSeconds); visible: generations.captionRunning; color: mutedColor; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                                    JobProgressBar { Layout.fillWidth: true; visible: generations.captionRunning; indeterminate: !generations.progressKnown; value: generations.progress }
                                    Label { text: generations.error; visible: generations.error !== "" && !generations.setupRunning; color: "#ffb899"; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 10 }
                                }
                                Rectangle {
                                    Layout.fillWidth: true; implicitHeight: 40; radius: 6; Layout.topMargin: 7
                                    color: voiceGenerationExpanded ? "#24364a" : panelRaised
                                    border.color: voiceGenerationExpanded ? "#3f658a" : borderColor
                                    RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                                        Label { text: "Voice Change"; color: textColor; font.pixelSize: 12; font.bold: true; Layout.fillWidth: true }
                                        Label { text: "SeedVC"; color: "#91bfff"; font.pixelSize: 10 }
                                        Label { text: voiceGenerationExpanded ? "⌃" : "⌄"; color: mutedColor; font.pixelSize: 17 }
                                    }
                                    MouseArea { anchors.fill: parent; onClicked: voiceGenerationExpanded = !voiceGenerationExpanded }
                                }
                                ColumnLayout {
                                    visible: voiceGenerationExpanded
                                    Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 10
                                    spacing: 9
                                    Label { text: "Select an audio clip or video clip with audio."; visible: !activeClip || !activeClip.hasAudio; color: mutedColor; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 11 }
                                    RowLayout { Layout.fillWidth: true; visible: !!activeClip && !!activeClip.voiceChangePath
                                        Label { text: "Applied · " + (activeClip ? activeClip.voiceName : ""); color: textColor; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 11 }
                                        Switch { checked: !!activeClip && !!activeClip.voiceChangeEnabled; onToggled: if (activeClip) editor.setVoiceChangeEnabled(activeClip.id, checked) }
                                    }
                                    RowLayout { Layout.fillWidth: true; visible: !!activeClip && activeClip.hasAudio && !voiceGenerations.seedvcReady
                                        Label { text: "Local runtime needed"; color: mutedColor; font.pixelSize: 11; Layout.fillWidth: true }
                                        AppButton { text: "Open settings…"; onClicked: openGenerationSettings(2) }
                                    }
                                    RowLayout { Layout.fillWidth: true; visible: !!activeClip && activeClip.hasAudio; Layout.topMargin: 4
                                        Label { text: "TARGET VOICE"; color: "#8fa8bf"; font.pixelSize: 10; font.bold: true; Layout.fillWidth: true }
                                        AppButton { text: "+ Add voice"; onClicked: addVoiceDialog.open() }
                                    }
                                    Rectangle {
                                        visible: !!activeClip && activeClip.hasAudio
                                        Layout.fillWidth: true; implicitHeight: 218; radius: 6; color: "#111b27"; border.color: borderColor
                                        GridView {
                                            id: voiceGrid
                                            anchors.fill: parent; anchors.margins: 6
                                            clip: true
                                            cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 118)))
                                            cellHeight: 97
                                            model: voiceGenerations.voices
                                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                            delegate: Item {
                                                required property var modelData
                                                width: voiceGrid.cellWidth; height: voiceGrid.cellHeight
                                                Rectangle {
                                                    anchors.fill: parent; anchors.margins: 3; radius: 5
                                                    color: selectedVoiceId === modelData.id ? "#1d3a57" : panelRaised
                                                    border.color: selectedVoiceId === modelData.id ? "#69b7ff" : borderColor
                                                    border.width: selectedVoiceId === modelData.id ? 2 : 1
                                                    Rectangle {
                                                        x: 8; y: 8; width: 34; height: 34; radius: 5
                                                        color: "#263849"; clip: true
                                                        Rectangle { x: 11; y: 6; width: 12; height: 12; radius: 6; color: "#a9bfd0" }
                                                        Rectangle { x: 4; y: 20; width: 26; height: 20; radius: 13; color: "#a9bfd0" }
                                                        Image { anchors.fill: parent; source: modelData.thumbnail; fillMode: Image.PreserveAspectCrop; asynchronous: true }
                                                    }
                                                    Label { x: 8; y: 48; width: parent.width - 16; text: modelData.title; color: textColor; font.pixelSize: 11; elide: Text.ElideRight }
                                                    Label { x: 8; y: 67; text: modelData.builtin ? "Included" : "Custom"; color: mutedColor; font.pixelSize: 9 }
                                                    MouseArea { anchors.fill: parent; onClicked: {
                                                        voiceReferencePlayer.stop()
                                                        selectedVoiceId = modelData.id
                                                        voiceReferenceStart.text = String(modelData.referenceStart)
                                                        voiceReferenceDuration.text = String(modelData.referenceDuration)
                                                    } }
                                                }
                                            }
                                        }
                                        Label { anchors.centerIn: parent; visible: voiceGenerations.voices.length === 0; text: "No voices available"; color: mutedColor; font.pixelSize: 11 }
                                    }
                                    RowLayout { Layout.fillWidth: true; visible: !!selectedVoice && !selectedVoice.builtin
                                        Item { Layout.fillWidth: true }
                                        AppButton { text: "Remove selected voice"; subtle: true; onClicked: { voiceGenerations.removeVoice(selectedVoiceId); selectedVoiceId = "" } }
                                    }
                                    Label { text: "REFERENCE FOR THIS CONVERSION"; visible: !!selectedVoice; color: "#8fa8bf"; font.pixelSize: 10; font.bold: true; Layout.topMargin: 7 }
                                    RowLayout { Layout.fillWidth: true; visible: !!selectedVoice; spacing: 7
                                        Label { text: "Start"; color: mutedColor; font.pixelSize: 11 }
                                        CaptionField { id: voiceReferenceStart; Layout.preferredWidth: 62; text: "0"; inputMethodHints: Qt.ImhFormattedNumbersOnly
                                            validator: DoubleValidator { bottom: 0; top: 36000; decimals: 1 } }
                                        Label { text: "s"; color: mutedColor; font.pixelSize: 11 }
                                        Item { Layout.fillWidth: true }
                                        Label { text: "Length"; color: mutedColor; font.pixelSize: 11 }
                                        CaptionField { id: voiceReferenceDuration; Layout.preferredWidth: 62; text: "10"; inputMethodHints: Qt.ImhFormattedNumbersOnly
                                            validator: DoubleValidator { bottom: 1; top: voiceGenerations.maxReferenceSeconds; decimals: 1 } }
                                        Label { text: "s"; color: mutedColor; font.pixelSize: 11 }
                                    }
                                    RowLayout { Layout.fillWidth: true; visible: !!selectedVoice
                                        AppButton { text: voiceReferencePlayer.playbackState === MediaPlayer.PlayingState ? "Stop preview" : "Preview excerpt"; onClicked: previewSelectedVoice() }
                                        Item { Layout.fillWidth: true }
                                        AppButton { text: "Save as default"; visible: !!selectedVoice && !selectedVoice.builtin; subtle: true
                                            onClicked: voiceGenerations.setVoiceReference(selectedVoiceId, Number(voiceReferenceStart.text), Number(voiceReferenceDuration.text)) }
                                    }
                                    Label { text: "Use up to " + voiceGenerations.maxReferenceSeconds + " seconds of clean speech. This span applies to the next conversion; save it as the voice default if you want to reuse it."; visible: !!selectedVoice; color: mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                    Label { text: "SeedVC speech conversion"; visible: !!activeClip && activeClip.hasAudio; color: mutedColor; font.pixelSize: 10; Layout.topMargin: 7 }
                                    RowLayout { Layout.fillWidth: true; visible: !!activeClip && activeClip.hasAudio; spacing: 8
                                        Label { text: "CFG"; color: mutedColor; font.pixelSize: 11 }
                                        CaptionField { id: seedvcCfg; Layout.preferredWidth: 68; text: "1.2"; horizontalAlignment: Text.AlignHCenter
                                            validator: DoubleValidator { bottom: 0; top: 3; decimals: 2 } }
                                        Label { text: "Guidance strength"; color: mutedColor; font.pixelSize: 10; Layout.fillWidth: true }
                                    }
                                    RowLayout { Layout.fillWidth: true; visible: !!activeClip && activeClip.hasAudio; spacing: 8
                                        Label { text: "Diffusion steps"; color: mutedColor; font.pixelSize: 11 }
                                        CaptionField { id: seedvcSteps; Layout.preferredWidth: 68; text: "50"; horizontalAlignment: Text.AlignHCenter
                                            validator: IntValidator { bottom: 1; top: 100 } }
                                        Label { text: "Higher values take longer"; color: mutedColor; font.pixelSize: 10; Layout.fillWidth: true }
                                    }
                                    RowLayout { Layout.fillWidth: true; visible: !!activeClip && activeClip.hasAudio && voiceGenerations.seedvcReady; Layout.topMargin: 5
                                        AppButton { text: voiceGenerations.running ? "Working…" : "Change voice"; primary: true; enabled: !voiceGenerations.running && selectedVoiceId !== ""
                                            onClicked: {
                                                voiceGenerations.generate(editor.selectedVoiceSource(), selectedVoiceId,
                                                    Number(voiceReferenceStart.text), Number(voiceReferenceDuration.text),
                                                    Number(seedvcCfg.text), Number(seedvcSteps.text))
                                                if (voiceGenerations.running) voiceGenerationProgressDialog.open()
                                            } }
                                        Item { Layout.fillWidth: true }
                                        AppButton { text: "Cancel"; visible: voiceGenerations.running; onClicked: voiceGenerations.cancel() }
                                    }
                                    Label { text: voiceGenerations.stage + " · " + jobTime(voiceGenerations.elapsedSeconds); visible: voiceGenerations.running; color: mutedColor; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                                    JobProgressBar { Layout.fillWidth: true; visible: voiceGenerations.running; value: Math.max(0, voiceGenerations.progress); indeterminate: voiceGenerations.progress < 0 }
                                    Label { text: voiceGenerations.error; visible: voiceGenerations.error !== ""; color: "#ffb899"; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 10 }
                                }
                            }
                            ColumnLayout {
                                id: captionEditor
                                property string insertAfterWordId: ""
                                property string pendingLimitRowId: ""
                                property string pendingLimitText: ""
                                function commitWordLimit() {
                                    captionLimitTimer.stop()
                                    const value = pendingLimitText.trim()
                                    if (pendingLimitRowId !== "" && /^(-1|[1-9][0-9]*)$/.test(value))
                                        editor.setCaptionMaxWords(pendingLimitRowId, Number(value))
                                    pendingLimitRowId = ""
                                }
                                Timer { id: captionLimitTimer; interval: 550; repeat: false
                                    onTriggered: captionEditor.commitWordLimit() }
                                function addWord() {
                                    if (!activeCaptionCue || newCaptionWord.text.trim().length === 0) return
                                    const rowId = activeCaptionRow.id
                                    const count = editor.captionCues.filter(function(cue) { return cue.rowId === rowId })
                                        .reduce(function(total, cue) { return total + cue.words.length }, 0)
                                    editor.insertCaptionWord(activeCaptionCue.id, insertAfterWordId, newCaptionWord.text)
                                    const after = editor.captionCues.filter(function(cue) { return cue.rowId === rowId })
                                        .reduce(function(total, cue) { return total + cue.words.length }, 0)
                                    if (after > count) newCaptionWord.text = ""
                                }
                                visible: inspectorTab === 6 && !!activeCaptionRow && captionSubtab === 0
                                Layout.fillWidth: true; Layout.topMargin: 17; spacing: 0
                                RowLayout { Layout.fillWidth: true; Layout.bottomMargin: 4
                                    Label { text: "Captions"; color: textColor; font.bold: true; font.pixelSize: 16 }
                                    Item { Layout.fillWidth: true }
                                    Label { text: activeCaptionRow ? trackName(activeCaptionRow.trackId) : ""; color: "#b597db"; font.pixelSize: 11 }
                                }
                                Label { text: "Edit words and timing here. Style applies to this CC row."; color: mutedColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.bottomMargin: 17 }
                                Label { text: "APPEARANCE"; color: "#8fa8bf"; font.pixelSize: 10; font.bold: true; Layout.bottomMargin: 8 }
                                RowLayout { Layout.fillWidth: true; spacing: 9
                                    ColumnLayout { Layout.fillWidth: true; spacing: 3
                                        Label { text: "Maximum words per sentence"; color: textColor; font.pixelSize: 11 }
                                        Label { text: "−1 keeps the original sentence breaks"; color: mutedColor; font.pixelSize: 10 }
                                    }
                                    CaptionField {
                                        Layout.preferredWidth: 62
                                        text: activeCaptionRow ? String(activeCaptionRow.maxWordsPerCue) : "-1"
                                        horizontalAlignment: Text.AlignHCenter
                                        validator: IntValidator { bottom: -1; top: 1000 }
                                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                                        onTextEdited: {
                                            captionEditor.pendingLimitRowId = activeCaptionRow ? activeCaptionRow.id : ""
                                            captionEditor.pendingLimitText = text
                                            captionLimitTimer.restart()
                                        }
                                        onEditingFinished: captionEditor.commitWordLimit()
                                    }
                                }
                                Label { text: "Font"; color: mutedColor; font.pixelSize: 11; Layout.topMargin: 16; Layout.bottomMargin: 5 }
                                CaptionSelect { Layout.fillWidth: true; model: editor.fontFamilies
                                    currentIndex: activeCaptionRow ? model.indexOf(activeCaptionRow.fontFamily) : -1
                                    onActivated: saveCaptionStyle({ fontFamily: currentText }) }
                                RowLayout { Layout.fillWidth: true; Layout.topMargin: 9; spacing: 6
                                    CaptionSelect { Layout.fillWidth: true
                                        model: { const sizes = [24, 32, 40, 48, 56, 64, 72, 80, 96, 112, 128, 160, 200, 256]; if (activeCaptionRow && !sizes.includes(activeCaptionRow.fontPixels)) sizes.push(activeCaptionRow.fontPixels); return sizes.sort(function(a,b) { return a-b }) }
                                        currentIndex: activeCaptionRow ? model.indexOf(activeCaptionRow.fontPixels) : -1
                                        onActivated: saveCaptionStyle({ fontPixels: Number(currentText) }) }
                                    CaptionField { Layout.preferredWidth: 49; text: activeCaptionRow ? String(activeCaptionRow.fontPixels) : "72"
                                        horizontalAlignment: Text.AlignHCenter; validator: IntValidator { bottom: 12; top: 300 }
                                        ToolTip.visible: hovered; ToolTip.text: "Exact font size in pixels"
                                        onEditingFinished: if (activeCaptionRow && Number(text) !== activeCaptionRow.fontPixels) saveCaptionStyle({ fontPixels: Number(text) }) }
                                    IconButton { symbol: "B"; selected: !!activeCaptionRow && activeCaptionRow.bold; ToolTip.visible: hovered; ToolTip.text: "Bold"
                                        onClicked: saveCaptionStyle({ bold: !activeCaptionRow.bold }) }
                                    IconButton { symbol: "I"; selected: !!activeCaptionRow && activeCaptionRow.italic; ToolTip.visible: hovered; ToolTip.text: "Italic"
                                        onClicked: saveCaptionStyle({ italic: !activeCaptionRow.italic }) }
                                }
                                Label { text: "Color"; color: mutedColor; font.pixelSize: 11; Layout.topMargin: 16; Layout.bottomMargin: 5 }
                                RowLayout { Layout.fillWidth: true; spacing: 6
                                    CaptionColor { Layout.fillWidth: true; text: "Text"; colorValue: activeCaptionRow ? activeCaptionRow.color : "#ffffff"
                                        onClicked: chooseCaptionColor("color", activeCaptionRow.color) }
                                    CaptionColor { Layout.fillWidth: true; text: "Highlight"; colorValue: activeCaptionRow ? activeCaptionRow.highlightColor : "#ffd34d"
                                        onClicked: chooseCaptionColor("highlightColor", activeCaptionRow.highlightColor) }
                                }
                                CaptionColor { Layout.fillWidth: true; Layout.topMargin: 6; text: "Background"; colorValue: activeCaptionRow ? activeCaptionRow.backgroundColor : "#000000"
                                    onClicked: chooseCaptionColor("backgroundColor", activeCaptionRow.backgroundColor) }
                                RowLayout { Layout.fillWidth: true; Layout.topMargin: 8; spacing: 8
                                    Label { text: "Background opacity"; color: mutedColor; font.pixelSize: 11; Layout.preferredWidth: 123 }
                                    AppSlider { Layout.fillWidth: true; from: 0; to: 100; value: activeCaptionRow ? activeCaptionRow.backgroundOpacity : 65
                                        onPressedChanged: if (!pressed && activeCaptionRow) saveCaptionStyle({ backgroundOpacity: Math.round(value) }) }
                                    Label { text: activeCaptionRow ? activeCaptionRow.backgroundOpacity + "%" : ""; color: textColor; font.pixelSize: 11; Layout.preferredWidth: 32 }
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: borderColor; Layout.topMargin: 18; Layout.bottomMargin: 17 }
                                Label { text: "PLACEMENT"; color: "#8fa8bf"; font.pixelSize: 10; font.bold: true; Layout.bottomMargin: 7 }
                                RowLayout { Layout.fillWidth: true; spacing: 8
                                    Label { text: "Horizontal"; color: mutedColor; font.pixelSize: 11; Layout.preferredWidth: 70 }
                                    AppSlider { Layout.fillWidth: true; from: -100; to: 200; value: activeCaptionVisual.x === undefined ? 50 : activeCaptionVisual.x
                                        onPressedChanged: if (!pressed && activeCaptionRow) {
                                            editor.setVisualPropertyValue("caption", activeCaptionRow.id, "x", editor.playheadFrame, Math.round(value))
                                        } }
                                    CaptionField { Layout.preferredWidth: 64; text: activeCaptionVisual.x === undefined ? "50" : String(activeCaptionVisual.x); unit: "%"; validator: IntValidator { bottom: -100; top: 200 } onEditingFinished: if (activeCaptionRow && acceptableInput) editor.setVisualPropertyValue("caption", activeCaptionRow.id, "x", editor.playheadFrame, Number(text)) }
                                    KeyframeControl { animated: !!activeCaptionRow?.positionXKeyframes?.length; atKeyframe: hasVisualKeyframe(activeCaptionRow?.positionXKeyframes, editor.playheadFrame); ToolTip.visible: hovered; ToolTip.text: "Toggle horizontal keyframe"; onClicked: toggleVisualKeyframe("caption", activeCaptionRow.id, "x", activeCaptionRow.positionXKeyframes, 0) }
                                }
                                RowLayout { Layout.fillWidth: true; Layout.topMargin: 4; spacing: 8
                                    Label { text: "Vertical"; color: mutedColor; font.pixelSize: 11; Layout.preferredWidth: 70 }
                                    AppSlider { Layout.fillWidth: true; from: -100; to: 200; value: activeCaptionVisual.y === undefined ? 84 : activeCaptionVisual.y
                                        onPressedChanged: if (!pressed && activeCaptionRow) {
                                            editor.setVisualPropertyValue("caption", activeCaptionRow.id, "y", editor.playheadFrame, Math.round(value))
                                        } }
                                    CaptionField { Layout.preferredWidth: 64; text: activeCaptionVisual.y === undefined ? "84" : String(activeCaptionVisual.y); unit: "%"; validator: IntValidator { bottom: -100; top: 200 } onEditingFinished: if (activeCaptionRow && acceptableInput) editor.setVisualPropertyValue("caption", activeCaptionRow.id, "y", editor.playheadFrame, Number(text)) }
                                    KeyframeControl { animated: !!activeCaptionRow?.positionYKeyframes?.length; atKeyframe: hasVisualKeyframe(activeCaptionRow?.positionYKeyframes, editor.playheadFrame); ToolTip.visible: hovered; ToolTip.text: "Toggle vertical keyframe"; onClicked: toggleVisualKeyframe("caption", activeCaptionRow.id, "y", activeCaptionRow.positionYKeyframes, 0) }
                                }
                                RowLayout { Layout.fillWidth: true; Layout.topMargin: 7; spacing: 8
                                    Label { text: "Scale"; color: mutedColor; font.pixelSize: 11; Layout.preferredWidth: 70 }
                                    AppSlider { Layout.fillWidth: true; from: 25; to: 400; value: activeCaptionVisual.scale || 100
                                        onPressedChanged: if (!pressed && activeCaptionRow) {
                                            editor.setVisualPropertyValue("caption", activeCaptionRow.id, "scale", editor.playheadFrame, Math.round(value))
                                        } }
                                    CaptionField { Layout.preferredWidth: 64; text: String(activeCaptionVisual.scale || 100); unit: "%"; validator: IntValidator { bottom: 25; top: 400 } onEditingFinished: if (activeCaptionRow && acceptableInput) editor.setVisualPropertyValue("caption", activeCaptionRow.id, "scale", editor.playheadFrame, Number(text)) }
                                    KeyframeControl { animated: !!activeCaptionRow?.scaleKeyframes?.length; atKeyframe: hasVisualKeyframe(activeCaptionRow?.scaleKeyframes, editor.playheadFrame); ToolTip.visible: hovered; ToolTip.text: "Toggle scale keyframe"; onClicked: toggleVisualKeyframe("caption", activeCaptionRow.id, "scale", activeCaptionRow.scaleKeyframes, 0) }
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: borderColor; Layout.topMargin: 20; Layout.bottomMargin: 18 }
                                Label { text: "TRANSCRIPT"; color: "#8fa8bf"; font.pixelSize: 10; font.bold: true; Layout.bottomMargin: 8 }
                                CaptionSelect { Layout.fillWidth: true; textRole: "text"; valueRole: "id"
                                    model: activeCaptionRow ? editor.captionCues.filter(function(cue) { return cue.rowId === activeCaptionRow.id }) : []
                                    currentIndex: activeCaptionCue ? model.findIndex(function(cue) { return cue.id === activeCaptionCue.id }) : -1
                                    onActivated: editor.selectCaption(currentValue) }
                                Label { text: "Select a CC clip on the track to edit its words."; visible: !activeCaptionCue; color: mutedColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.topMargin: 10 }
                                RowLayout { visible: !!activeCaptionCue; Layout.fillWidth: true; Layout.topMargin: 12; Layout.bottomMargin: 7
                                    Label { text: "WORDS"; color: "#8fa8bf"; font.pixelSize: 10; font.bold: true }
                                    Item { Layout.fillWidth: true }
                                    Label { text: activeCaptionCue ? activeCaptionCue.words.length + " words · frames" : ""; color: mutedColor; font.pixelSize: 10 }
                                }
                                ListView {
                                    id: captionWordsList
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: activeCaptionCue ? Math.min(320, activeCaptionCue.words.length * 80) : 0
                                    visible: !!activeCaptionCue
                                    clip: true; spacing: 5
                                    model: activeCaptionCue ? activeCaptionCue.words : []
                                    delegate: Rectangle {
                                        required property var modelData
                                        required property int index
                                        width: captionWordsList.width; height: 74; radius: 6
                                        color: controlColor; border.color: "#304459"
                                        ColumnLayout { anchors.fill: parent; anchors.margins: 7; spacing: 4
                                            RowLayout { Layout.fillWidth: true; spacing: 5
                                                Label { text: String(index + 1).padStart(2, "0"); color: "#7897b6"; font.pixelSize: 10; Layout.preferredWidth: 20 }
                                                CaptionField { Layout.fillWidth: true; implicitHeight: 28; text: modelData.text
                                                    onEditingFinished: if (activeCaptionCue && text !== modelData.text) editor.updateCaptionWord(activeCaptionCue.id, modelData.id, text, modelData.start, modelData.end) }
                                                IconButton { symbol: "+"; implicitWidth: 25; implicitHeight: 27; ToolTip.visible: hovered; ToolTip.text: "Insert a word after this one"
                                                    onClicked: { captionEditor.insertAfterWordId = modelData.id; newCaptionWord.forceActiveFocus() } }
                                                IconButton { symbol: "×"; implicitWidth: 25; implicitHeight: 27; ToolTip.visible: hovered; ToolTip.text: "Delete this word"
                                                    onClicked: if (activeCaptionCue) editor.removeCaptionWord(activeCaptionCue.id, modelData.id) }
                                            }
                                            RowLayout { Layout.fillWidth: true; spacing: 6
                                                Label { text: "IN"; color: mutedColor; font.pixelSize: 9; Layout.preferredWidth: 20 }
                                                CaptionField { Layout.fillWidth: true; implicitHeight: 25; text: String(modelData.start); font.pixelSize: 10; validator: IntValidator { bottom: 0; top: 2147483647 }
                                                    onEditingFinished: if (activeCaptionCue && Number(text) !== modelData.start) editor.updateCaptionWord(activeCaptionCue.id, modelData.id, modelData.text, Number(text), modelData.end) }
                                                Label { text: "OUT"; color: mutedColor; font.pixelSize: 9 }
                                                CaptionField { Layout.fillWidth: true; implicitHeight: 25; text: String(modelData.end); font.pixelSize: 10; validator: IntValidator { bottom: 1; top: 2147483647 }
                                                    onEditingFinished: if (activeCaptionCue && Number(text) !== modelData.end) editor.updateCaptionWord(activeCaptionCue.id, modelData.id, modelData.text, modelData.start, Number(text)) }
                                            }
                                        }
                                    }
                                }
                                RowLayout { visible: !!activeCaptionCue; Layout.fillWidth: true; Layout.topMargin: 7; spacing: 6
                                    IconButton { symbol: "↤"; implicitWidth: 28; ToolTip.visible: hovered; ToolTip.text: "Insert before the first word"
                                        onClicked: { captionEditor.insertAfterWordId = ""; newCaptionWord.forceActiveFocus() } }
                                    CaptionField { id: newCaptionWord; Layout.fillWidth: true; placeholderText: captionEditor.insertAfterWordId === "" ? "New word at start" : "New word after selection"
                                        onAccepted: captionEditor.addWord() }
                                    AppButton { id: addCaptionWordButton; text: "Add"; primary: true; enabled: !!activeCaptionCue && newCaptionWord.text.trim().length > 0
                                        onClicked: captionEditor.addWord() }
                                }
                                Rectangle { Layout.fillWidth: true; height: 1; color: borderColor; Layout.topMargin: 20; Layout.bottomMargin: 18 }
                                AppButton { text: activeCaptionCue ? "Delete selected CC clip" : "Delete CC row"; subtle: true; Layout.topMargin: 20; Layout.fillWidth: true
                                    onClicked: editor.removeSelectedCaption() }
                            }
                            ColumnLayout {
                                visible: inspectorTab === 4
                                Layout.fillWidth: true; Layout.topMargin: 14
                                spacing: 10
                                Label { text: activeTransition ? activeTransition.name : "Select a transition on the timeline"; color: textColor; font.pixelSize: 12; font.bold: true }
                                Label {
                                    visible: !!activeTransition
                                    text: activeTransition ? (activeTransition.paired ? "Between two clips" : "Clip edge to black") : ""
                                    color: mutedColor; font.pixelSize: 11
                                }
                                Label {
                                    visible: !!activeTransition && !activeTransitionDefinition
                                    text: "This transition's module is unavailable. Its settings are preserved."
                                    color: "#ffb085"; font.pixelSize: 11; wrapMode: Text.WordWrap
                                    Layout.fillWidth: true
                                }
                                RowLayout {
                                    visible: !!activeTransition; Layout.fillWidth: true
                                    Label { text: "Duration"; color: mutedColor; font.pixelSize: 11 }
                                    Item { Layout.fillWidth: true }
                                    CaptionField {
                                        Layout.preferredWidth: 80; implicitHeight: 27
                                        text: activeTransition ? String(activeTransition.duration) : ""
                                        color: textColor; font.pixelSize: 11
                                        validator: IntValidator { bottom: 1; top: 2147483647 }
                                        onEditingFinished: if (activeTransition) editor.setTransitionDuration(activeTransition.id, Number(text))
                                    }
                                    Label { text: "frames"; color: mutedColor; font.pixelSize: 11 }
                                }
                                Label {
                                    visible: !!activeTransition && activeTransition.paired
                                    text: "Balance · frames before / after the cut"
                                    color: mutedColor; font.pixelSize: 11
                                }
                                RowLayout {
                                    visible: !!activeTransition && activeTransition.paired; Layout.fillWidth: true
                                    AppSlider {
                                        Layout.fillWidth: true; from: 20; to: 80; stepSize: 1
                                        value: activeTransition ? activeTransition.balance : 50
                                        onPressedChanged: if (!pressed && activeTransition)
                                            editor.setTransitionBalance(activeTransition.id, Math.round(value))
                                    }
                                    Label { text: activeTransition ? activeTransition.balance + "%" : "50%"; color: textColor; font.pixelSize: 11; Layout.preferredWidth: 40 }
                                }
                                Repeater {
                                    model: activeTransitionDefinition ? activeTransitionDefinition.parameters.filter(function(item) {
                                        return item.id !== "balance"
                                    }) : []
                                    delegate: RowLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Label { text: modelData.name; color: mutedColor; font.pixelSize: 11 }
                                        AppSlider {
                                            Layout.fillWidth: true; from: modelData.minimum; to: modelData.maximum; stepSize: 1
                                            value: activeTransition && activeTransition.parameters[modelData.id] !== undefined
                                                ? activeTransition.parameters[modelData.id] : modelData.initial
                                            onPressedChanged: if (!pressed && activeTransition)
                                                editor.setTransitionParameter(activeTransition.id, modelData.id, Math.round(value))
                                        }
                                    }
                                }
                                AppButton {
                                    visible: !!activeTransition; text: "Remove transition"; subtle: true
                                    Layout.fillWidth: true
                                    onClicked: if (activeTransition) editor.removeTransition(activeTransition.id)
                                }
                            }
                            Label { text: "⌄  Text title"; color: textColor; font.bold: true; font.pixelSize: 12; Layout.topMargin: 15; Layout.bottomMargin: 8; visible: inspectorTab === 7 && !!activeTitle }
                            CaptionField { id: titleInspectorText; Layout.fillWidth: true; visible: inspectorTab === 7 && !!activeTitle; text: activeTitle ? activeTitle.text : ""; onEditingFinished: saveTitleContent() }
                            RowLayout { Layout.fillWidth: true; visible: inspectorTab === 7 && !!activeTitle; Layout.topMargin: 8
                                Label { text: "Start"; color: mutedColor; font.pixelSize: 10 }
                                CaptionField { id: titleStart; Layout.fillWidth: true; text: activeTitle ? String(activeTitle.start) : "0"; validator: IntValidator { bottom: 0; top: 2147483647 } onEditingFinished: saveTitleContent() }
                                Label { text: "Frames"; color: mutedColor; font.pixelSize: 10 }
                                CaptionField { id: titleDuration; Layout.fillWidth: true; text: activeTitle ? String(activeTitle.duration) : "150"; validator: IntValidator { bottom: 1; top: 2147483647 } onEditingFinished: saveTitleContent() }
                            }
                            RowLayout { Layout.fillWidth: true; visible: inspectorTab === 7 && !!activeTitle; Layout.topMargin: 7
                                Label { text: "Size"; color: mutedColor; font.pixelSize: 10 }
                                CaptionField { id: titleSize; Layout.fillWidth: true; text: activeTitle ? String(activeTitle.fontPixels) : "88"; validator: IntValidator { bottom: 12; top: 300 } onEditingFinished: saveTitleContent() }
                                Label { text: "Color"; color: mutedColor; font.pixelSize: 10 }
                                CaptionColor { id: titleColor; Layout.fillWidth: true; text: activeTitle ? activeTitle.color : "#ffffff"; colorValue: activeTitle ? activeTitle.color : "#ffffff"; onClicked: if (activeTitle) chooseTitleColor("text", activeTitle.color) }
                            }
                            CaptionSelect { id: titleFont; Layout.fillWidth: true; visible: inspectorTab === 7 && !!activeTitle; model: editor.fontFamilies
                                currentIndex: activeTitle ? model.indexOf(activeTitle.fontFamily) : -1
                                onActivated: saveTitleStyle() }
                            RowLayout { Layout.fillWidth: true; visible: inspectorTab === 7 && !!activeTitle
                                CheckBox { id: titleBold; text: "Bold"; checked: activeTitle ? activeTitle.bold : true; onToggled: saveTitleStyle() }
                                CheckBox { id: titleItalic; text: "Italic"; checked: activeTitle ? activeTitle.italic : false; onToggled: saveTitleStyle() }
                            }
                            RowLayout { Layout.fillWidth: true; visible: inspectorTab === 7 && !!activeTitle
                                CaptionColor { id: titleBackground; Layout.fillWidth: true; text: activeTitle ? activeTitle.backgroundColor : "#000000"; colorValue: activeTitle ? activeTitle.backgroundColor : "#000000"; onClicked: if (activeTitle) chooseTitleColor("background", activeTitle.backgroundColor) }
                                CaptionField { id: titleBackgroundOpacity; Layout.preferredWidth: 48; text: activeTitle ? String(activeTitle.backgroundOpacity) : "0"; validator: IntValidator { bottom: 0; top: 100 } onEditingFinished: saveTitleStyle() }
                            }
                            Label { text: "Position X / Y · Anchor X / Y (%)"; visible: inspectorTab === 7 && !!activeTitle; color: mutedColor; font.pixelSize: 10 }
                             RowLayout { Layout.fillWidth: true; visible: inspectorTab === 7 && !!activeTitle
                                  CaptionField { id: titlePositionX; Layout.fillWidth: true; text: activeTitle ? String(activeTitleVisual.x) : "50"; unit: "%"; validator: IntValidator { bottom: -200; top: 300 } onEditingFinished: setTitlePosition(Number(text), activeTitleVisual.y) }
                                 KeyframeControl { animated: !!activeTitle?.positionXKeyframes?.length; atKeyframe: hasVisualKeyframe(activeTitle?.positionXKeyframes, editor.playheadFrame - (activeTitle?.start || 0)); ToolTip.visible: hovered; ToolTip.text: "Toggle X keyframe"; onClicked: toggleVisualKeyframe("title", activeTitle.id, "x", activeTitle.positionXKeyframes, activeTitle.start) }
                                  CaptionField { id: titlePositionY; Layout.fillWidth: true; text: activeTitle ? String(activeTitleVisual.y) : "50"; unit: "%"; validator: IntValidator { bottom: -200; top: 300 } onEditingFinished: setTitlePosition(activeTitleVisual.x, Number(text)) }
                                 KeyframeControl { animated: !!activeTitle?.positionYKeyframes?.length; atKeyframe: hasVisualKeyframe(activeTitle?.positionYKeyframes, editor.playheadFrame - (activeTitle?.start || 0)); ToolTip.visible: hovered; ToolTip.text: "Toggle Y keyframe"; onClicked: toggleVisualKeyframe("title", activeTitle.id, "y", activeTitle.positionYKeyframes, activeTitle.start) }
                                CaptionField { id: titleAnchorX; Layout.fillWidth: true; text: activeTitle ? String(activeTitle.anchorX) : "50"; unit: "%"; validator: IntValidator { bottom: 0; top: 100 } onEditingFinished: saveTitleStyle() }
                                CaptionField { id: titleAnchorY; Layout.fillWidth: true; text: activeTitle ? String(activeTitle.anchorY) : "50"; unit: "%"; validator: IntValidator { bottom: 0; top: 100 } onEditingFinished: saveTitleStyle() }
                            }
                            RowLayout { Layout.fillWidth: true; visible: inspectorTab === 7 && !!activeTitle; Layout.topMargin: 8
                                Label { text: "Scale"; color: mutedColor; font.pixelSize: 11 }
                                AppSlider { Layout.fillWidth: true; from: 25; to: 400; value: activeTitleVisual.scale || 100
                                    onPressedChanged: if (!pressed && activeTitle) {
                                        editor.setVisualPropertyValue("title", activeTitle.id, "scale", editor.playheadFrame, Math.round(value))
                                    } }
                                  CaptionField { Layout.preferredWidth: 65; text: String(activeTitleVisual.scale || 100); unit: "%"; validator: IntValidator { bottom: 25; top: 400 } onEditingFinished: if (activeTitle && acceptableInput) editor.setVisualPropertyValue("title", activeTitle.id, "scale", editor.playheadFrame, Number(text)) }
                                 KeyframeControl { animated: !!activeTitle?.scaleKeyframes?.length; atKeyframe: hasVisualKeyframe(activeTitle?.scaleKeyframes, editor.playheadFrame - (activeTitle?.start || 0)); ToolTip.visible: hovered; ToolTip.text: "Toggle scale keyframe"; onClicked: toggleVisualKeyframe("title", activeTitle.id, "scale", activeTitle.scaleKeyframes, activeTitle.start) }
                             }
                            Rectangle { Layout.fillWidth: true; height: 1; color: borderColor; Layout.topMargin: 14; visible: inspectorTab === 7 && !!activeTitle }
                            Label { text: "TRANSFORM"; color: mutedColor; font.bold: true; font.pixelSize: 10; Layout.topMargin: 16; Layout.bottomMargin: 8; visible: inspectorTab === 0 && !!activeClip && activeClip.hasVideo }
                            RowLayout { Layout.fillWidth: true; visible: inspectorTab === 0 && !!activeClip && activeClip.hasVideo
                                Label { text: "Position"; color: mutedColor; font.pixelSize: 11; Layout.preferredWidth: 72 }
                                Label { text: "X"; color: mutedColor; font.pixelSize: 10 }
                                  CaptionField { Layout.fillWidth: true; unit: "px"; text: activeClip ? String(activeClipVisual.x) : "0"; validator: IntValidator { bottom: -4000; top: 4000 } onEditingFinished: if (activeClip && acceptableInput) editor.setVisualPropertyValue("clip", activeClip.id, "x", editor.playheadFrame, Number(text)) }
                                 KeyframeControl { animated: !!activeClip?.positionXKeyframes?.length; atKeyframe: hasVisualKeyframe(activeClip?.positionXKeyframes, editor.playheadFrame - (activeClip?.start || 0)); ToolTip.visible: hovered; ToolTip.text: "Toggle X keyframe"; onClicked: toggleVisualKeyframe("clip", activeClip.id, "x", activeClip.positionXKeyframes, activeClip.start) }
                                Label { text: "Y"; color: mutedColor; font.pixelSize: 10 }
                                  CaptionField { Layout.fillWidth: true; unit: "px"; text: activeClip ? String(activeClipVisual.y) : "0"; validator: IntValidator { bottom: -4000; top: 4000 } onEditingFinished: if (activeClip && acceptableInput) editor.setVisualPropertyValue("clip", activeClip.id, "y", editor.playheadFrame, Number(text)) }
                                 KeyframeControl { animated: !!activeClip?.positionYKeyframes?.length; atKeyframe: hasVisualKeyframe(activeClip?.positionYKeyframes, editor.playheadFrame - (activeClip?.start || 0)); ToolTip.visible: hovered; ToolTip.text: "Toggle Y keyframe"; onClicked: toggleVisualKeyframe("clip", activeClip.id, "y", activeClip.positionYKeyframes, activeClip.start) }
                            }
                            RowLayout { Layout.fillWidth: true; Layout.topMargin: 8; visible: inspectorTab === 0 && !!activeClip && activeClip.hasVideo
                                Label { text: "Scale"; color: mutedColor; font.pixelSize: 11; Layout.preferredWidth: 72 }
                                 AppSlider { id: scaleControl; Layout.fillWidth: true; from: 25; to: 400; value: activeClip ? activeClipVisual.scale : 100; onPressedChanged: if (!pressed && activeClip) editor.setVisualPropertyValue("clip", activeClip.id, "scale", editor.playheadFrame, Math.round(value)) }
                                  CaptionField { Layout.preferredWidth: 65; text: activeClip ? String(activeClipVisual.scale) : "100"; unit: "%"; validator: IntValidator { bottom: 25; top: 400 } onEditingFinished: if (activeClip && acceptableInput) editor.setVisualPropertyValue("clip", activeClip.id, "scale", editor.playheadFrame, Number(text)) }
                                 KeyframeControl { animated: !!activeClip?.scaleKeyframes?.length; atKeyframe: hasVisualKeyframe(activeClip?.scaleKeyframes, editor.playheadFrame - (activeClip?.start || 0)); ToolTip.visible: hovered; ToolTip.text: "Toggle scale keyframe"; onClicked: toggleVisualKeyframe("clip", activeClip.id, "scale", activeClip.scaleKeyframes, activeClip.start) }
                            }
                            RowLayout { Layout.fillWidth: true; Layout.topMargin: 5; visible: inspectorTab === 0 && !!activeClip && activeClip.hasVideo
                                Label { text: "Rotation"; color: mutedColor; font.pixelSize: 11; Layout.preferredWidth: 72 }
                                 AppSlider { Layout.fillWidth: true; from: -180; to: 180; value: activeClip ? activeClipVisual.rotation : 0; onPressedChanged: if (!pressed && activeClip) editor.setVisualPropertyValue("clip", activeClip.id, "rotation", editor.playheadFrame, Math.round(value)) }
                                  CaptionField { Layout.preferredWidth: 65; text: activeClip ? String(activeClipVisual.rotation) : "0"; unit: "°"; validator: IntValidator { bottom: -180; top: 180 } onEditingFinished: if (activeClip && acceptableInput) editor.setVisualPropertyValue("clip", activeClip.id, "rotation", editor.playheadFrame, Number(text)) }
                                 KeyframeControl { animated: !!activeClip?.rotationKeyframes?.length; atKeyframe: hasVisualKeyframe(activeClip?.rotationKeyframes, editor.playheadFrame - (activeClip?.start || 0)); ToolTip.visible: hovered; ToolTip.text: "Toggle rotation keyframe"; onClicked: toggleVisualKeyframe("clip", activeClip.id, "rotation", activeClip.rotationKeyframes, activeClip.start) }
                            }
                            Label { text: "BLEND"; color: mutedColor; font.bold: true; font.pixelSize: 10; Layout.topMargin: 19; Layout.bottomMargin: 7; visible: inspectorTab === 0 && !!activeClip && activeClip.hasVideo }
                            RowLayout { Layout.fillWidth: true; visible: inspectorTab === 0 && !!activeClip && activeClip.hasVideo
                                Label { text: "Opacity"; color: mutedColor; font.pixelSize: 11; Layout.preferredWidth: 72 }
                                 AppSlider { Layout.fillWidth: true; from: 0; to: 100; value: activeClip ? activeClipVisual.opacity : 100; onPressedChanged: if (!pressed && activeClip) editor.setVisualPropertyValue("clip", activeClip.id, "opacity", editor.playheadFrame, Math.round(value)) }
                                  CaptionField { Layout.preferredWidth: 65; text: activeClip ? String(activeClipVisual.opacity) : "100"; unit: "%"; validator: IntValidator { bottom: 0; top: 100 } onEditingFinished: if (activeClip && acceptableInput) editor.setVisualPropertyValue("clip", activeClip.id, "opacity", editor.playheadFrame, Number(text)) }
                                 KeyframeControl { animated: !!activeClip?.opacityKeyframes?.length; atKeyframe: hasVisualKeyframe(activeClip?.opacityKeyframes, editor.playheadFrame - (activeClip?.start || 0)); ToolTip.visible: hovered; ToolTip.text: "Toggle opacity keyframe"; onClicked: toggleVisualKeyframe("clip", activeClip.id, "opacity", activeClip.opacityKeyframes, activeClip.start) }
                            }
                            Label { text: "SPEED"; color: mutedColor; font.bold: true; font.pixelSize: 10; Layout.topMargin: 19; Layout.bottomMargin: 7; visible: !!activeClip && (inspectorTab === 0 || (inspectorTab === 1 && !activeClip.hasVideo)) }
                            RowLayout { Layout.fillWidth: true; visible: !!activeClip && (inspectorTab === 0 || (inspectorTab === 1 && !activeClip.hasVideo))
                                Label { text: "Speed"; color: mutedColor; font.pixelSize: 11; Layout.preferredWidth: 72 }
                                AppSlider { Layout.fillWidth: true; from: 50; to: 200; value: activeClip ? activeClip.speedPercent : 100; onPressedChanged: if (!pressed && activeClip) editor.setClipSpeed(activeClip.id, Math.round(value)) }
                                 CaptionField { Layout.preferredWidth: 65; text: activeClip ? String(activeClip.speedPercent) : "100"; unit: "%"; validator: IntValidator { bottom: 50; top: 200 } onEditingFinished: if (activeClip && acceptableInput) editor.setClipSpeed(activeClip.id, Number(text)) }
                            }
                            RowLayout { Layout.fillWidth: true; visible: !!activeClip && (inspectorTab === 0 || (inspectorTab === 1 && !activeClip.hasVideo)); Layout.topMargin: 6
                                Label { text: "Duration"; color: mutedColor; font.pixelSize: 11 }
                                Item { Layout.fillWidth: true }
                                Label { text: activeClip ? editor.projectFrameTimestamp(activeClip.duration) + " · " + activeClip.duration + "f" : "00:00:00.000"; color: textColor; font.pixelSize: 11 }
                            }
                            Label { text: "GAIN"; color: mutedColor; font.bold: true; font.pixelSize: 10; Layout.topMargin: 16; Layout.bottomMargin: 8; visible: inspectorTab === 1 && !!activeClip && activeClip.hasAudio }
                            RowLayout { Layout.fillWidth: true; visible: inspectorTab === 1 && !!activeClip && activeClip.hasAudio
                                Label { text: "Gain at cursor"; color: mutedColor; font.pixelSize: 11; Layout.preferredWidth: 91 }
                                 AppSlider { Layout.fillWidth: true; from: 0; to: 300; value: selectedClipGain; onPressedChanged: if (!pressed && activeClip) editor.setVisualPropertyValue("clip", activeClip.id, "gain", editor.playheadFrame, Math.round(value)) }
                                  CaptionField { Layout.preferredWidth: 65; text: String(selectedClipGain); unit: "%"; validator: IntValidator { bottom: 0; top: 300 } onEditingFinished: if (activeClip && acceptableInput) editor.setVisualPropertyValue("clip", activeClip.id, "gain", editor.playheadFrame, Number(text)) }
                                 KeyframeControl { animated: !!activeClip?.gainKeyframes?.length; atKeyframe: hasVisualKeyframe(activeClip?.gainKeyframes, editor.playheadFrame - (activeClip?.start || 0)); ToolTip.visible: hovered; ToolTip.text: "Toggle gain keyframe"; onClicked: toggleVisualKeyframe("clip", activeClip.id, "gain", activeClip.gainKeyframes, activeClip.start) }
                            }
                            RowLayout { Layout.fillWidth: true; visible: inspectorTab === 1 && !!activeClip && activeClip.hasAudio
                                Label { text: activeClip && activeClip.gainKeyframes.length ? activeClip.gainKeyframes.length + " gain points · linear tween" : "Constant gain"; color: mutedColor; font.pixelSize: 10; Layout.fillWidth: true }
                                 AppButton { text: "Reset gain"; subtle: true; visible: !!activeClip && (activeClip.gainPercent !== 100 || activeClip.gainKeyframes.length > 0); onClicked: editor.resetClipGain(activeClip.id) }
                             }
                            Label { text: "FADES"; color: mutedColor; font.bold: true; font.pixelSize: 10; Layout.topMargin: 18; Layout.bottomMargin: 7; visible: inspectorTab === 1 && !!activeClip && activeClip.hasAudio }
                            RowLayout { Layout.fillWidth: true; visible: inspectorTab === 1 && !!activeClip && activeClip.hasAudio; spacing: 10
                                ColumnLayout { Layout.fillWidth: true; spacing: 5
                                    Label { text: "Fade in"; color: mutedColor; font.pixelSize: 11 }
                                    CaptionField { Layout.fillWidth: true; text: activeClip ? String(activeClip.fadeInFrames) : "0"; unit: "frames"; validator: IntValidator { bottom: 0; top: 2147483647 } onEditingFinished: if (activeClip && acceptableInput) editor.setClipAudio(activeClip.id, activeClip.gainPercent, Number(text), activeClip.fadeOutFrames) }
                                }
                                ColumnLayout { Layout.fillWidth: true; spacing: 5
                                    Label { text: "Fade out"; color: mutedColor; font.pixelSize: 11 }
                                    CaptionField { Layout.fillWidth: true; text: activeClip ? String(activeClip.fadeOutFrames) : "0"; unit: "frames"; validator: IntValidator { bottom: 0; top: 2147483647 } onEditingFinished: if (activeClip && acceptableInput) editor.setClipAudio(activeClip.id, activeClip.gainPercent, activeClip.fadeInFrames, Number(text)) }
                                }
                            }
                            Label { text: "AUDIO EFFECT"; color: mutedColor; font.bold: true; font.pixelSize: 10; Layout.topMargin: 18; Layout.bottomMargin: 7; visible: inspectorTab === 1 && !!activeClip && activeClip.hasAudio }
                            CaptionSelect {
                                Layout.fillWidth: true
                                visible: inspectorTab === 1 && !!activeClip && activeClip.hasAudio
                                textRole: "name"; valueRole: "id"
                                model: [{id: "", name: "None"}].concat(editor.audioEffects)
                                currentIndex: Math.max(0, model.findIndex(function(effect) { return effect.id === activeClip?.audioEffectId }))
                                onActivated: if (activeClip) editor.setClipAudioEffect(activeClip.id, currentValue, activeClip.audioEffectStrength)
                            }
                            RowLayout { Layout.fillWidth: true; visible: inspectorTab === 1 && !!activeClip && activeClip.hasAudio && activeClip.audioEffectId !== ""; Layout.topMargin: 7
                                Label { text: "Strength"; color: mutedColor; font.pixelSize: 11; Layout.preferredWidth: 68 }
                                AppSlider { Layout.fillWidth: true; from: 0; to: 100; value: activeClip ? activeClip.audioEffectStrength : 100; onPressedChanged: if (!pressed && activeClip) editor.setClipAudioEffect(activeClip.id, activeClip.audioEffectId, Math.round(value)) }
                                CaptionField { Layout.preferredWidth: 65; text: activeClip ? String(activeClip.audioEffectStrength) : "100"; unit: "%"; validator: IntValidator { bottom: 0; top: 100 } onEditingFinished: if (activeClip && acceptableInput) editor.setClipAudioEffect(activeClip.id, activeClip.audioEffectId, Number(text)) }
                            }
                             Label { text: trackName(selectedEffectTrack).toUpperCase() + " EFFECTS"; color: mutedColor; font.bold: true; font.pixelSize: 10; Layout.topMargin: 16; Layout.bottomMargin: 8; visible: inspectorTab === 2 }
                            Label { text: "Effects apply across this row. Use ◇ at the playhead, then change strength at another frame to animate."; color: mutedColor; wrapMode: Text.WordWrap; font.pixelSize: 10; Layout.fillWidth: true; visible: inspectorTab === 2 }
                            Repeater {
                                model: inspectorTab === 2 ? editor.trackEffects.filter(function(effect) { return effect.track === selectedEffectTrack }) : []
                                delegate: ColumnLayout {
                                    id: effectInspectorItem
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.topMargin: 12
                                    spacing: 7
                                    readonly property var effectData: modelData
                                    readonly property var definition: editor.effects.find(function(effect) { return effect.id === effectData.id })
                                    readonly property bool hasParameters: !!definition && definition.parameters.length > 0
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { text: effectInspectorItem.definition?.name || effectInspectorItem.effectData.id; color: textColor; font.pixelSize: 12; font.bold: true; Layout.fillWidth: true }
                                        AppButton { text: "✕"; subtle: true; onClicked: editor.removeTrackEffect(effectInspectorItem.effectData.track, effectInspectorItem.effectData.id) }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        visible: !effectInspectorItem.hasParameters
                                         AppSlider { Layout.fillWidth: true; from: 0; to: 100; value: editor.trackEffectStrengthAt(effectInspectorItem.effectData.track, effectInspectorItem.effectData.id, editor.playheadFrame); onPressedChanged: if (!pressed) editor.setTrackEffectStrength(effectInspectorItem.effectData.track, effectInspectorItem.effectData.id, Math.round(value)) }
                                         CaptionField { Layout.preferredWidth: 65; text: String(editor.trackEffectStrengthAt(effectInspectorItem.effectData.track, effectInspectorItem.effectData.id, editor.playheadFrame)); unit: "%"; validator: IntValidator { bottom: 0; top: 100 } onEditingFinished: if (acceptableInput) editor.setTrackEffectStrength(effectInspectorItem.effectData.track, effectInspectorItem.effectData.id, Number(text)) }
                                         KeyframeControl { animated: effectInspectorItem.effectData.keyframes.length > 0; atKeyframe: effectInspectorItem.effectData.keyframes.some(function(point) { return Number(point.frame) === editor.playheadFrame }); ToolTip.visible: hovered; ToolTip.text: "Set effect keyframe at cursor"; onClicked: editor.setTrackEffectKeyframe(effectInspectorItem.effectData.track, effectInspectorItem.effectData.id, editor.playheadFrame, editor.trackEffectStrengthAt(effectInspectorItem.effectData.track, effectInspectorItem.effectData.id, editor.playheadFrame)) }
                                    }
                                    Label { visible: !effectInspectorItem.hasParameters; text: effectInspectorItem.effectData.keyframes.length ? effectInspectorItem.effectData.keyframes.length + " keyframes · linear tween" : "No keyframes"; color: mutedColor; font.pixelSize: 10 }
                                    Repeater {
                                        model: effectInspectorItem.hasParameters ? effectInspectorItem.definition.parameters : []
                                        delegate: ColumnLayout {
                                            required property var modelData
                                            readonly property var parameter: modelData
                                            readonly property var parameterState: effectInspectorItem.effectData.parameters.find(function(item) { return item.id === parameter.id })
                                            Layout.fillWidth: true
                                            spacing: 2
                                            RowLayout {
                                                Layout.fillWidth: true
                                                Label { text: parameter.name; color: mutedColor; font.pixelSize: 11; Layout.fillWidth: true }
                                                Label { text: parameterState && parameterState.keyframes.length ? parameterState.keyframes.length + " ◆" : ""; color: "#d9adff"; font.pixelSize: 10 }
                                                AppButton {
                                                    text: "↺"; subtle: true
                                                    visible: !!parameterState && (parameterState.value !== parameter.initial || parameterState.keyframes.length > 0)
                                                    ToolTip.visible: hovered; ToolTip.text: "Reset " + parameter.name + " and remove its keyframes"
                                                    onClicked: editor.resetTrackEffectParameter(effectInspectorItem.effectData.track,
                                                        effectInspectorItem.effectData.id, parameter.id)
                                                }
                                            }
                                            RowLayout {
                                                Layout.fillWidth: true
                                                 AppSlider {
                                                    Layout.fillWidth: true
                                                    from: parameter.minimum; to: parameter.maximum; stepSize: 1
                                                    value: editor.trackEffectParameterAt(effectInspectorItem.effectData.track,
                                                        effectInspectorItem.effectData.id, parameter.id, editor.playheadFrame)
                                                    onPressedChanged: if (!pressed) editor.setTrackEffectParameter(
                                                        effectInspectorItem.effectData.track, effectInspectorItem.effectData.id,
                                                        parameter.id, Math.round(value))
                                                }
                                                 CaptionField { Layout.preferredWidth: 66
                                                     text: String(editor.trackEffectParameterAt(effectInspectorItem.effectData.track,
                                                         effectInspectorItem.effectData.id, parameter.id, editor.playheadFrame))
                                                     validator: IntValidator { bottom: parameter.minimum; top: parameter.maximum }
                                                     onEditingFinished: if (acceptableInput) editor.setTrackEffectParameter(
                                                         effectInspectorItem.effectData.track, effectInspectorItem.effectData.id,
                                                         parameter.id, Number(text))
                                                 }
                                                 KeyframeControl {
                                                     animated: !!parameterState && parameterState.keyframes.length > 0
                                                     atKeyframe: parameterState && parameterState.keyframes.some(function(point) { return Number(point.frame) === editor.playheadFrame })
                                                     ToolTip.visible: hovered; ToolTip.text: "Set " + parameter.name + " keyframe at cursor"
                                                     onClicked: editor.setTrackEffectParameterKeyframe(effectInspectorItem.effectData.track,
                                                         effectInspectorItem.effectData.id, parameter.id, editor.playheadFrame,
                                                         editor.trackEffectParameterAt(effectInspectorItem.effectData.track,
                                                             effectInspectorItem.effectData.id, parameter.id, editor.playheadFrame))
                                                 }
                                            }
                                        }
                                    }
                                }
                            }
                            Label { text: "Select an effect from the Effects library"; color: mutedColor; font.pixelSize: 11; visible: inspectorTab === 2 && editor.trackEffects.filter(function(effect) { return effect.track === selectedEffectTrack }).length === 0 }
                            AppButton { text: "Remove clip effect: " + (activeClip ? activeClip.effectId : ""); visible: inspectorTab === 2 && !!activeClip && activeClip.effectId !== ""; Layout.fillWidth: true; onClicked: editor.setClipEffect(activeClip.id, "", 100) }
                            RowLayout { Layout.fillWidth: true; Layout.topMargin: 8; visible: (inspectorTab === 0 || inspectorTab === 8) && !!selectedAsset && selectedAsset.hasVideo && !selectedAsset.isStillImage
                                Label { text: selectedAsset && selectedAsset.proxyReady ? "Proxy ready" : editor.proxyRunning ? "Proxy " + Math.round(editor.proxyProgress * 100) + "%" : "No proxy"; color: mutedColor; font.pixelSize: 10 }
                                Item { Layout.fillWidth: true }
                                AppButton { text: editor.proxyRunning ? "Cancel" : selectedAsset && selectedAsset.proxyReady ? "Ready" : "Create proxy"; enabled: editor.proxyRunning || (!!selectedAsset && !selectedAsset.proxyReady); onClicked: editor.proxyRunning ? editor.cancelProxy() : editor.startProxyForSelected() }
                            }
                        }
                    }
                    ColumnLayout {
                        id: captionDisplayPage
                        visible: inspectorTab === 6 && !!activeCaptionRow && captionSubtab === 1
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.topMargin: 14
                        spacing: 5
                        RowLayout { Layout.fillWidth: true; spacing: 7
                            Label { text: "DISPLAY STYLE"; color: mutedColor; font.pixelSize: 10; font.bold: true }
                            Item { Layout.fillWidth: true }
                            Label { text: editor.captionDisplayStyles.length + " styles"; color: "#7895ae"; font.pixelSize: 10 }
                        }
                        Label { text: "Click a tile to use it and preview at the playhead."; color: mutedColor; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.topMargin: 4; Layout.bottomMargin: 8 }
                        RowLayout { Layout.fillWidth: true; spacing: 6
                            CaptionField { Layout.fillWidth: true; placeholderText: "Find a style"; text: captionStyleSearch
                                onTextChanged: captionStyleSearch = text }
                            CaptionSelect { Layout.preferredWidth: 105
                                model: ["All", "Basic", "Word", "Phrase", "Reveal", "Highlight", "Frame", "Motion"]
                                currentIndex: model.indexOf(captionStyleCategory)
                                onActivated: captionStyleCategory = currentText }
                        }
                        GridView {
                            id: captionStyleGrid
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.topMargin: 8
                            clip: true
                            cellWidth: width / 2
                            cellHeight: 104
                            reuseItems: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: editor.captionDisplayStyles.filter(function(style) {
                                return (captionStyleCategory === "All" || style.category === captionStyleCategory)
                                    && (captionStyleSearch.trim() === "" || (style.name + " " + style.category
                                        + " " + style.description).toLowerCase().includes(captionStyleSearch.trim().toLowerCase()))
                            })
                            delegate: Rectangle {
                                required property var modelData
                                width: captionStyleGrid.cellWidth - 6
                                height: 98
                                radius: 7
                                color: activeCaptionRow && activeCaptionRow.displayMode === modelData.id ? "#233c59" : controlColor
                                border.width: activeCaptionRow && activeCaptionRow.displayMode === modelData.id ? 2 : 1
                                border.color: activeCaptionRow && activeCaptionRow.displayMode === modelData.id
                                    ? accentColor : "#34495c"
                                Column {
                                    anchors.fill: parent; anchors.margins: 7; spacing: 5
                                    Rectangle {
                                        width: parent.width; height: 51; radius: 4
                                        color: modelData.treatment === "clear" ? "#273b4f"
                                            : modelData.treatment === "card" ? "#05090e" : "#101923"
                                        Text {
                                            anchors.centerIn: parent; width: parent.width - 6
                                            text: modelData.id === "word" || modelData.id === "word_accent"
                                                || modelData.id === "fade_word" || modelData.id === "typewriter_word" ? "MAKE"
                                                : modelData.id.startsWith("phrase") || modelData.id.startsWith("word_") ? "MAKE IT"
                                                : modelData.id.startsWith("rolling") ? "MAKE IT HAPPEN"
                                                : modelData.id.startsWith("build") || modelData.id === "typewriter" ? "MAKE IT"
                                                : "MAKE IT HAPPEN"
                                            color: modelData.treatment === "accent" ? "#ffd34d" : "#f4f7fb"
                                            font.pixelSize: 12; font.bold: true
                                            horizontalAlignment: Text.AlignHCenter
                                            elide: Text.ElideRight
                                        }
                                        Rectangle {
                                            visible: modelData.treatment === "block"
                                            width: 24; height: 3; radius: 2; color: "#ffd34d"
                                            anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 5
                                        }
                                    }
                                    Text { text: modelData.name; color: textColor; font.pixelSize: 11; font.bold: true
                                        width: parent.width; elide: Text.ElideRight }
                                }
                                MouseArea { anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        const rowId = activeCaptionRow.id
                                        editor.setCaptionMode(rowId, modelData.id)
                                        editor.previewCaptionStyle(rowId, modelData.id)
                                    }
                                    ToolTip.visible: containsMouse
                                    ToolTip.text: modelData.description
                                }
                            }
                        }
                    }
                }
                Rectangle {
                    id: collectionInspector
                    anchors.fill: parent
                    visible: libraryMode === 1 && collectionInspectorActive
                    z: 20
                    color: panelColor
                    property bool loadingFields: false
                    property bool dirtyFields: false
                    property string editingCollectionId: ""
                    property string editingSegmentId: ""
                    function markChanged() {
                        if (loadingFields || !collectionInspectorActive || libraryMode !== 1) return
                        dirtyFields = true
                        collectionSaveTimer.restart()
                    }
                    function commitDetails() {
                        if (!dirtyFields) return
                        collectionSaveTimer.stop()
                        dirtyFields = false
                        if (editingSegmentId !== "") {
                            if (segmentDetailLabel.text.trim() === "") {
                                const current = editor.segments.find(function(s) { return s.id === editingSegmentId })
                                loadingFields = true
                                segmentDetailLabel.text = current ? current.label : ""
                                loadingFields = false
                            }
                            editor.updateSegmentDetails(editingSegmentId, segmentDetailLabel.text,
                                segmentDetailTags.text, segmentDetailNotes.text,
                                segmentDetailRating.value, segmentDetailCaption.text)
                        } else if (editingCollectionId !== "") {
                            if (collectionDetailName.text.trim() === "") {
                                const current = editor.collections.find(function(c) { return c.id === editingCollectionId })
                                loadingFields = true
                                collectionDetailName.text = current ? current.name : ""
                                loadingFields = false
                            }
                            editor.renameCollection(editingCollectionId, collectionDetailName.text)
                        }
                    }
                    function refreshDetails() {
                        commitDetails()
                        loadingFields = true
                        const segment = selectedSegmentIds.length === 1
                            ? editor.segments.find(function(s) { return s.id === selectedSegmentIds[0] }) : null
                        const collection = editor.collections.find(function(c) {
                            return c.id === (openedCollectionId !== "" ? openedCollectionId : selectedCollectionTileId)
                        })
                        editingSegmentId = segment ? segment.id : ""
                        editingCollectionId = !segment && selectedSegmentIds.length === 0 && collection ? collection.id : ""
                        segmentDetailLabel.text = segment ? segment.label : ""
                        segmentDetailTags.text = segment ? segment.tags : ""
                        segmentDetailNotes.text = segment ? segment.notes : ""
                        segmentDetailCaption.text = segment ? segment.caption : ""
                        segmentDetailRating.value = segment ? segment.rating : 0
                        collectionDetailName.text = collection ? collection.name : ""
                        dirtyFields = false
                        loadingFields = false
                    }
                    Component.onCompleted: refreshDetails()
                    Timer { id: collectionSaveTimer; interval: 900; repeat: false; onTriggered: collectionInspector.commitDetails() }
                    Connections {
                        target: window
                        function onSelectedSegmentIdsChanged() { collectionInspector.refreshDetails() }
                        function onSelectedCollectionTileIdChanged() { collectionInspector.refreshDetails() }
                        function onOpenedCollectionIdChanged() { collectionInspector.refreshDetails() }
                    }
                    ColumnLayout {
                        anchors.fill: parent; anchors.leftMargin: 15; anchors.rightMargin: 15
                        spacing: 0
                        Label { Layout.fillWidth: true; Layout.preferredHeight: 44; verticalAlignment: Text.AlignVCenter
                            text: selectedSegmentIds.length > 1 ? selectedSegmentIds.length + " segments selected"
                                : inspectedSegment ? "Segment settings" : inspectedCollection ? "Collection settings" : "Collections"
                            color: textColor; font.pixelSize: 13; font.bold: true
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: borderColor }
                        ScrollView {
                            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                            ColumnLayout {
                                width: collectionInspector.width - 30
                                spacing: 8
                                Label { visible: !!inspectedCollection && selectedSegmentIds.length === 0; text: "Folder name"; color: mutedColor; font.pixelSize: 11; Layout.topMargin: 17 }
                                CaptionField { id: collectionDetailName; visible: !!inspectedCollection && selectedSegmentIds.length === 0; Layout.fillWidth: true; color: textColor; font.pixelSize: 12
                                    onTextChanged: collectionInspector.markChanged()
                                    onEditingFinished: collectionInspector.commitDetails()
                                    onActiveFocusChanged: if (!activeFocus) collectionInspector.commitDetails()
                                }
                                Label { visible: !!inspectedCollection && selectedSegmentIds.length === 0; text: inspectedCollection ? inspectedCollection.count + " saved segments" : ""; color: mutedColor; font.pixelSize: 11 }
                                AppButton { visible: !!inspectedCollection && selectedSegmentIds.length === 0; text: "Delete folder…"; subtle: true; Layout.fillWidth: true; onClicked: { selectedCollectionTileId = inspectedCollection.id; deleteCollectionDialog.open() } }
                                Label { visible: !!inspectedSegment; text: "Label"; color: mutedColor; font.pixelSize: 11; Layout.topMargin: 17 }
                                CaptionField { id: segmentDetailLabel; visible: !!inspectedSegment; Layout.fillWidth: true; color: textColor; font.pixelSize: 12
                                    onTextChanged: collectionInspector.markChanged()
                                    onEditingFinished: collectionInspector.commitDetails()
                                    onActiveFocusChanged: if (!activeFocus) collectionInspector.commitDetails()
                                }
                                Label { visible: !!inspectedSegment; text: "Source"; color: mutedColor; font.pixelSize: 11 }
                                Label { visible: !!inspectedSegment; text: inspectedSegment ? inspectedSegment.sourceName + " · "
                                    + (inspectedSegment.timelineFrames > 0 ? inspectedSegment.timelineFrames + " project frames" : inspectedSegment.frameCount + (inspectedSegment.hasVideo ? " source frames" : " samples")) : ""; color: textColor; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                                Label { visible: !!inspectedSegment; text: "Tags"; color: mutedColor; font.pixelSize: 11 }
                                CaptionField { id: segmentDetailTags; visible: !!inspectedSegment; Layout.fillWidth: true; color: textColor; font.pixelSize: 12; placeholderText: "Comma separated"
                                    onTextChanged: collectionInspector.markChanged()
                                    onEditingFinished: collectionInspector.commitDetails()
                                    onActiveFocusChanged: if (!activeFocus) collectionInspector.commitDetails()
                                }
                                Label { visible: !!inspectedSegment; text: "Notes"; color: mutedColor; font.pixelSize: 11 }
                                AppTextArea { id: segmentDetailNotes; visible: !!inspectedSegment; Layout.fillWidth: true; implicitHeight: 66; color: textColor; wrapMode: TextArea.Wrap
                                    onTextChanged: collectionInspector.markChanged()
                                    onActiveFocusChanged: if (!activeFocus) collectionInspector.commitDetails()
                                }
                                Label { visible: !!inspectedSegment; text: "Caption"; color: mutedColor; font.pixelSize: 11 }
                                AppTextArea { id: segmentDetailCaption; visible: !!inspectedSegment; Layout.fillWidth: true; implicitHeight: 84; color: textColor; wrapMode: TextArea.Wrap; placeholderText: "Exported beside media as a .txt file"
                                    onTextChanged: collectionInspector.markChanged()
                                    onActiveFocusChanged: if (!activeFocus) collectionInspector.commitDetails()
                                }
                                RowLayout { visible: !!inspectedSegment; Layout.fillWidth: true
                                    Label { text: "Rating"; color: mutedColor; font.pixelSize: 11 }
                                    Item { Layout.fillWidth: true }
                                    AppSlider { id: segmentDetailRating; from: 0; to: 5; stepSize: 1; snapMode: Slider.SnapAlways; value: 0; Layout.preferredWidth: 110; onValueChanged: collectionInspector.markChanged() }
                                    Label { text: String(Math.round(segmentDetailRating.value)); color: textColor; font.pixelSize: 11; Layout.preferredWidth: 12 }
                                }
                                AppButton { visible: !!inspectedSegment; text: "Add to timeline"; subtle: true; Layout.fillWidth: true
                                    onClicked: if (inspectedSegment) editor.addSegmentToTimeline(inspectedSegment.id)
                                }
                                Rectangle { visible: selectedSegmentIds.length > 0; Layout.fillWidth: true; height: 1; color: borderColor; Layout.topMargin: 15 }
                                Label { visible: selectedSegmentIds.length > 0; text: "Move selected to"; color: mutedColor; font.pixelSize: 11; Layout.topMargin: 8 }
                                CaptionSelect { id: destinationCollection; visible: selectedSegmentIds.length > 0; Layout.fillWidth: true; textRole: "name"; valueRole: "id"; model: editor.collections }
                                AppButton { visible: selectedSegmentIds.length > 0; text: "Move " + selectedSegmentIds.length + " selected"; Layout.fillWidth: true
                                    onClicked: {
                                        const target = destinationCollection.currentValue
                                        editor.moveSegments(selectedSegmentIds, target)
                                        if (target !== openedCollectionId) selectedSegmentIds = []
                                    }
                                }
                                Label { visible: selectedSegmentIds.length > 1; text: "Use Ctrl or Shift to select tiles. Cut, copy, and paste also work between folders."; color: mutedColor; wrapMode: Text.WordWrap; font.pixelSize: 11; Layout.fillWidth: true; Layout.topMargin: 10 }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            SplitView.preferredHeight: 390
            SplitView.minimumHeight: 260
            SplitView.maximumHeight: 650
            color: panelColor
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 46
                    color: panelColor; border.color: borderColor
                    RowLayout { anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: window.width < 1360 ? 2 : 5
                        IconButton { symbol: "↖"; selected: true; ToolTip.visible: hovered; ToolTip.text: "Selection tool" }
                        Rectangle { width: 1; height: 22; color: borderColor; Layout.leftMargin: 3; Layout.rightMargin: 3 }
                        IconButton { symbol: "["; enabled: editor.selectedClipId !== ""; ToolTip.visible: hovered; ToolTip.text: "Trim start to playhead · ["; onClicked: editor.trimClipStart(editor.selectedClipId, editor.playheadFrame) }
                        IconButton { symbol: "✂"; enabled: editor.selectedClipId !== ""; ToolTip.visible: hovered; ToolTip.text: "Split at playhead · Ctrl+K"; onClicked: editor.splitSelectedClip() }
                        IconButton { symbol: "]"; enabled: editor.selectedClipId !== ""; ToolTip.visible: hovered; ToolTip.text: "Trim end to playhead · ]"; onClicked: editor.trimClipEnd(editor.selectedClipId, editor.playheadFrame + 1) }
                        IconButton { symbol: "✕"; enabled: editor.selectedClipId !== "" || editor.selectedTitleId !== "" || editor.selectedCaptionRowId !== ""; ToolTip.visible: hovered; ToolTip.text: "Delete selection · Delete"; onClicked: editor.selectedCaptionRowId !== "" ? editor.removeSelectedCaption() : editor.selectedTitleId !== "" ? editor.removeSelectedTitle() : editor.removeSelectedClip() }
                        IconButton { symbol: "⇤"; enabled: editor.selectedClipId !== ""; ToolTip.visible: hovered; ToolTip.text: "Ripple delete clip"; onClicked: editor.rippleDeleteSelectedClip() }
                        Rectangle { width: 1; height: 22; color: borderColor; Layout.leftMargin: 3; Layout.rightMargin: 3 }
                        Item {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: rangeToolbarControls.implicitWidth
                            Layout.preferredHeight: 30
                            Label { x: 3; y: -7; z: 1; text: "RANGE"; color: mutedColor; font.pixelSize: 9; font.bold: true }
                            RowLayout {
                                id: rangeToolbarControls
                                anchors.centerIn: parent
                                spacing: 2
                                IconButton { symbol: "I"; ToolTip.visible: hovered; ToolTip.text: "Mark range in at playhead"; onClicked: editor.markTimelineIn() }
                                IconButton { symbol: "O"; ToolTip.visible: hovered; ToolTip.text: "Mark range out at playhead"; onClicked: editor.markTimelineOut() }
                                IconButton { symbol: "⊘"; enabled: editor.hasTimelineRange; ToolTip.visible: hovered; ToolTip.text: "Clear range"; onClicked: editor.clearTimelineRange() }
                                IconButton { symbol: "▶"; enabled: editor.hasTimelineRange; selected: editor.rangePreviewPlaying; ToolTip.visible: hovered; ToolTip.text: "Play marked range from its start; click again to replay"; onClicked: editor.playTimelineRange() }
                                IconButton { symbol: "💾"; enabled: editor.hasTimelineRange; ToolTip.visible: hovered; ToolTip.text: "Save range from " + trackName(selectedTrack); onClicked: {
                                    const savedCollectionId = editor.saveTimelineRangeToCollection(activeCollectionId, selectedTrack)
                                    if (savedCollectionId !== "") {
                                        openCollection(savedCollectionId)
                                        const items = editor.segments.filter(function(s) { return s.collectionId === savedCollectionId })
                                        if (items.length) selectedSegmentIds = [items[items.length - 1].id]
                                        libraryMode = 1
                                    }
                                } }
                                CaptionField {
                                    id: timelineRangeSizeInput
                                     Layout.preferredWidth: window.width < 1360 ? 48 : 56
                                    implicitHeight: 28
                                    color: textColor
                                    font.pixelSize: 12
                                    horizontalAlignment: TextInput.AlignHCenter
                                    text: String(editor.timelineRangeLength)
                                    validator: IntValidator { bottom: -1; top: 2147483647 }
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Range size in project frames; -1 means no range"
                                    function commitSize() {
                                        if (acceptableInput) editor.setTimelineRangeLength(Number(text))
                                        text = String(editor.timelineRangeLength)
                                    }
                                    onEditingFinished: commitSize()
                                    Connections {
                                        target: editor
                                        function onTimelineRangeChanged() {
                                            if (!timelineRangeSizeInput.activeFocus)
                                                timelineRangeSizeInput.text = String(editor.timelineRangeLength)
                                        }
                                    }
                                }
                                IconButton { symbol: "−"; ToolTip.visible: hovered; ToolTip.text: "Reduce range by one increment"; onClicked: editor.adjustTimelineRangeLength(-1) }
                                IconButton { symbol: "+"; ToolTip.visible: hovered; ToolTip.text: "Increase range by one increment"; onClicked: editor.adjustTimelineRangeLength(1) }
                                CaptionSelect {
                                    id: rangeIncrementCombo
                                     Layout.preferredWidth: window.width < 1360 ? 80 : 92
                                    implicitHeight: 28
                                    model: editor.rangeIncrementOptions.concat(["+ Custom…"])
                                    currentIndex: model.indexOf(editor.rangeIncrementRule)
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Frame increment rule"
                                    onActivated: {
                                        if (index === model.length - 1) customRangeIncrementDialog.open()
                                        else editor.setRangeIncrementRule(currentText)
                                    }
                                }
                                IconButton {
                                    symbol: "×"
                                    implicitWidth: 22
                                    visible: editor.customRangeIncrements.indexOf(editor.rangeIncrementRule) >= 0
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Delete this custom increment"
                                    onClicked: editor.removeCustomRangeIncrement(editor.rangeIncrementRule)
                                }
                            }
                        }
                        Rectangle { width: 1; height: 22; color: borderColor; Layout.leftMargin: 3; Layout.rightMargin: 3 }
                        IconButton { symbol: "♫"; enabled: !!activeClip && activeClip.hasVideo && activeClip.hasAudio; ToolTip.visible: hovered; ToolTip.text: "Extract audio to A1"; onClicked: editor.extractSelectedAudio() }
                        IconButton { symbol: "▣"; enabled: editor.selectedClipId !== ""; ToolTip.visible: hovered; ToolTip.text: "Copy selected clip"; onClicked: editor.copySelectedClip() }
                        IconButton { symbol: "⇥"; ToolTip.visible: hovered; ToolTip.text: "Paste clip at playhead"; onClicked: editor.pasteClip() }
                        Rectangle { width: 1; height: 22; color: borderColor; Layout.leftMargin: 3; Layout.rightMargin: 3 }
                        IconButton { symbol: "◇"; selected: snappingEnabled; ToolTip.visible: hovered; ToolTip.text: "Toggle snapping"; onClicked: snappingEnabled = !snappingEnabled }
                        Rectangle { width: 1; height: 22; color: borderColor; Layout.leftMargin: 3; Layout.rightMargin: 3 }
                        IconButton { symbol: "+V"; ToolTip.visible: hovered; ToolTip.text: "Add video row above existing video rows (maximum 30)"; onClicked: { const row = editor.addTrack("video"); if (row >= 0) { selectedTrack = row; trackSelectionExplicit = true; selectedEffectTrack = row } } }
                        IconButton { symbol: "+A"; ToolTip.visible: hovered; ToolTip.text: "Add audio row below existing audio rows (maximum 30)"; onClicked: { const row = editor.addTrack("audio"); if (row >= 0) { selectedTrack = row; trackSelectionExplicit = true } } }
                        Item { Layout.fillWidth: true }
                        IconButton { symbol: "−"; onClicked: setTimelineZoom(pixelsPerFrame / 1.25) }
                         AppSlider { from: 0; to: 1; value: Math.log(pixelsPerFrame / 0.02) / Math.log(1200); Layout.preferredWidth: window.width < 1360 ? 72 : 105; onMoved: setTimelineZoom(0.02 * Math.pow(1200, value)) }
                        IconButton { symbol: "+"; onClicked: setTimelineZoom(pixelsPerFrame * 1.25) }
                        AppButton { text: "Fit"; subtle: true; ToolTip.visible: hovered; ToolTip.text: "Fit project in timeline"; onClicked: { setTimelineZoom((timeline.width - 60) / Math.max(1, timelineClips.maxEnd)); timeline.contentX = 0 } }
                        CaptionSelect { Layout.preferredWidth: 104; implicitHeight: 29; model: ["Compact", "Thumbnail", "Filmstrip", "Frame"]; currentIndex: model.indexOf(timelineVisualMode); onActivated: { timelineVisualMode = currentText; if (currentText === "Frame" && pixelsPerFrame < 8) pixelsPerFrame = 8 } }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                    Rectangle {
                        id: trackHeaderPanel
                        Layout.preferredWidth: 130; Layout.fillHeight: true
                        color: panelColor
                        clip: true
                        Timer {
                            interval: 45; repeat: true
                            running: draggedTrackId >= 0 && draggedTrackPointerY >= 0
                                && (draggedTrackPointerY < 70 || draggedTrackPointerY > trackHeaderPanel.height - 38)
                            onTriggered: {
                                const step = draggedTrackPointerY < 70 ? -14 : 14
                                timeline.contentY = Math.max(0, Math.min(timeline.contentHeight - timeline.height,
                                    timeline.contentY + step))
                                updateDraggedTrackTarget(draggedTrackPointerY)
                            }
                        }
                        Column {
                            id: trackHeaderColumn
                            y: 36 - timeline.contentY
                            width: parent.width
                            Rectangle { width: parent.width; height: newTrackDropHeight; color: "#19293c"; Label { anchors.centerIn: parent; text: "+  Video row"; color: "#82b6e8"; font.pixelSize: 10 } }
                            Repeater {
                                model: editor.tracks
                                delegate: Rectangle {
                                    required property var modelData
                                    required property int index
                                    width: 130; height: trackRowHeight
                                     color: draggedTrackId === modelData.id ? "#28445e"
                                         : draggedTrackTargetRow === index ? "#1d3850"
                                         : selectedTrack === modelData.id ? accentSoft : panelColor
                                     border.color: draggedTrackTargetRow === index ? accentColor : borderColor
                                    Behavior on color { ColorAnimation { duration: 110 } }
                                    MouseArea {
                                        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                                        width: modelData.kind === "caption" ? 98 : 75
                                        hoverEnabled: true
                                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                        ToolTip.visible: containsMouse && !pressed
                                        ToolTip.delay: 500
                                        ToolTip.text: "Drag to reorder this row"
                                        property real pressY: 0
                                        property bool moving: false
                                        onPressed: (mouse) => {
                                            const point = mapToItem(trackHeaderColumn, mouse.x, mouse.y)
                                            pressY = point.y
                                            moving = false
                                            timeline.forceActiveFocus()
                                        }
                                        onPositionChanged: (mouse) => {
                                            if (!pressed) return
                                            const point = mapToItem(trackHeaderColumn, mouse.x, mouse.y)
                                            if (!moving && Math.abs(point.y - pressY) < 6) return
                                            moving = true
                                            draggedTrackId = modelData.id
                                            const viewport = mapToItem(trackHeaderPanel, mouse.x, mouse.y)
                                            updateDraggedTrackTarget(viewport.y)
                                        }
                                        onReleased: {
                                            const destination = draggedTrackTargetRow
                                            const wasMoving = moving
                                            draggedTrackId = -1
                                            draggedTrackTargetRow = -1
                                            draggedTrackPointerY = -1
                                            moving = false
                                            if (wasMoving) editor.moveTrackRow(modelData.id, destination)
                                            else {
                                                selectedTrack = modelData.id
                                                trackSelectionExplicit = true
                                                if (modelData.kind === "video") selectedEffectTrack = modelData.id
                                                editor.selectTrackRow(modelData.id)
                                            }
                                        }
                                        onCanceled: { draggedTrackId = -1; draggedTrackTargetRow = -1; draggedTrackPointerY = -1; moving = false }
                                    }
                                    RowLayout {
                                        anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 5; spacing: 2
                                        Label { text: modelData.kind === "caption" ? "CC" : modelData.name; color: modelData.kind === "caption" ? "#e2b9ff" : modelData.kind === "video" ? "#8bc9ff" : "#c3d2e2"; font.pixelSize: 11; font.bold: true }
                                        Item { Layout.fillWidth: true }
                                        IconButton {
                                            visible: modelData.kind === "video" || modelData.kind === "caption"
                                            symbol: modelData.visible ? "👁" : "⊘"
                                            implicitWidth: 25; implicitHeight: 27
                                            selected: !modelData.visible
                                            ToolTip.visible: hovered
                                            ToolTip.text: modelData.visible ? "Hide video row" : "Show video row"
                                            onClicked: modelData.kind === "caption" ? editor.toggleCaptionVisibility(modelData.captionRowId) : editor.toggleTrackVisibility(modelData.id)
                                        }
                                        IconButton {
                                            visible: modelData.kind !== "caption"
                                            implicitWidth: 27; implicitHeight: 27
                                            selected: modelData.muted
                                            ToolTip.visible: hovered
                                            ToolTip.text: modelData.muted ? "Unmute row" : "Mute row"
                                            onClicked: editor.toggleTrackMute(modelData.id)
                                            contentItem: Item {
                                                VolumeIcon { anchors.centerIn: parent; muted: modelData.muted
                                                    iconColor: modelData.muted ? "#70b7ff" : "#dbe7f5" }
                                            }
                                        }
                                    }
                                }
                            }
                            Rectangle { width: parent.width; height: newTrackDropHeight; color: "#1b273b"; Label { anchors.centerIn: parent; text: "+  Audio row"; color: "#a2bddf"; font.pixelSize: 10 } }
                        }
                        Rectangle { x: 0; y: 0; width: parent.width; height: 36; color: panelColor; z: 2
                            Label { x: 12; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 18; text: editor.projectRate; color: "#8da4ba"; font.pixelSize: 10; elide: Text.ElideRight }
                        }
                    }
                    Flickable {
                        id: timeline
                        focus: true
                        WheelHandler {
                            target: null
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            acceptedModifiers: Qt.ControlModifier
                            onWheel: (event) => {
                                const steps = event.angleDelta.y !== 0 ? event.angleDelta.y / 120
                                    : event.pixelDelta.y / 100
                                if (steps !== 0) zoomTimelineAt(pixelsPerFrame * Math.pow(1.18, steps), event.x)
                                event.accepted = true
                            }
                        }
                        PinchHandler {
                            id: timelinePinch
                            target: null
                            acceptedDevices: PointerDevice.TouchPad | PointerDevice.TouchScreen
                            rotationAxis.enabled: false
                            xAxis.enabled: false
                            yAxis.enabled: false
                            scaleAxis.onActiveValueChanged: (delta) =>
                                zoomTimelineAt(pixelsPerFrame * delta,
                                    timelinePinch.centroid.position.x)
                        }
                        Keys.priority: Keys.BeforeItem
                        Keys.onPressed: (event) => {
                            if (event.modifiers !== Qt.NoModifier) return
                            if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                                editor.pauseTimelinePlayback()
                                seekProjectFrame(editor.playheadFrame + (event.key === Qt.Key_Right ? 1 : -1))
                                event.accepted = true
                            }
                        }
                        property int tickFrames: {
                            const second = editor.framesForSeconds(1)
                            const candidates = [1, 2, 5, 10, Math.max(1, Math.round(second / 2))]
                            for (const seconds of [1, 2, 5, 10, 20, 30, 60, 120, 300, 600])
                                candidates.push(editor.framesForSeconds(seconds))
                            candidates.sort(function(a, b) { return a - b })
                            for (const frames of candidates)
                                if (frames * pixelsPerFrame >= 112) return frames
                            return candidates[candidates.length - 1]
                        }
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true
                        contentWidth: Math.max(width, timelineClips.maxEnd * pixelsPerFrame + 120)
                        contentHeight: Math.max(height, trackRowsY + editor.tracks.length * trackRowHeight + newTrackDropHeight)
                        Rectangle { x: 0; y: 35; width: timeline.contentWidth; height: 1; color: borderColor }
                        Canvas {
                            id: timelineRuler
                            x: timeline.contentX; y: timeline.contentY; z: 40
                            width: timeline.width; height: 35
                            onPaint: {
                                const context = getContext("2d")
                                context.clearRect(0, 0, width, height)
                                const majorFrames = timeline.tickFrames
                                const first = Math.max(0, Math.floor(timeline.contentX / (majorFrames * pixelsPerFrame)) - 1)
                                const last = Math.ceil((timeline.contentX + width) / (majorFrames * pixelsPerFrame)) + 1
                                for (let index = first; index <= last; ++index) {
                                    const frame = index * majorFrames
                                    const x = frame * pixelsPerFrame - timeline.contentX + 0.5
                                     context.strokeStyle = "#70869c"
                                    context.beginPath()
                                    context.moveTo(x, 25)
                                    context.lineTo(x, 35)
                                    context.stroke()
                                     context.fillStyle = "#cbdbea"
                                    context.font = "10px Segoe UI"
                                    context.fillText(editor.projectFrameTimestamp(frame), x + 6, 14)
                                     context.fillStyle = "#8fa8bf"
                                    context.font = "9px Segoe UI"
                                    context.fillText("F" + frame, x + 6, 27)
                                    if (majorFrames >= 5 && majorFrames * pixelsPerFrame / 5 >= 9) {
                                         context.strokeStyle = "#40576b"
                                        for (let minor = 1; minor < 5; ++minor) {
                                            const minorFrame = frame + Math.round(majorFrames * minor / 5)
                                            const minorX = minorFrame * pixelsPerFrame - timeline.contentX + 0.5
                                            context.beginPath()
                                            context.moveTo(minorX, 30)
                                            context.lineTo(minorX, 35)
                                            context.stroke()
                                        }
                                    }
                                }
                            }
                            onWidthChanged: requestPaint()
                            Connections {
                                target: timeline
                                function onContentXChanged() { timelineRuler.requestPaint() }
                                function onTickFramesChanged() { timelineRuler.requestPaint() }
                            }
                            Connections {
                                target: window
                                function onPixelsPerFrameChanged() { timelineRuler.requestPaint() }
                            }
                            Connections {
                                target: editor
                                function onProjectChanged() { timelineRuler.requestPaint() }
                            }
                        }
                        MouseArea {
                            y: timeline.contentY; width: timeline.contentWidth; height: 35; z: 41
                            preventStealing: true
                            cursorShape: Qt.SizeHorCursor
                            property int rangeAnchor: -1
                            onPressed: (mouse) => {
                                timeline.forceActiveFocus()
                                if (mouse.modifiers & Qt.ShiftModifier) {
                                    rangeAnchor = Math.max(0, Math.round(mouse.x / pixelsPerFrame))
                                    editor.setTimelineRange(rangeAnchor, rangeAnchor + 1)
                                } else {
                                    rangeAnchor = -1
                                    editor.scrubTimelineFrame(Math.round(mouse.x / pixelsPerFrame), snappingEnabled, snapToleranceFrames)
                                }
                            }
                            onPositionChanged: (mouse) => {
                                if (!pressed) return
                                const frame = Math.max(0, Math.round(mouse.x / pixelsPerFrame))
                                if (rangeAnchor >= 0)
                                    editor.setTimelineRange(Math.min(rangeAnchor, frame), Math.max(rangeAnchor, frame) + 1)
                                else editor.scrubTimelineFrame(frame, snappingEnabled, snapToleranceFrames)
                            }
                            onReleased: rangeAnchor = -1
                        }
                        Rectangle {
                            id: rangeDragStrip
                            visible: editor.hasTimelineRange
                            readonly property real spanWidth: (Number(editor.timelineRangeEnd) - Number(editor.timelineRangeStart)) * pixelsPerFrame
                            x: Number(editor.timelineRangeStart) * pixelsPerFrame + (spanWidth - width) / 2
                            y: timeline.contentY + 22
                            width: Math.max(16, spanWidth)
                            height: 13; radius: 3; z: 42
                            color: rangeMouse.pressed ? "#70baff" : "#447eae"
                            border.color: "#9fd5ff"
                            Label { anchors.centerIn: parent; text: parent.width > 72
                                ? (Number(editor.timelineRangeEnd) - Number(editor.timelineRangeStart)) + "f · " + trackName(selectedTrack)
                                : "≡"; color: "#f3faff"; font.pixelSize: 9 }
                            MouseArea {
                                id: rangeMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                preventStealing: true
                                cursorShape: Qt.SizeHorCursor
                                ToolTip.visible: containsMouse && !pressed
                                ToolTip.text: "Drag the marked range · "
                                    + (Number(editor.timelineRangeEnd) - Number(editor.timelineRangeStart))
                                    + " project frames on " + trackName(selectedTrack)
                                property real pressContentX: 0
                                property int initialFirstFrame: 0
                                onPressed: (mouse) => {
                                    timeline.forceActiveFocus()
                                    pressContentX = mapToItem(timelineClips, mouse.x, mouse.y).x
                                    initialFirstFrame = Number(editor.timelineRangeStart)
                                }
                                onPositionChanged: (mouse) => {
                                    if (!pressed) return
                                    const viewport = timeline.mapFromItem(rangeMouse, mouse.x, mouse.y)
                                    if (viewport.x > timeline.width - 18)
                                        timeline.contentX = Math.min(timeline.contentWidth - timeline.width, timeline.contentX + 12)
                                    else if (viewport.x < 18)
                                        timeline.contentX = Math.max(0, timeline.contentX - 12)
                                    const pointerX = mapToItem(timelineClips, mouse.x, mouse.y).x
                                    editor.moveTimelineRange(initialFirstFrame
                                        + Math.round((pointerX - pressContentX) / pixelsPerFrame))
                                }
                            }
                        }
                        Column {
                            y: trackRowsY
                            Repeater {
                                model: editor.tracks
                                delegate: Rectangle {
                                    required property var modelData
                                    required property int index
                                    width: timeline.contentWidth; height: trackRowHeight
                                    color: index % 2 === 0 ? "#141e29" : "#111a24"
                                    border.color: borderColor
                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        preventStealing: true
                                        property int selectionAnchor: -1
                                        onPressed: (mouse) => {
                                            timeline.forceActiveFocus()
                                            if (mouse.button === Qt.RightButton) {
                                                trackContextMenu.targetTrack = modelData.id
                                                const p = mapToItem(window.contentItem, mouse.x, mouse.y)
                                                trackContextMenu.x = p.x
                                                trackContextMenu.y = p.y
                                                trackContextMenu.open()
                                                return
                                            }
                                            editor.clearSelection()
                                            const frame = Math.max(0, Math.round(mouse.x / pixelsPerFrame))
                                            if (mouse.modifiers & Qt.ShiftModifier) {
                                                selectionAnchor = frame
                                                editor.setTimelineRange(frame, frame + 1)
                                            } else {
                                                selectionAnchor = -1
                                                editor.scrubTimelineFrame(frame, snappingEnabled, snapToleranceFrames)
                                            }
                                            selectedTrack = modelData.id
                                            trackSelectionExplicit = true
                                            if (modelData.kind === "video") selectedEffectTrack = modelData.id
                                            editor.selectTrackRow(modelData.id)
                                        }
                                        onPositionChanged: (mouse) => {
                                            if (!(mouse.buttons & Qt.LeftButton)) return
                                            const frame = Math.max(0, Math.round(mouse.x / pixelsPerFrame))
                                            if (selectionAnchor >= 0)
                                                editor.setTimelineRange(Math.min(selectionAnchor, frame), Math.max(selectionAnchor, frame) + 1)
                                            else editor.scrubTimelineFrame(frame, snappingEnabled, snapToleranceFrames)
                                        }
                                        onReleased: selectionAnchor = -1
                                    }
                                }
                            }
                        }
                        Rectangle { y: 36; width: timeline.contentWidth; height: newTrackDropHeight; color: "#14283c"; opacity: draggedTimelineTarget.track === -1 ? 0.9 : 0.5
                            MouseArea { anchors.fill: parent; onClicked: { timeline.forceActiveFocus(); editor.clearSelection() } } }
                        Rectangle { y: trackRowsY + editor.tracks.length * trackRowHeight; width: timeline.contentWidth; height: newTrackDropHeight; color: "#17283a"; opacity: draggedTimelineTarget.track === -2 ? 0.9 : 0.5
                            MouseArea { anchors.fill: parent; onClicked: { timeline.forceActiveFocus(); editor.clearSelection() } } }
                        Rectangle {
                            visible: editor.hasTimelineRange
                            x: Number(editor.timelineRangeStart) * pixelsPerFrame
                            y: trackRowsY
                            width: Math.max(1, (Number(editor.timelineRangeEnd) - Number(editor.timelineRangeStart)) * pixelsPerFrame)
                            height: editor.tracks.length * trackRowHeight
                            color: "#308ccf"
                            opacity: 0.18
                        }
                        Rectangle {
                            visible: editor.hasTimelineRange && visibleTrackRow(selectedTrack) >= 0
                            x: Number(editor.timelineRangeStart) * pixelsPerFrame
                            y: trackRowsY + Math.max(0, visibleTrackRow(selectedTrack)) * trackRowHeight
                            width: Math.max(1, (Number(editor.timelineRangeEnd) - Number(editor.timelineRangeStart)) * pixelsPerFrame)
                            height: trackRowHeight
                            color: "#63b7ff"
                            opacity: 0.16
                        }
                        Item {
                            id: timelineClips
                            property real maxEnd: {
                                let end = 0
                                for (let clip of editor.clips) end = Math.max(end, clip.start + clip.duration)
                                for (let title of editor.titles) end = Math.max(end, title.start + title.duration)
                                for (let cue of editor.captionCues) end = Math.max(end, cue.start + cue.duration)
                                return end
                            }
                            y: trackRowsY
                            Repeater {
                                model: editor.captionCues.filter(function(cue) {
                                    const left = timeline.contentX / pixelsPerFrame - 100
                                    const right = (timeline.contentX + timeline.width) / pixelsPerFrame + 100
                                    return cue.start + cue.duration >= left && cue.start <= right
                                })
                                delegate: Rectangle {
                                    required property var modelData
                                    x: modelData.start * pixelsPerFrame
                                    y: visibleTrackRow(modelData.track) * trackRowHeight + 3
                                    z: 3
                                    width: Math.max(2, modelData.duration * pixelsPerFrame)
                                    height: 42; radius: 5; color: "#704b9c"
                                    border.width: editor.selectedCaptionId === modelData.id ? 2 : 1
                                    border.color: editor.selectedCaptionId === modelData.id ? "#a8d6ff" : "#a67dce"
                                    Row { anchors.fill: parent; anchors.leftMargin: 5; anchors.rightMargin: 5; spacing: 5
                                        Rectangle { width: 18; height: 18; radius: 3; anchors.verticalCenter: parent.verticalCenter; color: "#a675d5"
                                            Label { anchors.centerIn: parent; text: "T"; color: "white"; font.bold: true; font.pixelSize: 10 } }
                                        Label { width: Math.max(0, parent.width - 28); anchors.verticalCenter: parent.verticalCenter; text: modelData.text; color: "white"; elide: Text.ElideRight; font.pixelSize: 10 }
                                    }
                                    MouseArea { anchors.fill: parent; hoverEnabled: true
                                        ToolTip.visible: containsMouse; ToolTip.delay: 450
                                        ToolTip.text: modelData.text + " · " + modelData.start + "–" + (modelData.start + modelData.duration - 1) + "f"
                                        onClicked: (mouse) => { timeline.forceActiveFocus(); editor.selectCaption(modelData.id); seekProjectFrame(modelData.start + Math.max(0, Math.min(modelData.duration - 1, Math.floor(mouse.x / pixelsPerFrame)))) } }
                                }
                            }
                            Repeater {
                                model: editor.titles.filter(function(title) {
                                    const left = timeline.contentX / pixelsPerFrame - 100
                                    const right = (timeline.contentX + timeline.width) / pixelsPerFrame + 100
                                    return title.start + title.duration >= left && title.start <= right
                                })
                                delegate: Rectangle {
                                    id: titleItem
                                    required property var modelData
                                    readonly property bool selected: editor.selectedTitleId === modelData.id
                                    x: modelData.start * pixelsPerFrame; y: Math.max(0, visibleTrackRow(modelData.track)) * trackRowHeight
                                    width: Math.max(1, modelData.duration * pixelsPerFrame); height: 48
                                    radius: 4; color: "#6945aa"
                                    opacity: editor.tracks.some(function(row) { return row.id === modelData.track && !row.visible }) ? 0.45 : 1
                                    border.width: 1
                                    border.color: "#9b79dc"
                                    Label { anchors.fill: parent; anchors.leftMargin: 9; verticalAlignment: Text.AlignVCenter; text: "T   " + modelData.text; color: "white"; font.pixelSize: 11; elide: Text.ElideRight }
                                    MouseArea {
                                        id: titleMouse
                                        anchors.fill: parent
                                        preventStealing: true
                                        property real pressOffsetX: 0
                                        property real pressOffsetY: 0
                                        property real pressTimelineX: 0
                                        property real pressTimelineY: 0
                                        property bool draggingTitle: false
                                        onPressed: (mouse) => {
                                            timeline.forceActiveFocus()
                                            editor.selectTitle(modelData.id)
                                            seekProjectFrame(modelData.start + Math.max(0,
                                                Math.min(modelData.duration - 1, Math.floor(mouse.x / pixelsPerFrame))))
                                            pressOffsetX = mouse.x
                                            pressOffsetY = mouse.y
                                            const pointer = mapToItem(timelineClips, mouse.x, mouse.y)
                                            pressTimelineX = pointer.x
                                            pressTimelineY = pointer.y
                                            draggingTitle = false
                                        }
                                        onPositionChanged: (mouse) => {
                                            if (!(mouse.buttons & Qt.LeftButton)) return
                                            const pointer = mapToItem(timelineClips, mouse.x, mouse.y)
                                            if (!draggingTitle && Math.hypot(pointer.x - pressTimelineX,
                                                pointer.y - pressTimelineY) < 4) return
                                            draggingTitle = true
                                            autoScrollTimeline(timeline.mapFromItem(titleMouse, mouse.x, mouse.y).y)
                                            const track = rowForClipY(pointer.y)
                                            const raw = Math.max(0, Math.round((pointer.x - pressOffsetX) / pixelsPerFrame))
                                            const placement = editor.previewTitleSnap(modelData.id, raw,
                                                track, snappingEnabled, snapToleranceFrames,
                                                Math.max(0, Math.round(pointer.x / pixelsPerFrame)))
                                            snapGuideFrame = placement.guide
                                            parent.x = placement.start * pixelsPerFrame
                                            parent.y = Math.max(-newTrackDropHeight,
                                                Math.min(editor.tracks.length * trackRowHeight, pointer.y - pressOffsetY))
                                        }
                                        onReleased: (mouse) => {
                                            if (!draggingTitle) return
                                            const pointer = mapToItem(timelineClips, mouse.x, mouse.y)
                                            const track = rowForClipY(pointer.y)
                                            const raw = Math.max(0, Math.round((pointer.x - pressOffsetX) / pixelsPerFrame))
                                            const placement = editor.previewTitleSnap(modelData.id, raw,
                                                track, snappingEnabled, snapToleranceFrames,
                                                Math.max(0, Math.round(pointer.x / pixelsPerFrame)))
                                            parent.x = Qt.binding(function() { return modelData.start * pixelsPerFrame })
                                            parent.y = Qt.binding(function() { return Math.max(0, visibleTrackRow(modelData.track)) * trackRowHeight })
                                            snapGuideFrame = -1
                                            draggingTitle = false
                                            editor.moveTitle(modelData.id, placement.start, track,
                                                snappingEnabled, snapToleranceFrames,
                                                Math.max(0, Math.round(pointer.x / pixelsPerFrame)))
                                        }
                                        onCanceled: {
                                            parent.x = Qt.binding(function() { return modelData.start * pixelsPerFrame })
                                            parent.y = Qt.binding(function() { return Math.max(0, visibleTrackRow(modelData.track)) * trackRowHeight })
                                            snapGuideFrame = -1
                                            draggingTitle = false
                                        }
                                    }
                                    Rectangle {
                                        anchors.fill: parent; anchors.margins: 1; radius: 3
                                        color: "transparent"; border.width: 2; border.color: "#69b7ff"
                                        opacity: titleItem.selected ? 1 : 0
                                        Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                                    }
                                    Item {
                                        x: 0; y: 0; width: Math.min(12, titleItem.width / 3); height: parent.height; z: 4
                                        MouseArea {
                                            anchors.fill: parent; acceptedButtons: Qt.LeftButton
                                            hoverEnabled: true; preventStealing: true; cursorShape: Qt.SizeHorCursor
                                            property real pressTimelineX: 0
                                            property bool resizing: false
                                            ToolTip.visible: containsMouse
                                            ToolTip.text: "Drag text edge to change start"
                                            onPressed: (mouse) => {
                                                timeline.forceActiveFocus()
                                                editor.selectTitle(modelData.id)
                                                pressTimelineX = mapToItem(timelineClips, mouse.x, mouse.y).x
                                                resizing = false
                                                snapGuideFrame = -1
                                            }
                                            onPositionChanged: (mouse) => {
                                                if (!pressed) return
                                                const pointerX = mapToItem(timelineClips, mouse.x, mouse.y).x
                                                if (!resizing && Math.abs(pointerX - pressTimelineX) < 4) return
                                                resizing = true
                                                const originalEnd = modelData.start + modelData.duration
                                                const raw = Math.max(0, Math.min(originalEnd - 1,
                                                    modelData.start + Math.round((pointerX - pressTimelineX) / pixelsPerFrame)))
                                                const placement = snappingEnabled
                                                    ? editor.previewTitleTrimSnap(modelData.id, raw, true, snapToleranceFrames)
                                                    : { frame: raw, guide: -1 }
                                                snapGuideFrame = placement.guide
                                                titleItem.x = placement.frame * pixelsPerFrame
                                                titleItem.width = Math.max(1, (originalEnd - placement.frame) * pixelsPerFrame)
                                            }
                                            onReleased: (mouse) => {
                                                if (!resizing) return
                                                const pointerX = mapToItem(timelineClips, mouse.x, mouse.y).x
                                                const originalEnd = modelData.start + modelData.duration
                                                const raw = Math.max(0, Math.min(originalEnd - 1,
                                                    modelData.start + Math.round((pointerX - pressTimelineX) / pixelsPerFrame)))
                                                titleItem.x = Qt.binding(function() { return modelData.start * pixelsPerFrame })
                                                titleItem.width = Qt.binding(function() { return Math.max(1, modelData.duration * pixelsPerFrame) })
                                                snapGuideFrame = -1
                                                resizing = false
                                                editor.trimTitleStartSnapped(modelData.id, raw, snappingEnabled, snapToleranceFrames)
                                            }
                                            onCanceled: {
                                                titleItem.x = Qt.binding(function() { return modelData.start * pixelsPerFrame })
                                                titleItem.width = Qt.binding(function() { return Math.max(1, modelData.duration * pixelsPerFrame) })
                                                snapGuideFrame = -1
                                                resizing = false
                                            }
                                        }
                                    }
                                    Item {
                                        x: parent.width - width; y: 0; width: Math.min(12, titleItem.width / 3); height: parent.height; z: 4
                                        MouseArea {
                                            anchors.fill: parent; acceptedButtons: Qt.LeftButton
                                            hoverEnabled: true; preventStealing: true; cursorShape: Qt.SizeHorCursor
                                            property real pressTimelineX: 0
                                            property bool resizing: false
                                            ToolTip.visible: containsMouse
                                            ToolTip.text: "Drag text edge to change end"
                                            onPressed: (mouse) => {
                                                timeline.forceActiveFocus()
                                                editor.selectTitle(modelData.id)
                                                pressTimelineX = mapToItem(timelineClips, mouse.x, mouse.y).x
                                                resizing = false
                                                snapGuideFrame = -1
                                            }
                                            onPositionChanged: (mouse) => {
                                                if (!pressed) return
                                                const pointerX = mapToItem(timelineClips, mouse.x, mouse.y).x
                                                if (!resizing && Math.abs(pointerX - pressTimelineX) < 4) return
                                                resizing = true
                                                const raw = Math.max(modelData.start + 1,
                                                    modelData.start + modelData.duration
                                                    + Math.round((pointerX - pressTimelineX) / pixelsPerFrame))
                                                const placement = snappingEnabled
                                                    ? editor.previewTitleTrimSnap(modelData.id, raw, false, snapToleranceFrames)
                                                    : { frame: raw, guide: -1 }
                                                snapGuideFrame = placement.guide
                                                titleItem.width = Math.max(1, (placement.frame - modelData.start) * pixelsPerFrame)
                                            }
                                            onReleased: (mouse) => {
                                                if (!resizing) return
                                                const pointerX = mapToItem(timelineClips, mouse.x, mouse.y).x
                                                const raw = Math.max(modelData.start + 1,
                                                    modelData.start + modelData.duration
                                                    + Math.round((pointerX - pressTimelineX) / pixelsPerFrame))
                                                titleItem.width = Qt.binding(function() { return Math.max(1, modelData.duration * pixelsPerFrame) })
                                                snapGuideFrame = -1
                                                resizing = false
                                                editor.trimTitleEndSnapped(modelData.id, raw, snappingEnabled, snapToleranceFrames)
                                            }
                                            onCanceled: {
                                                titleItem.width = Qt.binding(function() { return Math.max(1, modelData.duration * pixelsPerFrame) })
                                                snapGuideFrame = -1
                                                resizing = false
                                            }
                                        }
                                    }
                                }
                            }
                            Repeater {
                                model: editor.clips.filter(function(clip) {
                                    const left = timeline.contentX / pixelsPerFrame - 100
                                    const right = (timeline.contentX + timeline.width) / pixelsPerFrame + 100
                                    return clip.start + clip.duration >= left && clip.start <= right
                                })
                                delegate: Rectangle {
                                    id: clipItem
                                    required property var modelData
                                    property var clipData: modelData
                                    readonly property bool selected: editor.selectedClipId === modelData.id
                                    property real waveformTrimOffsetX: 0
                                    readonly property rect waveformSourceRect: {
                                        const total = Math.max(1, modelData.assetFrames || 0)
                                        const first = Math.max(0, Math.min(total - 1, modelData.sourceFirst || 0))
                                        const end = Math.max(first + 1,
                                            Math.min(total, first + Math.max(1, modelData.sourceFrames || 0)))
                                        const left = Math.floor(first * 1600 / total)
                                        const right = Math.max(left + 1, Math.ceil(end * 1600 / total))
                                        return Qt.rect(left, 0, right - left, 80)
                                    }
                                    x: modelData.start * pixelsPerFrame
                                    y: visibleTrackRow(modelData.track) * trackRowHeight
                                    width: Math.max(1, modelData.duration * pixelsPerFrame)
                                    height: 48
                                    opacity: editor.tracks.some(function(row) {
                                        return row.id === modelData.track && (row.muted || !row.visible)
                                    }) ? 0.5 : 1
                                    radius: 4
                                    clip: true
                                    color: modelData.hasVideo ? "#315b82" : modelData.track === 1 ? "#146e68" : "#334f9f"
                                    border.width: 1
                                    border.color: "#557f9f"
                                    Row {
                                        anchors.fill: parent
                                        visible: timelineVisualMode === "Filmstrip" && clipItem.clipData.hasVideo && !!clipItem.clipData.filmstrip && clipItem.clipData.filmstrip.length > 0
                                        Repeater { model: parent.visible ? Math.min(30, Math.ceil(clipItem.width / 120)) : 0
                                            delegate: Image { required property int index; width: 120; height: clipItem.height; source: clipItem.clipData.filmstrip[index % clipItem.clipData.filmstrip.length]; fillMode: Image.PreserveAspectCrop; asynchronous: true; opacity: 0.82 }
                                        }
                                    }
                                    Image {
                                        x: modelData.hasVideo ? 0 : clipItem.waveformTrimOffsetX
                                        y: 0
                                        width: modelData.hasVideo ? clipItem.width : Math.max(1, modelData.duration * pixelsPerFrame)
                                        height: clipItem.height
                                        visible: (timelineVisualMode === "Thumbnail" || timelineVisualMode === "Filmstrip") && (!modelData.hasVideo || timelineVisualMode === "Thumbnail" || !modelData.filmstrip || modelData.filmstrip.length === 0)
                                        source: modelData.hasVideo ? (modelData.thumbnail || "") : (modelData.waveform || "")
                                        sourceSize: modelData.hasVideo ? undefined : Qt.size(1600, 80)
                                        sourceClipRect: modelData.hasVideo ? undefined : clipItem.waveformSourceRect
                                        fillMode: modelData.hasVideo ? Image.PreserveAspectCrop : Image.Stretch
                                        asynchronous: true
                                        opacity: modelData.hasVideo ? 0.7 : 0.9
                                    }
                                    Rectangle { anchors.fill: parent; color: modelData.hasVideo ? "#3a08131d" : "#2208131d" }
                                    Label { x: 9; y: 6; width: parent.width - 18; text: modelData.name + (modelData.voiceChangeEnabled ? " · Voice: " + modelData.voiceName : ""); color: "white"; font.pixelSize: 10; elide: Text.ElideRight }
                                    MouseArea {
                                        id: clipMouse
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        property real pressOffsetX: 0
                                        property real pressOffsetY: 0
                                        property real pressTimelineX: 0
                                        property real pressTimelineY: 0
                                        property bool draggingClip: false
                                        property int selectionAnchor: -1
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        preventStealing: true
                                        ToolTip.visible: containsMouse && !draggingClip
                                        ToolTip.delay: 500
                                        ToolTip.text: editor.timelineFrameDetails(modelData.id,
                                            modelData.start + Math.max(0, Math.floor(mouseX / pixelsPerFrame)))
                                        onPressed: (mouse) => {
                                            timeline.forceActiveFocus()
                                            if (mouse.button === Qt.RightButton) {
                                                editor.selectClipWithoutSeek(modelData.id)
                                                clipContextMenu.clipData = modelData
                                                const p = mapToItem(window.contentItem, mouse.x, mouse.y)
                                                clipContextMenu.x = p.x
                                                clipContextMenu.y = p.y
                                                clipContextMenu.open()
                                                return
                                            }
                                            pressOffsetX = mouse.x
                                            pressOffsetY = mouse.y
                                            const pointer = mapToItem(timelineClips, mouse.x, mouse.y)
                                            pressTimelineX = pointer.x
                                            pressTimelineY = pointer.y
                                            draggingClip = false
                                            snapGuideFrame = -1
                                            editor.selectClip(modelData.id)
                                            const clickedFrame = modelData.start + Math.max(0,
                                                Math.min(modelData.duration - 1, Math.floor(mouse.x / pixelsPerFrame)))
                                            if (mouse.modifiers & Qt.ShiftModifier) {
                                                selectionAnchor = clickedFrame
                                                editor.setTimelineRange(clickedFrame, clickedFrame + 1)
                                            } else {
                                                selectionAnchor = -1
                                                editor.scrubTimelineFrame(clickedFrame, snappingEnabled, snapToleranceFrames)
                                            }
                                        }
                                        onPositionChanged: (mouse) => {
                                            if (!(mouse.buttons & Qt.LeftButton)) return
                                            if (selectionAnchor >= 0) {
                                                const frame = modelData.start + Math.max(0,
                                                    Math.min(modelData.duration, Math.floor(mouse.x / pixelsPerFrame)))
                                                editor.setTimelineRange(Math.min(selectionAnchor, frame), Math.max(selectionAnchor, frame) + 1)
                                                return
                                            }
                                            const pointer = mapToItem(timelineClips, mouse.x, mouse.y)
                                            if (!draggingClip && Math.hypot(pointer.x - pressTimelineX,
                                                pointer.y - pressTimelineY) < 4) return
                                            draggingClip = true
                                            autoScrollTimeline(timeline.mapFromItem(clipMouse, mouse.x, mouse.y).y)
                                            const placement = previewClipPlacement(modelData,
                                                pointer.x - pressOffsetX, pointer.y, pointer.x)
                                            snapGuideFrame = placement.guide
                                            parent.x = placement.start * pixelsPerFrame
                                            parent.y = Math.max(-newTrackDropHeight,
                                                Math.min(editor.tracks.length * trackRowHeight, pointer.y - pressOffsetY))
                                        }
                                        onReleased: (mouse) => {
                                            if (mouse.button === Qt.RightButton) return
                                            if (selectionAnchor >= 0) { selectionAnchor = -1; return }
                                            if (!draggingClip) return
                                            const pointer = mapToItem(timelineClips, mouse.x, mouse.y)
                                            const rawY = pointer.y - pressOffsetY
                                            const track = rowForClipY(pointer.y)
                                            const placement = previewClipPlacement(modelData,
                                                pointer.x - pressOffsetX, pointer.y, pointer.x)
                                            const destination = placement.start
                                            snapGuideFrame = -1
                                            draggingClip = false
                                            parent.x = Qt.binding(function() { return modelData.start * pixelsPerFrame })
                                            parent.y = Qt.binding(function() { return visibleTrackRow(modelData.track) * trackRowHeight })
                                            editor.moveClip(modelData.id, destination, track,
                                                            snappingEnabled, snapToleranceFrames,
                                                            Math.max(0, Math.round(pointer.x / pixelsPerFrame)))
                                        }
                                        onCanceled: {
                                            snapGuideFrame = -1
                                            draggingClip = false
                                            selectionAnchor = -1
                                            parent.x = Qt.binding(function() { return modelData.start * pixelsPerFrame })
                                            parent.y = Qt.binding(function() { return visibleTrackRow(modelData.track) * trackRowHeight })
                                        }
                                    }
                                    Canvas {
                                        id: gainCanvas
                                        visible: clipItem.clipData.hasAudio
                                        x: 6; y: 0; width: Math.max(1, clipItem.width - 12); height: clipItem.height
                                        z: 8
                                        function gainY(value) { return 41 - Math.min(300, Math.max(0, value)) * (32 / 300) }
                                        function valueAtY(y) { return Math.max(0, Math.min(300, Math.round((41 - y) * (300 / 32)))) }
                                        function frameAt(x) {
                                            return Math.max(0, Math.min(clipItem.clipData.duration,
                                                Math.round(x / Math.max(1, width) * clipItem.clipData.duration)))
                                        }
                                        function gainAt(frame) {
                                            const points = clipItem.clipData.gainKeyframes || []
                                            let previousFrame = 0
                                            let previousValue = clipItem.clipData.gainPercent
                                            for (const point of points) {
                                                if (frame <= point.frame) {
                                                    if (point.frame === previousFrame) return point.value
                                                    return previousValue + (point.value - previousValue)
                                                        * (frame - previousFrame) / (point.frame - previousFrame)
                                                }
                                                previousFrame = point.frame
                                                previousValue = point.value
                                            }
                                            return previousValue
                                        }
                                        onPaint: {
                                            const ctx = getContext("2d")
                                            ctx.clearRect(0, 0, width, height)
                                            const points = (clipItem.clipData.gainKeyframes || []).slice()
                                            if (gainLineMouse.editing) {
                                                const frame = gainLineMouse.frameAtPointer(gainLineMouse.previewX)
                                                const value = gainLineMouse.valueAtPointer(gainLineMouse.previewY)
                                                const existing = points.findIndex(function(point) { return point.frame === frame })
                                                if (existing >= 0) points[existing] = {frame: frame, value: value}
                                                else points.push({frame: frame, value: value})
                                                points.sort(function(a, b) { return a.frame - b.frame })
                                            }
                                            ctx.beginPath()
                                            ctx.moveTo(0, gainY(points.length && points[0].frame === 0 ? points[0].value : clipItem.clipData.gainPercent))
                                            for (const point of points)
                                                ctx.lineTo(Math.min(width, point.frame / Math.max(1, clipItem.clipData.duration) * width), gainY(point.value))
                                            ctx.lineTo(width, gainY(points.length ? points[points.length - 1].value : clipItem.clipData.gainPercent))
                                            ctx.lineWidth = 2
                                            ctx.strokeStyle = clipItem.selected ? "#dbf7ff" : "#b5e9f3"
                                            ctx.shadowColor = "#102b39"
                                            ctx.shadowBlur = 3
                                            ctx.stroke()
                                        }
                                        onWidthChanged: requestPaint()
                                        Connections { target: editor; function onProjectChanged() { gainCanvas.requestPaint() } }
                                        MouseArea {
                                            id: gainLineMouse
                                            anchors.fill: parent
                                            acceptedButtons: Qt.LeftButton
                                            hoverEnabled: true
                                            preventStealing: true
                                            property bool editing: false
                                            property real previewX: 0
                                            property real previewY: 0
                                            readonly property bool nearLine: containsMouse &&
                                                Math.abs(mouseY - gainCanvas.gainY(gainCanvas.gainAt(frameAtPointer(mouseX)))) <= 9
                                            function frameAtPointer(x) { return gainCanvas.frameAt(x) }
                                            function valueAtPointer(y) {
                                                return gainCanvas.valueAtY(y)
                                            }
                                            cursorShape: nearLine || editing ? Qt.SizeVerCursor : Qt.ArrowCursor
                                            ToolTip.visible: nearLine && !editing
                                            ToolTip.text: "Drag to adjust gain · double-click a point to remove it"
                                            onPressed: (mouse) => {
                                                timeline.forceActiveFocus()
                                                if (Math.abs(mouse.y - gainCanvas.gainY(gainCanvas.gainAt(frameAtPointer(mouse.x)))) > 9) {
                                                    mouse.accepted = false
                                                    return
                                                }
                                                editor.selectClipWithoutSeek(clipItem.clipData.id)
                                                previewX = mouse.x
                                                previewY = mouse.y
                                                editing = true
                                                gainCanvas.requestPaint()
                                            }
                                            onPositionChanged: (mouse) => {
                                                if (!editing) return
                                                previewX = Math.max(0, Math.min(width, mouse.x))
                                                previewY = Math.max(9, Math.min(41, mouse.y))
                                                gainCanvas.requestPaint()
                                            }
                                            onReleased: {
                                                if (!editing) return
                                                const frame = frameAtPointer(previewX)
                                                const gain = valueAtPointer(previewY)
                                                editing = false
                                                editor.setAudioGainPoint(clipItem.clipData.id, frame, gain)
                                                gainCanvas.requestPaint()
                                            }
                                            onCanceled: { editing = false; gainCanvas.requestPaint() }
                                            Rectangle {
                                                visible: gainLineMouse.nearLine || gainLineMouse.editing
                                                x: gainLineMouse.editing ? gainLineMouse.previewX - width / 2 : gainLineMouse.mouseX - width / 2
                                                y: gainLineMouse.editing ? gainLineMouse.previewY - height / 2
                                                    : gainCanvas.gainY(gainCanvas.gainAt(gainLineMouse.frameAtPointer(gainLineMouse.mouseX))) - height / 2
                                                width: 10; height: 10; radius: 5
                                                color: "#f3fcff"; border.color: "#197fae"; border.width: 2
                                                scale: gainLineMouse.editing ? 1.25 : 1
                                                Behavior on scale { NumberAnimation { duration: 100 } }
                                            }
                                        }
                                    }
                                    Repeater {
                                        model: clipItem.clipData.hasAudio ? clipItem.clipData.gainKeyframes : []
                                        delegate: Rectangle {
                                            id: gainPoint
                                            required property var modelData
                                            x: 6 + Math.min(clipItem.width - 12, modelData.frame / Math.max(1, clipItem.clipData.duration) * (clipItem.width - 12)) - 5
                                            y: gainCanvas.gainY(modelData.value) - 5
                                            width: 10; height: 10; radius: 5; z: 14
                                            color: selectedTimelineKeyframe?.kind === "gain" && selectedTimelineKeyframe?.id === clipItem.clipData.id && selectedTimelineKeyframe?.frame === modelData.frame ? "#ffdd7d" : "#e9fbff"; border.color: "#197fae"; border.width: 2
                                            MouseArea {
                                                anchors.fill: parent
                                                acceptedButtons: Qt.LeftButton
                                                hoverEnabled: true
                                                cursorShape: Qt.SizeAllCursor
                                                preventStealing: true
                                                property real originX: 0
                                                property real originY: 0
                                                onPressed: { timeline.forceActiveFocus(); originX = gainPoint.x; originY = gainPoint.y; editor.selectClipWithoutSeek(clipItem.clipData.id); selectedTimelineKeyframe = {kind: "gain", id: clipItem.clipData.id, frame: modelData.frame} }
                                                onPositionChanged: (mouse) => {
                                                    if (!pressed) return
                                                    gainPoint.x = Math.max(1, Math.min(clipItem.width - 11, gainPoint.x + mouse.x - 5))
                                                    gainPoint.y = Math.max(4, Math.min(37, gainPoint.y + mouse.y - 5))
                                                }
                                                onReleased: {
                                                    const frame = Math.max(0, Math.min(clipItem.clipData.duration,
                                                        Math.round((gainPoint.x - 1) / Math.max(1, clipItem.width - 12) * clipItem.clipData.duration)))
                                                    const gain = gainCanvas.valueAtY(gainPoint.y + 5)
                                                    gainPoint.x = Qt.binding(function() { return 6 + Math.min(clipItem.width - 12, modelData.frame / Math.max(1, clipItem.clipData.duration) * (clipItem.width - 12)) - 5 })
                                                    gainPoint.y = Qt.binding(function() { return gainCanvas.gainY(modelData.value) - 5 })
                                                    editor.moveAudioGainPoint(clipItem.clipData.id, modelData.frame, frame, gain)
                                                    selectedTimelineKeyframe = {kind: "gain", id: clipItem.clipData.id, frame: frame}
                                                }
                                                onDoubleClicked: editor.removeAudioGainPoint(clipItem.clipData.id, modelData.frame)
                                            }
                                        }
                                    }
                                    Item {
                                        x: 0; y: 0; width: Math.min(12, clipItem.width / 3); height: parent.height
                                        MouseArea {
                                            anchors.fill: parent
                                            acceptedButtons: Qt.LeftButton
                                            cursorShape: Qt.SizeHorCursor
                                            preventStealing: true
                                            property real pressTimelineX: 0
                                            property bool resizing: false
                                            ToolTip.visible: containsMouse
                                            ToolTip.text: "Drag clip edge to trim start"
                                            hoverEnabled: true
                                            onPressed: (mouse) => {
                                                timeline.forceActiveFocus()
                                                pressTimelineX = mapToItem(timelineClips, mouse.x, mouse.y).x
                                                resizing = false
                                                snapGuideFrame = -1
                                                editor.selectClipWithoutSeek(modelData.id)
                                            }
                                            onPositionChanged: (mouse) => {
                                                if (!pressed) return
                                                const pointerX = mapToItem(timelineClips, mouse.x, mouse.y).x
                                                if (!resizing && Math.abs(pointerX - pressTimelineX) < 4) return
                                                resizing = true
                                                const originalEnd = modelData.start + modelData.duration
                                                const raw = Math.max(0, Math.min(originalEnd - 1,
                                                    modelData.start + Math.round((pointerX - pressTimelineX) / pixelsPerFrame)))
                                                const placement = snappingEnabled
                                                    ? editor.previewClipTrimSnap(modelData.id, raw, true, snapToleranceFrames)
                                                    : { frame: raw, guide: -1 }
                                                snapGuideFrame = placement.guide
                                                clipItem.x = placement.frame * pixelsPerFrame
                                                clipItem.width = Math.max(1, (originalEnd - placement.frame) * pixelsPerFrame)
                                                clipItem.waveformTrimOffsetX = modelData.start * pixelsPerFrame - clipItem.x
                                            }
                                            onReleased: (mouse) => {
                                                if (!resizing) return
                                                const pointerX = mapToItem(timelineClips, mouse.x, mouse.y).x
                                                const originalEnd = modelData.start + modelData.duration
                                                const raw = Math.max(0, Math.min(originalEnd - 1,
                                                    modelData.start + Math.round((pointerX - pressTimelineX) / pixelsPerFrame)))
                                                clipItem.x = Qt.binding(function() { return modelData.start * pixelsPerFrame })
                                                clipItem.width = Qt.binding(function() { return Math.max(1, modelData.duration * pixelsPerFrame) })
                                                clipItem.waveformTrimOffsetX = 0
                                                snapGuideFrame = -1
                                                resizing = false
                                                editor.trimClipStartSnapped(modelData.id, raw, snappingEnabled, snapToleranceFrames)
                                            }
                                            onCanceled: {
                                                clipItem.x = Qt.binding(function() { return modelData.start * pixelsPerFrame })
                                                clipItem.width = Qt.binding(function() { return Math.max(1, modelData.duration * pixelsPerFrame) })
                                                clipItem.waveformTrimOffsetX = 0
                                                snapGuideFrame = -1
                                                resizing = false
                                            }
                                        }
                                    }
                                    Item {
                                        x: parent.width - width; y: 0; width: Math.min(12, clipItem.width / 3); height: parent.height
                                        MouseArea {
                                            anchors.fill: parent
                                            acceptedButtons: Qt.LeftButton
                                            cursorShape: Qt.SizeHorCursor
                                            preventStealing: true
                                            property real pressTimelineX: 0
                                            property bool resizing: false
                                            ToolTip.visible: containsMouse
                                            ToolTip.text: "Drag clip edge to trim end"
                                            hoverEnabled: true
                                            onPressed: (mouse) => {
                                                timeline.forceActiveFocus()
                                                pressTimelineX = mapToItem(timelineClips, mouse.x, mouse.y).x
                                                resizing = false
                                                snapGuideFrame = -1
                                                editor.selectClipWithoutSeek(modelData.id)
                                            }
                                            onPositionChanged: (mouse) => {
                                                if (!pressed) return
                                                const pointerX = mapToItem(timelineClips, mouse.x, mouse.y).x
                                                if (!resizing && Math.abs(pointerX - pressTimelineX) < 4) return
                                                resizing = true
                                                const raw = Math.max(modelData.start + 1,
                                                    modelData.start + modelData.duration
                                                    + Math.round((pointerX - pressTimelineX) / pixelsPerFrame))
                                                const placement = snappingEnabled
                                                    ? editor.previewClipTrimSnap(modelData.id, raw, false, snapToleranceFrames)
                                                    : { frame: raw, guide: -1 }
                                                snapGuideFrame = placement.guide
                                                clipItem.width = Math.max(1, (placement.frame - modelData.start) * pixelsPerFrame)
                                            }
                                            onReleased: (mouse) => {
                                                if (!resizing) return
                                                const pointerX = mapToItem(timelineClips, mouse.x, mouse.y).x
                                                const raw = Math.max(modelData.start + 1,
                                                    modelData.start + modelData.duration
                                                    + Math.round((pointerX - pressTimelineX) / pixelsPerFrame))
                                                clipItem.width = Qt.binding(function() { return Math.max(1, modelData.duration * pixelsPerFrame) })
                                                snapGuideFrame = -1
                                                resizing = false
                                                editor.trimClipEndSnapped(modelData.id, raw, snappingEnabled, snapToleranceFrames)
                                            }
                                            onCanceled: {
                                                clipItem.width = Qt.binding(function() { return Math.max(1, modelData.duration * pixelsPerFrame) })
                                                snapGuideFrame = -1
                                                resizing = false
                                            }
                                        }
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: 1
                                        radius: 3
                                        color: "#1e83ed"
                                        opacity: clipItem.selected ? 0.12 : 0
                                        z: 12
                                        Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: 1
                                        radius: 3
                                        color: "transparent"
                                        border.width: 2
                                        border.color: "#69b7ff"
                                        opacity: clipItem.selected ? 1 : 0
                                        z: 13
                                        Behavior on opacity { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                                    }
                                }
                            }
                        }
                        Repeater {
                            model: editor.transitions.filter(function(item) {
                                const left = timeline.contentX / pixelsPerFrame - 100
                                const right = (timeline.contentX + timeline.width) / pixelsPerFrame + 100
                                return item.start + item.duration >= left && item.start <= right
                            })
                            delegate: Rectangle {
                                id: transitionBand
                                required property var modelData
                                readonly property bool selected: editor.selectedTransitionId === modelData.id
                                property real resizeDelta: 0
                                x: modelData.start * pixelsPerFrame
                                y: trackRowsY + visibleTrackRow(modelData.track) * trackRowHeight + 1
                                width: Math.max(3, modelData.duration * pixelsPerFrame)
                                height: 21; radius: 3; z: 18
                                color: selected ? "#9c67d3" : "#5f4d86"
                                border.width: selected ? 2 : 1
                                border.color: selected ? "#ebc9ff" : "#b092ce"
                                Behavior on color { ColorAnimation { duration: 120 } }
                                Label {
                                    anchors.centerIn: parent
                                    width: Math.max(0, parent.width - 14)
                                    text: "⋈ " + modelData.name
                                    color: "white"; font.pixelSize: 10; font.bold: true
                                    elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onPressed: (mouse) => {
                                        timeline.forceActiveFocus()
                                        editor.selectTransition(modelData.id)
                                        inspectorTab = 4
                                        editor.scrubTimelineFrame(modelData.start
                                            + Math.max(0, Math.min(modelData.duration - 1,
                                                Math.floor(mouse.x / pixelsPerFrame))), false, 0)
                                    }
                                }
                                MouseArea {
                                    anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                                    width: Math.min(9, parent.width / 3)
                                    enabled: modelData.paired || !modelData.startEdge
                                    hoverEnabled: true; cursorShape: Qt.SizeHorCursor
                                    property real originX: 0
                                    onPressed: (mouse) => { originX = mapToItem(timelineClips, mouse.x, mouse.y).x; editor.selectTransition(modelData.id); inspectorTab = 4 }
                                    onReleased: (mouse) => {
                                        const delta = Math.round((originX - mapToItem(timelineClips, mouse.x, mouse.y).x) / pixelsPerFrame)
                                        const factor = modelData.paired ? 100 / modelData.balance : 1
                                        editor.setTransitionDuration(modelData.id,
                                            Math.max(1, modelData.duration + Math.round(delta * factor)))
                                    }
                                }
                                MouseArea {
                                    anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                                    width: Math.min(9, parent.width / 3)
                                    enabled: modelData.paired || modelData.startEdge
                                    hoverEnabled: true; cursorShape: Qt.SizeHorCursor
                                    property real originX: 0
                                    onPressed: (mouse) => { originX = mapToItem(timelineClips, mouse.x, mouse.y).x; editor.selectTransition(modelData.id); inspectorTab = 4 }
                                    onReleased: (mouse) => {
                                        const delta = Math.round((mapToItem(timelineClips, mouse.x, mouse.y).x - originX) / pixelsPerFrame)
                                        const factor = modelData.paired ? 100 / (100 - modelData.balance) : 1
                                        editor.setTransitionDuration(modelData.id,
                                            Math.max(1, modelData.duration + Math.round(delta * factor)))
                                    }
                                }
                            }
                        }
                        Canvas {
                            id: frameGrid
                            visible: timelineVisualMode === "Frame" && pixelsPerFrame >= 6
                            x: timeline.contentX
                            y: trackRowsY
                            width: timeline.width
                            height: editor.tracks.length * trackRowHeight
                            onPaint: {
                                const context = getContext("2d")
                                context.clearRect(0, 0, width, height)
                                const first = Math.max(0, Math.ceil(timeline.contentX / pixelsPerFrame))
                                const last = Math.floor((timeline.contentX + width) / pixelsPerFrame)
                                context.strokeStyle = "rgba(189, 221, 248, 0.38)"
                                context.fillStyle = "rgba(226, 240, 255, 0.85)"
                                context.font = "10px Segoe UI"
                                for (let frame = first; frame <= last; ++frame) {
                                    const x = frame * pixelsPerFrame - timeline.contentX + 0.5
                                    context.beginPath()
                                    context.moveTo(x, 0)
                                    context.lineTo(x, height)
                                    context.stroke()
                                    if (pixelsPerFrame >= 18 && frame % 5 === 0)
                                        context.fillText(String(frame), x + 3, 15)
                                }
                            }
                            onWidthChanged: requestPaint()
                            onVisibleChanged: if (visible) requestPaint()
                            Connections {
                                target: timeline
                                function onContentXChanged() { frameGrid.requestPaint() }
                            }
                            Connections {
                                target: window
                                function onPixelsPerFrameChanged() { frameGrid.requestPaint() }
                            }
                        }
                        Rectangle {
                            readonly property int activeGuide: snapGuideFrame >= 0
                                ? snapGuideFrame : draggedClipPlacement.guide
                            x: activeGuide * pixelsPerFrame; y: 35
                            width: 2; height: trackRowsY + editor.tracks.length * trackRowHeight - 35; color: "#77c2ff"; z: 9
                            opacity: activeGuide >= 0 ? 0.95 : 0
                            visible: opacity > 0
                            Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                        }
                        Rectangle { visible: editor.hasTimelineRange; x: Number(editor.timelineRangeStart) * pixelsPerFrame; y: trackRowsY; width: 2; height: editor.tracks.length * trackRowHeight; color: "#70bdff" }
                        Rectangle { visible: editor.hasTimelineRange; x: Number(editor.timelineRangeEnd) * pixelsPerFrame; y: trackRowsY; width: 2; height: editor.tracks.length * trackRowHeight; color: "#70bdff" }
                        Repeater {
                            model: {
                                const markers = []
                                function add(kind, item, property, list, lane, absolute) {
                                    for (const point of (list || [])) {
                                        const frame = absolute ? point.frame : item.start + point.frame
                                        if (frame >= item.start && frame < item.start + item.duration)
                                            markers.push({ kind: kind, id: item.id, property: property,
                                                frame: frame, track: kind === "caption" ? item.trackId : item.track,
                                                lane: lane, value: point.value, start: item.start, duration: item.duration })
                                    }
                                }
                                const clip = editor.clips.find(function(item) { return item.id === editor.selectedClipId })
                                if (clip) {
                                    add("clip", clip, "x", clip.positionXKeyframes, 0, false)
                                    add("clip", clip, "y", clip.positionYKeyframes, 1, false)
                                    add("clip", clip, "scale", clip.scaleKeyframes, 2, false)
                                    add("clip", clip, "rotation", clip.rotationKeyframes, 3, false)
                                    add("clip", clip, "opacity", clip.opacityKeyframes, 4, false)
                                }
                                const title = editor.titles.find(function(item) { return item.id === editor.selectedTitleId })
                                if (title) {
                                    add("title", title, "x", title.positionXKeyframes, 0, false)
                                    add("title", title, "y", title.positionYKeyframes, 1, false)
                                    add("title", title, "scale", title.scaleKeyframes, 2, false)
                                }
                                const row = editor.captionRows.find(function(item) { return item.id === editor.selectedCaptionRowId })
                                if (row) {
                                    const extent = { id: row.id, trackId: row.trackId, start: 0,
                                        duration: editor.timelineDurationFrames }
                                    add("caption", extent, "x", row.positionXKeyframes, 0, true)
                                    add("caption", extent, "y", row.positionYKeyframes, 1, true)
                                    add("caption", extent, "scale", row.scaleKeyframes, 2, true)
                                }
                                return markers
                            }
                            delegate: Item {
                                id: visualKeyframeMarker
                                required property var modelData
                                property int draggedFrame: -1
                                x: (draggedFrame >= 0 ? draggedFrame : modelData.frame) * pixelsPerFrame - width / 2
                                y: trackRowsY + visibleTrackRow(modelData.track) * trackRowHeight + 3 + modelData.lane * 9
                                width: 16; height: 16; z: 17
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: visualKeyframeMouse.containsMouse || visualKeyframeMouse.pressed ? 11 : 9
                                    height: width; rotation: 45; radius: 1
                                    color: selectedTimelineKeyframe?.kind === modelData.kind
                                        && selectedTimelineKeyframe?.id === modelData.id
                                        && selectedTimelineKeyframe?.property === modelData.property
                                        && selectedTimelineKeyframe?.frame === modelData.frame ? "#ffdd7d" : "#a9e2ff"
                                    border.color: "#235a86"; border.width: 1
                                    Behavior on width { NumberAnimation { duration: 90 } }
                                }
                                MouseArea {
                                    id: visualKeyframeMouse
                                    anchors.fill: parent; hoverEnabled: true; preventStealing: true
                                    cursorShape: Qt.SizeHorCursor
                                    ToolTip.visible: containsMouse && !pressed
                                    ToolTip.text: modelData.property + " · " + modelData.value + " · " + editor.projectFrameTimestamp(modelData.frame)
                                    onPressed: {
                                        timeline.forceActiveFocus()
                                        if (modelData.kind === "clip") editor.selectClipWithoutSeek(modelData.id)
                                        else if (modelData.kind === "title") editor.selectTitle(modelData.id)
                                        else editor.selectCaptionRow(modelData.id)
                                        selectedTimelineKeyframe = { kind: modelData.kind, id: modelData.id,
                                            property: modelData.property, frame: modelData.frame }
                                    }
                                    onPositionChanged: (mouse) => {
                                        if (!pressed) return
                                        const pointer = mapToItem(timelineClips, mouse.x, mouse.y)
                                        draggedFrame = Math.max(modelData.start,
                                            Math.min(modelData.start + modelData.duration - 1,
                                                Math.round(pointer.x / pixelsPerFrame)))
                                    }
                                    onReleased: {
                                        if (draggedFrame < 0) { seekProjectFrame(modelData.frame); return }
                                        const target = draggedFrame
                                        draggedFrame = -1
                                        editor.moveVisualPropertyKeyframe(modelData.kind, modelData.id,
                                            modelData.property, modelData.frame, target)
                                        selectedTimelineKeyframe = { kind: modelData.kind, id: modelData.id,
                                            property: modelData.property, frame: target }
                                    }
                                    onCanceled: draggedFrame = -1
                                }
                            }
                        }
                        Repeater {
                            model: {
                                const points = []
                                for (const effect of editor.trackEffects) {
                                    for (const point of effect.keyframes)
                                        points.push({ frame: point.frame, track: effect.track,
                                            effectId: effect.id, parameterId: "", value: point.value, lane: 0 })
                                    let lane = 0
                                    for (const parameter of effect.parameters) {
                                        ++lane
                                        for (const point of parameter.keyframes)
                                            points.push({ frame: point.frame, track: effect.track,
                                                effectId: effect.id, parameterId: parameter.id,
                                                value: point.value, lane: lane })
                                    }
                                }
                                return points
                            }
                            delegate: Item {
                                required property var modelData
                                id: effectKeyframeMarker
                                property int draggedFrame: -1
                                x: (draggedFrame >= 0 ? draggedFrame : modelData.frame) * pixelsPerFrame - width / 2
                                y: trackRowsY + visibleTrackRow(modelData.track) * trackRowHeight
                                    + 1 + Math.min(modelData.lane, 3) * 10
                                width: 16; height: 16; z: 16
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: keyframeDrag.containsMouse || keyframeDrag.pressed ? 10 : 8
                                    height: width; rotation: 45; radius: 1
                                    color: selectedTimelineKeyframe?.kind === "effect"
                                        && selectedTimelineKeyframe?.track === modelData.track
                                        && selectedTimelineKeyframe?.effectId === modelData.effectId
                                        && selectedTimelineKeyframe?.parameterId === modelData.parameterId
                                        && selectedTimelineKeyframe?.frame === modelData.frame ? "#ffdd7d"
                                        : keyframeDrag.pressed ? "#ffffff" : "#d9adff"
                                    border.color: "#522b7b"; border.width: 1
                                    Behavior on width { NumberAnimation { duration: 90 } }
                                }
                                MouseArea {
                                    id: keyframeDrag
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    preventStealing: true
                                    cursorShape: Qt.SizeHorCursor
                                    ToolTip.visible: containsMouse && !pressed
                                    ToolTip.text: modelData.effectId + (modelData.parameterId ? " · " + modelData.parameterId : "")
                                        + " · " + modelData.value + " · "
                                        + editor.projectFrameTimestamp(modelData.frame)
                                    onPressed: {
                                        timeline.forceActiveFocus()
                                        selectedTimelineKeyframe = {kind: "effect", track: modelData.track,
                                            effectId: modelData.effectId, parameterId: modelData.parameterId,
                                            frame: modelData.frame}
                                    }
                                    onPositionChanged: (mouse) => {
                                        if (!pressed) return
                                        const pointer = mapToItem(timelineClips, mouse.x, mouse.y)
                                        effectKeyframeMarker.draggedFrame = Math.max(0,
                                            Math.min(Math.max(0, editor.timelineDurationFrames - 1),
                                                Math.round(pointer.x / pixelsPerFrame)))
                                        autoScrollTimeline(timeline.mapFromItem(keyframeDrag, mouse.x, mouse.y).y)
                                    }
                                    onReleased: {
                                        if (effectKeyframeMarker.draggedFrame < 0) return
                                        const destination = effectKeyframeMarker.draggedFrame
                                        effectKeyframeMarker.draggedFrame = -1
                                        if (modelData.parameterId)
                                            editor.moveTrackEffectParameterKeyframe(modelData.track, modelData.effectId,
                                                modelData.parameterId, modelData.frame, destination)
                                        else
                                            editor.moveTrackEffectKeyframe(modelData.track, modelData.effectId,
                                                modelData.frame, destination)
                                        selectedTimelineKeyframe = {kind: "effect", track: modelData.track,
                                            effectId: modelData.effectId, parameterId: modelData.parameterId,
                                            frame: destination}
                                    }
                                    onDoubleClicked: {
                                        if (modelData.parameterId)
                                            editor.removeTrackEffectParameterKeyframe(modelData.track,
                                                modelData.effectId, modelData.parameterId, modelData.frame)
                                        else
                                            editor.removeTrackEffectKeyframe(modelData.track, modelData.effectId,
                                                modelData.frame)
                                    }
                                    onCanceled: effectKeyframeMarker.draggedFrame = -1
                                }
                            }
                        }
                        Rectangle {
                            visible: draggedTimelineTarget.valid && draggedTransitionId === ""
                            x: draggedTimelineTarget.valid
                                ? (draggedTransitionId !== "" && draggedTransitionTarget.valid
                                    ? draggedTransitionTarget.cut
                                    : draggedEffectId !== "" ? draggedTimelineTarget.frame
                                    : draggedClipPlacement.start) * pixelsPerFrame : 0
                            y: draggedTimelineTarget.valid ? (draggedTimelineTarget.track === -1 ? 36
                                : draggedTimelineTarget.track === -2 ? trackRowsY + editor.tracks.length * trackRowHeight
                                : trackRowsY + visibleTrackRow(draggedTimelineTarget.track) * trackRowHeight) : trackRowsY
                            width: 3; height: 48; radius: 1
                            color: draggedTargetCompatible ? "#7ac2ff" : "#ff9b85"
                        }
                        Rectangle {
                            visible: draggedTransitionId !== "" && !!draggedTransitionTarget.valid
                            x: visible ? draggedTransitionTarget.start * pixelsPerFrame : 0
                            y: visible ? trackRowsY + visibleTrackRow(draggedTransitionTarget.track)
                                * trackRowHeight + 1 : 0
                            width: visible ? Math.max(3, draggedTransitionTarget.duration * pixelsPerFrame) : 0
                            height: 22; radius: 3; z: 38
                            color: "#aa7654a2"; border.width: 2; border.color: "#e7c5ff"
                            Label {
                                anchors.centerIn: parent
                                text: draggedTransitionTarget.paired ? "⋈  between clips" : "⋈  clip edge"
                                color: "white"; font.pixelSize: 10; font.bold: true
                                visible: parent.width > implicitWidth + 10
                            }
                        }
                        Rectangle { x: editor.playheadFrame * pixelsPerFrame; y: timeline.contentY + 19; width: 2; height: timeline.height - 19; color: accentColor; z: 42 }
                        Rectangle {
                            id: playheadHandle
                            x: editor.playheadFrame * pixelsPerFrame - 5; y: timeline.contentY + 16
                            width: 12; height: 12; radius: 3; z: 43
                            color: playheadDrag.pressed ? "#78baff" : accentColor
                            MouseArea {
                                id: playheadDrag
                                anchors.fill: parent
                                preventStealing: true
                                cursorShape: Qt.SizeHorCursor
                                onPressed: timeline.forceActiveFocus()
                                onPositionChanged: (mouse) => {
                                    if (pressed) editor.scrubTimelineFrame(
                                        Math.round((playheadHandle.x + mouse.x) / pixelsPerFrame),
                                        snappingEnabled, snapToleranceFrames)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    DropArea {
        id: externalFileDrop
        anchors.fill: parent
        z: 100
        onEntered: (drag) => { drag.accepted = drag.hasUrls }
        onPositionChanged: (drag) => { drag.accepted = drag.hasUrls; if (drag.hasUrls) autoScrollTimeline(timeline.mapFromItem(externalFileDrop, drag.x, drag.y).y) }
        onDropped: (drop) => {
            if (!drop.hasUrls) {
                drop.accepted = false
                return
            }
            const target = timelineDropPosition(externalFileDrop, drop.x, drop.y)
            libraryMode = 0
            for (let index = 0; index < drop.urls.length; ++index) {
                if (index === 0 && target.valid)
                    editor.importMediaAt(drop.urls[index], target.frame, target.track,
                                         snappingEnabled, snapToleranceFrames)
                else
                    editor.importMedia(drop.urls[index])
            }
            drop.acceptProposedAction()
        }
        Rectangle {
            visible: externalFileDrop.containsDrag
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 72
            width: 310; height: 46; radius: 7
            color: "#1a4874"; border.color: accentColor
            Label { anchors.centerIn: parent; text: "Drop media to import"; color: "white"; font.pixelSize: 14 }
        }
    }
    Rectangle {
        visible: draggedAssetId !== "" || draggedSegmentId !== "" || draggedEffectId !== "" || draggedTextObject
            || draggedTransitionId !== ""
        z: 101
        x: draggedAssetX + 12
        y: draggedAssetY + 12
        width: Math.max(120, dragLabel.implicitWidth + 24)
        height: 35
        radius: 5
        color: "#24588a"
        border.color: draggedTransitionId !== "" ? (draggedTransitionTarget.valid ? accentColor : "#ff9b85")
            : draggedTimelineTarget.valid
            ? (draggedTargetCompatible ? accentColor : "#ff9b85") : "#8a9bac"
        opacity: 0.88
        Label { id: dragLabel; anchors.centerIn: parent; text: "▶  " + draggedAssetName + (draggedTimelineTarget.valid && !draggedTargetCompatible ? " · wrong track" : ""); color: "white"; font.pixelSize: 11 }
    }
    Rectangle {
        anchors.fill: parent
        z: 150
        color: "transparent"
        border.width: window.normalWindow ? 1 : 0
        border.color: window.active ? "#3d5268" : borderColor
    }
    Timer {
        id: fullscreenIdleTimer
        interval: 2400
        onTriggered: {
            if (fullscreenBarHover.hovered) restart()
            else fullscreenControlsVisible = false
        }
    }
    Window {
        id: fullScreenViewerWindow
        title: "DataCut Viewer"
        screen: window.screen
        transientParent: null
        flags: Qt.Window | Qt.FramelessWindowHint
        color: "black"
        onClosing: (close) => { close.accepted = false; exitPreviewFullscreen() }
        Shortcut { sequence: "Escape"; onActivated: exitPreviewFullscreen() }
        Shortcut { sequence: "Space"; onActivated: togglePreviewPlayback() }
        Rectangle {
        id: fullscreenViewer
        anchors.fill: parent
        color: "black"
        Item { id: fullscreenCanvas; anchors.fill: parent }
        MouseArea {
            anchors.fill: parent
            z: 1
            hoverEnabled: true
            cursorShape: fullscreenControlsVisible ? Qt.ArrowCursor : Qt.BlankCursor
            onEntered: { fullscreenControlsVisible = true; fullscreenIdleTimer.restart() }
            onPositionChanged: {
                fullscreenControlsVisible = true
                fullscreenIdleTimer.restart()
            }
            onClicked: togglePreviewPlayback()
        }
        Rectangle {
            id: fullscreenControlBar
            z: 2
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 22
            width: Math.min(parent.width - 32, 1080)
            height: 84
            radius: 10
            color: "#e6131c28"
            border.color: "#475a6e"
            opacity: fullscreenControlsVisible || fullscreenBarHover.hovered ? 1 : 0
            enabled: opacity > 0.05
            Behavior on opacity { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
            HoverHandler {
                id: fullscreenBarHover
                onHoveredChanged: {
                    if (hovered) fullscreenControlsVisible = true
                    else fullscreenIdleTimer.restart()
                }
            }
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 3
                AppSlider {
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, editor.timelinePreviewEnabled
                        ? editor.projectFramesToMs(Math.max(0, editor.timelineDurationFrames - 1))
                        : player.duration)
                    value: editor.timelinePreviewEnabled
                        ? editor.projectFramesToMs(editor.playheadFrame) : player.position
                    enabled: editor.timelineDurationFrames > 0 || player.seekable
                    onMoved: {
                        if (editor.timelinePreviewEnabled) seekProjectFrame(editor.projectMsToFrames(value))
                        else player.position = value
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        text: editor.timelinePreviewEnabled
                            ? editor.projectFrameTimestamp(editor.playheadFrame) + " · F" + editor.playheadFrame
                            : clockTime(player.position) + " / " + clockTime(player.duration)
                        color: textColor; font.pixelSize: 12
                        Layout.preferredWidth: 152
                        elide: Text.ElideRight
                    }
                    Item { Layout.fillWidth: true }
                    IconButton { symbol: "◀"; enabled: editor.timelineDurationFrames > 0
                        onClicked: seekProjectFrame(Math.max(0, editor.playheadFrame - 1)) }
                    IconButton {
                        symbol: editor.timelinePlaying
                            || (!editor.timelinePreviewEnabled && player.playbackState === MediaPlayer.PlayingState)
                            ? "❚❚" : "▶"
                        enabled: editor.timelineDurationFrames > 0 || player.seekable
                        onClicked: togglePreviewPlayback()
                    }
                    IconButton { symbol: "▶"; enabled: editor.timelineDurationFrames > 0
                        onClicked: seekProjectFrame(editor.playheadFrame + 1) }
                    Item { Layout.fillWidth: true }
                    IconButton {
                        id: fullscreenMuteButton
                        ToolTip.visible: hovered; ToolTip.text: previewMuted ? "Unmute" : "Mute"
                        onClicked: previewMuted = !previewMuted
                        contentItem: Item {
                            VolumeIcon { anchors.centerIn: parent; muted: previewMuted
                                iconColor: fullscreenMuteButton.enabled ? "#dbe7f5" : "#607083" }
                        }
                    }
                    AppSlider { from: 0; to: 1; value: previewVolume
                        Layout.preferredWidth: 88; onValueChanged: previewVolume = value }
                    IconButton { symbol: "⛶"; ToolTip.visible: hovered
                        ToolTip.text: "Exit fullscreen · Esc"; onClicked: exitPreviewFullscreen() }
                }
            }
        }
        }
    }
    Item {
        anchors.fill: parent
        z: 200
        visible: window.normalWindow
        MouseArea { x: 0; y: 0; width: 6; height: parent.height - 12; cursorShape: Qt.SizeHorCursor; onPressed: window.startSystemResize(Qt.LeftEdge) }
        MouseArea { x: parent.width - 6; y: 0; width: 6; height: parent.height - 12; cursorShape: Qt.SizeHorCursor; onPressed: window.startSystemResize(Qt.RightEdge) }
        MouseArea { x: 12; y: parent.height - 6; width: parent.width - 24; height: 6; cursorShape: Qt.SizeVerCursor; onPressed: window.startSystemResize(Qt.BottomEdge) }
        MouseArea { x: 0; y: parent.height - 12; width: 12; height: 12; cursorShape: Qt.SizeBDiagCursor; onPressed: window.startSystemResize(Qt.BottomEdge | Qt.LeftEdge) }
        MouseArea { x: parent.width - 12; y: parent.height - 12; width: 12; height: 12; cursorShape: Qt.SizeFDiagCursor; onPressed: window.startSystemResize(Qt.BottomEdge | Qt.RightEdge) }
    }
}
