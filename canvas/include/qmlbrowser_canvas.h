/*
 * The C interface between QmlRenderer and a canvas library.
 *
 * A canvas library is a plain shared library (.dll, .so, .dylib) with no Qt
 * dependency. A page shows it with
 *
 *     import QmlBrowser.Native
 *     NativeCanvas { library: "plasma"; time: ... }
 *
 * and the renderer loads canvas/<library> from its own folder, never from a
 * path the page supplies. The library exports the two functions below.
 *
 * Because only C types cross the boundary, a library keeps working when the
 * browser moves to another Qt version, and it may be written in any language
 * that can export C functions. It runs inside the tab's renderer process: a
 * crash in it takes down that tab and nothing else.
 */
#ifndef QMLBROWSER_CANVAS_H
#define QMLBROWSER_CANVAS_H

#include <stdint.h>

#define QMLBROWSER_CANVAS_ABI 1

#if defined(_WIN32)
#  define QMLBROWSER_CANVAS_EXPORT __declspec(dllexport)
#else
#  define QMLBROWSER_CANVAS_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Returns QMLBROWSER_CANVAS_ABI. The renderer refuses a library whose
 * answer differs from its own. */
typedef int (*qmlbrowser_canvas_abi_fn)(void);

/* Fills width × height pixels. Each row starts `stride` bytes after the
 * previous one. A pixel is a uint32_t 0xAARRGGBB with premultiplied alpha
 * (QImage::Format_ARGB32_Premultiplied); use 0xFF in the top byte for opaque
 * drawing. `time` is whatever the page binds, conventionally seconds. The
 * function is called on the renderer's GUI thread and must not keep the
 * pointer after it returns. */
typedef void (*qmlbrowser_canvas_draw_fn)(uint32_t *pixels, int width, int height, int stride,
                                          double time);

#ifdef __cplusplus
}
#endif

#endif /* QMLBROWSER_CANVAS_H */
