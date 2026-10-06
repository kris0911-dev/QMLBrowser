# QML Browser

A browser whose document format is **QML** instead of HTML. Three native Qt/C++
programs:

| | |
|---|---|
| `QmlServer.exe` | Serves `.qml` documents and their assets over HTTP from `server\wwwroot`. |
| `QmlBrowser.exe` | The browser window: tab strip, address bar, history, status line. Renders no page content itself. |
| `QmlRenderer.exe` | One process per tab. Fetches a document, compiles it with the Qt Quick engine and draws it. |

There is no Chromium, no `QWebEngineView`, no HTML parser and no `QtWebView`
anywhere in the build. Rendering is the Qt Quick scene graph through Qt RHI:
Direct3D 11 on Windows, Metal on macOS.

`DOCUMENTATION.md` describes the project: the HTTP server, the page API, the
address bar, and the messages between the browser and a tab.

Tabs are isolated the way Chrome and Edge isolate them: a page that crashes,
hangs or aborts takes down only its own `QmlRenderer.exe`, and the browser
window shows a recovery page in that tab while the other tabs keep running.

## Prerequisites

### Windows

* Windows
* Qt 6 for MSVC x64. Developed against **Qt 6.10.3** at `C:\Qt\6.10.3\msvc2022_64`
* Visual Studio 2022 or later, with the **Desktop development with C++** workload

If Qt is installed somewhere else, set `QtDir` before building, or change it in
`qt.props`:

```bat
set QtDir=C:\Qt\6.9.0\msvc2022_64
```

Nothing else is required for this build. There is no CMake, no qmake, no Qt VS
Tools extension, and no extra libraries. `moc` and `rcc` run as MSBuild steps,
and `windeployqt` copies the Qt DLLs next to the executables. `build.bat` and
`QmlBrowser.sln` are this configuration.

### macOS

