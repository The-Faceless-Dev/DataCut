cmake_minimum_required(VERSION 3.24)
project(DataCut VERSION 0.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Quick QuickControls2 Multimedia Concurrent ShaderTools)
qt_standard_project_setup(REQUIRES 6.5)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
    qt_policy(SET QTP0004 NEW)
endif()

find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h REQUIRED)
find_library(AVFORMAT_LIBRARY NAMES avformat REQUIRED)
find_library(AVCODEC_LIBRARY NAMES avcodec REQUIRED)
find_library(AVUTIL_LIBRARY NAMES avutil REQUIRED)
find_library(SWSCALE_LIBRARY NAMES swscale REQUIRED)

qt_add_executable(datacut
    app/main.cpp
    app/StartupSplash.cpp
    app/StartupSplash.h
    app/CrashReporter.cpp
    app/CrashReporter.h
    app/EditorController.cpp
    app/EditorController.h
    app/GenerationManager.cpp
    app/GenerationManager.h
    app/MediaGenerationManager.cpp
    app/MediaGenerationManager.h
    app/VoiceGenerationManager.cpp
    app/VoiceGenerationManager.h
    core/FrameTime.h
    core/MediaRange.h
    media/MediaProbe.cpp
    media/MediaProbe.h
    media/ThumbnailGenerator.cpp
    media/ThumbnailGenerator.h
    media/WaveformGenerator.cpp
    media/WaveformGenerator.h
    media/FilmstripGenerator.cpp
    media/FilmstripGenerator.h
    effects/EffectRegistry.cpp
    effects/EffectRegistry.h
    transitions/TransitionRegistry.cpp
    transitions/TransitionRegistry.h
    media/ProxyCache.cpp
    media/ProxyCache.h
)

if(WIN32)
    enable_language(RC)
    target_sources(datacut PRIVATE
        app/datacut.rc
        platform/windows/WindowControls.cpp
        platform/windows/WindowControls.h
    )
    target_link_libraries(datacut PRIVATE dbghelp)
endif()

qt_add_resources(datacut datacut_branding PREFIX "/"
    FILES app/datacut.ico app/datacut.png app/splashscreen.png)

qt_add_shaders(datacut datacut_effect_shaders
    PREFIX "/shaders"
    FILES render/shaders/video_effects.frag
)

qt_add_qml_module(datacut
    URI DataCut
    VERSION 1.0
    QML_FILES
        ui/MainWindow.qml
        ui/JobProgressBar.qml
        ui/VolumeIcon.qml
)

target_include_directories(datacut PRIVATE ${AVFORMAT_INCLUDE_DIR})
target_link_libraries(datacut PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Concurrent
    ${AVFORMAT_LIBRARY} ${AVCODEC_LIBRARY} ${AVUTIL_LIBRARY} ${SWSCALE_LIBRARY}
)

set_target_properties(datacut PROPERTIES WIN32_EXECUTABLE TRUE)
