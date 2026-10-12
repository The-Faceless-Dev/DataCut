#include "app/MediaGenerationManager.h"

#include <QCoreApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QGuiApplication>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace datacut {
namespace {
const QStringList kinds{QStringLiteral("image"), QStringLiteral("video"),
                        QStringLiteral("music"), QStringLiteral("sound")};

QSettings settings()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(directory);
    return {QDir(directory).filePath(QStringLiteral("settings.ini")), QSettings::IniFormat};
}

QString label(const QString &kind)
{
    if (kind == QLatin1String("image")) return QStringLiteral("Image · FLUX.2 Klein 4B");
    if (kind == QLatin1String("video")) return QStringLiteral("Video · LTX 2.3");
    if (kind == QLatin1String("music")) return QStringLiteral("Music · ACE-Step 1.5 Turbo");
    return QStringLiteral("Sound effects · TangoFlux");
}

bool substantial(const QString &path, qint64 minimum = 1000000)
{
    return QFileInfo(path).size() >= minimum;
}

QStorageInfo storageForPath(QString path)
{
    QFileInfo info(path);
    while (!info.exists() && info.absoluteFilePath() != info.absolutePath())
        info = QFileInfo(info.absolutePath());
    return QStorageInfo(info.absoluteFilePath());
}

constexpr qint64 musicSetupSpace = 25LL * 1024 * 1024 * 1024;
}

MediaGenerationManager::MediaGenerationManager(QObject *parent) : QObject(parent)
{
#ifdef Q_OS_WIN
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            m_jobHandle = job;
        else CloseHandle(job);
    }
    if (!m_jobHandle)
        m_error = QStringLiteral("Windows process cleanup could not be initialized. Generation is unavailable.");
#endif
    QTimer::singleShot(0, this, &MediaGenerationManager::refresh);
}

MediaGenerationManager::~MediaGenerationManager()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(2000);
    }
#ifdef Q_OS_WIN
    if (m_jobHandle) CloseHandle(static_cast<HANDLE>(m_jobHandle));
#endif
}

bool MediaGenerationManager::validKind(const QString &kind) const { return kinds.contains(kind); }

QString MediaGenerationManager::defaultRuntimePath(const QString &kind) const
{
    const QDir app(QCoreApplication::applicationDirPath());
    const QString root = settings().value(QStringLiteral("generations/root"),
        app.filePath(QStringLiteral("generations"))).toString();
    return QDir(root).filePath(QStringLiteral("runtimes/") + kind);
}

QString MediaGenerationManager::defaultOutputPath(const QString &kind) const
{
    const QString root = settings().value(QStringLiteral("generations/root"),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("generations"))).toString();
    return QDir(root).filePath(QStringLiteral("results/") + kind);
}

QString MediaGenerationManager::runtimePath(const QString &kind) const
{
    const QString configured = settings().value(QStringLiteral("generations/") + kind + QStringLiteral("/runtime")).toString();
    return configured.isEmpty() ? defaultRuntimePath(kind) : configured;
}

QString MediaGenerationManager::outputPath(const QString &kind) const
{
    return settings().value(QStringLiteral("generations/") + kind + QStringLiteral("/output"),
                            defaultOutputPath(kind)).toString();
}

QString MediaGenerationManager::pythonPath(const QString &kind) const
{
    return QDir(runtimePath(kind)).filePath(QStringLiteral(".venv/Scripts/python.exe"));
}

