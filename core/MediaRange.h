#pragma once

#include "core/FrameTime.h"

#include <QString>
#include <QStringList>

namespace datacut {

// A selection refers to source media. The units are video frames for video
// sources and PCM samples for audio-only sources. It does not mutate media.
struct MediaRange {
    QString assetId;
    FrameRange frames;
    bool includeVideo = true;
    bool includeAudio = true;
};

struct Segment {
    QString id;
    QString label;
    MediaRange source;
    QString collectionId;
    QStringList tags;
    QString notes;
    int rating = 0;
    QString caption;
    std::int64_t timelineFrames = 0; // Exact project-frame length when captured from a timeline range.
    FrameRate timelineRate{0, 0};
    int speedPercent = 100;
};

struct Collection {
    QString id;
    QString name;
};

} // namespace datacut
