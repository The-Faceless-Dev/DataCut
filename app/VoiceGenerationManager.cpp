#include "app/VoiceGenerationManager.h"
#include "media/MediaProbe.h"

#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <cmath>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace datacut {
namespace {
QSettings settings()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(directory);
    return {QDir(directory).filePath(QStringLiteral("settings.ini")), QSettings::IniFormat};
}

QString rootDirectory()
{
    return settings().value(QStringLiteral("generations/root"),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("generations"))).toString();
}

QString voiceFolder() { return QDir(rootDirectory()).filePath(QStringLiteral("voices")); }

bool withinRuntime(const QString &path, const QString &runtime)
{
    const QString base = QDir(runtime).absolutePath() + QLatin1Char('/');
    return QFileInfo(path).absoluteFilePath().startsWith(base, Qt::CaseInsensitive);
}

QString referenceSpanError(const QString &audio, double start, double duration)
{
    if (!std::isfinite(start) || !std::isfinite(duration) || start < 0
        || duration < 1 || duration > 25)
        return QStringLiteral("Choose a target excerpt between 1 and 25 seconds.");
    try {
        const MediaInfo info = probeMedia(audio);
        if (!info.hasAudio || info.durationUs <= 0)
            return QStringLiteral("The target recording has no readable audio duration.");
        const double available = double(info.durationUs) / 1000000.0;
        if (start + duration > available + 0.02)
            return QStringLiteral("The target excerpt extends past the end of the recording.");
    } catch (const std::exception &) {
        return QStringLiteral("Could not read the target recording.");
    }
    return {};
}
}

VoiceGenerationManager::VoiceGenerationManager(QObject *parent) : QObject(parent)
{
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        if (m_process && m_progress < 0.95) {
            const double expectedMs = m_setupJob ? 180000.0
                : std::max(20000.0, m_expectedConversionSeconds * 1000.0);
            m_progress = std::max(m_progress,
                0.05 + 0.88 * (1.0 - std::exp(-m_clock.elapsed() / expectedMs)));
            if (!m_stage.contains(QStringLiteral("estimated")))
                m_stage += QStringLiteral(" · estimated");
        }
        emit changed();
    });
#ifdef Q_OS_WIN
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            m_jobHandle = job;
        else CloseHandle(job);
    }
    if (!m_jobHandle) m_error = QStringLiteral("Voice worker cleanup is unavailable on this machine.");
#endif
}

VoiceGenerationManager::~VoiceGenerationManager()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(2000);
    }
#ifdef Q_OS_WIN
    if (m_jobHandle) CloseHandle(static_cast<HANDLE>(m_jobHandle));
#endif
}

QString VoiceGenerationManager::outputPath() const
{
    return settings().value(QStringLiteral("generations/voice/output"),
        QDir(rootDirectory()).filePath(QStringLiteral("results/voice"))).toString();
}

QString VoiceGenerationManager::seedvcRuntimePath() const
{
    const QString fallback = QDir(rootDirectory()).filePath(QStringLiteral("runtimes/seedvc"));
    const QString configured = settings().value(QStringLiteral("generations/voice/seedvcRuntime")).toString();
    return configured.isEmpty() ? fallback : configured;
}

bool VoiceGenerationManager::seedvcReady() const
{
    return seedvcIssue().isEmpty();
}

QString VoiceGenerationManager::seedvcStatus() const
{
    const QString issue = seedvcIssue();
    return issue.isEmpty() ? QStringLiteral("Runtime ready") : issue;
}

