#include "NativePlatform.h"

#include <QCoreApplication>
#include <QWidget>
#include <QWindow>

#ifdef Q_OS_MACOS
#  include <objc/message.h>
#  include <objc/runtime.h>
#endif

void NativePlatform::select()
{
    // A native menu bar would be the global macOS one, and a native file or
    // color dialog a real window on the desktop. The widget versions are
    // ordinary windows, composited into the tab like any other.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

#ifdef Q_OS_MACOS
    // Qt activates a GUI application from applicationDidFinishLaunching
    // unless this is set. The tab would become the front process, and the
    // browser window would stay inactive until it was clicked.
    qputenv("QT_MAC_DISABLE_FOREGROUND_APPLICATION_TRANSFORM", "1");
#endif
}

void NativePlatform::hideFromDock()
{
#ifdef Q_OS_MACOS
    using SendId = id (*)(id, SEL);
    auto msg = reinterpret_cast<SendId>(objc_msgSend);
    id nsApp = msg(reinterpret_cast<id>(objc_getClass("NSApplication")),
                   sel_getUid("sharedApplication"));
    if (!nsApp)
        return;
    // NSApplicationActivationPolicyAccessory == 1
    reinterpret_cast<BOOL (*)(id, SEL, long)>(objc_msgSend)(
            nsApp, sel_getUid("setActivationPolicy:"), 1L);
#endif
}

void NativePlatform::keepOffScreen(QWidget *window)
{
    // Qt still treats the widget as shown: it is laid out, painted on
    // demand, takes input, and a modal one blocks the others. Only the native
    // window is never mapped.
    window->setAttribute(Qt::WA_DontShowOnScreen);

    // Some widgets ask the window system to activate their window (a dialog
    // being shown, activateWindow()). On Windows that is SetForegroundWindow,
    // which would take the keyboard away from the browser. The flag makes
    // QWindow::requestActivate() return before it reaches the platform;
    // activation inside the application is done through
    // QWindowSystemInterface instead.
    if (QWindow *handle = window->windowHandle())
        handle->setFlag(Qt::WindowDoesNotAcceptFocus);
}
