@echo off
REM Runs the smoke tests against a build from build.bat. They are headless
REM (offscreen platform, software scene graph), so they also run in CI.
REM
REM Usage: test.bat [Debug|Release]

setlocal

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
set "BIN=%~dp0bin\x64\%CONFIG%"

if not exist "%BIN%\CanvasSmokeTest.exe" (
    echo %BIN%\CanvasSmokeTest.exe not found. Run "build.bat %CONFIG%" first.
    exit /b 1
)

"%BIN%\CanvasSmokeTest.exe"
set "RESULT=%ERRORLEVEL%"

echo.
if "%RESULT%"=="0" (echo Smoke tests passed.) else (echo Smoke tests FAILED.)
exit /b %RESULT%