QString VoiceGenerationManager::seedvcIssue() const
{
    const QDir root(seedvcRuntimePath());
    if (!root.exists()) return QStringLiteral("Runtime folder not found. Run setup.");
    if (!QFileInfo::exists(root.filePath(QStringLiteral(".venv/Scripts/python.exe"))))
        return QStringLiteral("Isolated Python is missing. Run setup.");
    if (!QFileInfo::exists(root.filePath(QStringLiteral("seed_vc_wrapper.py"))))
        return QStringLiteral("SeedVC source is missing. Run setup.");
    if (!QFileInfo::exists(root.filePath(QStringLiteral("native/ready.json")))
        || !QFileInfo::exists(root.filePath(QStringLiteral("native/vcruntime140.dll")))
        || !QFileInfo::exists(root.filePath(QStringLiteral(".venv/Lib/site-packages/datacut_native.py")))
        || !QFileInfo::exists(root.filePath(QStringLiteral(".venv/Lib/site-packages/torch/lib/libomp140.x86_64.dll"))))
        return QStringLiteral("SeedVC native libraries are incomplete. Run setup again.");
    const auto cached = [&root](const QString &cache, const QString &repository,
                                const QString &file, qint64 minimum) {
        const QDir snapshots(root.filePath(cache + QLatin1Char('/') + repository
                                           + QStringLiteral("/snapshots")));
        for (const QString &name : snapshots.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QFileInfo entry(snapshots.filePath(name + QLatin1Char('/') + file));
            if (!entry.exists()) continue;
            // Hugging Face snapshots use links on Windows. QFileInfo::size()
            // reports zero for the link itself, even when its blob is complete.
            const QFileInfo content(entry.isSymLink() ? entry.symLinkTarget() : entry.filePath());
            if (content.size() > minimum) return true;
        }
        return false;
    };
    const QString primary = QStringLiteral("models--Plachta--Seed-VC");
    if (!cached(QStringLiteral("checkpoints"), primary,
                QStringLiteral("DiT_seed_v2_uvit_whisper_small_wavenet_bigvgan_pruned.pth"), 100000000))
        return QStringLiteral("SeedVC speech checkpoint is missing. Run setup.");
    if (!cached(QStringLiteral("checkpoints"), primary,
                QStringLiteral("config_dit_mel_seed_uvit_whisper_small_wavenet.yml"), 100))
        return QStringLiteral("SeedVC speech configuration is missing. Run setup.");
    if (!cached(QStringLiteral("checkpoints"), primary,
                QStringLiteral("DiT_seed_v2_uvit_whisper_base_f0_44k_bigvgan_pruned_ft_ema.pth"), 100000000)
        || !cached(QStringLiteral("checkpoints"), primary,
                   QStringLiteral("config_dit_mel_seed_uvit_whisper_base_f0_44k.yml"), 100))
        return QStringLiteral("SeedVC auxiliary model is missing. Run setup.");
    if (!cached(QStringLiteral("checkpoints"), QStringLiteral("models--funasr--campplus"),
                QStringLiteral("campplus_cn_common.bin"), 1000000))
        return QStringLiteral("SeedVC speaker encoder is missing. Run setup.");
    if (!cached(QStringLiteral("checkpoints"), QStringLiteral("models--lj1995--VoiceConversionWebUI"),
                QStringLiteral("rmvpe.pt"), 1000000))
        return QStringLiteral("SeedVC pitch model is missing. Run setup.");
    if (!cached(QStringLiteral("checkpoints/hf_cache"), QStringLiteral("models--openai--whisper-small"),
                QStringLiteral("model.safetensors"), 100000000))
        return QStringLiteral("SeedVC speech tokenizer is missing. Run setup.");
    for (const QString &vocoder : {QStringLiteral("models--nvidia--bigvgan_v2_22khz_80band_256x"),
                                   QStringLiteral("models--nvidia--bigvgan_v2_44khz_128band_512x")})
        if (!cached(QStringLiteral("checkpoints/hf_cache"), vocoder,
                    QStringLiteral("bigvgan_generator.pt"), 1000000))
            return QStringLiteral("SeedVC vocoder is missing. Run setup.");
    return {};
}

