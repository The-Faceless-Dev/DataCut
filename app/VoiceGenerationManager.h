#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QUrl>

namespace datacut {

class VoiceGenerationManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY changed)
    Q_PROPERTY(QString seedvcRuntimePath READ seedvcRuntimePath NOTIFY changed)
    Q_PROPERTY(bool seedvcReady READ seedvcReady NOTIFY changed)
    Q_PROPERTY(QString seedvcStatus READ seedvcStatus NOTIFY changed)
    Q_PROPERTY(QString setupMessage READ setupMessage NOTIFY changed)
    Q_PROPERTY(int maxReferenceSeconds READ maxReferenceSeconds CONSTANT)
    Q_PROPERTY(QVariantList voices READ voices NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString stage READ stage NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY changed)
public:
    explicit VoiceGenerationManager(QObject *parent = nullptr);
    ~VoiceGenerationManager() override;
    QString outputPath() const;
    QString seedvcRuntimePath() const;
    bool seedvcReady() const;
    QString seedvcStatus() const;
    QString setupMessage() const { return m_setupMessage; }
    int maxReferenceSeconds() const { return 25; }
    QVariantList voices() const;
    bool running() const { return m_process != nullptr; }
    QString stage() const { return m_stage; }
    double progress() const { return m_progress; }
    QString error() const { return m_error; }
    int elapsedSeconds() const { return m_clock.isValid() ? int(m_clock.elapsed() / 1000) : 0; }
    Q_INVOKABLE void setOutputPath(const QString &path);
    Q_INVOKABLE void refresh() { emit changed(); }
    Q_INVOKABLE void setupSeedvc();
    Q_INVOKABLE void setSeedvcRuntimePath(const QString &path);
    Q_INVOKABLE void generate(const QVariantMap &source, const QString &voiceId,
                              double referenceStart, double referenceDuration,
                              double guidance, int diffusionSteps);
    Q_INVOKABLE void setVoiceReference(const QString &voiceId, double start, double duration);
    Q_INVOKABLE void addVoice(const QString &title, const QUrl &audio, const QUrl &image,
                             double referenceStart, double referenceDuration);
    Q_INVOKABLE void removeVoice(const QString &id);
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void voiceReady(const QString &clipId, const QString &path,
                    const QString &voiceName, const QString &mode);
private:
    QString seedvcIssue() const;
    void launch(const QVariantMap &request, bool setupJob);
    void consume(const QByteArray &data);
    QString m_stage;
    QString m_setupMessage;
    QString m_error;
    double m_progress = 0;
    QByteArray m_pending;
    QByteArray m_diagnostics;
    QProcess *m_process = nullptr;
    bool m_canceling = false;
    bool m_setupJob = false;
    QString m_clipId;
    QString m_voiceName;
    QString m_mode;
    QString m_result;
    QString m_targetOutput;
    QElapsedTimer m_clock;
    QTimer m_tick;
    double m_expectedConversionSeconds = 30;
#ifdef Q_OS_WIN
    void *m_jobHandle = nullptr;
#endif
};

} // namespace datacut
