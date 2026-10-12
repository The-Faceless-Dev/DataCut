#include "platform/windows/WindowControls.h"

#include <QCoreApplication>
#include <QTimer>

namespace datacut {

WindowControls::WindowControls(QObject *parent) : QObject(parent) {}

WindowControls::~WindowControls()
{
    if (m_hwnd) QCoreApplication::instance()->removeNativeEventFilter(this);
}

void WindowControls::attach(QQuickWindow *window)
{
    m_window = window;
    m_hwnd = reinterpret_cast<HWND>(window->winId());
    m_restoreMaximized = IsZoomed(m_hwnd);
    m_lastMaximized = maximized();
    m_lastMinimized = minimized();
    QCoreApplication::instance()->installNativeEventFilter(this);
    emit stateChanged();
}

bool WindowControls::maximized() const
{
    return m_hwnd && IsZoomed(m_hwnd) && !IsIconic(m_hwnd);
}

bool WindowControls::minimized() const
{
    return m_hwnd && IsIconic(m_hwnd);
}

void WindowControls::minimize()
{
    if (!m_hwnd || minimized()) return;
    m_restoreMaximized = maximized();
    ShowWindow(m_hwnd, SW_MINIMIZE);
    queueStateRefresh();
}

void WindowControls::toggleMaximized()
{
    if (!m_hwnd) return;
    if (minimized()) {
        ShowWindow(m_hwnd, m_restoreMaximized ? SW_SHOWMAXIMIZED : SW_RESTORE);
    } else if (IsZoomed(m_hwnd)) {
        ShowWindow(m_hwnd, SW_SHOWNORMAL);
    } else {
        ShowWindow(m_hwnd, SW_MAXIMIZE);
    }
    if (!minimized()) m_restoreMaximized = IsZoomed(m_hwnd);
    queueStateRefresh();
}

void WindowControls::queueStateRefresh()
{
    if (m_refreshQueued) return;
    m_refreshQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_refreshQueued = false;
        const bool isMaximized = maximized();
        const bool isMinimized = minimized();
        if (!isMinimized) m_restoreMaximized = isMaximized;
        if (m_lastMaximized != isMaximized || m_lastMinimized != isMinimized) {
            m_lastMaximized = isMaximized;
            m_lastMinimized = isMinimized;
            emit stateChanged();
        }
    });
}

void WindowControls::queueTaskbarRestore()
{
    if (m_restoreQueued) return;
    m_restoreQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_restoreQueued = false;
        if (!m_window || !m_hwnd) return;
        if (IsIconic(m_hwnd))
            ShowWindow(m_hwnd, m_restoreMaximized ? SW_SHOWMAXIMIZED : SW_RESTORE);
        if (!IsIconic(m_hwnd)) {
            m_window->raise();
            m_window->requestActivate();
        }
        queueStateRefresh();
    });
}

bool WindowControls::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    if (eventType != "windows_generic_MSG") return false;
    const auto *msg = static_cast<MSG *>(message);
    if (msg->hwnd != m_hwnd) return false;

    if (msg->message == WM_SYSCOMMAND && (msg->wParam & 0xfff0) == SC_MINIMIZE)
        m_restoreMaximized = IsZoomed(m_hwnd);

    const bool queryOpen = msg->message == WM_QUERYOPEN;
    const bool restoreCommand = msg->message == WM_SYSCOMMAND
        && (msg->wParam & 0xfff0) == SC_RESTORE && IsIconic(m_hwnd);
    const bool activateMinimized = IsIconic(m_hwnd)
        && ((msg->message == WM_ACTIVATE && LOWORD(msg->wParam) != WA_INACTIVE)
            || (msg->message == WM_ACTIVATEAPP && msg->wParam));
    if (queryOpen || restoreCommand || activateMinimized) queueTaskbarRestore();

    if (msg->message == WM_SIZE || msg->message == WM_WINDOWPOSCHANGED
        || msg->message == WM_SHOWWINDOW) queueStateRefresh();

    if (queryOpen) {
        *result = TRUE;
        return true;
    }
    return false;
}

} // namespace datacut