QVariantList VoiceGenerationManager::voices() const
{
    QVariantList result;
    const auto stored = settings();
    const QStringList roots{
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("assets/voices")),
        voiceFolder()};
    for (const QString &root : roots) {
        const QDir directory(root);
        for (const QString &name : directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QDir folder(directory.filePath(name));
            QFile file(folder.filePath(QStringLiteral("voice.json")));
            if (!file.open(QIODevice::ReadOnly)) continue;
            const auto metadata = QJsonDocument::fromJson(file.readAll()).object();
            const QString audio = folder.filePath(metadata.value(QStringLiteral("audio")).toString());
            if (!QFileInfo::exists(audio)) continue;
            const QString imageName = metadata.value(QStringLiteral("image")).toString();
            const QString image = imageName.isEmpty() ? QString{} : folder.filePath(imageName);
            const QString id = (root == roots.first() ? QStringLiteral("builtin:") : QStringLiteral("user:")) + name;
            const QString referenceKey = QStringLiteral("generations/voice/reference/") + id + QLatin1Char('/');
            result.append(QVariantMap{{"id", id},
                {"title", metadata.value(QStringLiteral("title")).toString()},
                {"audio", audio}, {"audioUrl", QUrl::fromLocalFile(audio)},
                {"thumbnail", QFileInfo(image).isFile() ? QUrl::fromLocalFile(image) : QUrl{}},
                {"transcript", metadata.value(QStringLiteral("transcript")).toString()},
                {"referenceStart", stored.value(referenceKey + QStringLiteral("start"),
                    metadata.value(QStringLiteral("referenceStart")).toDouble(0.0))},
                {"referenceDuration", qMin(25.0, stored.value(referenceKey + QStringLiteral("duration"),
                    metadata.value(QStringLiteral("referenceDuration")).toDouble(10.0)).toDouble())},
                {"builtin", root == roots.first()}});
        }
    }
    return result;
}

void VoiceGenerationManager::setVoiceReference(const QString &voiceId, double start, double duration)
{
    QString audio;
    for (const auto &voice : voices())
        if (voice.toMap().value(QStringLiteral("id")).toString() == voiceId) {
            audio = voice.toMap().value(QStringLiteral("audio")).toString(); break;
        }
    if (audio.isEmpty()) { m_error = QStringLiteral("Choose a target voice."); emit changed(); return; }
    m_error = referenceSpanError(audio, start, duration);
    if (!m_error.isEmpty()) { emit changed(); return; }
    auto stored = settings();
    const QString key = QStringLiteral("generations/voice/reference/") + voiceId + QLatin1Char('/');
    stored.setValue(key + QStringLiteral("start"), start);
    stored.setValue(key + QStringLiteral("duration"), duration);
    m_error.clear();
    emit changed();
}

void VoiceGenerationManager::setSeedvcRuntimePath(const QString &path)
{
    if (running() || !QDir::isAbsolutePath(path)) {
        m_error = QStringLiteral("Choose an absolute SeedVC runtime folder."); emit changed(); return;
    }
    settings().setValue(QStringLiteral("generations/voice/seedvcRuntime"),
                        QDir::fromNativeSeparators(path));
    m_error.clear(); m_setupMessage.clear(); emit changed();
}

void VoiceGenerationManager::setOutputPath(const QString &path)
{
    if (running() || !QDir::isAbsolutePath(path) || !QDir().mkpath(path)) {
        m_error = QStringLiteral("Choose a writable absolute results folder."); emit changed(); return;
    }
    settings().setValue(QStringLiteral("generations/voice/output"), QDir::fromNativeSeparators(path));
    m_error.clear(); emit changed();
}

void VoiceGenerationManager::addVoice(const QString &title, const QUrl &audio,
                                      const QUrl &image, double referenceStart,
                                      double referenceDuration)
{
    const bool hasImage = !image.isEmpty();
    if (title.trimmed().isEmpty() || !audio.isLocalFile()
        || !QFileInfo(audio.toLocalFile()).isFile()
        || (hasImage && (!image.isLocalFile() || !QFileInfo(image.toLocalFile()).isFile()))) {
        m_error = QStringLiteral("Enter a title and choose an audio file. The thumbnail is optional."); emit changed(); return;
    }
    m_error = referenceSpanError(audio.toLocalFile(), referenceStart, referenceDuration);
    if (!m_error.isEmpty()) { emit changed(); return; }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QDir folder(QDir(voiceFolder()).filePath(id));
    if (!QDir().mkpath(folder.absolutePath())) { m_error = QStringLiteral("Could not create the voice folder."); emit changed(); return; }
    const QString audioName = QStringLiteral("target.") + QFileInfo(audio.toLocalFile()).suffix().toLower();
    const QString imageName = hasImage
        ? QStringLiteral("thumbnail.") + QFileInfo(image.toLocalFile()).suffix().toLower()
        : QString{};
    if (!QFile::copy(audio.toLocalFile(), folder.filePath(audioName))
        || (hasImage && !QFile::copy(image.toLocalFile(), folder.filePath(imageName)))) {
        m_error = QStringLiteral("Could not copy the voice files."); emit changed(); return;
    }
    QSaveFile manifest(folder.filePath(QStringLiteral("voice.json")));
    const QByteArray json = QJsonDocument(QJsonObject{{"schemaVersion", 1},
        {"title", title.trimmed()}, {"audio", audioName}, {"image", imageName},
        {"referenceStart", referenceStart}, {"referenceDuration", referenceDuration}}).toJson();
    if (!manifest.open(QIODevice::WriteOnly) || manifest.write(json) != json.size() || !manifest.commit()) {
        m_error = QStringLiteral("Could not save the voice metadata."); emit changed(); return;
    }
    m_error.clear(); emit changed();
}