QVariantMap MediaGenerationManager::provider(const QString &kind) const
{
    const QString root = runtimePath(kind);
    const QDir dir(root);
    QStringList missing;
    if (!QFileInfo::exists(pythonPath(kind))) missing << QStringLiteral("Python runtime");
    if (kind == QLatin1String("image")) {
        if (!QFileInfo::exists(dir.filePath(QStringLiteral("main.py")))) missing << QStringLiteral("ComfyUI");
        if (!substantial(dir.filePath(QStringLiteral("models/diffusion_models/flux-2-klein-4b.safetensors")))) missing << QStringLiteral("FLUX.2 Klein weights");
        if (!substantial(dir.filePath(QStringLiteral("models/text_encoders/qwen_3_4b.safetensors")))) missing << QStringLiteral("Qwen text encoder");
        if (!substantial(dir.filePath(QStringLiteral("models/vae/flux2-vae.safetensors")))) missing << QStringLiteral("FLUX VAE");
    } else if (kind == QLatin1String("video")) {
        if (!QFileInfo::exists(dir.filePath(QStringLiteral("main.py")))) missing << QStringLiteral("ComfyUI");
        if (!substantial(dir.filePath(QStringLiteral("models/diffusion_models/ltx-2.3-22b-distilled-UD-Q2_K.gguf")))) missing << QStringLiteral("LTX 2.3 weights");
        if (!substantial(dir.filePath(QStringLiteral("models/text_encoders/gemma-3-12b-it-heretic-v2_int4.safetensors")))) missing << QStringLiteral("Gemma text encoder");
        if (QFileInfo(dir.filePath(QStringLiteral("models/text_encoders/ltx-2.3_text_projection_bf16.safetensors"))).size() != 2312149072LL)
            missing << QStringLiteral("LTX text projection (setup can download)");
        if (!substantial(dir.filePath(QStringLiteral("models/vae/ltx-2.3-22b-distilled_video_vae.safetensors")))) missing << QStringLiteral("LTX video VAE");
    } else if (kind == QLatin1String("music")) {
        if (!QFileInfo::exists(dir.filePath(QStringLiteral("pyproject.toml")))) missing << QStringLiteral("ACE-Step source");
        if (!substantial(dir.filePath(QStringLiteral("checkpoints/acestep-v15-turbo/model.safetensors")), 1000000000)) missing << QStringLiteral("ACE-Step Turbo model");
    } else if (kind == QLatin1String("sound")) {
        if (!QDir(dir.filePath(QStringLiteral("tangoflux"))).exists()) missing << QStringLiteral("TangoFlux package");
        if (QFileInfo(dir.filePath(QStringLiteral("models/huggingface/hub/models--declare-lab--TangoFlux/blobs/720ae39607a6aa95c2c8f6a49457268b0ce2a5cd4ef0b34d2eea756553cdb588"))).size() != 3437051960LL)
            missing << QStringLiteral("TangoFlux weights");
        if (QFileInfo(dir.filePath(QStringLiteral("models/huggingface/hub/models--declare-lab--TangoFlux/blobs/d73619a1d1e1dc48e606632931ffce440b4959ce2a4ed5a3522c3bb573b103be"))).size() != 624490208LL)
            missing << QStringLiteral("TangoFlux VAE");
        if (QFileInfo(dir.filePath(QStringLiteral("models/huggingface/hub/models--google--flan-t5-large/blobs/a9dd06ce490f139af36e9eb77dd3758b4fd07a08a73d5a1abe5ff2591e2d388e"))).size() != 3132668804LL)
            missing << QStringLiteral("FLAN-T5 encoder");
    }
    const bool hardware = m_gpuMiB >= 10000;
    const QString signature = root + QLatin1Char('|') + pythonPath(kind);
    const QJsonObject marker = QJsonDocument::fromJson([&dir] {
        QFile file(dir.filePath(QStringLiteral(".datacut-verified.json")));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }()).object();
    const bool verified = settings().value(QStringLiteral("generations/") + kind + QStringLiteral("/verified")).toString() == signature
        || (marker.value(QStringLiteral("schemaVersion")).toInt() == 1
            && marker.value(QStringLiteral("kind")).toString() == kind);
    const bool ready = hardware && missing.isEmpty() && verified;
    const QStorageInfo storage = storageForPath(root);
    const qint64 freeBytes = storage.isValid() && storage.isReady() ? storage.bytesAvailable() : -1;
    QString suggestedRoot;
    qint64 suggestedFree = 0;
    if (kind == QLatin1String("music") && freeBytes >= 0 && freeBytes < musicSetupSpace && !verified) {
        for (const QStorageInfo &volume : QStorageInfo::mountedVolumes()) {
            if (!volume.isValid() || !volume.isReady() || volume.isReadOnly()
                || volume.rootPath().compare(storage.rootPath(), Qt::CaseInsensitive) == 0
                || volume.bytesAvailable() < musicSetupSpace || volume.bytesAvailable() <= suggestedFree)
                continue;
            suggestedRoot = QDir(volume.rootPath()).filePath(QStringLiteral("DataCut/generations"));
            suggestedFree = volume.bytesAvailable();
        }
    }
    const QString status = m_gpuMiB < 0 ? QStringLiteral("Checking NVIDIA GPU…")
        : !hardware ? QStringLiteral("Requires at least 10 GB NVIDIA VRAM; detected %1 MiB.").arg(m_gpuMiB)
        : !missing.isEmpty() ? QStringLiteral("Missing: ") + missing.join(QStringLiteral(", "))
        : verified ? QStringLiteral("Local runtime ready") : QStringLiteral("Run Check setup to verify this runtime");
    return {{QStringLiteral("id"), kind}, {QStringLiteral("name"), label(kind)},
            {QStringLiteral("runtimePath"), root}, {QStringLiteral("pythonPath"), pythonPath(kind)},
            {QStringLiteral("outputPath"), outputPath(kind)}, {QStringLiteral("ready"), ready},
            {QStringLiteral("status"), status}, {QStringLiteral("missing"), missing},
            {QStringLiteral("freeGiB"), freeBytes < 0 ? -1.0 : freeBytes / 1073741824.0},
            {QStringLiteral("setupGiB"), kind == QLatin1String("music") ? 25 : 0},
            {QStringLiteral("suggestedRoot"), suggestedRoot}};
}

