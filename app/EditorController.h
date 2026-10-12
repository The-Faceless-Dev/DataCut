#pragma once

#include "media/MediaProbe.h"
#include "core/MediaRange.h"
#include "transitions/TransitionRegistry.h"

#include <QObject>
#include <QElapsedTimer>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVector>
#include <QSet>
#include <QMap>
#include <QThreadPool>

#include <optional>

namespace datacut {

struct Asset {
    QString id;
    MediaInfo media;
    QString thumbnailPath;
    QString waveformPath;
    QStringList filmstripPaths;
    QString proxyPath;
};

struct ValueKeyframe {
    qint64 frame = 0;
    int value = 100;
};

struct EffectParameterState {
    QString id;
    int value = 0;
    QVector<ValueKeyframe> keyframes;
};

struct TrackEffect {
    int track = 0;
    QString id;
    int strength = 100;
    QVector<ValueKeyframe> keyframes;
    QVector<EffectParameterState> parameters;
};

struct ClipTransition {
    QString id;
    QString definitionId;
    QString outgoingClipId;
    QString incomingClipId;
    qint64 durationFrames = 1;
    int balance = 50;
    QMap<QString, int> parameters;
};

struct Clip {
    QString id;
    QString assetId;
    std::int64_t startFrame = 0;
    std::int64_t durationFrames = 0;
    std::int64_t sourceFirstFrame = 0;
    std::int64_t sourceFrameCount = 0;
    int track = 0;
    int positionX = 0;
    int positionY = 0;
    int scalePercent = 100;
    int rotationDegrees = 0;
    int opacityPercent = 100;
    QVector<ValueKeyframe> positionXKeyframes;
    QVector<ValueKeyframe> positionYKeyframes;
    QVector<ValueKeyframe> scaleKeyframes;
    QVector<ValueKeyframe> rotationKeyframes;
    QVector<ValueKeyframe> opacityKeyframes;
    bool includeVideo = true;
    bool includeAudio = true;
    int gainPercent = 100;
    QVector<ValueKeyframe> gainKeyframes;
    qint64 fadeInFrames = 0;
    qint64 fadeOutFrames = 0;
    int speedPercent = 100;
    QString effectId;
    int effectStrength = 100;
    QString audioEffectId;
    int audioEffectStrength = 100;
    QString voiceChangePath;
    QString voiceName;
    QString voiceMode;
    qint64 voiceSourceFirstFrame = 0;
    qint64 voiceSourceFrameCount = 0;
    bool voiceChangeEnabled = false;
};

struct TitleClip {
    QString id;
    QString text;
    qint64 startFrame = 0;
    qint64 durationFrames = 150;
    int fontPixels = 88;
    QString color = QStringLiteral("#ffffff");
    QString fontFamily = QStringLiteral("Segoe UI");
    bool bold = true;
    bool italic = false;
    QString backgroundColor = QStringLiteral("#000000");
    int backgroundOpacity = 0;
    int positionX = 50;
    int positionY = 50;
    int anchorX = 50;
    int anchorY = 50;
    int track = 3;
    int scalePercent = 100;
    QVector<ValueKeyframe> positionXKeyframes;
    QVector<ValueKeyframe> positionYKeyframes;
    QVector<ValueKeyframe> scaleKeyframes;
};

struct CaptionWord {
    QString id;
    QString text;
    qint64 firstFrame = 0;
    qint64 endFrame = 1; // Exclusive.
};

struct CaptionCue {
    QString id;
    QVector<CaptionWord> words;
    QString groupId;
};

struct CaptionRow {
    QString id;
    QString sourceClipId;
    int trackId = 1000;
    int targetTrack = 0;
    QString displayMode = QStringLiteral("sentence");
    int maxWordsPerCue = -1;
    QString fontFamily = QStringLiteral("Segoe UI");
    int fontPixels = 72;
    bool bold = true;
    bool italic = false;
    QString color = QStringLiteral("#ffffff");
    QString highlightColor = QStringLiteral("#ffd34d");
    QString backgroundColor = QStringLiteral("#000000");
    int backgroundOpacity = 65;
    int positionX = 50;
    int positionY = 84;
    int scalePercent = 100;
    QVector<ValueKeyframe> positionXKeyframes;
    QVector<ValueKeyframe> positionYKeyframes;
    QVector<ValueKeyframe> scaleKeyframes;
    bool visible = true;
    QVector<CaptionCue> cues;
};

class EditorController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList assets READ assets NOTIFY projectChanged)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY projectChanged)
    Q_PROPERTY(QVariantList segments READ segments NOTIFY projectChanged)
    Q_PROPERTY(QVariantList collections READ collections NOTIFY projectChanged)
    Q_PROPERTY(QVariantList titles READ titles NOTIFY projectChanged)
    Q_PROPERTY(QVariantList captionRows READ captionRows NOTIFY projectChanged)
    Q_PROPERTY(QVariantList captionCues READ captionCues NOTIFY projectChanged)
    Q_PROPERTY(QVariantList captionDisplayStyles READ captionDisplayStyles CONSTANT)
    Q_PROPERTY(QStringList fontFamilies READ fontFamilies CONSTANT)
    Q_PROPERTY(QString selectedCaptionId READ selectedCaptionId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedCaptionRowId READ selectedCaptionRowId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList effects READ effects CONSTANT)
    Q_PROPERTY(QVariantList trackEffects READ trackEffects NOTIFY projectChanged)
    Q_PROPERTY(QVariantList transitions READ transitions NOTIFY projectChanged)
    Q_PROPERTY(QVariantList transitionDefinitions READ transitionDefinitions CONSTANT)
    Q_PROPERTY(QString selectedTransitionId READ selectedTransitionId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap previewEffects READ previewEffects NOTIFY playheadChanged)
    Q_PROPERTY(QVariantList audioEffects READ audioEffects CONSTANT)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QString projectFilePath READ projectFilePath NOTIFY projectChanged)
    Q_PROPERTY(QStringList recentProjects READ recentProjects NOTIFY projectChanged)
    Q_PROPERTY(QString selectedAssetId READ selectedAssetId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedClipId READ selectedClipId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedTitleId READ selectedTitleId NOTIFY selectionChanged)
    Q_PROPERTY(QUrl previewUrl READ previewUrl NOTIFY selectionChanged)
    Q_PROPERTY(QString sourceDetails READ sourceDetails NOTIFY selectionChanged)
    Q_PROPERTY(QString projectRate READ projectRate NOTIFY projectChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY projectChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY projectChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY projectChanged)
    Q_PROPERTY(bool hasProjectDirectory READ hasProjectDirectory NOTIFY projectChanged)
    Q_PROPERTY(QString rangeStart READ rangeStart NOTIFY selectionChanged)
    Q_PROPERTY(QString rangeEnd READ rangeEnd NOTIFY selectionChanged)
    Q_PROPERTY(bool canSaveRange READ canSaveRange NOTIFY selectionChanged)
    Q_PROPERTY(QString rangeUnit READ rangeUnit NOTIFY selectionChanged)
    Q_PROPERTY(QString frameRule READ frameRule NOTIFY projectChanged)
    Q_PROPERTY(bool frameRuleSatisfied READ frameRuleSatisfied NOTIFY selectionChanged)
    Q_PROPERTY(qint64 playheadFrame READ playheadFrame NOTIFY playheadChanged)
    Q_PROPERTY(bool hasCopiedClip READ hasCopiedClip NOTIFY clipboardChanged)
    Q_PROPERTY(qint64 viewerFrame READ viewerFrame NOTIFY playheadChanged)
    Q_PROPERTY(qint64 timelineDurationFrames READ timelineDurationFrames NOTIFY projectChanged)
    Q_PROPERTY(bool timelinePreviewEnabled READ timelinePreviewEnabled NOTIFY playbackChanged)
    Q_PROPERTY(bool timelinePlaying READ timelinePlaying NOTIFY playbackChanged)
    Q_PROPERTY(bool rangePreviewPlaying READ rangePreviewPlaying NOTIFY playbackChanged)
    Q_PROPERTY(QVariantMap timelineVideo READ timelineVideo NOTIFY playheadChanged)
    Q_PROPERTY(QVariantMap timelineAudio1 READ timelineAudio1 NOTIFY playheadChanged)
    Q_PROPERTY(QVariantMap timelineAudio2 READ timelineAudio2 NOTIFY playheadChanged)
    Q_PROPERTY(QString timelineRangeStart READ timelineRangeStart NOTIFY timelineRangeChanged)
    Q_PROPERTY(QString timelineRangeEnd READ timelineRangeEnd NOTIFY timelineRangeChanged)
    Q_PROPERTY(bool hasTimelineRange READ hasTimelineRange NOTIFY timelineRangeChanged)
    Q_PROPERTY(qint64 timelineRangeLength READ timelineRangeLength NOTIFY timelineRangeChanged)
    Q_PROPERTY(QStringList rangeIncrementOptions READ rangeIncrementOptions NOTIFY rangeIncrementOptionsChanged)
    Q_PROPERTY(QStringList customRangeIncrements READ customRangeIncrements NOTIFY rangeIncrementOptionsChanged)
    Q_PROPERTY(QString rangeIncrementRule READ rangeIncrementRule NOTIFY rangeIncrementOptionsChanged)
    Q_PROPERTY(qint64 previewPositionMs READ previewPositionMs NOTIFY selectionChanged)
    Q_PROPERTY(bool exportRunning READ exportRunning NOTIFY exportChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportChanged)
    Q_PROPERTY(int exportElapsedSeconds READ exportElapsedSeconds NOTIFY exportChanged)
    Q_PROPERTY(int exportRemainingSeconds READ exportRemainingSeconds NOTIFY exportChanged)
    Q_PROPERTY(QString exportStage READ exportStage NOTIFY exportChanged)
    Q_PROPERTY(QString exportOutput READ exportOutput NOTIFY exportChanged)
    Q_PROPERTY(QString exportResolution READ exportResolution NOTIFY exportChanged)
    Q_PROPERTY(QString projectAspectRatio READ projectAspectRatio NOTIFY projectChanged)
    Q_PROPERTY(int canvasWidth READ canvasWidth NOTIFY projectChanged)
    Q_PROPERTY(int canvasHeight READ canvasHeight NOTIFY projectChanged)
    Q_PROPERTY(bool recoveryAvailable READ recoveryAvailable NOTIFY recoveryChanged)
    Q_PROPERTY(QString previewQuality READ previewQuality NOTIFY proxyChanged)
    Q_PROPERTY(bool proxyRunning READ proxyRunning NOTIFY proxyChanged)
    Q_PROPERTY(double proxyProgress READ proxyProgress NOTIFY proxyChanged)
    Q_PROPERTY(QString proxyStage READ proxyStage NOTIFY proxyChanged)

