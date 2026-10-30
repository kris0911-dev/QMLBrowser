# QML Browser

A browser whose documents are **QML** instead of HTML. No Chromium, no
`QWebEngineView`, no HTML parser: pages are compiled by the Qt Quick engine and
drawn on the GPU through Qt RHI (Direct3D 11 on Windows, Metal on macOS).

| Program | What it does |
|---|---|
| `QmlServer` | Serves `.qml` documents and assets over HTTP from `server\wwwroot`. A Node.js copy lives in `nodejs\`. |
| `QmlBrowser` | The window: tab strip, address bar, history. Draws no page content itself. |
| `QmlRenderer` | One process per tab. Downloads a document, compiles it, renders it into shared memory. |
| `WidgetApp` | A Qt Widgets app that runs in an `app://widgets` tab: controls, painting, pictures, vectors, video, OpenGL 3D. |
| `QmlBrowserHost` | Static library (`apphost\`) that puts any Qt Widgets app in an `app://<name>` tab. |

Tabs are isolated like Chrome's: a page that crashes takes down only its own
process, and that tab shows a recovery page.

[DOCUMENTATION.md](DOCUMENTATION.md) is the full reference.

## Build and run

**Windows**: Visual Studio 2022+ (C++ workload) and Qt 6.10.3 for MSVC at
`C:\Qt\6.10.3\msvc2022_64` (or set `QtDir`).

```bat
build.bat            REM Debug; build.bat Release for Release
run.bat              REM starts QmlServer, then QmlBrowser
test.bat             REM headless smoke tests
```

Output goes to `bin\x64\<Config>\`. Opening `QmlBrowser.sln` and building does
the same.

**macOS**: macOS 12.6 / Xcode 14.2, Qt 6.7, CMake 3.22+.

```sh
export QTDIR=$HOME/Qt/6.7.3/macos
./build-macos.sh
open build/macos-xcode/QmlBrowser.xcodeproj   # run QmlServer, then debug QmlBrowser
```

Qt SVG, Qt Multimedia and Qt Quick 3D are optional; without them the parts
that use them show a note.

## Using it

The browser opens `http://127.0.0.1:8080/index.qml`; pass another URL as the
first argument to start elsewhere. On Windows the tab strip is the title bar.

| Shortcut | |
|---|---|
| `Ctrl+T` / `Ctrl+W` | New / close tab |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Next / previous tab |
| `Ctrl+L` | Address bar |
| `F5` / `Ctrl+R` | Reload |
| `Alt+Left` / `Alt+Right` | Back / forward |
| `Ctrl+U` | Document source |
| `F11` | Full screen (`Esc` leaves) |
| `Ctrl+Shift+F` | Hide the bars (`Esc` restores) |

On a Mac trackpad, swipe sideways with two fingers (or three, if System
Settings asks for three) to go back and forward, as in Safari. A page or
widget that scrolls sideways keeps the swipe. See
[Trackpad (macOS)](DOCUMENTATION.md#trackpad-macos).

Worth opening:

- `/gallery.qml`: animation, gradients, Canvas
- `/3d.qml`: a Qt Quick 3D scene, and a cube in plain Qt Quick
- `/native.qml`: items drawn by C++ in the renderer (QPainter, scene graph geometry, a computed texture), and pixels from a plain C library (`canvas\plasma.dll`, `plasma.dylib` on macOS)
- `/ime.qml`, `/ime-controls.qml`: input-method test pages (Chinese, Japanese, …)
- `app://widgets` (the ▣ button): the Widgets app; `app://widgets/3d` is its OpenGL page

Edit anything under `server\wwwroot` and press `F5`; no rebuild needed.

## Your own app in a tab

```cpp
#include <QmlBrowserHost.h>

int main(int argc, char *argv[])
{
    QmlBrowserHost::prepare(argc, argv, QmlBrowserHost::Graphics::Gpu);  // before QApplication
    QApplication app(argc, argv);
    MainWindow window;
    if (!QmlBrowserHost::attach(&window))   // false when not started by the browser
        window.show();
    return app.exec();
}
```

Link `QmlBrowserHost`, list the program in an `apps.json` next to the browser,
and open `app://<name>`. See
[Hosting your own application](DOCUMENTATION.md#hosting-your-own-application).

> [!IMPORTANT]
> Apps you cannot rebuild, standalone Qt Quick apps and native child windows
> (`createWindowContainer()`, `Qt3DWindow`) cannot be hosted yet. The
> recommended next step is a Qt platform plugin built from `QmlBrowserHost`;
> see [Next step](DOCUMENTATION.md#next-step-a-platform-plugin).