QVariantList MediaGenerationManager::providers() const
{
    QVariantList result;
    for (const QString &kind : kinds) result << provider(kind);
    return result;
}

void MediaGenerationManager::refresh()
{
    auto *probe = new QProcess(this);
    probe->setProgram(QStringLiteral("nvidia-smi.exe"));
    probe->setArguments({QStringLiteral("--query-gpu=memory.total"),
                         QStringLiteral("--format=csv,noheader,nounits")});
#ifdef Q_OS_WIN
    probe->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    connect(probe, &QProcess::finished, this, [this, probe](int code, QProcess::ExitStatus status) {
        m_gpuMiB = 0;
        if (status == QProcess::NormalExit && code == 0) {
            for (const QByteArray &line : probe->readAllStandardOutput().split('\n')) {
                bool ok = false;
                const int size = line.trimmed().toInt(&ok);
                if (ok) m_gpuMiB = std::max(m_gpuMiB, size);
            }
        }
        probe->deleteLater();
        emit changed();
    });
    connect(probe, &QProcess::errorOccurred, this, [this, probe](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_gpuMiB = 0;
            probe->deleteLater();
            emit changed();
        }
    });
    probe->start();
    QTimer::singleShot(5000, probe, [probe] {
        if (probe->state() != QProcess::NotRunning) probe->kill();
    });
}

void MediaGenerationManager::setRuntimePath(const QString &kind, const QString &path)
{
    if (!validKind(kind) || running()) return;
    const QString value = QDir::fromNativeSeparators(path.trimmed());
    if (!QDir::isAbsolutePath(value)) { m_error = QStringLiteral("Choose an absolute runtime folder."); emit changed(); return; }
    settings().setValue(QStringLiteral("generations/") + kind + QStringLiteral("/runtime"), value);
    m_error.clear(); emit changed();
}

void MediaGenerationManager::setOutputPath(const QString &kind, const QString &path)
{
    if (!validKind(kind) || running()) return;
    const QString value = QDir::fromNativeSeparators(path.trimmed());
    if (!QDir::isAbsolutePath(value) || !QDir().mkpath(value)) {
        m_error = QStringLiteral("Choose a writable absolute results folder."); emit changed(); return;
    }
    settings().setValue(QStringLiteral("generations/") + kind + QStringLiteral("/output"), value);
    m_error.clear(); emit changed();
}

void MediaGenerationManager::useSuggestedStorage(const QString &kind)
{
    if (!validKind(kind) || running()) return;
    const QString root = provider(kind).value(QStringLiteral("suggestedRoot")).toString();
    if (root.isEmpty()) return;
    settings().setValue(QStringLiteral("generations/") + kind + QStringLiteral("/runtime"),
                        QDir(root).filePath(QStringLiteral("runtimes/") + kind));
    settings().setValue(QStringLiteral("generations/") + kind + QStringLiteral("/output"),
                        QDir(root).filePath(QStringLiteral("results/") + kind));
    m_error.clear();
    emit changed();
}