void VoiceGenerationManager::removeVoice(const QString &id)
{
    if (!id.startsWith(QStringLiteral("user:")) || running()) return;
    const QString name = id.mid(5);
    if (QUuid(name).isNull()) return;
    const QString parent = QDir(voiceFolder()).absolutePath() + QDir::separator();
    const QString target = QDir::cleanPath(QDir(voiceFolder()).absoluteFilePath(name));
    if (!target.startsWith(parent, Qt::CaseInsensitive)) return;
    QDir(target).removeRecursively();
    emit changed();
}

void VoiceGenerationManager::setupSeedvc()
{
    if (running()) return;
    launch({{"action", "setup"}}, true);
}

void VoiceGenerationManager::generate(const QVariantMap &source, const QString &voiceId,
                                      double referenceStart, double referenceDuration,
                                      double guidance, int diffusionSteps)
{
    if (running()) return;
    if (!seedvcReady()) {
        m_error = QStringLiteral("Set up SeedVC in Settings > Generative first.");
        emit changed(); return;
    }
    QVariantMap voice;
    for (const QVariant &item : voices())
        if (item.toMap().value(QStringLiteral("id")).toString() == voiceId) {
            voice = item.toMap(); break;
        }
    const QString sourcePath = source.value(QStringLiteral("path")).toString();
    const QString clipId = source.value(QStringLiteral("clipId")).toString();
    const double sourceStart = source.value(QStringLiteral("startSeconds")).toDouble();
    const double sourceDuration = source.value(QStringLiteral("durationSeconds")).toDouble();
    if (voice.isEmpty() || clipId.isEmpty() || !QFileInfo::exists(sourcePath)
        || !std::isfinite(sourceStart) || sourceStart < 0
        || !std::isfinite(sourceDuration) || sourceDuration <= 0) {
        m_error = QStringLiteral("Select a clip with audio and a target voice.");
        emit changed(); return;
    }
    m_error = referenceSpanError(voice.value(QStringLiteral("audio")).toString(),
                                 referenceStart, referenceDuration);
    if (!m_error.isEmpty()) { emit changed(); return; }
    if (!std::isfinite(guidance) || guidance < 0 || guidance > 3
        || diffusionSteps < 1 || diffusionSteps > 100) {
        m_error = QStringLiteral("Choose SeedVC CFG from 0–3 and 1–100 diffusion steps.");
        emit changed(); return;
    }
    if (!QDir().mkpath(outputPath())) {
        m_error = QStringLiteral("Could not create voice results folder.");
        emit changed(); return;
    }
    m_clipId = clipId;
    m_voiceName = voice.value(QStringLiteral("title")).toString();
    m_mode = QStringLiteral("seedvc");
    launch({{"action", "generate"},
        {"source", sourcePath}, {"start", sourceStart}, {"duration", sourceDuration},
        {"target", voice.value(QStringLiteral("audio"))},
        {"reference_start", referenceStart}, {"reference_duration", referenceDuration},
        {"speaker_cfg", guidance}, {"cfm_steps", diffusionSteps},
        {"output", QDir(outputPath()).filePath(clipId + QLatin1Char('-')
            + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".wav"))}}, false);
}

