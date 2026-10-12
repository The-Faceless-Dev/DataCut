#pragma once

#include <QString>
#include "core/FrameTime.h"

namespace datacut {

// Thumbnails are disposable cache files keyed by source path, size, and mtime.
QString thumbnailCachePath(const QString &mediaPath);
QString generateThumbnail(const QString &mediaPath);
QString transitionFrameCachePath(const QString &mediaPath, qint64 sourceFrame,
                                 FrameRate sourceRate);
QString generateTransitionFrame(const QString &mediaPath, qint64 sourceFrame,
                                FrameRate sourceRate);

} // namespace datacut