void MediaGenerationManager::setup(const QString &kind)
{
    if (!validKind(kind) || running()) return;
    if (m_gpuMiB < 10000) {
        m_error = QStringLiteral("Local generation requires at least 10 GB NVIDIA VRAM. Detected %1 MiB.").arg(m_gpuMiB);
        emit changed(); return;
    }
    if (kind == QLatin1String("music")) {
        const QVariantMap entry = provider(kind);
        const double freeGiB = entry.value(QStringLiteral("freeGiB")).toDouble();
        if (freeGiB >= 0 && freeGiB < 25 && !entry.value(QStringLiteral("ready")).toBool()) {
            m_error = QStringLiteral("Music setup needs about 25 GiB of free space in its runtime folder. "
                                     "Only %1 GiB is available. Choose another runtime folder or use the suggested drive below.")
                          .arg(freeGiB, 0, 'f', 1);
            emit changed(); return;
        }
    }
    if (!QDir().mkpath(outputPath(kind))) {
        m_error = QStringLiteral("Could not create the results folder."); emit changed(); return;
    }
    run(kind, QStringLiteral("setup"));
}

void MediaGenerationManager::generate(const QString &kind, const QVariantMap &options)
{
    if (!validKind(kind) || running()) return;
    const QVariantMap entry = provider(kind);
    if (!entry.value(QStringLiteral("ready")).toBool()) {
        m_error = entry.value(QStringLiteral("status")).toString(); emit changed(); return;
    }
    if (options.value(QStringLiteral("prompt")).toString().trimmed().isEmpty()) {
        m_error = QStringLiteral("Enter a prompt first."); emit changed(); return;
    }
    const int steps = options.value(QStringLiteral("steps")).toInt();
    const int duration = options.value(QStringLiteral("duration")).toInt();
    if (kind == QLatin1String("sound") && (steps < 10 || steps > 100)) {
        m_error = QStringLiteral("TangoFlux needs 10–100 inference steps; 50 is recommended."); emit changed(); return;
    }
    if (steps < 1 || steps > 200 || (kind == QLatin1String("video") && (duration < 1 || duration > 10))
        || (kind == QLatin1String("music") && (duration < 10 || duration > 300))
        || (kind == QLatin1String("sound") && (duration < 1 || duration > 30))) {
        m_error = QStringLiteral("Check duration and inference steps for this generator."); emit changed(); return;
    }
    if (!QDir().mkpath(outputPath(kind))) {
        m_error = QStringLiteral("Could not create the results folder."); emit changed(); return;
    }
    run(kind, QStringLiteral("generate"), options);
}

