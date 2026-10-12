#pragma once

#include <QObject>
#include <QFile>
#include <QMutex>

namespace datacut {

// Keeps a small, durable diagnostic trail outside the project directory.
// A previous session marker means the process ended without a normal shutdown.
class CrashReporter final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool previousExitUnexpected READ previousExitUnexpected CONSTANT)
    Q_PROPERTY(QString reportDirectory READ reportDirectory CONSTANT)
    Q_PROPERTY(QString previousLogPath READ previousLogPath CONSTANT)
    Q_PROPERTY(QString previousDumpPath READ previousDumpPath CONSTANT)

public:
    explicit CrashReporter(QObject *parent = nullptr);
    ~CrashReporter() override;

    [[nodiscard]] bool previousExitUnexpected() const { return m_previousExitUnexpected; }
    [[nodiscard]] QString reportDirectory() const { return m_reportDirectory; }
    [[nodiscard]] QString previousLogPath() const { return m_previousLogPath; }
    [[nodiscard]] QString previousDumpPath() const { return m_previousDumpPath; }

    Q_INVOKABLE void openReportDirectory() const;
    void markCleanExit();

private:
    static void messageHandler(QtMsgType type, const QMessageLogContext &context,
                               const QString &message);
    void writeMessage(QtMsgType type, const QMessageLogContext &context,
                      const QString &message);

    QString m_reportDirectory;
    QString m_markerPath;
    QString m_previousLogPath;
    QString m_previousDumpPath;
    QFile m_log;
    QMutex m_logMutex;
    bool m_previousExitUnexpected = false;
    bool m_cleanExit = false;
};

} // namespace datacut
