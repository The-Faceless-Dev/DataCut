#pragma once

#include "core/FrameTime.h"

#include <QString>

#include <cstdint>

namespace datacut {

struct MediaInfo {
    QString path;
    QString name;
    FrameRate frameRate;
    std::int64_t frameCount = 0;
    std::int64_t durationUs = 0;
    int width = 0;
    int height = 0;
    bool hasVideo = false;
    bool hasAudio = false;
    bool isStillImage = false;
    int audioSampleRate = 0;
};

MediaInfo probeMedia(const QString &path);

} // namespace datacut
