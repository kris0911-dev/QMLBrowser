#pragma once

#include <QtGlobal>

#include <functional>

QT_BEGIN_NAMESPACE
class QString;
class QWidget;
QT_END_NAMESPACE

// Hosts a Qt Widgets application inside a QmlBrowser tab.
//
// The browser starts the application with --channel <name> --tab <id> when a
// tab opens app://<name>. None of the application's windows then reach the
// screen (see Graphics for the two ways this is done). Its main window,
// menus, popups, tooltips and dialogs are painted into the tab's frame, and the
// browser's pointer, wheel, key, focus and input-method events are fed back
// in as if the platform had produced them. Started without --channel the
// application is unchanged and opens its windows on the desktop as usual.
//
// A typical main():
//
//     int main(int argc, char *argv[])
//     {
//         QmlBrowserHost::prepare(argc, argv, QmlBrowserHost::Graphics::Gpu);
//         QApplication app(argc, argv);
//         MainWindow window;
//         if (!QmlBrowserHost::attach(&window))
//             window.show();
//         return app.exec();
//     }
//
// Everything here is called from the GUI thread.
namespace QmlBrowserHost {

// How a hosted application's windows are drawn.
enum class Graphics {
    // Qt's offscreen platform, all in software. The tab gets exact control
    // of the screen size and DPI. OpenGL, Vulkan, Metal and Direct3D are not
    // available, so QOpenGLWidget and QQuickWidget stay blank.
    Software,
    // The platform the application normally runs on, with its windows kept
    // off screen. GPU APIs work: QOpenGLWidget and QQuickWidget are read back
    // from the GPU into the tab's frame. Use this for 3D content.
    Gpu,
};

// Call before QApplication is constructed. When the command line contains
// --channel, prepares the platform for `graphics` and returns true. The
// options the browser adds (--channel, --tab) are removed from argc/argv, so
// the application's own QCommandLineParser never sees them. Without
// --channel nothing changes and the result is false.
//
// The environment variable QMLBROWSERHOST_GRAPHICS (software or gpu)
// overrides `graphics`, for trying an application both ways.
//
// Hosted in Software mode, QApplication's constructor then also sizes the
// offscreen screen and sets the application font to the platform's
// interface font, as a native window would get it, since the offscreen
// platform has no theme. An application that wants a different font sets it
// after QApplication exists.
bool prepare(int &argc, char **argv, Graphics graphics = Graphics::Software);

// What prepare() returned. Lets an application leave out things that make
// no sense in a tab, such as a Quit action or a size grip.
bool isHosted();

// The mode in effect after prepare(), including the environment override.
Graphics graphics();

// Call after QApplication and the main window exist, and before
// QApplication::exec(). Hosted, takes the window into the tab, connects to
// the browser and returns true; the application must not show the window
// itself. The window is sized to the tab from then on. If the browser cannot
// be reached, exec() returns 1. Not hosted, does nothing and returns false.
bool attach(QWidget *mainWindow);

// Optional routing, for an application with places worth a history entry.
//
// The browser asked for app://<name>/<path>: on first load, through the
// address bar, or with Back and Forward. `path` has no leading slash and is
// empty for app://<name> itself. While the handler runs, setPath() calls are
// ignored, so a handler may switch pages through the same code that reports
// a switch made by the user.
using NavigateHandler = std::function<void(const QString &path)>;
void setNavigateHandler(NavigateHandler handler);

// The application moved to another place by itself, for instance the user
// picked a tab in it. The browser adds a history entry for
// app://<name>/<path>, the way a single-page web app routes. Does nothing
// when not hosted, when `path` is the current one, or before the browser has
// sent the first address.
void setPath(const QString &path);

} // namespace QmlBrowserHost