void MediaGenerationManager::run(const QString &kind, const QString &action, const QVariantMap &options)
{
#ifdef Q_OS_WIN
    if (!m_jobHandle) {
        m_error = QStringLiteral("Windows process cleanup is unavailable; generation was not started.");
        emit changed(); return;
    }
#endif
    const QString script = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("scripts/runtime/media_generate.py"));
    if (!QFileInfo::exists(script)) { m_error = QStringLiteral("Generation worker script is missing."); emit changed(); return; }
    const QString python = pythonPath(kind);
    const QString bootstrap = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("scripts/runtime/setup_media_runtime.ps1"));
    if (action == QLatin1String("setup") && !QFileInfo::exists(bootstrap)) {
        m_error = QStringLiteral("Generation setup script is missing."); emit changed(); return;
    }
    if (action != QLatin1String("setup") && !QFileInfo::exists(python)) {
        m_error = QStringLiteral("Runtime Python is missing. Run Setup in Settings > Generative."); emit changed(); return;
    }
    if (!QDir().mkpath(runtimePath(kind))) {
        m_error = QStringLiteral("Could not create the generation runtime folder."); emit changed(); return;
    }
    QJsonObject payload = QJsonObject::fromVariantMap(options);
    payload.insert(QStringLiteral("kind"), kind);
    payload.insert(QStringLiteral("action"), action);
    payload.insert(QStringLiteral("runtime"), runtimePath(kind));
    payload.insert(QStringLiteral("output_dir"), outputPath(kind));
    payload.insert(QStringLiteral("job_id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_pending.clear(); m_diagnostics.clear(); m_error.clear(); m_lastResult.clear();
    m_stage = action == QLatin1String("setup") ? QStringLiteral("Preparing runtime") : QStringLiteral("Starting generation");
    m_progress = 0; m_progressKnown = true; m_activeKind = kind; m_canceling = false;
    auto *process = new QProcess(this); m_process = process;
    const QString powershell = QDir(QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("SystemRoot"), QStringLiteral("C:/Windows")))
        .filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe"));
    process->setProgram(action == QLatin1String("setup") ? powershell : python);
    process->setArguments(action == QLatin1String("setup")
        ? QStringList{QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                      QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-File"), bootstrap}
        : QStringList{QStringLiteral("-u"), script});
    process->setWorkingDirectory(runtimePath(kind));
    process->setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    env.insert(QStringLiteral("PIP_NO_INPUT"), QStringLiteral("1"));
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("HF_HUB_DISABLE_IMPLICIT_TOKEN"), QStringLiteral("1"));
    env.insert(QStringLiteral("HF_HUB_ENABLE_HF_TRANSFER"), QStringLiteral("0"));
    env.insert(QStringLiteral("HF_HOME"),
               QDir(runtimePath(kind)).filePath(QStringLiteral("models/huggingface")));
    for (const QString &name : {QStringLiteral("PYTHONHOME"), QStringLiteral("PYTHONPATH"),
                                QStringLiteral("VIRTUAL_ENV"), QStringLiteral("CONDA_PREFIX"),
                                QStringLiteral("CONDA_DEFAULT_ENV"), QStringLiteral("UV_PROJECT_ENVIRONMENT"),
                                QStringLiteral("HUGGINGFACE_HUB_CACHE"), QStringLiteral("HF_HUB_CACHE"),
                                QStringLiteral("HF_HUB_ENABLE_HF_XET"), QStringLiteral("TRANSFORMERS_CACHE"),
                                QStringLiteral("TORCH_HOME"), QStringLiteral("XDG_CACHE_HOME")})
        env.remove(name);
#ifdef Q_OS_WIN
    const QString windows = env.value(QStringLiteral("SystemRoot"), QStringLiteral("C:/Windows"));
    env.insert(QStringLiteral("PATH"), QDir::toNativeSeparators(QFileInfo(python).absolutePath())
        + QLatin1Char(';') + QDir::toNativeSeparators(windows + QStringLiteral("/System32"))
        + QLatin1Char(';') + QDir::toNativeSeparators(windows));
    env.insert(QStringLiteral("VHS_FORCE_FFMPEG_PATH"),
               QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ffmpeg.exe")));
#endif
    process->setProcessEnvironment(env);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        if (m_process == process) consume(process->readAllStandardOutput());
    });
#ifdef Q_OS_WIN
    connect(process, &QProcess::started, this, [this, process] {
        if (!m_jobHandle || m_process != process) return;
        HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                   static_cast<DWORD>(process->processId()));
        if (!child || !AssignProcessToJobObject(static_cast<HANDLE>(m_jobHandle), child)) {
            m_error = QStringLiteral("Could not secure generation worker process cleanup.");
            process->kill();
            emit changed();
        }
        if (child) CloseHandle(child);
    });
#endif
    connect(process, &QProcess::started, this, [process, payload] {
        process->write(QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n');
        process->closeWriteChannel();
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process] {
        if (m_process == process) m_diagnostics = (m_diagnostics + process->readAllStandardError()).right(5000);
    });
    connect(process, &QProcess::finished, this, [this, process, action, kind](int code, QProcess::ExitStatus status) {
        if (m_process != process) return;
        consume(process->readAllStandardOutput());
        m_diagnostics += process->readAllStandardError();
        m_process = nullptr; process->deleteLater();
        if (m_canceling) m_error = QStringLiteral("Generation canceled.");
        else if (status != QProcess::NormalExit || code != 0) {
            if (m_error.isEmpty()) m_error = QString::fromUtf8(m_diagnostics.right(1800)).trimmed();
            if (m_error.isEmpty()) {
                switch (static_cast<quint32>(code)) {
                case 0xC0000135: m_error = QStringLiteral("Runtime failed to load: a Windows DLL is missing. Run Set up / repair."); break;
                case 0xC0000139: m_error = QStringLiteral("Runtime failed to load: incompatible DLL entry point. Run Set up / repair."); break;
                case 0xC000007B: m_error = QStringLiteral("Runtime failed to load: incompatible DLL architecture. Run Set up / repair."); break;
                case 0xC0000142: m_error = QStringLiteral("Runtime failed to initialize a Windows DLL. Run Set up / repair."); break;
                default: m_error = QStringLiteral("Generation worker exited without a result (code %1).").arg(code); break;
                }
            }
        } else if (action == QLatin1String("generate") && !QFileInfo::exists(m_lastResult)) {
            m_error = QStringLiteral("Generation finished without a readable output file.");
        }
        if (m_error.isEmpty()) { m_progress = 1; m_stage = QStringLiteral("Complete");
            if (action == QLatin1String("setup"))
                settings().setValue(QStringLiteral("generations/") + kind + QStringLiteral("/verified"),
                                    runtimePath(kind) + QLatin1Char('|') + pythonPath(kind));
            if (action == QLatin1String("generate")) emit resultReady(m_lastResult);
        } else m_stage = QStringLiteral("Failed");
        if (action == QLatin1String("setup")) saveSetupLog(kind);
        emit changed();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (m_process != process || error != QProcess::FailedToStart) return;
        m_error = process->errorString(); m_stage = QStringLiteral("Failed");
        m_process = nullptr; process->deleteLater();
        saveSetupLog(m_activeKind);
        emit changed();
    });
    process->start(); emit changed();
}

