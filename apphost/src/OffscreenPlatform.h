#pragma once

// Setup of Qt's offscreen platform for a hosted application.
namespace OffscreenPlatform {

// Selects the offscreen platform. Must run before QApplication exists.
void select();

// Makes the offscreen screen exactly the tab's page area, in logical pixels,
// with synchronous event delivery and no window frame margins.
void resizeScreen(int width, int height);

// Windows only, after QApplication exists: makes the system's font
// collections (CJK fonts among them) available to the offscreen platform.
void registerFontCollections();

// Sets the application font to the one a native window here would get. The
// offscreen platform has no theme, so it would otherwise use a generic font.
void applyInterfaceFont();

} // namespace OffscreenPlatform
