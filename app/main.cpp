#include "app/EditorController.h"
#include "app/CrashReporter.h"
#include "app/GenerationManager.h"
#include "app/MediaGenerationManager.h"
#include "app/VoiceGenerationManager.h"
#include "app/StartupSplash.h"
#ifdef Q_OS_WIN
#include "platform/windows/WindowControls.h"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QScreen>
#include <QQuickStyle>
#include <QTimer>
#include <QFile>
#include <QIcon>
#include <QPixmap>
#include <QEventLoop>

#include <memory>

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // Child runtimes inherit this mode. Loader/DLL failures become process exit
    // codes captured by our job UI instead of blocking on a Windows dialog.
    SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    QApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("DataCut"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.7-beta.1"));
    const QIcon applicationIcon(QStringLiteral(":/app/datacut.png"));
    QGuiApplication::setWindowIcon(applicationIcon);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    const QString exportProject = qEnvironmentVariable("DATACUT_DIAGNOSTIC_EXPORT_PROJECT");
    const QString exportOutput = qEnvironmentVariable("DATACUT_DIAGNOSTIC_EXPORT_OUTPUT");
    const QString capturePath = qEnvironmentVariable("DATACUT_CAPTURE_UI");
    std::unique_ptr<datacut::StartupSplash> splash;
    if (exportProject.isEmpty() && capturePath.isEmpty()) {
        QPixmap artwork(QStringLiteral(":/app/splashscreen.png"));
        if (!artwork.isNull()) {
            const QScreen *screen = QGuiApplication::primaryScreen();
            if (screen) {
                const int width = qMin(960, qRound(screen->availableGeometry().width() * 0.75));
                artwork = artwork.scaledToWidth(width, Qt::SmoothTransformation);
            }
            splash = std::make_unique<datacut::StartupSplash>(artwork);
            splash->show();
            app.processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }
    const auto startupStage = [&app, &splash](const QString &message) {
        if (!splash) return;
        splash->setStage(message);
        app.processEvents(QEventLoop::ExcludeUserInputEvents);
    };
    startupStage(QStringLiteral("Initializing editor"));

    datacut::CrashReporter crashReporter;
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     &crashReporter, &datacut::CrashReporter::markCleanExit);

    datacut::EditorController controller;
    startupStage(QStringLiteral("Preparing media and project tools"));
    datacut::GenerationManager generations;
    datacut::MediaGenerationManager mediaGenerations;
    datacut::VoiceGenerationManager voiceGenerations;
#ifdef Q_OS_WIN
    datacut::WindowControls windowControls;
#endif
    if (!exportProject.isEmpty() && !exportOutput.isEmpty()) {
        controller.openProject(QUrl::fromLocalFile(exportProject));
        QObject::connect(&controller, &datacut::EditorController::exportChanged, &app,
            [&controller, &app] {
                if (!controller.exportRunning()
                    && (controller.exportStage() == QStringLiteral("Completed")
                        || controller.exportStage() == QStringLiteral("Failed"))) {
                    QTimer::singleShot(0, &app, [&controller, &app] {
                        fprintf(stderr, "%s\n", controller.status().toUtf8().constData());
                        app.quit();
                    });
                }
            });
        QTimer::singleShot(0, &app, [&controller, exportOutput] {
            controller.startExport(QStringLiteral("projectVideo"),
                                   QUrl::fromLocalFile(exportOutput));
            if (!controller.exportRunning()) {
                fprintf(stderr, "%s\n", controller.status().toUtf8().constData());
                QCoreApplication::quit();
            }
        });
        return app.exec();
    }
    if (qEnvironmentVariableIsSet("DATACUT_OPEN_DEMO")) {
        startupStage(QStringLiteral("Opening demo project"));
        controller.openDemoProject();
    }
    const QString captureProject = qEnvironmentVariable("DATACUT_CAPTURE_PROJECT");
    if (!captureProject.isEmpty()) {
        startupStage(QStringLiteral("Opening project"));
        controller.openProject(QUrl::fromLocalFile(captureProject));
        bool frameOkay = false;
        const qint64 frame = qEnvironmentVariable("DATACUT_CAPTURE_FRAME")
                                 .toLongLong(&frameOkay);
        if (frameOkay) controller.seekTimelineFrame(frame);
    }
    startupStage(QStringLiteral("Building workspace"));
    QQmlApplicationEngine engine;
    const QString qmlDiagnosticsPath = qEnvironmentVariable("DATACUT_QML_DIAGNOSTICS");
    if (!qmlDiagnosticsPath.isEmpty()) {
        QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
            [qmlDiagnosticsPath](const QList<QQmlError> &warnings) {
                QFile file(qmlDiagnosticsPath);
                if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return;
                for (const auto &warning : warnings)
                    file.write(warning.toString().toUtf8() + '\n');
            });
    }
    engine.rootContext()->setContextProperty(QStringLiteral("editor"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("crashReporter"), &crashReporter);
    engine.rootContext()->setContextProperty(QStringLiteral("generations"), &generations);
    engine.rootContext()->setContextProperty(QStringLiteral("mediaGenerations"), &mediaGenerations);
    engine.rootContext()->setContextProperty(QStringLiteral("voiceGenerations"), &voiceGenerations);
    QObject::connect(&generations, &datacut::GenerationManager::settingsChanged,
                     &mediaGenerations, &datacut::MediaGenerationManager::refresh);
    QObject::connect(&generations, &datacut::GenerationManager::settingsChanged,
                     &voiceGenerations, &datacut::VoiceGenerationManager::refresh);
    QObject::connect(&generations, &datacut::GenerationManager::captionWordsReady,
        &controller, &datacut::EditorController::acceptGeneratedCaptions);
    QObject::connect(&controller, &datacut::EditorController::autoCaptionRequested,
        &generations, &datacut::GenerationManager::transcribeClip);
    QObject::connect(&voiceGenerations, &datacut::VoiceGenerationManager::voiceReady,
        &controller, &datacut::EditorController::applyVoiceChange);
#ifdef Q_OS_WIN
    engine.rootContext()->setContextProperty(QStringLiteral("windowControls"), &windowControls);
#endif
    engine.loadFromModule("DataCut", "MainWindow");
    if (engine.rootObjects().isEmpty()) {
        if (splash) splash->close();
        return 1;
    }
    startupStage(QStringLiteral("Showing editor"));
    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
        window->setIcon(applicationIcon);
        if (splash) {
            QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&splash] {
                if (splash && splash->isVisible()) splash->close();
            }, Qt::QueuedConnection);
            QTimer::singleShot(5000, &app, [&splash] {
                if (splash && splash->isVisible()) splash->close();
            });
        }
        if (!capturePath.isEmpty()) {
            window->resize(1672, 941);
            window->show();
            // Diagnostic captures can include an actual playback frame, which
            // exercises bindings that a static seek does not.
            if (qEnvironmentVariableIsSet("DATACUT_CAPTURE_PLAYBACK"))
                QTimer::singleShot(3000, &app, [&controller] {
                    controller.toggleTimelinePlayback();
                });
            bool validDelay = false;
            const int requestedDelay = qEnvironmentVariableIntValue(
                "DATACUT_CAPTURE_DELAY_MS", &validDelay);
            const int captureDelay = validDelay ? qBound(500, requestedDelay, 30000) : 2000;
            QTimer::singleShot(captureDelay, &app, [window, capturePath, &app] {
                QImage captured = window->grabWindow();
                if (qEnvironmentVariableIsSet("DATACUT_CAPTURE_VIEWPORT")) {
                    if (auto *viewport = window->findChild<QQuickItem *>(
                            QStringLiteral("previewVisual"))) {
                        const QRectF sceneRect = viewport->mapRectToScene(
                            QRectF(0, 0, viewport->width(), viewport->height()));
                        const double scaleX = double(captured.width()) / window->width();
                        const double scaleY = double(captured.height()) / window->height();
                        captured = captured.copy(QRect(qRound(sceneRect.x() * scaleX),
                            qRound(sceneRect.y() * scaleY), qRound(sceneRect.width() * scaleX),
                            qRound(sceneRect.height() * scaleY)));
                    }
                }
                captured.save(capturePath);
                app.quit();
            });
        } else {
            const QScreen *screen = window->screen() ? window->screen()
                                                     : QGuiApplication::primaryScreen();
            if (screen) {
                const QRect available = screen->availableGeometry();
                const int minimumWidth = qMin(1150, available.width());
                const int minimumHeight = qMin(700, available.height());
                window->setMinimumSize({minimumWidth, minimumHeight});
                const int normalWidth = qBound(minimumWidth,
                    qRound(available.width() * 0.9), qMin(1672, available.width()));
                const int normalHeight = qBound(minimumHeight,
                    qRound(available.height() * 0.9), qMin(941, available.height()));
                window->setGeometry(available.x() + (available.width() - normalWidth) / 2,
                                    available.y() + (available.height() - normalHeight) / 2,
                                    normalWidth, normalHeight);
            }
            window->showMaximized();
        }
#ifdef Q_OS_WIN
        windowControls.attach(window);
#endif
    } else if (splash) splash->close();
    return app.exec();
}