void MediaGenerationManager::consume(const QByteArray &data)
{
    m_pending += data;
    while (true) {
        const qsizetype end = m_pending.indexOf('\n');
        if (end < 0) break;
        const QByteArray line = m_pending.left(end).trimmed(); m_pending.remove(0, end + 1);
        const QJsonObject event = QJsonDocument::fromJson(line).object();
        if (event.isEmpty()) continue;
        if (event.contains(QStringLiteral("stage"))) m_stage = event.value(QStringLiteral("stage")).toString();
        if (event.contains(QStringLiteral("progress")))
            m_progress = std::max(m_progress, event.value(QStringLiteral("progress")).toDouble());
        // A stage without a fresh percentage keeps the last known estimate.
        if (event.contains(QStringLiteral("progress"))
            || event.value(QStringLiteral("determinate")).toBool()) m_progressKnown = true;
        if (event.contains(QStringLiteral("error"))) m_error = event.value(QStringLiteral("error")).toString();
        if (event.contains(QStringLiteral("result"))) m_lastResult = event.value(QStringLiteral("result")).toString();
        emit changed();
    }
}

void MediaGenerationManager::cancel()
{
    if (!m_process) return;
    m_canceling = true;
    m_process->kill();
    m_stage = QStringLiteral("Canceling"); emit changed();
}

void MediaGenerationManager::copyError() const
{
    if (!m_error.isEmpty() && QGuiApplication::clipboard())
        QGuiApplication::clipboard()->setText(m_error);
}

void MediaGenerationManager::openLogFolder(const QString &kind) const
{
    if (!validKind(kind)) return;
    const QString folder = outputPath(kind);
    if (QDir(folder).exists()) QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void MediaGenerationManager::saveSetupLog(const QString &kind) const
{
    if (!validKind(kind) || !QDir().mkpath(outputPath(kind))) return;
    QSaveFile file(QDir(outputPath(kind)).filePath(QStringLiteral("datacut-last-setup.log")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    const QString report = QStringLiteral("Time: %1\nGenerator: %2\nRuntime: %3\nStage: %4\nError: %5\n\nDiagnostics:\n%6\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate), kind, runtimePath(kind),
             m_stage, m_error.isEmpty() ? QStringLiteral("None") : m_error,
             QString::fromUtf8(m_diagnostics.right(5000)));
    file.write(report.toUtf8());
    file.commit();
}

QVariantList MediaGenerationManager::results(const QString &kind) const
{
    QVariantList entries;
    if (!validKind(kind)) return entries;
    QDir dir(outputPath(kind));
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.png"), QStringLiteral("*.jpg"),
        QStringLiteral("*.jpeg"), QStringLiteral("*.mp4"), QStringLiteral("*.mov"),
        QStringLiteral("*.wav"), QStringLiteral("*.flac"), QStringLiteral("*.mp3")},
        QDir::Files, QDir::Time);
    for (const QFileInfo &file : files) entries << QVariantMap{{QStringLiteral("name"), file.fileName()},
        {QStringLiteral("path"), file.absoluteFilePath()}, {QStringLiteral("url"), QUrl::fromLocalFile(file.absoluteFilePath())}};
    return entries;
}
} // namespace datacut
