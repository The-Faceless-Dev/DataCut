#include "media/ProxyCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace datacut {

QString proxyCachePath(const QString &mediaPath)
{
    const QFileInfo info(mediaPath);
    const QByteArray identity = info.absoluteFilePath().toUtf8() + '|'
        + QByteArray::number(info.size()) + '|'
        + QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("proxies/") + key + QStringLiteral(".mp4"));
}

} // namespace datacut
