@echo off
REM Builds both projects from a plain command prompt. Opening QmlBrowser.sln in
REM Visual Studio and pressing Ctrl+Shift+B does exactly the same thing.
REM
REM Usage: build.bat [Debug|Release]

setlocal

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Could not find vswhere.exe. Is Visual Studio installed?
    exit /b 1
)

set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"

if not defined VSPATH (
    echo No Visual Studio installation with the C++ workload was found.
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo Failed to initialise the MSVC environment.
    exit /b 1
)

pushd "%~dp0"
msbuild QmlBrowser.sln /p:Configuration=%CONFIG% /p:Platform=x64 /m /v:minimal /nologo
set "RESULT=%ERRORLEVEL%"
popd

if not "%RESULT%"=="0" (
    echo.
    echo Build FAILED.
    exit /b %RESULT%
)

echo.
echo Build succeeded: bin\x64\%CONFIG%\
endlocal
