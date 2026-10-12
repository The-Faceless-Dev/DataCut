#include "app/GenerationManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace datacut {
namespace {
QSettings generationSettings()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(directory);
    return QSettings(QDir(directory).filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
}

QString processError(const QByteArray &diagnostics, const QString &fallback)
{
    const QString detail = QString::fromUtf8(diagnostics.right(1600)).trimmed();
    return detail.isEmpty() ? fallback : detail;
}
}

GenerationManager::GenerationManager(QObject *parent) : QObject(parent)
{
    m_jobTick.setInterval(1000);
    connect(&m_jobTick, &QTimer::timeout, this, [this] {
        if (m_asrProcess) {
            const double expectedMs = std::max(20000.0, m_expectedSeconds * 650.0);
            const double fraction = 0.2 + 0.73 * (1.0 - std::exp(-m_jobClock.elapsed() / expectedMs));
            setStage(QStringLiteral("Transcribing with Parakeet · estimated"),
                     std::max(m_progress, fraction));
        } else emit jobChanged();
    });
    const QString defaultRoot = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("generations"));
    m_rootPath = generationSettings().value(QStringLiteral("generations/root"), defaultRoot)
        .toString();
}

GenerationManager::~GenerationManager()
{
    for (QProcess *process : {m_setupProcess, m_mediaProcess, m_asrProcess}) {
        if (process && process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(2000);
        }
    }
    if (!m_wavPath.isEmpty()) QFile::remove(m_wavPath);
}

QString GenerationManager::runtimeExe() const
{
    return QDir(m_rootPath).filePath(QStringLiteral("parakeet-tdt-v3/runtime/bin/nemo-speech.exe"));
}

QString GenerationManager::modelFile() const
{
    return QDir(m_rootPath).filePath(
        QStringLiteral("parakeet-tdt-v3/models/parakeet-tdt-0.6b-v3.q8_0.gguf"));
}

bool GenerationManager::localReady() const
{
    return QFileInfo::exists(runtimeExe()) && QFileInfo(modelFile()).size() > 700000000;
}

void GenerationManager::setRootPath(const QString &path)
{
    if (setupRunning() || captionRunning()) return;
    const QString cleaned = QDir::fromNativeSeparators(path.trimmed());
    if (cleaned.isEmpty() || !QDir::isAbsolutePath(cleaned)) {
        m_error = QStringLiteral("Choose an absolute folder path for generations.");
        emit jobChanged();
        return;
    }
    m_rootPath = QDir(cleaned).absolutePath();
    generationSettings().setValue(QStringLiteral("generations/root"), m_rootPath);
    m_error.clear();
    emit settingsChanged();
    emit jobChanged();
}

void GenerationManager::setStage(const QString &stage, double progress)
{
    m_stage = stage;
    if (progress >= 0) {
        m_progressKnown = true;
        m_progress = std::clamp(progress, 0.0, 1.0);
    }
    emit jobChanged();
}