void VoiceGenerationManager::launch(const QVariantMap &request, bool setupJob)
{
#ifdef Q_OS_WIN
    if (!m_jobHandle) { emit changed(); return; }
#endif
    const QString runtime = seedvcRuntimePath();
    const QString python = QDir(runtime).filePath(QStringLiteral(".venv/Scripts/python.exe"));
    if (!withinRuntime(python, runtime)) {
        m_error = QStringLiteral("The voice Python executable must be inside its isolated runtime folder.");
        emit changed(); return;
    }
    const bool bootstrap = setupJob && !seedvcReady();
    const QString script = QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("scripts/runtime/seedvc_voice_change.py"));
    const QString bootstrapScript = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("scripts/runtime/setup_seedvc_bootstrap.ps1"));
    if ((!bootstrap && !QFileInfo::exists(python)) || !QFileInfo::exists(script)
        || (bootstrap && !QFileInfo::exists(bootstrapScript))) {
        m_error = QStringLiteral("The isolated voice runtime or worker script is missing."); emit changed(); return;
    }
    QVariantMap payload = request;
    payload.insert(QStringLiteral("runtime"), runtime);
    payload.insert(QStringLiteral("python"), python);
    payload.insert(QStringLiteral("ffmpeg"), QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ffmpeg.exe")));
    m_pending.clear(); m_diagnostics.clear(); m_result.clear(); m_error.clear();
    if (setupJob) m_setupMessage.clear();
    m_targetOutput = request.value(QStringLiteral("output")).toString();
    m_expectedConversionSeconds = std::max(25.0,
        request.value(QStringLiteral("duration")).toDouble() *
        request.value(QStringLiteral("cfm_steps"), 50).toInt() / 8.0);
    m_progress = -1; m_stage = setupJob ? QStringLiteral("Preparing SeedVC")
        : QStringLiteral("Preparing voice conversion");
    m_canceling = false; m_setupJob = setupJob;
    m_clock.start(); m_tick.start();
    auto *process = new QProcess(this); m_process = process;
    const QString windows = QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("SystemRoot"), QStringLiteral("C:/Windows"));
    process->setProgram(bootstrap
        ? QDir(windows).filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"))
        : python);
    process->setArguments(bootstrap
        ? QStringList{QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                      QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                      QStringLiteral("-File"), bootstrapScript}
        : QStringList{QStringLiteral("-u"), script});
    process->setProcessChannelMode(QProcess::SeparateChannels);
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const QString &name : {QStringLiteral("PYTHONHOME"), QStringLiteral("PYTHONPATH"),
                                QStringLiteral("VIRTUAL_ENV"), QStringLiteral("CONDA_PREFIX"),
                                QStringLiteral("CONDA_DEFAULT_ENV"), QStringLiteral("UV_PROJECT_ENVIRONMENT"),
                                QStringLiteral("HUGGINGFACE_HUB_CACHE"), QStringLiteral("HF_HUB_CACHE"),
                                QStringLiteral("HF_HUB_ENABLE_HF_XET"), QStringLiteral("TRANSFORMERS_CACHE"),
                                QStringLiteral("TORCH_HOME"), QStringLiteral("XDG_CACHE_HOME")})
        environment.remove(name);
    environment.insert(QStringLiteral("PATH"), QFileInfo(python).absolutePath()
        + QDir::listSeparator() + QDir(windows).filePath(QStringLiteral("System32"))
        + QDir::listSeparator() + windows);
    environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    environment.insert(QStringLiteral("PIP_NO_INPUT"), QStringLiteral("1"));
    environment.insert(QStringLiteral("HF_HUB_DISABLE_IMPLICIT_TOKEN"), QStringLiteral("1"));
    environment.insert(QStringLiteral("HF_HUB_ENABLE_HF_TRANSFER"), QStringLiteral("0"));
    environment.insert(QStringLiteral("HF_HOME"), QDir(runtime).filePath(QStringLiteral("checkpoints/hf_cache")));
    process->setProcessEnvironment(environment);
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
    connect(process, &QProcess::started, this, [this, process] {
        HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                   static_cast<DWORD>(process->processId()));
        if (!child || !AssignProcessToJobObject(static_cast<HANDLE>(m_jobHandle), child)) {
            m_error = QStringLiteral("Could not secure voice worker cleanup."); process->kill(); emit changed();
        }
        if (child) CloseHandle(child);
    });
