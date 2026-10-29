#pragma once

// Registers the C++ item types that pages can use with
//
//     import QmlBrowser.Native
//
// ClockFace (QPainter), WaveShape (scene graph geometry), JuliaSet (a QImage
// computed in C++, shown as a texture) and NativeCanvas (pixels from a plain C
// library in the canvas folder). /native.qml on the sample site shows them
// all. Call before any page is compiled.
void registerNativeItems();