void GenerationManager::setupLocal()
{
    if (setupRunning() || captionRunning()) return;
    m_cancelRequested = false;
    const QString script = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("scripts/runtime/setup_parakeet.ps1"));
    if (!QFileInfo::exists(script)) {
        m_error = QStringLiteral("Setup script is missing from the DataCut folder.");
        emit jobChanged();
        return;
    }
    m_error.clear();
    m_diagnostics.clear();
    m_setupLineBuffer.clear();
    auto *process = new QProcess(this);
    m_setupProcess = process;
    process->setProgram(QStringLiteral("powershell.exe"));
    process->setArguments({QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
        QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
        QStringLiteral("-File"), script, QStringLiteral("-Root"), m_rootPath});
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        if (m_setupProcess != process) return;
        const QByteArray incoming = process->readAllStandardOutput();
        m_diagnostics = (m_diagnostics + incoming).right(6000);
        m_setupLineBuffer += incoming;
        while (true) {
            const qsizetype newline = m_setupLineBuffer.indexOf('\n');
            if (newline < 0) break;
            const QByteArray line = m_setupLineBuffer.left(newline).trimmed();
            m_setupLineBuffer.remove(0, newline + 1);
            if (line.startsWith("STAGE:"))
                setStage(QString::fromUtf8(line.mid(6)).trimmed());
            else if (line.startsWith("PROGRESS:")) {
                bool valid = false;
                const double value = line.mid(9).trimmed().toDouble(&valid);
                if (valid) setStage(m_stage, value);
            }
        }
    });
    connect(process, &QProcess::finished, this,
        [this, process](int exitCode, QProcess::ExitStatus status) {
            if (m_setupProcess != process) return;
            const QString failure = processError(m_diagnostics + process->readAllStandardOutput(),
                QStringLiteral("Parakeet setup failed. Check network access and disk space."));
            m_setupProcess = nullptr;
            m_jobTick.stop();
            process->deleteLater();
            if (m_cancelRequested) {
                m_error = QStringLiteral("Setup canceled");
                setStage(QStringLiteral("Canceled"), 0);
            } else if (status == QProcess::NormalExit && exitCode == 0 && localReady()) {
                m_error.clear();
                setStage(QStringLiteral("Parakeet is ready"), 1);
            } else {
                m_error = failure;
                setStage(QStringLiteral("Setup failed"), 0);
            }
            emit settingsChanged();
        });
    connect(process, &QProcess::errorOccurred, this,
        [this, process](QProcess::ProcessError error) {
            if (m_setupProcess != process || error != QProcess::FailedToStart) return;
            m_setupProcess = nullptr;
            m_jobTick.stop();
            m_error = process->errorString();
            process->deleteLater();
            setStage(QStringLiteral("Setup failed"), 0);
            emit settingsChanged();
    });
    m_jobClock.start();
    m_jobTick.start();
    setStage(QStringLiteral("Preparing local runtime"), 0);
    process->start();
}

void GenerationManager::transcribeClip(const QString &clipId, const QString &sourcePath,
                                       double sourceStartSeconds, double sourceDurationSeconds)
{
    if (setupRunning() || captionRunning()) return;
    m_cancelRequested = false;
    if (!localReady()) {
        m_error = QStringLiteral("Set up Parakeet in Settings > Generative first.");
        emit jobChanged();
        return;
    }
    const QString ffmpegInApp = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("ffmpeg.exe"));
    const QString ffmpeg = QFileInfo::exists(ffmpegInApp) ? ffmpegInApp
        : QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    const QString cache = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("generations"));
    if (ffmpeg.isEmpty() || !QDir().mkpath(cache)) {
        m_error = QStringLiteral("FFmpeg or the generations cache is unavailable.");
        emit jobChanged();
        return;
    }
    m_error.clear();
    m_output.clear();
    m_diagnostics.clear();
    m_clipId = clipId;
    m_expectedSeconds = sourceDurationSeconds;
    m_wavPath = QDir(cache).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)
                                    + QStringLiteral(".wav"));
    m_jobClock.start();
    m_jobTick.start();

    auto *process = new QProcess(this);
    m_mediaProcess = process;
    process->setProgram(ffmpeg);
    process->setArguments({QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"),
        QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-progress"), QStringLiteral("pipe:1"),
        QStringLiteral("-ss"), QString::number(sourceStartSeconds, 'f', 9),
        QStringLiteral("-t"), QString::number(sourceDurationSeconds, 'f', 9),
        QStringLiteral("-i"), sourcePath, QStringLiteral("-vn"),
        QStringLiteral("-ar"), QStringLiteral("16000"),
        QStringLiteral("-ac"), QStringLiteral("1"),
        QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), m_wavPath});
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        if (m_mediaProcess != process) return;
        const QByteArray output = process->readAllStandardOutput();
        const QList<QByteArray> lines = output.split('\n');
        for (const QByteArray &line : lines) {
            if (!line.startsWith("out_time_us=") || m_expectedSeconds <= 0) continue;
            bool ok = false;
            const double seconds = line.mid(12).trimmed().toDouble(&ok) / 1000000.0;
            if (ok) setStage(QStringLiteral("Preparing clip audio"),
                             std::clamp(seconds / m_expectedSeconds * 0.2, 0.0, 0.2));
        }
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process] {
        if (m_mediaProcess == process)
            m_diagnostics = (m_diagnostics + process->readAllStandardError()).right(3000);
    });
    connect(process, &QProcess::finished, this,
        [this, process](int exitCode, QProcess::ExitStatus status) {
            if (m_mediaProcess != process) return;
            m_mediaProcess = nullptr;
            process->deleteLater();
            if (m_cancelRequested) finishJob(QStringLiteral("Canceled"));
            else if (status == QProcess::NormalExit && exitCode == 0 && QFileInfo::exists(m_wavPath))
                startRecognition();
            else finishJob(processError(m_diagnostics,
                QStringLiteral("Could not extract audio from the selected clip.")));
        });
    connect(process, &QProcess::errorOccurred, this,
        [this, process](QProcess::ProcessError error) {
            if (m_mediaProcess != process || error != QProcess::FailedToStart) return;
            m_mediaProcess = nullptr;
            const QString detail = process->errorString();
            process->deleteLater();
            finishJob(QStringLiteral("Could not start FFmpeg: %1").arg(detail));
        });
    setStage(QStringLiteral("Preparing clip audio"), 0);
    process->start();
}