#endif
    connect(process, &QProcess::started, this, [process, payload] {
        process->write(QJsonDocument::fromVariant(payload).toJson(QJsonDocument::Compact) + '\n');
        process->closeWriteChannel();
    });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        if (m_process == process) consume(process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process] {
        if (m_process == process) m_diagnostics = (m_diagnostics + process->readAllStandardError()).right(5000);
    });
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        if (m_process != process) return;
        consume(process->readAllStandardOutput());
        m_diagnostics += process->readAllStandardError();
        if (status != QProcess::NormalExit || code != 0)
            qWarning().noquote() << "[voice] worker failed at" << m_stage
                                 << "exit code" << code << "error" << m_error
                                 << QString::fromUtf8(m_diagnostics.right(5000));
        m_process = nullptr; process->deleteLater();
        m_tick.stop();
        if (!m_setupJob && !m_targetOutput.isEmpty()) {
            const QString parent = QDir(outputPath()).absolutePath() + QLatin1Char('/');
            const QString target = QFileInfo(m_targetOutput).absoluteFilePath();
            if (target.startsWith(parent, Qt::CaseInsensitive)) {
                QFile::remove(QFileInfo(target).absolutePath() + QLatin1Char('/')
                    + QFileInfo(target).completeBaseName() + QStringLiteral(".partial.wav"));
                const QString work = QFileInfo(target).absolutePath() + QLatin1Char('/')
                    + QFileInfo(target).completeBaseName() + QStringLiteral(".work");
                if (work.startsWith(parent, Qt::CaseInsensitive) && QFileInfo(work).isDir())
                    QDir(work).removeRecursively();
            }
        }
        if (m_canceling) m_error = m_setupJob ? QStringLiteral("SeedVC setup canceled.")
            : QStringLiteral("Voice job canceled.");
        else if (status != QProcess::NormalExit || code != 0) {
            if (m_error.isEmpty()) m_error = QString::fromUtf8(m_diagnostics.right(1800)).trimmed();
            if (m_error.isEmpty()) m_error = QStringLiteral("SeedVC worker exited without an error message (code %1).").arg(code);
        }
        else if (!m_setupJob && QFileInfo(m_result).size() > 1000) {
            emit voiceReady(m_clipId, m_result, m_voiceName, m_mode);
        } else if (!m_setupJob) m_error = QStringLiteral("Voice worker did not return an audio file.");
        if (m_setupJob) {
            if (m_error.isEmpty()) {
                const QString issue = seedvcIssue();
                if (!issue.isEmpty())
                    m_error = QStringLiteral("Setup finished, but DataCut cannot use SeedVC: %1").arg(issue);
            }
            m_setupMessage = m_error.isEmpty()
                ? QStringLiteral("SeedVC is installed and verified. Voice Change is ready.")
                : QStringLiteral("SeedVC setup failed: %1").arg(m_error);
        }
        m_stage = m_error.isEmpty() ? QStringLiteral("Completed") : QStringLiteral("Failed");
        m_progress = m_error.isEmpty() ? 1 : m_progress;
        emit changed();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_process == process) {
            m_process = nullptr;
            process->deleteLater();
            m_tick.stop();
            m_stage = QStringLiteral("Failed");
            m_error = QStringLiteral("Could not launch the voice worker.");
            if (m_setupJob) m_setupMessage = QStringLiteral("SeedVC setup failed: %1").arg(m_error);
            emit changed();
        }
    });
    process->start(); emit changed();
}

void VoiceGenerationManager::consume(const QByteArray &data)
{
    m_pending += data;
    for (;;) {
        const qsizetype end = m_pending.indexOf('\n');
        if (end < 0) break;
        const QByteArray line = m_pending.left(end); m_pending.remove(0, end + 1);
        const QJsonObject event = QJsonDocument::fromJson(line).object();
        if (event.contains(QStringLiteral("stage"))) m_stage = event.value(QStringLiteral("stage")).toString();
        if (event.contains(QStringLiteral("progress"))) {
            const double reported = event.value(QStringLiteral("progress")).toDouble();
            if (reported >= 0) m_progress = std::max(m_progress, reported);
        }
        if (event.contains(QStringLiteral("error"))) m_error = event.value(QStringLiteral("error")).toString();
        if (event.contains(QStringLiteral("result"))) m_result = event.value(QStringLiteral("result")).toString();
        emit changed();
    }
}

void VoiceGenerationManager::cancel()
{
    if (!m_process) return;
    m_canceling = true; m_stage = QStringLiteral("Canceling"); m_process->kill(); emit changed();
}

} // namespace datacut