public:
    explicit EditorController(QObject *parent = nullptr);

    [[nodiscard]] QVariantList assets() const;
    [[nodiscard]] QVariantList clips() const;
    [[nodiscard]] QVariantList segments() const;
    [[nodiscard]] QVariantList collections() const;
    [[nodiscard]] QVariantList titles() const;
    [[nodiscard]] QVariantList captionRows() const;
    [[nodiscard]] QVariantList captionDisplayStyles() const;
    [[nodiscard]] qint64 viewerFrame() const { return m_effectPreviewFrame >= 0 ? m_effectPreviewFrame : m_playheadFrame; }
    [[nodiscard]] QVariantList captionCues() const;
    [[nodiscard]] QStringList fontFamilies() const;
    [[nodiscard]] QString selectedCaptionId() const { return m_selectedCaptionId; }
    [[nodiscard]] QString selectedCaptionRowId() const { return m_selectedCaptionRowId; }
    [[nodiscard]] QVariantList effects() const;
    [[nodiscard]] QVariantList trackEffects() const;
    [[nodiscard]] QVariantList transitions() const;
    [[nodiscard]] QVariantList transitionDefinitions() const;
    [[nodiscard]] QString selectedTransitionId() const { return m_selectedTransitionId; }
    [[nodiscard]] QVariantList tracks() const;
    [[nodiscard]] QVariantMap previewEffects() const;
    [[nodiscard]] QVariantList audioEffects() const;
    [[nodiscard]] QString projectName() const { return m_projectName; }
    [[nodiscard]] QString projectFilePath() const { return m_projectFilePath; }
    [[nodiscard]] QStringList recentProjects() const;
    [[nodiscard]] QString selectedAssetId() const { return m_selectedAssetId; }
    [[nodiscard]] QString selectedClipId() const { return m_selectedClipId; }
    [[nodiscard]] QString selectedTitleId() const { return m_selectedTitleId; }
    [[nodiscard]] QUrl previewUrl() const;
    [[nodiscard]] QString sourceDetails() const;
    [[nodiscard]] QString projectRate() const;
    [[nodiscard]] QString status() const { return m_status; }
    [[nodiscard]] bool canUndo() const { return !m_undo.isEmpty(); }
    [[nodiscard]] bool canRedo() const { return !m_redo.isEmpty(); }
    [[nodiscard]] bool dirty() const { return m_dirty; }
    [[nodiscard]] bool hasProjectDirectory() const { return !m_projectFilePath.isEmpty() && !m_demoProject; }
    [[nodiscard]] QString rangeStart() const { return QString::number(m_currentRange.first); }
    [[nodiscard]] QString rangeEnd() const { return QString::number(m_currentRange.end); }
    [[nodiscard]] bool canSaveRange() const;
    [[nodiscard]] QString rangeUnit() const;
    [[nodiscard]] QString frameRule() const { return m_frameRule; }
    [[nodiscard]] bool frameRuleSatisfied() const;
    [[nodiscard]] qint64 playheadFrame() const { return m_playheadFrame; }
    [[nodiscard]] bool hasCopiedClip() const { return m_clipboard.has_value(); }
    [[nodiscard]] qint64 timelineDurationFrames() const;
    [[nodiscard]] bool timelinePreviewEnabled() const { return m_timelinePreviewEnabled; }
    [[nodiscard]] bool timelinePlaying() const { return m_timelinePlaying; }
    [[nodiscard]] bool rangePreviewPlaying() const { return m_rangePreviewPlaying; }
    [[nodiscard]] QVariantMap timelineVideo() const;
    [[nodiscard]] QVariantMap timelineAudio1() const;
    [[nodiscard]] QVariantMap timelineAudio2() const;
    [[nodiscard]] QString timelineRangeStart() const { return QString::number(m_timelineRange.first); }
    [[nodiscard]] QString timelineRangeEnd() const { return QString::number(m_timelineRange.end); }
    [[nodiscard]] bool hasTimelineRange() const { return m_timelineRange.valid(); }
    [[nodiscard]] qint64 timelineRangeLength() const { return m_timelineRange.valid() ? m_timelineRange.length() : -1; }
    [[nodiscard]] QStringList rangeIncrementOptions() const;
    [[nodiscard]] QStringList customRangeIncrements() const { return m_customRangeIncrements; }
    [[nodiscard]] QString rangeIncrementRule() const { return m_rangeIncrementRule; }
    [[nodiscard]] qint64 previewPositionMs() const;
    [[nodiscard]] bool exportRunning() const { return m_exportProcess != nullptr; }
    [[nodiscard]] double exportProgress() const { return m_exportProgress; }
    [[nodiscard]] int exportElapsedSeconds() const;
    [[nodiscard]] int exportRemainingSeconds() const;
    [[nodiscard]] QString exportStage() const { return m_exportStage; }
    [[nodiscard]] QString exportOutput() const { return m_exportOutput; }
    [[nodiscard]] QString exportResolution() const { return m_exportResolution; }
    [[nodiscard]] QString projectAspectRatio() const { return m_projectAspectRatio; }
    [[nodiscard]] int canvasWidth() const { return m_exportWidth; }
    [[nodiscard]] int canvasHeight() const { return m_exportHeight; }
    [[nodiscard]] bool recoveryAvailable() const { return m_recoveryAvailable; }
    [[nodiscard]] QString previewQuality() const { return m_previewQuality; }
    [[nodiscard]] bool proxyRunning() const { return m_proxyProcess != nullptr; }
    [[nodiscard]] double proxyProgress() const { return m_proxyProgress; }
    [[nodiscard]] QString proxyStage() const { return m_proxyStage; }

    Q_INVOKABLE void importMedia(const QUrl &fileUrl);
    Q_INVOKABLE void importMediaAt(const QUrl &fileUrl, qint64 startFrame,
                                   int track, bool snap, qint64 snapToleranceFrames);
    Q_INVOKABLE void relinkAsset(const QString &id, const QUrl &fileUrl);
    Q_INVOKABLE void relinkFolder(const QUrl &folderUrl);
    Q_INVOKABLE void selectAsset(const QString &id);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void selectClip(const QString &id);
    Q_INVOKABLE void selectClipWithoutSeek(const QString &id);
    Q_INVOKABLE void addToTimeline(const QString &assetId);
    Q_INVOKABLE void placeAssetOnTimeline(const QString &assetId,
                                          qint64 startFrame, int track, bool snap,
                                          qint64 snapToleranceFrames);
    Q_INVOKABLE void removeSelectedClip();
    Q_INVOKABLE void rippleDeleteSelectedClip();
    Q_INVOKABLE void copySelectedClip();
    Q_INVOKABLE void cutSelectedClip();
    Q_INVOKABLE void pasteClip();
    Q_INVOKABLE void pasteClipToTrack(int track);
    Q_INVOKABLE bool canPasteClipToTrack(int track) const;
    Q_INVOKABLE void splitSelectedClip();
    Q_INVOKABLE void moveClip(const QString &id, qint64 startFrame, int track,
                             bool snap, qint64 snapToleranceFrames,
                             qint64 dropFrame = -1);
    Q_INVOKABLE QVariantMap previewClipSnap(const QString &id, qint64 startFrame,
                                             int track, bool snap,
                                             qint64 snapToleranceFrames,
                                             qint64 dropFrame) const;
    Q_INVOKABLE QVariantMap previewNewClipSnap(const QString &id, bool savedSegment,
                                               qint64 startFrame, int track,
                                               qint64 snapToleranceFrames) const;
    Q_INVOKABLE void trimClipStart(const QString &id, qint64 startFrame);
    Q_INVOKABLE void trimClipEnd(const QString &id, qint64 endFrame);
    Q_INVOKABLE QVariantMap previewClipTrimSnap(const QString &id, qint64 edgeFrame,
                                                bool startEdge, qint64 toleranceFrames) const;
    Q_INVOKABLE void trimClipStartSnapped(const QString &id, qint64 startFrame,
                                         bool snap, qint64 toleranceFrames);
    Q_INVOKABLE void trimClipEndSnapped(const QString &id, qint64 endFrame,
                                       bool snap, qint64 toleranceFrames);
    Q_INVOKABLE void setClipVisual(const QString &id, int x, int y, int scale,
                                   int rotation, int opacity);
    Q_INVOKABLE void setClipVisualKeyframe(const QString &id, qint64 frame,
                                           int x, int y, int scale);
    Q_INVOKABLE void removeClipVisualKeyframe(const QString &id, qint64 frame);
    Q_INVOKABLE void setVisualPropertyValue(const QString &kind, const QString &id,
                                           const QString &property, qint64 frame, int value);
    Q_INVOKABLE void setVisualPropertyKeyframe(const QString &kind, const QString &id,
                                              const QString &property, qint64 frame);
    Q_INVOKABLE void removeVisualPropertyKeyframe(const QString &kind, const QString &id,
                                                 const QString &property, qint64 frame);
    Q_INVOKABLE void moveVisualPropertyKeyframe(const QString &kind, const QString &id,
                                               const QString &property, qint64 oldFrame,
                                               qint64 newFrame);
    Q_INVOKABLE QVariantMap clipVisualAt(const QString &id, qint64 frame) const;
    Q_INVOKABLE void setClipAudio(const QString &id, int gainPercent,
                                  qint64 fadeInFrames, qint64 fadeOutFrames);
    Q_INVOKABLE int clipGainAt(const QString &id, qint64 timelineFrame) const;
    Q_INVOKABLE void setClipGainAt(const QString &id, qint64 timelineFrame, int gainPercent);
    Q_INVOKABLE void resetClipGain(const QString &id);
    Q_INVOKABLE void setClipSpeed(const QString &id, int speedPercent);
    Q_INVOKABLE void setClipEffect(const QString &id, const QString &effectId, int strength);
    Q_INVOKABLE void addTrackEffect(int track, const QString &effectId);
    Q_INVOKABLE void previewEffect(const QString &effectId, int track);
    Q_INVOKABLE int addTrack(const QString &kind);
    Q_INVOKABLE int trackRow(int track) const;
    Q_INVOKABLE void moveTrackRow(int track, int destinationRow);
    Q_INVOKABLE bool isVideoTrack(int track) const;
    Q_INVOKABLE bool isAudioTrack(int track) const;
    Q_INVOKABLE QVariantMap timelineMediaForTrack(int track) const;
    Q_INVOKABLE QVariantMap previewEffectsForTrack(int track) const;
    Q_INVOKABLE QVariantMap effectTilePreviewSettings(const QString &effectId) const;
    Q_INVOKABLE QVariantMap transitionTargetAt(int track, qint64 frame,
                                               qint64 toleranceFrames) const;
    Q_INVOKABLE QVariantMap transitionPreviewForTrack(int track) const;
    Q_INVOKABLE void addTransition(const QString &definitionId, int track, qint64 frame);
    Q_INVOKABLE void selectTransition(const QString &id);
    Q_INVOKABLE void removeTransition(const QString &id);
    Q_INVOKABLE void setTransitionDuration(const QString &id, qint64 frames);
    Q_INVOKABLE void setTransitionBalance(const QString &id, int balance);
    Q_INVOKABLE void setTransitionParameter(const QString &id,
                                            const QString &parameterId, int value);
    Q_INVOKABLE void removeTrackEffect(int track, const QString &effectId);
    Q_INVOKABLE void setTrackEffectStrength(int track, const QString &effectId, int strength);
    Q_INVOKABLE int trackEffectStrengthAt(int track, const QString &effectId, qint64 frame) const;
    Q_INVOKABLE void setTrackEffectKeyframe(int track, const QString &effectId, qint64 frame, int strength);
    Q_INVOKABLE void setTrackEffectParameter(int track, const QString &effectId,
                                             const QString &parameterId, int value);
    Q_INVOKABLE int trackEffectParameterAt(int track, const QString &effectId,
                                           const QString &parameterId, qint64 frame) const;
    Q_INVOKABLE void resetTrackEffectParameter(int track, const QString &effectId,
                                                const QString &parameterId);
    Q_INVOKABLE void setTrackEffectParameterKeyframe(int track, const QString &effectId,
                                                     const QString &parameterId, qint64 frame, int value);
    Q_INVOKABLE void moveTrackEffectParameterKeyframe(int track, const QString &effectId,
                                                      const QString &parameterId,
                                                      qint64 oldFrame, qint64 newFrame);
    Q_INVOKABLE void removeTrackEffectParameterKeyframe(int track, const QString &effectId,
                                                        const QString &parameterId, qint64 frame);
    Q_INVOKABLE void removeTrackEffectKeyframe(int track, const QString &effectId, qint64 frame);
    Q_INVOKABLE void moveTrackEffectKeyframe(int track, const QString &effectId,
                                             qint64 oldFrame, qint64 newFrame);
    Q_INVOKABLE void setAudioGainPoint(const QString &clipId, qint64 localFrame, int gainPercent);
    Q_INVOKABLE void moveAudioGainPoint(const QString &clipId, qint64 oldFrame, qint64 newFrame, int gainPercent);
    Q_INVOKABLE void removeAudioGainPoint(const QString &clipId, qint64 localFrame);
    Q_INVOKABLE void setClipAudioEffect(const QString &id, const QString &effectId, int strength);
    Q_INVOKABLE qint64 projectFramesToMs(qint64 frames) const;
    Q_INVOKABLE QString projectFrameTimestamp(qint64 frame) const;
    Q_INVOKABLE QString timelineFrameDetails(const QString &clipId,
                                              qint64 projectFrame) const;
    Q_INVOKABLE qint64 seekTimelineFrame(qint64 frame);
    Q_INVOKABLE void scrubTimelineFrame(qint64 frame, bool snap, qint64 toleranceFrames);
    Q_INVOKABLE void toggleTimelinePlayback();
    Q_INVOKABLE void playTimelineRange();
    Q_INVOKABLE void pauseTimelinePlayback();
    Q_INVOKABLE void cancelEffectPreview();
    Q_INVOKABLE qint64 projectMsToFrames(qint64 milliseconds) const;
    Q_INVOKABLE void markTimelineIn();
    Q_INVOKABLE void markTimelineOut();
    Q_INVOKABLE void clearTimelineRange();
    Q_INVOKABLE void setTimelineRange(qint64 firstFrame, qint64 endFrame);
    Q_INVOKABLE void moveTimelineRange(qint64 firstFrame);
    Q_INVOKABLE void setTimelineRangeLength(qint64 frames);
    Q_INVOKABLE void adjustTimelineRangeLength(int direction);
    Q_INVOKABLE void setRangeIncrementRule(const QString &rule);
    Q_INVOKABLE bool addCustomRangeIncrement(const QString &rule);
    Q_INVOKABLE void removeCustomRangeIncrement(const QString &rule);
    Q_INVOKABLE void toggleTrackVisibility(int track);
    Q_INVOKABLE void toggleTrackMute(int track);
    Q_INVOKABLE void markRangeStart(qint64 positionMs);
    Q_INVOKABLE void markRangeEnd(qint64 positionMs);
    Q_INVOKABLE void setRange(qint64 firstFrame, qint64 endFrame);
    Q_INVOKABLE void setFrameRule(const QString &rule);
    Q_INVOKABLE void setProjectFrameRate(qint64 numerator, qint64 denominator);
    Q_INVOKABLE qint64 framesForSeconds(qint64 seconds) const;
    Q_INVOKABLE void saveRangeToCollection(const QString &collectionId,
                                           bool includeVideo, bool includeAudio);
    Q_INVOKABLE QString saveTimelineRangeToCollection(const QString &collectionId, int track);
    Q_INVOKABLE void createCollection(const QString &name);
    Q_INVOKABLE void renameCollection(const QString &id, const QString &name);
    Q_INVOKABLE void removeCollection(const QString &id);
    Q_INVOKABLE void updateSegment(const QString &id, const QString &label,
                                   const QString &tags, const QString &notes, int rating);
    Q_INVOKABLE void updateSegmentDetails(const QString &id, const QString &label,
                                          const QString &tags, const QString &notes,
                                          int rating, const QString &caption);
    Q_INVOKABLE void moveSegments(const QStringList &ids, const QString &collectionId);
    Q_INVOKABLE void copySegments(const QStringList &ids, const QString &collectionId);
    Q_INVOKABLE void removeSegments(const QStringList &ids);
    Q_INVOKABLE void removeCollectionWithSegments(const QString &id);
    Q_INVOKABLE void selectSegment(const QString &id);
    Q_INVOKABLE void addSegmentToTimeline(const QString &id);
    Q_INVOKABLE void placeSegmentOnTimeline(const QString &id, qint64 startFrame,
                                            int track, bool snap, qint64 snapToleranceFrames);
    Q_INVOKABLE void extractSelectedAudio();
    Q_INVOKABLE void addTitle(const QString &text);
    Q_INVOKABLE void addTitleAt(const QString &text, qint64 frame, int track);
    Q_INVOKABLE void placeTitleAt(const QString &text, qint64 frame, int track,
                                 bool snap, qint64 snapToleranceFrames);
    Q_INVOKABLE void moveTitle(const QString &id, qint64 frame, int track,
                              bool snap, qint64 snapToleranceFrames,
                              qint64 dropFrame);
    Q_INVOKABLE QVariantMap previewTitleSnap(const QString &id, qint64 frame,
                                              int track, bool snap,
                                              qint64 snapToleranceFrames,
                                              qint64 dropFrame) const;
    Q_INVOKABLE QVariantMap previewNewTitleSnap(qint64 frame, int track,
                                                 bool snap,
                                                 qint64 snapToleranceFrames) const;
    Q_INVOKABLE QVariantMap previewTitleTrimSnap(const QString &id, qint64 edgeFrame,
                                                 bool startEdge, qint64 toleranceFrames) const;
    Q_INVOKABLE void trimTitleStartSnapped(const QString &id, qint64 startFrame,
                                          bool snap, qint64 toleranceFrames);
    Q_INVOKABLE void trimTitleEndSnapped(const QString &id, qint64 endFrame,
                                        bool snap, qint64 toleranceFrames);
    Q_INVOKABLE void startAutoCaption();
    Q_INVOKABLE QVariantMap selectedVoiceSource() const;
    Q_INVOKABLE void applyVoiceChange(const QString &clipId, const QString &path,
                                      const QString &voiceName, const QString &mode);
    Q_INVOKABLE void setVoiceChangeEnabled(const QString &clipId, bool enabled);
    Q_INVOKABLE void acceptGeneratedCaptions(const QString &clipId, const QVariantList &words);
    Q_INVOKABLE void selectCaption(const QString &cueId);
    Q_INVOKABLE void selectCaptionRow(const QString &rowId);
    Q_INVOKABLE void selectTrackRow(int track);
    Q_INVOKABLE void setCaptionMode(const QString &rowId, const QString &mode);
    Q_INVOKABLE void setCaptionMaxWords(const QString &rowId, int maxWords);
    Q_INVOKABLE void previewCaptionStyle(const QString &rowId, const QString &mode);
    Q_INVOKABLE void toggleCaptionVisibility(const QString &rowId);
    Q_INVOKABLE void updateCaptionStyle(const QString &rowId, const QString &fontFamily,
                                         int fontPixels, bool bold, bool italic,
                                         const QString &color, const QString &highlightColor,
                                         const QString &backgroundColor, int backgroundOpacity,
                                         int positionX, int positionY);
    Q_INVOKABLE QVariantMap captionVisualAt(const QString &rowId, qint64 frame) const;
    Q_INVOKABLE void setCaptionVisual(const QString &rowId, int x, int y, int scale);
    Q_INVOKABLE void setCaptionVisualKeyframe(const QString &rowId, qint64 frame,
                                              int x, int y, int scale);
    Q_INVOKABLE void removeCaptionVisualKeyframe(const QString &rowId, qint64 frame);
    Q_INVOKABLE void updateCaptionWord(const QString &cueId, const QString &wordId,
                                        const QString &text, qint64 firstFrame,
                                        qint64 endFrame);
    Q_INVOKABLE void insertCaptionWord(const QString &cueId, const QString &afterWordId,
                                      const QString &text);
    Q_INVOKABLE void removeCaptionWord(const QString &cueId, const QString &wordId);
    Q_INVOKABLE void removeSelectedCaption();
    Q_INVOKABLE QVariantMap captionDisplayForRow(const QString &rowId, qint64 frame) const;
    Q_INVOKABLE void selectTitle(const QString &id);
    Q_INVOKABLE void updateTitle(const QString &id, const QString &text,
                                 qint64 startFrame, qint64 durationFrames,
                                 int fontPixels, const QString &color);
    Q_INVOKABLE void updateTitleStyle(const QString &id, const QString &fontFamily,
        bool bold, bool italic, const QString &backgroundColor, int backgroundOpacity,
        int positionX, int positionY, int anchorX, int anchorY);
    Q_INVOKABLE void setTitleVisual(const QString &id, int x, int y, int scale);
    Q_INVOKABLE void setTitleVisualKeyframe(const QString &id, qint64 frame,
                                            int x, int y, int scale);
    Q_INVOKABLE void removeTitleVisualKeyframe(const QString &id, qint64 frame);
    Q_INVOKABLE QVariantMap titleVisualAt(const QString &id, qint64 frame) const;
    Q_INVOKABLE void removeSelectedTitle();
    Q_INVOKABLE void removeSegment(const QString &id);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void saveProject(const QUrl &directoryUrl);
    Q_INVOKABLE void openProject(const QUrl &directoryUrl);
    Q_INVOKABLE void openProjectLocation() const;
    Q_INVOKABLE void recoverProject();
    Q_INVOKABLE void discardRecovery();
    Q_INVOKABLE void openDemoProject();
    Q_INVOKABLE void startExport(const QString &kind, const QUrl &outputUrl);
    Q_INVOKABLE void startFrameExport(qint64 projectFrame, const QUrl &outputUrl);
    Q_INVOKABLE void exportCollection(const QString &collectionId, const QUrl &destinationFolder);
    Q_INVOKABLE void cancelExport();
    Q_INVOKABLE void setExportResolution(const QString &resolution);
    Q_INVOKABLE void setProjectAspectRatio(const QString &ratio);
    Q_INVOKABLE void setPreviewQuality(const QString &quality);
    Q_INVOKABLE void startProxyForSelected();
    Q_INVOKABLE void cancelProxy();
    Q_INVOKABLE void newProject();

