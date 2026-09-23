cmake_minimum_required(VERSION 3.16)

project(QMLBrowser VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 WebEngineQuick)

qt_add_executable(QMLBrowser
    src/main.cpp
    qml.qrc
)

target_link_libraries(QMLBrowser PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::WebEngineQuick
)

install(TARGETS QMLBrowser
    RUNTIME DESTINATION bin
)
