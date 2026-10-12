#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QQuickWindow>

#include <windows.h>

namespace datacut {

// Windows owns the actual minimize/maximize state for the frameless main window.
// The QML title bar reads this object instead of inferring it from Qt visibility.
class WindowControls final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(bool maximized READ maximized NOTIFY stateChanged)
    Q_PROPERTY(bool minimized READ minimized NOTIFY stateChanged)

public:
    explicit WindowControls(QObject *parent = nullptr);
    ~WindowControls() override;

    void attach(QQuickWindow *window);
    bool maximized() const;
    bool minimized() const;

    Q_INVOKABLE void minimize();
    Q_INVOKABLE void toggleMaximized();

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void stateChanged();

private:
    void queueStateRefresh();
    void queueTaskbarRestore();

    QPointer<QQuickWindow> m_window;
    HWND m_hwnd = nullptr;
    bool m_restoreMaximized = false;
    bool m_lastMaximized = false;
    bool m_lastMinimized = false;
    bool m_refreshQueued = false;
    bool m_restoreQueued = false;
};

} // namespace datacut
