#!/bin/sh
# Configures an Xcode Debug build against the Qt macOS kit and compiles it.
#
#   export QTDIR=$HOME/Qt/6.10.3/macos
#   ./build-macos.sh
#
# Then open build/macos-xcode/QmlBrowser.xcodeproj, choose the QmlBrowser
# scheme, and run Debug. Start the QmlServer scheme first so port 8080 is up.

set -eu

root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)

if [ -z "${QTDIR:-}" ]; then
    for candidate in \
        "$HOME/Qt/6.10.3/macos" \
        "$HOME/Qt/6.10.3/clang_64" \
        "/opt/Qt/6.10.3/macos"
    do
        if [ -d "$candidate/lib/cmake/Qt6" ]; then
            QTDIR=$candidate
            break
        fi
    done
fi

if [ -z "${QTDIR:-}" ] || [ ! -d "$QTDIR/lib/cmake/Qt6" ]; then
    echo "Set QTDIR to the Qt macOS kit, for example:"
    echo "  export QTDIR=\$HOME/Qt/6.10.3/macos"
    exit 1
fi

echo "Qt: $QTDIR"
cmake -S "$root" -B "$root/build/macos-xcode" -G Xcode -DCMAKE_PREFIX_PATH="$QTDIR"
cmake --build "$root/build/macos-xcode" --config Debug --target QmlServer QmlBrowser QmlRenderer

echo
echo "Debug in Xcode:"
echo "  open \"$root/build/macos-xcode/QmlBrowser.xcodeproj\""
echo "  Scheme QmlServer, Run, then scheme QmlBrowser, Debug."
