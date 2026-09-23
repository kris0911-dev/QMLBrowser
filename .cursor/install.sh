#!/usr/bin/env bash
# Idempotent Cloud Agent setup for QMLBrowser.
# Installs the Qt6/QML + WebEngine toolchain and headless GUI dependencies,
# then configures and builds the application.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

export DEBIAN_FRONTEND=noninteractive

APT_PACKAGES=(
  # Build toolchain
  build-essential
  cmake
  ninja-build
  # Qt 6 core + QML/Quick + WebEngine development files
  qt6-base-dev
  qt6-declarative-dev
  qt6-declarative-dev-tools
  qt6-webengine-dev
  qt6-webengine-dev-tools
  # QML runtime modules required by the application
  qml6-module-qtquick
  qml6-module-qtquick-controls
  qml6-module-qtquick-layouts
  qml6-module-qtquick-templates
  qml6-module-qtquick-window
  qml6-module-qtquick-dialogs
  qml6-module-qtquick-nativestyle
  qml6-module-qtqml
  qml6-module-qtqml-models
  qml6-module-qtqml-workerscript
  qml6-module-qtqml-statemachine
  qml6-module-qtwebengine
  qml6-module-qtwebengine-controlsdelegates
  # Software OpenGL (llvmpipe) so Qt Quick / WebEngine render headlessly
  libgl1-mesa-dri
  libglx-mesa0
  libegl1
  libopengl0
  mesa-utils
  # Headless display + screenshot/interaction tooling
  xvfb
  x11-utils
  xauth
  xdotool
  ffmpeg
  fonts-dejavu-core
)

echo "==> Installing system packages"
sudo apt-get update
sudo apt-get install -y --no-install-recommends "${APT_PACKAGES[@]}"

echo "==> Configuring build (Qt requires the GNU toolchain; /usr/bin/c++ is clang here)"
cmake -S "$REPO_ROOT" -B "$REPO_ROOT/build" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=gcc \
  -DCMAKE_CXX_COMPILER=g++

echo "==> Building QMLBrowser"
cmake --build "$REPO_ROOT/build"

echo "==> QMLBrowser build complete: $REPO_ROOT/build/QMLBrowser"
