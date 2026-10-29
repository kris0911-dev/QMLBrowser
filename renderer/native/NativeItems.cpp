#include "NativeItems.h"

#include "ClockFace.h"
#include "JuliaSet.h"
#include "NativeCanvas.h"
#include "WaveShape.h"

#include <QQmlEngine>

void registerNativeItems()
{
    // Pages come from the network, so anything registered here is reachable
    // by any site the browser opens. Expose drawing, not files or processes.
    const char *uri = "QmlBrowser.Native";
    qmlRegisterType<ClockFace>(uri, 1, 0, "ClockFace");
    qmlRegisterType<WaveShape>(uri, 1, 0, "WaveShape");
    qmlRegisterType<JuliaSet>(uri, 1, 0, "JuliaSet");
    qmlRegisterType<NativeCanvas>(uri, 1, 0, "NativeCanvas");
}
