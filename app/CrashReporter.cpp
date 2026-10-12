#include "app/CrashReporter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <iterator>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#endif

namespace datacut {
namespace {
CrashReporter *activeReporter = nullptr;
QtMessageHandler previousHandler = nullptr;

#ifdef Q_OS_WIN
wchar_t dumpPath[32768] = {};

LONG WINAPI writeCrashDump(EXCEPTION_POINTERS *exception)
{
    if (!dumpPath[0]) return EXCEPTION_CONTINUE_SEARCH;
    const HANDLE file = CreateFileW(dumpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION details{};
        details.ThreadId = GetCurrentThreadId();
        details.ExceptionPointers = exception;
        details.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
            static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo),
            &details, nullptr, nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH; // Windows Error Reporting can record it too.
}
#endif
} // namespace

CrashReporter::CrashReporter(QObject *parent) : QObject(parent)
{
    m_reportDirectory = QDir(QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("diagnostics"));
    if (!QDir().mkpath(m_reportDirectory)) return;
    m_markerPath = QDir(m_reportDirectory).filePath(QStringLiteral("active-session.json"));
    QFile previous(m_markerPath);
    if (previous.open(QIODevice::ReadOnly)) {
        m_previousExitUnexpected = true;
        const auto record = QJsonDocument::fromJson(previous.readAll()).object();
        m_previousLogPath = record.value(QStringLiteral("log")).toString();
        m_previousDumpPath = record.value(QStringLiteral("dump")).toString();
    }

    const QString stamp = QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString base = QStringLiteral("datacut-%1-%2")
        .arg(stamp).arg(QCoreApplication::applicationPid());
    const QString logPath = QDir(m_reportDirectory).filePath(base + QStringLiteral(".log"));
    const QString crashPath = QDir(m_reportDirectory).filePath(base + QStringLiteral(".dmp"));
    m_log.setFileName(logPath);
    const bool logOpened = m_log.open(QIODevice::WriteOnly | QIODevice::Append
                                     | QIODevice::Unbuffered);
    if (!logOpened) m_log.setFileName(QString{});

    QSaveFile marker(m_markerPath);
    if (marker.open(QIODevice::WriteOnly)) {
        const QByteArray data = QJsonDocument(QJsonObject{
            {QStringLiteral("log"), logPath},
            {QStringLiteral("dump"), crashPath},
            {QStringLiteral("startedUtc"), QDateTime::currentDateTimeUtc()
                .toString(Qt::ISODateWithMs)}}).toJson();
        if (marker.write(data) == data.size()) marker.commit();
    }

#ifdef Q_OS_WIN
    const std::wstring nativePath = QDir::toNativeSeparators(crashPath).toStdWString();
    if (nativePath.size() < std::size(dumpPath)) {
        std::copy(nativePath.cbegin(), nativePath.cend(), dumpPath);
        dumpPath[nativePath.size()] = L'\0';
        SetUnhandledExceptionFilter(writeCrashDump);
    }
#endif
    activeReporter = this;
    previousHandler = qInstallMessageHandler(messageHandler);
    qInfo().noquote() << "DataCut session started; version:"
                      << QCoreApplication::applicationVersion()
                      << "executable:" << QCoreApplication::applicationFilePath()
                      << "diagnostics:" << logPath;
    if (m_previousExitUnexpected)
        qWarning().noquote() << "Previous session ended unexpectedly; previous log:"
                             << m_previousLogPath;
}

CrashReporter::~CrashReporter()
{
    if (activeReporter == this) {
        qInstallMessageHandler(previousHandler);
        activeReporter = nullptr;
    }
    markCleanExit();
}

void CrashReporter::messageHandler(QtMsgType type, const QMessageLogContext &context,
                                   const QString &message)
{
    if (activeReporter) activeReporter->writeMessage(type, context, message);
    if (previousHandler) previousHandler(type, context, message);
}

void CrashReporter::writeMessage(QtMsgType type, const QMessageLogContext &context,
                                 const QString &message)
{
    if (!m_log.isOpen()) return;
    const char *severity = type == QtDebugMsg ? "DEBUG" : type == QtInfoMsg ? "INFO"
        : type == QtWarningMsg ? "WARN" : type == QtCriticalMsg ? "ERROR" : "FATAL";
    const QString category = context.category ? QString::fromUtf8(context.category)
                                              : QStringLiteral("app");
    const QByteArray line = QStringLiteral("%1 [%2] [thread %3] [%4] %5\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
             QString::fromLatin1(severity),
             QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16),
             category, message).toUtf8();
    QMutexLocker lock(&m_logMutex);
    m_log.write(line);
    m_log.flush();
}

void CrashReporter::markCleanExit()
{
    if (m_cleanExit) return;
    m_cleanExit = true;
    qInfo() << "DataCut session closed normally";
    if (!m_markerPath.isEmpty()) QFile::remove(m_markerPath);
}

void CrashReporter::openReportDirectory() const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_reportDirectory));
}

} // namespace datacut