* macOS 12.6, with Xcode 14.2. That is the newest Xcode this release of macOS can run. The build sets `CMAKE_OSX_DEPLOYMENT_TARGET` to `11.0`, which is the oldest macOS Qt 6.7 itself runs on
* Qt 6.7 for macOS. The kit directory is usually `$HOME/Qt/6.7.3/macos`. Qt 6.8 and later need Xcode 15, which does not install here
* CMake 3.22 or newer on `PATH` (Qt Creator's CMake, or Homebrew)

`build.bat` and `QmlBrowser.sln` stay the Windows build. On macOS, `CMakeLists.txt` produces a Debug Xcode project. Code signing is turned off so a local run does not need an Apple Developer team. Each app bundle gets a `qt.conf` that points at `QTDIR`, which is what lets the debugger start the Qt libraries.

```sh
export QTDIR=$HOME/Qt/6.7.3/macos
./build-macos.sh
open build/macos-xcode/QmlBrowser.xcodeproj
```

In Xcode choose the **QmlServer** scheme and Run it, then the **QmlBrowser** scheme and Debug. The Debug configuration is the one the scheme uses for a normal run. Breakpoints in `client/`, `renderer/`, and `server/` work in that session.

Qt Creator can open `CMakeLists.txt` and use the `macos-debug` preset (Ninja, `CMAKE_BUILD_TYPE=Debug`). The same `QTDIR` is required. Run `QmlServer`, then debug `QmlBrowser`.

The renderer has no window on either platform. It rasterizes into shared memory and the browser window draws that frame, so a tab does not get a Dock icon (`LSUIElement` on the renderer bundle) and does not have to be stacked over the browser. The custom caption stays Windows-only.

The Node server also runs here, with Node.js 18 or newer and no packages:

```sh
node nodejs/server.js
```

### Either platform

The Node server is optional and does not replace the Qt programs. On Windows,
Node.js 18 or newer is enough for `node nodejs\server.js`.

## Build and run

Open `QmlBrowser.sln` and press **Ctrl+Shift+B**, then start `QmlServer` and
`QmlBrowser`. Or from a command prompt:

```bat
build.bat            REM or: build.bat Release
run.bat              REM starts the server, then the browser
```

All three executables land in `bin\x64\Debug\`. Run `QmlBrowser.exe`; it starts
`QmlRenderer.exe` itself, once per tab.

## Using it

The browser opens `http://127.0.0.1:8080/index.qml`. Pass a different URL as the
first argument to start somewhere else.

The window has no classical title bar. The tab strip is the caption: drag its
empty space to move the window, double-click it to maximise, and the minimise,
maximise/restore and close buttons sit at its right end. Windows still owns the
resize border, snapping and the shadow.

| Shortcut | |
|---|---|
| `Ctrl+T` / `Ctrl+W` | New tab / close tab |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Next / previous tab |
| `Ctrl+L` | Focus the address bar |
| `F5` / `Ctrl+R` | Reload |
| `Alt+Left` / `Alt+Right` | Back / forward |
| `Ctrl+U` | Toggle the document source panel |
| `F11` | Full screen; `Esc` also leaves it |
| `Ctrl+Shift+F` | Hide the tab strip, address bar and status line; `Esc` brings them back |

Each tab keeps its own history, and the status line on the right reports the
HTTP status, document size and the process id of the tab you are looking at.
The shortcuts work whether the keyboard focus is in the browser chrome or in the
page. They belong to the browser window, so a focused page does not swallow them.

Full screen hides the tab strip, toolbar and status line and gives the whole
monitor to the page. Leaving it returns the window to whatever it was before,
including maximised.

`Ctrl+Shift+F` hides just the bars and leaves the window where it is, which is
the less drastic version of the same thing. The two are independent: hiding the
bars, going full screen and then pressing `Esc` leaves full screen but keeps
the bars hidden, and a second `Esc` brings them back. `Esc` is only claimed
while the chrome is actually off screen, so pages keep it the rest of the time.

Any page can call `browser.crash()` to abort its own renderer, which is the
quickest way to see the isolation for yourself.

Because the server sends `Cache-Control: no-cache` and reads from disk on every
request, editing a file under `server\wwwroot` and pressing `F5` shows the change
immediately — no rebuild.

### Server options

```
QmlServer.exe
QmlServer.exe -help
QmlServer.exe -port 9000
QmlServer.exe -root D:\my\qml\site -address 0.0.0.0
QmlServer.exe -public
```

`-help` prints every option. One dash and two dashes are the same thing, so
`-port` and `--port` both work. `-address 127.0.0.1` (the default) is this
computer only; `-address 0.0.0.0` and `-public` accept connections from other
computers. Without `-root`, the server walks up from the executable to find
`server\wwwroot`, so it serves the sources you edit rather than a copy. A bad
argument prints the same help text instead of a one-line error.

The same server is also in `nodejs\`, with no extra packages. It serves the
same `server\wwwroot` and accepts the same options:

```
node nodejs\server.js
node nodejs\server.js -help
node nodejs\server.js -public
```

## Writing a page

A page is a `.qml` document whose root object is an `Item`. The renderer sizes
that root to the viewport.

```qml
import QtQuick

Rectangle {
    property string title: "Hello"      // shown in the window title bar
    color: "#101218"

    Text {
        anchors.centerIn: parent
        text: "Served from " + browser.url
        color: "white"
    }

    MouseArea {
        anchors.fill: parent
        onClicked: browser.navigate("index.qml")
    }
}
```

`browser` is the C++ `PageView` instance, injected into each document's context:

| Member | |
|---|---|
| `browser.navigate(url)` | Go to a relative or absolute URL, pushing a history entry |
| `browser.reload()` | Re-fetch the current document |
| `browser.url` | Absolute URL of the current document |
| `browser.httpStatus` | Status code the server returned |
| `browser.setFullScreen(on)` | Ask the browser to hide or restore its chrome |
| `browser.crash()` | Abort this tab's renderer process |

Relative paths — images, `import "shared"`, links — resolve against the
document's own URL, so a directory with a `qmldir` file works as a remote
component library. `server\wwwroot\shared\` is one.

## Layout

```
qt.props                  Qt paths, warning setup, output dirs; shared by all three projects
QmlBrowser.sln             Windows solution
build.bat                  msbuild Debug or Release, x64
CMakeLists.txt             CMake build. Apple requires Qt 6.7; other platforms require Qt 6.10
CMakePresets.json          macos-debug and macos-xcode, deployment target 11.0
build-macos.sh             Xcode Debug build from QTDIR
shared\
  IpcChannel.h/.cpp       newline-delimited JSON over QLocalSocket, used by both sides
  SharedPixels.h/.cpp     shared-memory bitmap a renderer submits and the browser draws
server\
  main.cpp                CLI, document-root discovery
  HttpServer.h/.cpp       QTcpServer; GET/HEAD, keep-alive, MIME, directory listings
  wwwroot\                the served site
    index.qml about.qml gallery.qml docs.qml
    shared\               qmldir + Page/Card/NavLink components
    assets\logo.svg
client\                   QmlBrowser.exe — the browser window
  main.cpp                QGuiApplication, type registration, start URL
  WindowFrame.h/.cpp      custom caption: tab strip replaces the Windows title bar
  TabManager.h/.cpp       QLocalServer, the tab list, viewport size
  BrowserTab.h/.cpp       one renderer process: QProcess + IPC channel + history + frames
  BrowserHistory.h/.cpp   back/forward stack and URL normalisation
  TabViewport.h/.cpp      draws the active tab's frame inside the browser window
  ui\Browser.qml          tab strip, toolbar, address bar, crash page, source panel
  ui\TabButton.qml ui\ToolButton.qml
  resources.qrc           chrome compiled into the exe via rcc
renderer\                 QmlRenderer.exe — one instance per tab
  main.cpp                offscreen frame loop and IPC plumbing
  OffscreenPage.h/.cpp    windowless Qt Quick rasterization into shared memory
  Info.plist.in           macOS agent bundle so a tab has no Dock icon
  PageView.h/.cpp         QQuickItem that downloads and instantiates remote QML
  ui\Renderer.qml         the page plus its loading spinner and error page
  resources.qrc
```

## How a tab works

The browser process listens on a `QLocalServer` named `qmlbrowser-<pid>-<uuid>`.
Opening a tab starts `QmlRenderer --channel <name> --tab <id>`. On Windows that
executable is `QmlRenderer.exe`. On macOS it is `QmlRenderer.app`. The renderer
connects back and sends `hello` with its process id. It never creates a window.

Drawing follows the same split Chromium uses. The renderer rasterizes the Qt
Quick scene with `QQuickRenderControl` into an offscreen texture, copies the
pixels into shared memory, and submits a `frame`. `TabViewport`, inside the
browser window, uploads that bitmap into the browser's scene graph. Moving or
resizing the window moves the page with it, because the page is pixels in that
window rather than a second window being dragged along. The browser sends
`viewport` (logical size, device pixel ratio, and whether the tab is the
visible one) and forwards pointer and key events as `input`, since the
renderer has nothing on screen to click. A `frameAck` lets the renderer reuse
that shared-memory slot.

If a renderer exits for any reason, `BrowserTab` records the exit code and the
tab shows a recovery page; **Reload tab** launches a fresh process into the same
history.

## How a page load works

1. `PageView` issues the `GET` through `QNetworkAccessManager`.
2. The response body goes to `QQmlComponent::setData(body, url)`. Passing the URL
   is what makes relative assets and imports resolve back to the server.
3. The component is instantiated into a private `QQmlContext` holding `browser`.
4. The resulting `QQuickItem` is reparented into the viewport and sized to fill it.
5. Anything in `QQmlComponent::errors()` becomes the error page.

A 404 is still rendered rather than discarded, because this server replies to a
missing document with an error page that is itself QML — the same way a web
server returns an HTML error body.

## Notes

* The page is a frame inside the browser window, so the crash page can be drawn
  over it by the chrome. The "press F11 to leave full screen" reminder is still
  sent over the `chrome` message and drawn into the page frame. Toolbar hints
  stay on the status line.
* Every page load also produces one `404 /qmldir` line in the server log. That is
  the QML engine probing for a directory module next to the document; it is
  expected and harmless.
* Remote QML is sandboxed by Qt: a served document cannot read local files on the
  machine running the client, and can only import modules the client already
  ships.
* The first build runs `windeployqt`, which takes a few seconds. It is skipped
  afterwards via a stamp file in the output directory; delete
  `bin\x64\<Config>\qtdeploy_*.stamp` to force a redeploy.
