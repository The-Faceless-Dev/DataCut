#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QUrl>

namespace datacut {

class GenerationManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY settingsChanged)
    Q_PROPERTY(bool localReady READ localReady NOTIFY settingsChanged)
    Q_PROPERTY(bool setupRunning READ setupRunning NOTIFY jobChanged)
    Q_PROPERTY(bool captionRunning READ captionRunning NOTIFY jobChanged)
    Q_PROPERTY(QString stage READ stage NOTIFY jobChanged)
    Q_PROPERTY(double progress READ progress NOTIFY jobChanged)
    Q_PROPERTY(bool progressKnown READ progressKnown NOTIFY jobChanged)
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY jobChanged)
    Q_PROPERTY(QString error READ error NOTIFY jobChanged)

public:
    explicit GenerationManager(QObject *parent = nullptr);
    ~GenerationManager() override;

    QString rootPath() const { return m_rootPath; }
    bool localReady() const;
    bool setupRunning() const { return m_setupProcess != nullptr; }
    bool captionRunning() const { return m_mediaProcess != nullptr || m_asrProcess != nullptr; }
    QString stage() const { return m_stage; }
    double progress() const { return m_progress; }
    bool progressKnown() const { return m_progressKnown; }
    int elapsedSeconds() const { return m_jobClock.isValid() ? int(m_jobClock.elapsed() / 1000) : 0; }
    QString error() const { return m_error; }

    Q_INVOKABLE void setRootPath(const QString &path);
    Q_INVOKABLE void setRootUrl(const QUrl &url) { if (url.isLocalFile()) setRootPath(url.toLocalFile()); }
    Q_INVOKABLE void setupLocal();
    Q_INVOKABLE void cancelJob();
    void transcribeClip(const QString &clipId, const QString &sourcePath,
                        double sourceStartSeconds, double sourceDurationSeconds);

signals:
    void settingsChanged();
    void jobChanged();
    void captionWordsReady(const QString &clipId, const QVariantList &words);

private:
    QString runtimeExe() const;
    QString modelFile() const;
    void setStage(const QString &stage, double progress = -1);
    void finishJob(const QString &error = {});
    void startRecognition();

    QString m_rootPath;
    QString m_stage;
    QString m_error;
    QString m_clipId;
    QString m_wavPath;
    QByteArray m_output;
    QByteArray m_diagnostics;
    QByteArray m_setupLineBuffer;
    double m_progress = 0;
    bool m_progressKnown = false;
    QElapsedTimer m_jobClock;
    QTimer m_jobTick;
    double m_expectedSeconds = 0;
    bool m_cancelRequested = false;
    QProcess *m_setupProcess = nullptr;
    QProcess *m_mediaProcess = nullptr;
    QProcess *m_asrProcess = nullptr;
};

} // namespace datacut
