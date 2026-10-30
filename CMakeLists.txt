cmake_minimum_required(VERSION 3.22)

# Built on macOS 12.6 with Xcode 14.2. Qt 6.7 is the newest kit that compiler
# can use, and its own floor is macOS 11. Set before project() so the Apple
# toolchain records it. Windows ignores the variable.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)

project(QmlBrowser VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# One Xcode scheme per target, with Debug as a run configuration.
set(CMAKE_XCODE_GENERATE_SCHEME ON)

# macOS 12.6 stops at Xcode 14.2, so that build stays on Qt 6.7.
# Windows keeps the Qt 6.10 kit in qt.props. The renderer and QmlBrowserHost
# use Qt6::GuiPrivate. Qt 6.7 defines it along with Gui; from Qt 6.9 it is a
# package of its own that has to be asked for.
if(APPLE)
    find_package(Qt6 6.7...<6.8 REQUIRED COMPONENTS Core Network Gui Qml Quick Widgets OpenGL OpenGLWidgets Test)
else()
    find_package(Qt6 6.10 REQUIRED COMPONENTS Core Network Gui GuiPrivate Qml Quick Widgets OpenGL OpenGLWidgets Test)
endif()
# The widget app uses these when the kit has them. Without Qt SVG the vector
# page skips its SVG item; without Qt Multimedia the video page plays only
# its generated clip.
find_package(Qt6 QUIET OPTIONAL_COMPONENTS Svg Multimedia)

if(APPLE)
    get_filename_component(QMLBROWSER_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    # So an Xcode Debug run finds QtCore and the cocoa plugin without a deploy step.
    set(CMAKE_BUILD_RPATH "${QMLBROWSER_QT_PREFIX}/lib")
endif()

qt_standard_project_setup()

# qt_standard_project_setup enables moc and uic only. The client and renderer
# still ship their QML through resources.qrc, so autorcc has to be on or
# qrc:/ui/Browser.qml is missing at runtime.
set(CMAKE_AUTORCC ON)

qt_add_library(qmlbrowser_ipc STATIC
    shared/InputEvents.cpp
    shared/InputEvents.h
    shared/IpcChannel.cpp
    shared/IpcChannel.h
    shared/SharedPixels.cpp
    shared/SharedPixels.h
)
target_include_directories(qmlbrowser_ipc PUBLIC "${CMAKE_SOURCE_DIR}/shared")
target_link_libraries(qmlbrowser_ipc PUBLIC Qt6::Gui Qt6::Network)

qt_add_executable(QmlServer
    server/main.cpp
    server/HttpServer.cpp
    server/HttpServer.h
)
target_link_libraries(QmlServer PRIVATE Qt6::Core Qt6::Network)
set_target_properties(QmlServer PROPERTIES
    MACOSX_BUNDLE FALSE
    WIN32_EXECUTABLE FALSE
)

qt_add_executable(QmlBrowser
    client/main.cpp
    client/AppRegistry.cpp
    client/AppRegistry.h
    client/BrowserHistory.cpp
    client/BrowserHistory.h
    client/BrowserTab.cpp
    client/BrowserTab.h
    client/RendererShutdown.cpp
    client/RendererShutdown.h
    client/TabManager.cpp
    client/TabManager.h
    client/TabViewport.cpp
    client/TabViewport.h
    client/WindowFrame.cpp
    client/WindowFrame.h
    client/resources.qrc
)
target_link_libraries(QmlBrowser PRIVATE qmlbrowser_ipc Qt6::Gui Qt6::Qml Qt6::Quick)
if(WIN32)
    target_link_libraries(QmlBrowser PRIVATE dwmapi)
endif()
if(APPLE)
    # The trackpad's "swipe between pages" setting.
    target_link_libraries(QmlBrowser PRIVATE "-framework CoreFoundation")
endif()

qt_add_executable(QmlRenderer
    renderer/main.cpp
    renderer/OffscreenPage.cpp
    renderer/OffscreenPage.h
    renderer/PageView.cpp
    renderer/PageView.h
    renderer/native/CanvasLibrary.cpp
    renderer/native/CanvasLibrary.h
    renderer/native/ClockFace.cpp
    renderer/native/ClockFace.h
    renderer/native/JuliaSet.cpp
    renderer/native/JuliaSet.h
    renderer/native/NativeCanvas.cpp
    renderer/native/NativeCanvas.h
    renderer/native/NativeItems.cpp
    renderer/native/NativeItems.h
    renderer/native/WaveShape.cpp
    renderer/native/WaveShape.h
    renderer/resources.qrc
)
target_include_directories(QmlRenderer PRIVATE "${CMAKE_SOURCE_DIR}/canvas/include")
target_link_libraries(QmlRenderer PRIVATE qmlbrowser_ipc Qt6::Gui Qt6::GuiPrivate Qt6::Qml Qt6::Quick)

# A canvas library: plain C, no Qt (canvas/include/qmlbrowser_canvas.h). The
# renderer loads canvas/<name> from its own folder, so the build copies it
# there and next to the smoke test.
add_library(plasma SHARED canvas/plasma/plasma.c)
target_include_directories(plasma PRIVATE "${CMAKE_SOURCE_DIR}/canvas/include")
set_target_properties(plasma PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)
if(UNIX AND NOT APPLE)
    target_link_libraries(plasma PRIVATE m)
endif()

# Loads the plasma library and shows it through NativeCanvas, headless.
qt_add_executable(CanvasSmokeTest
    tests/CanvasSmokeTest.cpp
    tests/CanvasSmokeTest.h
    renderer/native/CanvasLibrary.cpp
    renderer/native/CanvasLibrary.h
    renderer/native/NativeCanvas.cpp
    renderer/native/NativeCanvas.h
)
target_include_directories(CanvasSmokeTest PRIVATE
    "${CMAKE_SOURCE_DIR}/renderer/native"
    "${CMAKE_SOURCE_DIR}/canvas/include")
target_link_libraries(CanvasSmokeTest PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
set_target_properties(CanvasSmokeTest PROPERTIES MACOSX_BUNDLE FALSE WIN32_EXECUTABLE FALSE)

# Sends trackpad scrolls and pinches through the browser's input messages to
# a Qt Quick scene, headless, and compares them with direct delivery.
qt_add_executable(InputEventsTest
    tests/InputEventsTest.cpp
    tests/InputEventsTest.h
)
target_link_libraries(InputEventsTest PRIVATE qmlbrowser_ipc Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
set_target_properties(InputEventsTest PROPERTIES MACOSX_BUNDLE FALSE WIN32_EXECUTABLE FALSE)

# On macOS the copy lands inside QmlRenderer.app before that bundle is
# signed, and codesign refuses a bundle holding unsigned code. Xcode signs
# plasma itself only after this step, so the copy is signed here (ad hoc).
if(APPLE)
    set(_canvas_sign codesign --force --sign -)
else()
    set(_canvas_sign ${CMAKE_COMMAND} -E true)
endif()
foreach(_canvas_host QmlRenderer CanvasSmokeTest)
    add_custom_command(TARGET plasma POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${_canvas_host}>/canvas"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:plasma>" "$<TARGET_FILE_DIR:${_canvas_host}>/canvas/"
        COMMAND ${_canvas_sign} "$<TARGET_FILE_DIR:${_canvas_host}>/canvas/$<TARGET_FILE_NAME:plasma>"
        COMMENT "Copy the plasma canvas library next to ${_canvas_host}"
        VERBATIM)
endforeach()
add_dependencies(QmlRenderer plasma)
add_dependencies(CanvasSmokeTest plasma)

enable_testing()
add_test(NAME CanvasSmokeTest COMMAND CanvasSmokeTest)
add_test(NAME InputEventsTest COMMAND InputEventsTest)

# Puts a Qt Widgets application in an app://<name> tab. An application links
# this and calls QmlBrowserHost::prepare() and attach() from main(). Only
# include/ is public; the bridge uses Qt's private QPA headers internally.
qt_add_library(QmlBrowserHost STATIC
    apphost/include/QmlBrowserHost.h
    apphost/src/BrowserBridge.cpp
    apphost/src/BrowserBridge.h
    apphost/src/InputInjector.cpp
    apphost/src/InputInjector.h
    apphost/src/NativePlatform.cpp
    apphost/src/NativePlatform.h
    apphost/src/OffscreenPlatform.cpp
    apphost/src/OffscreenPlatform.h
    apphost/src/QmlBrowserHost.cpp
    apphost/src/WindowCompositor.cpp
    apphost/src/WindowCompositor.h
)
target_include_directories(QmlBrowserHost PUBLIC "${CMAKE_SOURCE_DIR}/apphost/include")
target_link_libraries(QmlBrowserHost
    PUBLIC Qt6::Widgets
    PRIVATE qmlbrowser_ipc Qt6::Gui Qt6::GuiPrivate)

# A Qt Widgets application that runs inside an app://widgets tab, through
# QmlBrowserHost. It is also the library's example application.
qt_add_executable(WidgetApp
    widgetapp/main.cpp
    widgetapp/ControlsPage.cpp
    widgetapp/ControlsPage.h
    widgetapp/GalleryWindow.cpp
    widgetapp/GalleryWindow.h
    widgetapp/PaintPage.cpp
    widgetapp/PaintPage.h
    widgetapp/PicturePage.cpp
    widgetapp/PicturePage.h
    widgetapp/ScenePage.cpp
    widgetapp/ScenePage.h
    widgetapp/VectorPage.cpp
    widgetapp/VectorPage.h
    widgetapp/VideoPage.cpp
    widgetapp/VideoPage.h
)
target_link_libraries(WidgetApp PRIVATE QmlBrowserHost Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
if(TARGET Qt6::Svg)
    target_link_libraries(WidgetApp PRIVATE Qt6::Svg)
    target_compile_definitions(WidgetApp PRIVATE WIDGETAPP_HAVE_SVG)
endif()
if(TARGET Qt6::Multimedia)
    target_link_libraries(WidgetApp PRIVATE Qt6::Multimedia)
    target_compile_definitions(WidgetApp PRIVATE WIDGETAPP_HAVE_MULTIMEDIA)
endif()
message(STATUS "WidgetApp: Qt SVG ${Qt6Svg_FOUND}, Qt Multimedia ${Qt6Multimedia_FOUND}")

if(APPLE)
    # Local debugging does not need an Apple Developer team.
    set(_qmlbrowser_nosign
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
    )
    # qt_add_executable does not create a bundle. The qt.conf copy and the
    # renderer lookup both require QmlBrowser.app and QmlRenderer.app.
    set_target_properties(QmlBrowser QmlRenderer WidgetApp PROPERTIES
        MACOSX_BUNDLE TRUE
        ${_qmlbrowser_nosign}
        MACOSX_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}"
        XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}"
    )
    set_target_properties(QmlServer PROPERTIES
        ${_qmlbrowser_nosign}
        MACOSX_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}"
        XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}"
    )
    set_target_properties(QmlBrowser PROPERTIES
        XCODE_SCHEME_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    # Each tab is a QmlRenderer process. LSUIElement makes that bundle an agent,
    # so macOS does not give it a Dock icon. Only QmlBrowser stays in the Dock.
    set_target_properties(QmlRenderer PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/renderer/Info.plist.in"
    )

    function(qmlbrowser_bundle_qt_conf target)
        set(conf "${CMAKE_BINARY_DIR}/${target}-qt.conf")
        file(WRITE "${conf}" "[Paths]\nPrefix=${QMLBROWSER_QT_PREFIX}\n")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources"
            COMMAND ${CMAKE_COMMAND} -E copy "${conf}" "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/qt.conf"
            COMMENT "Point ${target} at the Qt macOS kit"
            VERBATIM)
    endfunction()

    qmlbrowser_bundle_qt_conf(QmlBrowser)
    qmlbrowser_bundle_qt_conf(QmlRenderer)
    # Hosted, the app uses the offscreen platform and never touches the Dock.
    # Run on its own it is a normal application, so it keeps the default
    # Info.plist rather than the renderer's LSUIElement one.
    qmlbrowser_bundle_qt_conf(WidgetApp)
endif()
