#pragma once

#include <QString>
#include <QStringList>

namespace datacut {
QStringList generateFilmstrip(const QString &mediaPath, qint64 durationUs);
}
