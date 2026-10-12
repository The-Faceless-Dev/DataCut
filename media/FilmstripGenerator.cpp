#include "media/FilmstripGenerator.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace datacut {

QStringList generateFilmstrip(const QString &mediaPath, qint64 durationUs)
{
    if (durationUs <= 0) return {};
    const QFileInfo info(mediaPath);
    const QByteArray identity = info.absoluteFilePath().toUtf8() + '|'
        + QByteArray::number(info.size()) + '|'
        + QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    const QString directory = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("filmstrips/") + key);
    QStringList frames;
    for (int i = 1; i <= 8; ++i)
        frames.append(QDir(directory).filePath(QStringLiteral("frame%1.jpg").arg(i, 2, 10, QLatin1Char('0'))));
    if (QFileInfo::exists(frames.last())) return frames;
    if (!QDir().mkpath(directory)) return {};
    const QString ffmpeg = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("ffmpeg.exe"));
    const QString rate = QStringLiteral("8000000/%1").arg(durationUs);
    QProcess process;
    process.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"),
        QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
        QStringLiteral("-i"), mediaPath, QStringLiteral("-vf"),
        QStringLiteral("fps=%1,scale=240:-2").arg(rate),
        QStringLiteral("-frames:v"), QStringLiteral("8"),
        QStringLiteral("-q:v"), QStringLiteral("5"),
        QDir(directory).filePath(QStringLiteral("frame%02d.jpg"))});
    if (!process.waitForStarted(5000) || !process.waitForFinished(60000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};
    QStringList available;
    for (const auto &frame : frames)
        if (QFileInfo::exists(frame)) available.append(frame);
    return available;
}

} // namespace datacut