void GenerationManager::startRecognition()
{
    m_output.clear();
    m_diagnostics.clear();
    auto *process = new QProcess(this);
    m_asrProcess = process;
    process->setProgram(runtimeExe());
    process->setArguments({QStringLiteral("--json"), QStringLiteral("transcribe"),
        m_wavPath, QStringLiteral("--model"), modelFile(),
        QStringLiteral("--device"), QStringLiteral("cpu")});
    process->setWorkingDirectory(QFileInfo(runtimeExe()).absolutePath());
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        if (m_asrProcess == process) m_output += process->readAllStandardOutput();
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process] {
        if (m_asrProcess == process)
            m_diagnostics = (m_diagnostics + process->readAllStandardError()).right(3000);
    });
    connect(process, &QProcess::finished, this,
        [this, process](int exitCode, QProcess::ExitStatus status) {
            if (m_asrProcess != process) return;
            m_output += process->readAllStandardOutput();
            m_diagnostics += process->readAllStandardError();
            m_asrProcess = nullptr;
            process->deleteLater();
            if (m_cancelRequested) { finishJob(QStringLiteral("Canceled")); return; }
            if (status != QProcess::NormalExit || exitCode != 0) {
                finishJob(processError(m_diagnostics,
                    QStringLiteral("Parakeet could not transcribe this clip.")));
                return;
            }
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(m_output, &parseError);
            const QJsonArray sourceWords = document.object().value(QStringLiteral("words")).toArray();
            QVariantList words;
            for (const QJsonValue &value : sourceWords) {
                const QJsonObject word = value.toObject();
                const QString text = word.value(QStringLiteral("word")).toString().trimmed();
                const double start = word.value(QStringLiteral("start")).toDouble(-1);
                const double end = word.value(QStringLiteral("end")).toDouble(-1);
                if (!text.isEmpty() && start >= 0 && end > start)
                    words.append(QVariantMap{{"text", text}, {"start", start}, {"end", end}});
            }
            if (parseError.error != QJsonParseError::NoError || words.isEmpty()) {
                finishJob(QStringLiteral("Parakeet returned no usable word timestamps."));
                return;
            }
            const QString clipId = m_clipId;
            finishJob();
            emit captionWordsReady(clipId, words);
        });
    connect(process, &QProcess::errorOccurred, this,
        [this, process](QProcess::ProcessError error) {
            if (m_asrProcess != process || error != QProcess::FailedToStart) return;
            m_asrProcess = nullptr;
            const QString detail = process->errorString();
            process->deleteLater();
            finishJob(QStringLiteral("Could not start Parakeet: %1").arg(detail));
        });
    setStage(QStringLiteral("Transcribing with Parakeet · estimated"), 0.2);
    process->start();
}

void GenerationManager::finishJob(const QString &error)
{
    m_jobTick.stop();
    if (!m_wavPath.isEmpty()) QFile::remove(m_wavPath);
    m_wavPath.clear();
    m_error = error;
    setStage(error.isEmpty() ? QStringLiteral("Captions ready")
                             : QStringLiteral("Auto caption failed"), error.isEmpty() ? 1 : 0);
}

void GenerationManager::cancelJob()
{
    m_cancelRequested = true;
    for (QProcess *process : {m_setupProcess, m_mediaProcess, m_asrProcess})
        if (process && process->state() != QProcess::NotRunning) process->kill();
    m_error = QStringLiteral("Canceled");
    setStage(QStringLiteral("Canceling"));
}

} // namespace datacut
