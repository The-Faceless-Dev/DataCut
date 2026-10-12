#include "media/WaveformGenerator.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace datacut {

QString generateWaveform(const QString &mediaPath)
{
    const QFileInfo info(mediaPath);
    const QByteArray identity = info.absoluteFilePath().toUtf8() + '|'
        + QByteArray::number(info.size()) + '|'
        + QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    const auto key = QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex();
    const QString output = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("waveforms/") + QString::fromLatin1(key) + QStringLiteral(".png"));
    if (QFileInfo::exists(output)) return output;
    QDir().mkpath(QFileInfo(output).absolutePath());
    const QString ffmpeg = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("ffmpeg.exe"));
    QProcess process;
    process.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"),
        QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
        QStringLiteral("-i"), mediaPath, QStringLiteral("-filter_complex"),
        QStringLiteral("[0:a:0]aformat=channel_layouts=mono,showwavespic=s=1600x80:colors=0x7ce7db[v]"),
        QStringLiteral("-map"), QStringLiteral("[v]"), QStringLiteral("-frames:v"),
        QStringLiteral("1"), output});
    if (!process.waitForStarted(5000) || !process.waitForFinished(60000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QFile::remove(output);
        return {};
    }
    return QFileInfo::exists(output) ? output : QString{};
}

} // namespace datacut
