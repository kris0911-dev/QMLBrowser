@echo off
REM Starts the server in its own window, then opens the browser on it.
REM
REM Usage: run.bat [Debug|Release]

setlocal

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
set "BIN=%~dp0bin\x64\%CONFIG%"

if not exist "%BIN%\QmlServer.exe" (
    echo %BIN%\QmlServer.exe not found. Run "build.bat %CONFIG%" first.
    exit /b 1
)

start "QML server" "%BIN%\QmlServer.exe"
REM Give the listening socket a moment before the first request.
ping -n 2 127.0.0.1 >nul
start "" "%BIN%\QmlBrowser.exe" http://127.0.0.1:8080/index.qml

endlocal