signals:
    void clipboardChanged();
    void projectChanged();
    void autoCaptionRequested(const QString &clipId, const QString &sourcePath,
                              double sourceStartSeconds, double sourceDurationSeconds);
    void selectionChanged();
    void statusChanged();
    void playheadChanged();
    void playbackChanged();
    void exportChanged();
    void recoveryChanged();
    void proxyChanged();
    void timelineRangeChanged();
    void rangeIncrementOptionsChanged();

private:
    void editVisualProperty(const QString &kind, const QString &id,
                            const QString &property, qint64 frame,
                            qint64 destinationFrame, int value, int operation);
    [[nodiscard]] const Asset *findAsset(const QString &id) const;
    void setStatus(const QString &message);
    void queueThumbnail(const QString &assetId, const QString &mediaPath);
    void queueTransitionFrames();
    void queueWaveform(const QString &assetId, const QString &mediaPath);
    void queueFilmstrip(const QString &assetId, const QString &mediaPath, qint64 durationUs);
    [[nodiscard]] QByteArray projectContents(const QString &directory) const;
    void openProjectFile(const QString &directory, const QString &fileName, bool recovered);
    void autosave();
    void recordRecentProject(const QString &path);

    struct Edit {
        enum Kind { Add, Remove, AddSegment, RemoveSegment, ReplaceClip, SplitClip,
                    AddCollection, RemoveCollection, RenameCollection, ReplaceSegment,
                    BatchSegments, RemoveCollectionWithSegments,
                    ExtractAudio, RippleDelete, ChangeProjectRate,
                    AddTitle, RemoveTitle, ReplaceTitle, ReplaceTrackEffects,
                    AddVideoTrack, AddAudioTrack, SetTrackVisibility, SetTrackMute,
                    ReplaceTransitions, ReplaceCaptions, ReorderTracks,
                    PlaceTimelineItems } kind;
        Clip clip;
        Clip extraClip;
        Segment segment;
        qsizetype index;
        Segment extraSegment;
        Collection collection;
        Collection extraCollection;
        QVector<Clip> affectedClips;
        QVector<Clip> resultingClips;
        FrameRate previousRate;
        FrameRate resultingRate;
        qint64 previousPlayhead = 0;
        qint64 resultingPlayhead = 0;
        TitleClip title;
        TitleClip extraTitle;
        QVector<TitleClip> previousTitles;
        QVector<TitleClip> resultingTitles;
        QVector<CaptionRow> previousCaptionRows;
        QVector<CaptionRow> resultingCaptionRows;
        QVector<int> previousTrackOrder;
        QVector<int> resultingTrackOrder;
        struct IndexedSegment { Segment segment; qsizetype index = 0; };
        QVector<IndexedSegment> previousSegments;
        QVector<IndexedSegment> resultingSegments;
        QVector<TrackEffect> previousTrackEffects;
        QVector<TrackEffect> resultingTrackEffects;
        QVector<ClipTransition> previousTransitions;
        QVector<ClipTransition> resultingTransitions;
        int addedTrack = -1;
        int stateTrack = -1;
        bool previousTrackState = false;
        bool resultingTrackState = false;
    };
    void applyEdit(const Edit &edit, bool forward);
    void replaceClip(const Clip &before, const Clip &after, const QString &message);
    struct SnapPlacement {
        qint64 startFrame = 0;
        qint64 guideFrame = -1;
    };
    [[nodiscard]] SnapPlacement snapPlacement(qint64 startFrame, qint64 duration,
                                               int track, qint64 toleranceFrames,
                                               const QString &excludeId = {}) const;
    [[nodiscard]] qint64 snappedStartFrame(qint64 startFrame, qint64 duration,
                                            int track, qint64 toleranceFrames,
                                            const QString &excludeId = {}) const;
    struct TimelinePlacement {
        qint64 startFrame = 0;
        qint64 shiftFrom = -1;
        qint64 shiftBy = 0;
    };
    [[nodiscard]] TimelinePlacement timelinePlacement(qint64 startFrame,
        qint64 duration, int track, const QString &excludeId,
        qint64 dropFrame) const;
    void applyTimelinePlacement(const TimelinePlacement &placement, int track,
        const QString &excludeId, Edit &edit);
    void shiftCaptionsWithClip(const QString &clipId, qint64 frames, int track,
                               Edit &edit);
    void finishTimelinePlacement(Edit edit);
    [[nodiscard]] SnapPlacement snapTrimEdge(const QString &id, qint64 edgeFrame,
                                              bool startEdge, qint64 toleranceFrames) const;
    [[nodiscard]] const Clip *clipAtFrame(qint64 frame, int track) const;
    [[nodiscard]] int resolveNewTrack(int track);
    [[nodiscard]] QVariantMap timelineMedia(int track) const;
    void advanceTimelinePlayback();
    void readExportProgress();
    void launchExport(QStringList args, const QString &output, qint64 expectedUs,
                      bool directoryOutput = false);
    void startImageSequenceExport(const QUrl &destinationFolder);
    void startProjectExport(const QUrl &outputUrl, bool selectedRange = false,
                            std::optional<FrameRange> explicitRange = std::nullopt,
                            bool stillFrame = false);
    void startProjectAudioExport(const QUrl &outputUrl);
    void launchNextCollectionExport();

    struct CollectionExportJob {
        QStringList args;
        QString output;
        qint64 expectedUs = 0;
    };

    QVector<Asset> m_assets;
    QVector<Clip> m_clips;
    QVector<TrackEffect> m_trackEffects;
    QVector<ClipTransition> m_transitions;
    TransitionRegistry m_transitionRegistry;
    QThreadPool m_transitionFramePool;
    QSet<QString> m_pendingTransitionFrames;
    QVector<int> m_videoTracks{0, 3}; // bottom to top; legacy IDs remain stable
    QVector<int> m_audioTracks{1, 2}; // top to bottom
    QVector<int> m_trackOrder; // top to bottom; empty uses the default layout
    QVector<Segment> m_segments;
    QVector<Collection> m_collections;
    QVector<TitleClip> m_titles;
    QVector<CaptionRow> m_captionRows;
    QString m_projectName = QStringLiteral("Untitled Project");
    QString m_projectDirectory;
    QString m_projectFilePath;
    QString m_selectedAssetId;
    QString m_selectedClipId;
    QString m_selectedTitleId;
    QString m_selectedCaptionId;
    QString m_selectedCaptionRowId;
    QString m_selectedTransitionId;
    QString m_status = QStringLiteral("Ready");
    FrameRange m_currentRange;
    FrameRange m_timelineRange;
    QSet<int> m_hiddenVideoTracks;
    QSet<int> m_mutedTracks;
    QStringList m_customRangeIncrements;
    QString m_rangeIncrementRule = QStringLiteral("1");
    QString m_frameRule = QStringLiteral("any");
    FrameRate m_projectRate{30, 1};
    qint64 m_playheadFrame = 0;
    bool m_timelinePreviewEnabled = false;
    bool m_timelinePlaying = false;
    bool m_rangePreviewPlaying = false;
    qint64 m_playbackStopFrame = -1;
    qint64 m_playbackAnchorFrame = 0;
    QElapsedTimer m_playbackClock;
    QTimer m_playbackTick;
    QTimer m_effectPreviewTimer;
    QString m_previewEffectId;
    QString m_previewCaptionRowId;
    QString m_previewCaptionMode;
    int m_previewEffectTrack = -1;
    qint64 m_effectPreviewFrame = -1;
    QVector<Edit> m_undo;
    QVector<Edit> m_redo;
    std::optional<Clip> m_clipboard;
    std::uint64_t m_projectGeneration = 0;
    bool m_dirty = false;
    QProcess *m_exportProcess = nullptr;
    QByteArray m_exportReadBuffer;
    QString m_exportError;
    QString m_exportStage;
    QString m_exportOutput;
    QString m_exportTempOutput;
    QString m_exportGraphPath;
    QStringList m_exportAuxiliaryPaths;
    double m_exportProgress = 0.0;
    QElapsedTimer m_exportClock;
    QTimer m_exportTick;
    qint64 m_exportExpectedUs = 0;
    qint64 m_collectionExportTotalUs = 0;
    qint64 m_collectionExportCompletedUs = 0;
    bool m_exportDirectory = false;
    QByteArray m_exportManifest;
    QVector<CollectionExportJob> m_collectionExportJobs;
    int m_collectionExportIndex = 0;
    QString m_collectionExportFolder;
    QByteArray m_collectionExportManifest;
    QString m_exportResolution = QStringLiteral("1080p");
    QString m_projectAspectRatio = QStringLiteral("16:9");
    int m_exportWidth = 1920;
    int m_exportHeight = 1080;
    QTimer m_autosaveTimer;
    QTimer m_autosaveDebounce;
    std::uint64_t m_editRevision = 0;
    std::uint64_t m_manualSaveSerial = 0;
    bool m_autoNamedProject = false;
    bool m_autosavePending = false;
    bool m_recoveryAvailable = false;
    bool m_demoProject = false;
    QString m_previewQuality = QStringLiteral("Full");
    QProcess *m_proxyProcess = nullptr;
    QString m_proxyAssetId;
    QString m_proxyOutput;
    QString m_proxyTempOutput;
    QByteArray m_proxyReadBuffer;
    double m_proxyProgress = 0.0;
    QString m_proxyStage;
};

} // namespace datacut
