#pragma once

#include <QObject>
#include <QProcess>
#include <QVariantList>
#include <QVariantMap>
#include <QUrl>

namespace datacut {

// Owns one short-lived generation worker. No model or server lives in the editor process.
class MediaGenerationManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList providers READ providers NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString activeKind READ activeKind NOTIFY changed)
    Q_PROPERTY(QString stage READ stage NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(bool progressKnown READ progressKnown NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString lastResult READ lastResult NOTIFY changed)
    Q_PROPERTY(int gpuMiB READ gpuMiB NOTIFY changed)
public:
    explicit MediaGenerationManager(QObject *parent = nullptr);
    ~MediaGenerationManager() override;

    QVariantList providers() const;
    bool running() const { return m_process != nullptr; }
    QString activeKind() const { return m_activeKind; }
    QString stage() const { return m_stage; }
    double progress() const { return m_progress; }
    bool progressKnown() const { return m_progressKnown; }
    QString error() const { return m_error; }
    QString lastResult() const { return m_lastResult; }
    int gpuMiB() const { return m_gpuMiB; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setRuntimePath(const QString &kind, const QString &path);
    Q_INVOKABLE void setOutputPath(const QString &kind, const QString &path);
    Q_INVOKABLE void useSuggestedStorage(const QString &kind);
    Q_INVOKABLE void setup(const QString &kind);
    Q_INVOKABLE void generate(const QString &kind, const QVariantMap &options);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void copyError() const;
    Q_INVOKABLE void openLogFolder(const QString &kind) const;
    Q_INVOKABLE QVariantList results(const QString &kind) const;
    Q_INVOKABLE QString localPath(const QUrl &url) const { return url.toLocalFile(); }
    Q_INVOKABLE QUrl fileUrl(const QString &path) const { return QUrl::fromLocalFile(path); }

signals:
    void changed();
    void resultReady(const QString &path);

private:
    QVariantMap provider(const QString &kind) const;
    QString defaultRuntimePath(const QString &kind) const;
    QString defaultOutputPath(const QString &kind) const;
    QString runtimePath(const QString &kind) const;
    QString outputPath(const QString &kind) const;
    QString pythonPath(const QString &kind) const;
    bool validKind(const QString &kind) const;
    void run(const QString &kind, const QString &action, const QVariantMap &options = {});
    void consume(const QByteArray &data);
    void saveSetupLog(const QString &kind) const;

    QProcess *m_process = nullptr;
    QByteArray m_pending;
    QByteArray m_diagnostics;
    QString m_activeKind;
    QString m_stage;
    QString m_error;
    QString m_lastResult;
    double m_progress = 0;
    bool m_progressKnown = false;
    int m_gpuMiB = -1;
    bool m_canceling = false;
#ifdef Q_OS_WIN
    void *m_jobHandle = nullptr;
#endif
};

} // namespace datacut
