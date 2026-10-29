/*
 * A sample canvas library: an animated plasma, in plain C.
 * See canvas/include/qmlbrowser_canvas.h for the interface.
 */
#include "qmlbrowser_canvas.h"

#include <math.h>

QMLBROWSER_CANVAS_EXPORT int qmlbrowser_canvas_abi(void)
{
    return QMLBROWSER_CANVAS_ABI;
}

static uint32_t channel(double v)
{
    if (v < 0.0)
        v = 0.0;
    if (v > 1.0)
        v = 1.0;
    return (uint32_t)(v * 255.0 + 0.5);
}

QMLBROWSER_CANVAS_EXPORT void qmlbrowser_canvas_draw(uint32_t *pixels, int width, int height,
                                                     int stride, double time)
{
    const double pi = 3.14159265358979323846;
    int x, y;

    for (y = 0; y < height; ++y) {
        uint32_t *row = (uint32_t *)((unsigned char *)pixels + (long long)y * stride);
        const double v = (double)y / height;
        for (x = 0; x < width; ++x) {
            const double u = (double)x / width;
            /* Four interfering waves; the sum drives a cosine palette. */
            double s = sin(u * 10.0 + time);
            s += sin((v * 10.0 + time) * 0.5);
            s += sin((u * 10.0 + v * 10.0 + time) * 0.5);
            {
                const double cx = u + 0.5 * sin(time / 5.0);
                const double cy = v + 0.5 * cos(time / 3.0);
                s += sin(sqrt(100.0 * (cx * cx + cy * cy) + 1.0) + time);
            }
            s *= 0.5;
            row[x] = 0xFF000000u
                    | channel(0.5 + 0.5 * cos(pi * s)) << 16
                    | channel(0.5 + 0.5 * cos(pi * s + 2.0 * pi / 3.0)) << 8
                    | channel(0.5 + 0.5 * cos(pi * s + 4.0 * pi / 3.0));
        }
    }
}
