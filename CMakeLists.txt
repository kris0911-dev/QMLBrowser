cmake_minimum_required(VERSION 3.22)

# Built on macOS 12.6 with Xcode 14.2. Qt 6.7 is the newest kit that compiler
# can use, and its own floor is macOS 11. Set before project() so the Apple
# toolchain records it. Windows ignores the variable.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)

project(QmlBrowser VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# One Xcode scheme per target, with Debug as a run configuration.
set(CMAKE_XCODE_GENERATE_SCHEME ON)

# macOS 12.6 stops at Xcode 14.2, so that build stays on Qt 6.7.
# Windows keeps the Qt 6.10 kit in qt.props.
if(APPLE)
    find_package(Qt6 6.7...<6.8 REQUIRED COMPONENTS Core Network Gui Qml Quick)
else()
    find_package(Qt6 6.10 REQUIRED COMPONENTS Core Network Gui Qml Quick)
endif()

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
    shared/IpcChannel.cpp
    shared/IpcChannel.h
)
target_include_directories(qmlbrowser_ipc PUBLIC "${CMAKE_SOURCE_DIR}/shared")
target_link_libraries(qmlbrowser_ipc PUBLIC Qt6::Network)

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
    client/BrowserHistory.cpp
    client/BrowserHistory.h
    client/BrowserTab.cpp
    client/BrowserTab.h
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

qt_add_executable(QmlRenderer
    renderer/main.cpp
    renderer/PageView.cpp
    renderer/PageView.h
    renderer/resources.qrc
)
target_link_libraries(QmlRenderer PRIVATE qmlbrowser_ipc Qt6::Gui Qt6::Qml Qt6::Quick)
if(UNIX AND NOT APPLE)
    # X11 only. The macOS build keeps using AppKit and must not look for X11.
    find_package(X11 REQUIRED)
    target_link_libraries(QmlRenderer PRIVATE X11::X11)
    target_compile_definitions(QmlRenderer PRIVATE QMLBROWSER_HAS_X11)
endif()

if(APPLE)
    # Local debugging does not need an Apple Developer team.
    set(_qmlbrowser_nosign
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
    )
    # qt_add_executable does not create a bundle. The qt.conf copy and the
    # renderer lookup both require QmlBrowser.app and QmlRenderer.app.
    set_target_properties(QmlBrowser QmlRenderer PROPERTIES
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
endif()
