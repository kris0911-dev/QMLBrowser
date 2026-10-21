#pragma once

#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

// Setup of the native platform for a hosted application in GPU mode.
//
// In GPU mode the application keeps the platform it would normally run on
// (cocoa, windows), because that is where OpenGL, Metal and Direct3D
// contexts come from: the offscreen platform has none on macOS and Windows.
// Its windows exist but are never put on screen.
namespace NativePlatform {

// Before QApplication exists: widget menu bars and dialogs instead of the
// system ones, which would appear on the desktop, and no activation of the
// process at startup.
void select();

// After QApplication exists: on macOS, makes the process an accessory, so it
// has no Dock icon and does not become the active application.
void hideFromDock();

// Keeps a top-level widget off screen. Called from its Show event, after Qt
// has created its native window and before it maps it: the window stays
// unmapped, and cannot be activated by the window system, so it never takes
// keyboard focus from the browser.
void keepOffScreen(QWidget *window);

} // namespace NativePlatform
