# QML Browser: reference

[README.md](README.md) is the overview. This file is the reference: build details, the page API, the server, the tab protocol, app tabs and 3D.

- [Architecture](#architecture)
- [Building](#building)
- [Using the browser](#using-the-browser)
- [Writing a page](#writing-a-page)
- [The HTTP server](#the-http-server)
- [How a tab works](#how-a-tab-works)
- [Input methods](#input-methods)
- [App tabs](#app-tabs)
- [3D](#3d)
- [Next step: a platform plugin](#next-step-a-platform-plugin)
- [Source tree](#source-tree)

## Architecture

```
QmlBrowser                               QmlServer  or  node nodejs/server.js
  tab strip, address bar, history            static HTTP of *.qml and assets
  draws each tab's frame                           ^
        |                                          |
        |  local socket, one JSON object per line  |  HTTP GET
        v                                          |
  QmlRenderer        one process per QML tab ------+
                     renders into shared memory, no window
  WidgetApp / any app linked with QmlBrowserHost
                     one process per app:// tab, same protocol
```

State (URL, title, input-method state) travels on the socket; pixels travel in shared memory. The page is pixels inside the browser window, never a second window, so it moves and resizes with it.

The shipped build is Windows (Visual Studio, Qt 6.10.3). macOS builds with CMake and Xcode against Qt 6.7. Everything but the custom window caption is cross-platform.

## Building

### Windows

Visual Studio 2022+ with the C++ workload, and Qt 6 for MSVC x64 (developed against 6.10.3). `build.bat [Debug|Release]` or `QmlBrowser.sln`; `run.bat` starts the server and then the browser. There is no CMake or qmake step: `moc` and `rcc` are MSBuild steps, and `windeployqt` copies the Qt DLLs next to the executables in `bin\x64\<Config>\`.

| Setting | |
|---|---|
| `QtDir` | Qt kit, default `C:\Qt\6.10.3\msvc2022_64`. Set it in the environment or in `qt.props`. |
| `QtVersion` | Version folder for Qt's private headers, which `QmlBrowserHost` needs. Derived from the folder above `QtDir`; set it only for an unusual layout. The build stops with a clear message if the headers are missing. |

Required Qt modules: Core, Gui, Network, Widgets, OpenGL, OpenGLWidgets, Qml, Quick, Quick Controls (all in the default installer selection). Optional:

| Module | Without it |
|---|---|
| Qt SVG | WidgetApp's Vectors page shows a note instead of its SVG item. |
| Qt Multimedia | WidgetApp's Video page plays only its generated clip. |
| Qt Quick 3D | `/3d.qml` shows a note instead of its Qt Quick 3D scene. |

Deployment details:

- `windeployqt` runs once per program; a `bin\x64\<Config>\qtdeploy_*.stamp` file skips it afterwards. Delete the stamp to redeploy. **Delete `qtdeploy_renderer.stamp` once in an existing build** so the modules below get deployed.
- Pages are downloaded at run time, so `windeployqt` can't see their imports. `renderer\deploy\` lists the modules the sample site needs (Qt Quick Controls, plus Qt Quick 3D when the kit has it) for the renderer's deploy step.
- `WidgetApp.vcxproj` also copies `platforms\qoffscreen.dll`, which `windeployqt` skips; apps hosted in Software mode need it.
- `QmlBrowserHost.lib` goes to `obj\QmlBrowserHost\x64\<Config>\lib\`.

### macOS

macOS 12.6 with Xcode 14.2 (the newest it runs), Qt 6.7 (Qt 6.8+ needs Xcode 15), CMake 3.22+. `QTDIR=$HOME/Qt/6.7.3/macos ./build-macos.sh` generates `build/macos-xcode` and builds Debug. Each bundle gets a `qt.conf` pointing at `QTDIR`; code signing is off. In Xcode run the QmlServer scheme, then debug QmlBrowser; attach to `QmlRenderer` or `WidgetApp` by name to debug a tab. Qt Creator can use the `macos-debug` preset.

Tab processes are Dock-less accessories. Pages render through Metal; where there is no Metal device (a virtual machine), Qt falls back to OpenGL, and the renderer asks for a 4.1 core profile so Qt Quick 3D still works.

### Running

`QmlBrowser [url]` opens `url`, or `http://127.0.0.1:8080/index.qml`. `QmlBrowser app://widgets` needs no server. `WidgetApp` started on its own is an ordinary window (`--page video` opens a page). The browser's own QML (`client\ui\`, `renderer\ui\`) is compiled in; pages under `server\wwwroot` are read on every request, so `F5` shows edits.

## Using the browser

On Windows the tab strip is the title bar: drag empty space to move, double-click to maximize. Each tab has its own history. The status line shows the HTTP status, document size and the tab's process id. A new tab opens blank with the address bar focused.

| Shortcut | Action |
|---|---|
| Ctrl+T / Ctrl+W | New / close tab |
| Ctrl+Tab / Ctrl+Shift+Tab | Next / previous tab |
| Ctrl+L | Focus the address bar (Enter goes, Esc restores) |
| F5 / Ctrl+R | Reload |
| Alt+Left / Alt+Right | Back / forward |
| Ctrl+U | Document source |
| F11 | Full screen; Esc leaves |
| Ctrl+Shift+F | Hide the bars; Esc restores |

Shortcuts belong to the browser window, so they work while a page has focus. Esc is only claimed while the chrome is hidden. Full screen and hidden bars are independent.

The address bar (`BrowserHistory::resolve`):

| Typed | Result |
|---|---|
| `about.qml`, `/index.qml`, `./docs.qml` | Relative to the open page. A bare name like `docs` stays relative. |
| `127.0.0.1`, `localhost`, `[::1]` | `http://` that host, keeping the open page's port for the same host |
| `localhost:9000`, `example.com:8080/x` | `http://` host, port and path |
| `http://`, `https://`, `file://`, `qrc:` | As written |
| `app://<name>[/<path>]` | An app tab; `app://widgets/<page>` with `controls`, `paint`, `pictures`, `vectors`, `video`, `3d` |
| the open URL | Reload |

## Writing a page

A page is a `.qml` file whose root is an `Item`, sized to the viewport. A `title` property on the root becomes the tab title.

```qml
import QtQuick

Rectangle {
    property string title: "Hello"
    color: "#101218"
    Text { anchors.centerIn: parent; text: "Served from " + browser.url; color: "white" }
    MouseArea { anchors.fill: parent; onClicked: browser.navigate("about.qml") }
}
```

`browser` is the tab's `PageView`, in a private context:

| Member | |
|---|---|
| `navigate(url)` | Go to a URL relative to this document, adding history |
| `reload()`, `stop()` | Fetch again / abort the download |
| `url`, `httpStatus`, `status`, `progress` | Current document, last HTTP status, `PageView.Null/Loading/Ready/Error`, 0–1 |
| `errorString`, `sourceText`, `pageTitle` | Compile errors, the compiled source, the root's `title` |
| `setFullScreen(on)` | Ask the window to enter or leave full screen |
| `crash()` | Abort this renderer (to see tab isolation) |

Relative URLs, images and `import "shared"` resolve against the document URL, so a folder with a `qmldir` works as a remote module (`server\wwwroot\shared\`: `Page`, `Card`, `NavLink`). A page cannot read local files and can import only modules the renderer ships. Text fields need nothing for input methods; `Qt.ImhHiddenText`, `ImhDigitsOnly`, `ImhFormattedNumbersOnly` and password echo keep the IME off.

How a load works: the browser sends `navigate`; `PageView` GETs it (`AlwaysNetwork`, user agent `QmlBrowser/1.0`); any response with a body, a 404 included, goes to `QQmlComponent::setData(body, url)`; compile errors become the error page; the renderer reports `state` and `source`. The `404 /qmldir` line in the server log is the QML engine probing for a module and is harmless.

Sample site (`server\wwwroot`): `index.qml`, `about.qml`, `docs.qml`, `gallery.qml`, `ime.qml` and `ime-controls.qml` (input-method test benches), `3d.qml` with `scene3d/QuickScene.qml` (see [3D](#3d)), `shared/`, `assets/`.

## The HTTP server

`QmlServer` and `nodejs/server.js` behave the same: static files over HTTP/1.1, `GET` and `HEAD` only. Only the `Server` header differs, so you can tell them apart. The Node version uses only `net` and `fs` (Node 18+, no packages).

| Option | |
|---|---|
| `-port <n>` (`-p`) | Default 8080 |
| `-root <dir>` (`-r`) | Document root |
| `-address <ip>` (`-a`) | Default `127.0.0.1`; `0.0.0.0` or `*` for every interface |
| `-public` | Same as `-address 0.0.0.0` |
| `-help`, `-version` | |

One or two dashes both work. A bad argument prints the help. To run both servers, give one another port.

Without `-root`, the root is the first of: `QMLSERVER_ROOT`; `wwwroot` next to the executable or script; `server/wwwroot` found by walking up to six directories (so a built exe serves the source tree); `wwwroot` in the working directory.

| Request | Response |
|---|---|
| File | 200 |
| Directory with `index.qml` | 200, that file |
| Directory without | 200, a generated QML listing |
| Missing | 404, a QML error page |
| `..` or outside the root | 403 |
| Other method | 405, connection closed |
| Read error | 500 |

Responses carry `Cache-Control: no-cache, no-store, must-revalidate`, so edits show at once. `.qml` is `text/x-qml`; common web types are mapped by suffix, anything else is `text/plain`. Keep-alive is the HTTP/1.1 default; pipelined requests are answered in order. Each response is logged as `hh:mm:ss METHOD CODE SIZE target`.

## How a tab works

The browser listens on a `QLocalServer` (`qmlbrowser-<pid>-<uuid>`) and starts each tab as `QmlRenderer --channel <name> --tab <id>`, or as the registered app for an `app://` URL. The process connects, says `hello`, and never creates a window: `QQuickRenderControl` renders into an offscreen texture, the pixels go into one of two shared-memory slots, and `frame` tells the browser, which draws them with `TabViewport`. Closing a tab closes the socket; the process exits, or is killed after 2 seconds. Any other exit marks the tab crashed and shows a recovery page; reloading starts a new process on the same history.

Messages are one compact JSON object per line (`IpcChannel`). Unknown types and fields are ignored.

Browser to tab:

| type | Fields | |
|---|---|---|
| `navigate` | `url` | Load this URL |
| `reload`, `stop` | | |
| `viewport` | `visible`, `width`, `height`, `dpr` | Logical size and pixel ratio; only the visible tab keeps drawing |
| `chrome` | `fullScreen`, `chromeHidden` | Show the "press Esc" notice in the frame |
| `input` | `kind` + fields (below) | Page-coordinate input |
| `frameAck` | `seq` | That slot may be reused |

Tab to browser:

| type | Fields | |
|---|---|---|
| `hello` | `tab`, `pid` | Connected |
| `frame` | `seq`, `width`, `height`, `stride`, `format`, `shm` | Device pixels in shared memory `shm`; `format` 1 = RGBA, 2 = BGRA, premultiplied |
| `state` | `status`, `title`, `url`, `httpStatus`, `progress`, `error`, `bytes` | Status 0 null, 1 loading, 2 ready, 3 error |
| `source` | `text` | For the source panel |
| `cursor` | `shape` | `Qt::CursorShape` |
| `navigate` | `url` | The page asked to go somewhere; the browser owns history and sends `navigate` back |
| `fullscreen` | `on` | The page called `setFullScreen` |
| `ime` | `serial`, `enabled`, rects, text, positions | Focused field's input-method state; see below |

`input` kinds: `mouse` (`action` press/release/move/dbl, `x`, `y`, `button`, `buttons`, `mods`), `hover` (move/leave), `wheel` (pixel and angle deltas, `phase`, `inverted`), `key` (`action`, `key`, `text`, `mods`, `repeat`, native codes), `focus` (`on`), `ime` (`preedit`, `commit`, replacement range, `attributes`).

## Input methods

The OS input method (macOS text input, Windows IMM/TSF) only talks to the browser window, so `TabViewport` stands in for the field in the tab process:

1. The tab reports the focused field as `ime`: whether it takes text, hints, caret and anchor rectangles in page coordinates, surrounding text. It re-reads after every input event and frame, and sends only changes.
2. `TabViewport` answers `inputMethodQuery` from that state, which turns composition on (macOS requires `ImEnabled`).
3. Each `QInputMethodEvent` (preedit, commit, formats) is forwarded as `input` kind `ime` and replayed into the field.
4. When focus moves mid-composition, the tab commits the preedit into the old field and bumps `serial`; the browser then resets the OS input method and drops the duplicate commit.

The candidate window can trail the caret by one message while typing fast.

## App tabs

`app://<name>` runs a native Qt Widgets application instead of a QML document. `AppRegistry` (`client\AppRegistry.cpp`) maps the name to a program; `widgets` is built in and runs `WidgetApp`. The app links `QmlBrowserHost` (`apphost\`), which speaks the protocol above. Moving between a page and an app, or between apps, swaps the tab's process; reload restarts the app; an unknown name gets the renderer's error page.

### Hosting your own application

1. **Link** `QmlBrowserHost`: `target_link_libraries(MyApp PRIVATE QmlBrowserHost)` in CMake, or a project reference to `apphost\QmlBrowserHost.vcxproj` plus `apphost\include` and `Qt6Network` in Visual Studio. Build it with the app's Qt kit; it uses Qt's private QPA API.
2. **Call it from `main()`**, as in the [README](README.md#your-own-app-in-a-tab). `widgetapp\main.cpp` is a full example with routing.
3. **Register it** in an `apps.json` next to the browser (on macOS next to `QmlBrowser.app` or inside it); restart the browser after editing:

```json
{
    "notes":  "Notes",
    "viewer": "../tools/Viewer.exe",
    "editor": { "program": "C:/Apps/Editor.exe", "arguments": ["--compact"] }
}
```

A bare name is looked up next to the browser; a path is relative to `apps.json`; a macOS `.app` path means the executable inside. `arguments` come before the browser's `--channel` and `--tab`.

| API (`QmlBrowserHost.h`) | |
|---|---|
| `prepare(argc, argv, graphics)` | Before `QApplication`. Returns true when started by the browser; sets up the platform for `Graphics::Software` (default) or `Gpu`, and strips `--channel`/`--tab` from `argv`. |
| `isHosted()`, `graphics()` | What `prepare()` decided |
| `attach(window)` | Hosted: takes over the main window (don't show it), sizes it to the tab, connects. Returns false when not hosted. If the browser can't be reached, `exec()` returns 1. |
| `setNavigateHandler(fn)` | Optional. Called with the path of `app://<name>/<path>` on load, address-bar entry, Back and Forward |
| `setPath(path)` | Optional. The app moved on its own; adds a browser history entry. Ignored while the handler runs. |

`QMLBROWSERHOST_GRAPHICS=software|gpu` overrides the app's choice of mode.

### How it works

Every visible top-level widget (main window, menus, combo lists, tooltips, dialogs) is kept off screen and painted with `QWidget::render` into the tab's frame; dialogs get a drawn title strip and shadow. Input is replayed through `QWindowSystemInterface`, so Qt handles double clicks, hover, tooltips, popups, modality and shortcuts as usual. Key presses go through the shortcut map first, as a platform plugin does. The library tracks open popups and modal windows itself, because Qt 6.8+ does not count a popup whose native window is not mapped.

| | Software (default) | Gpu |
|---|---|---|
| Platform | Qt `offscreen` | Native (`cocoa`, `windows`), windows hidden with `WA_DontShowOnScreen` and unable to take focus |
| OpenGL / Metal / Direct3D | None: `QOpenGLWidget`, `QQuickWidget` stay blank | Yes: GPU widgets are read back into the frame |
| Popups and dialogs | Screen is resized to the tab | Library moves them inside the tab |
| Fonts, style, clipboard | Set by the library; private clipboard | The platform's own |

GPU mode also disables native menu bars and dialogs (they would appear on the desktop) and keeps the process out of the Dock and from activating. On Windows, Software mode points `QT_QPA_FONTDIR` at `%WINDIR%\Fonts` and registers the `.ttc` collections (CJK fonts), because the offscreen platform finds no fonts otherwise.

Two details keep nested event loops (`QDialog::exec`, `QMenu::exec`) working: socket messages are handled through a queued connection, and the mouse grab ends before a release is delivered.

Limits: dialogs can't be dragged; drag and drop stays inside the app; native child windows (`createWindowContainer()`, `Qt3DWindow`, `QVulkanWindow`, Qt 6's `QVideoWidget`) are not widgets and stay blank. Qt uses one graphics API per window, so mixing `QOpenGLWidget` and `QQuickWidget` needs `QQuickWindow::setGraphicsApi(OpenGL)`.

## 3D

**In a page.** Pages render through Qt RHI on the GPU, so Qt Quick 3D works like any module. `/3d.qml` loads its `View3D` scene (`scene3d/QuickScene.qml`) through a `Loader`, so a kit without Qt Quick 3D only loses that card. Below it, a cube made of six `Rectangle`s with perspective `Matrix4x4` transforms works with any kit; its matrices flatten depth to 0 because Qt Quick clips depth outside −1…1.

**In an app tab.** `QOpenGLWidget` and `QQuickWidget` need GPU mode. `app://widgets/3d` shows lit models in a `QOpenGLWidget` (orbit by drag, zoom by wheel, wireframe, spin), using only OpenGL 2.1 / ES 2.0 so it runs everywhere, including software OpenGL. In Software mode it shows a note instead.

GPU widgets are read back every frame (and `grabFramebuffer()` renders the scene once more). That is cheap on a GPU; with software OpenGL, as in a virtual machine, expect a few frames per second.

## Next step: a platform plugin

What a tab can show today:

| Content | |
|---|---|
| QML documents | Yes |
| Qt Widgets apps you can rebuild, OpenGL and Qt Quick widgets included | Yes, with `QmlBrowserHost` |
| Apps you can't rebuild, standalone Qt Quick apps, native child windows | Not yet |
| Non-Qt programs | No; out of scope |

> [!IMPORTANT]
> **Recommended: build a Qt platform plugin (`-platform qmlbrowser`) from `QmlBrowserHost`.** Qt loads a platform plugin before any window exists, so the plugin sees every window, native child windows included, and can do what the library does without being linked in. `AppRegistry` would start unmodified executables with that option. `InputInjector` and `BrowserBridge` carry over almost unchanged; `WindowCompositor` gives way to the plugin's own window list. The cost: the plugin uses private Qt API and must match each app's exact Qt version, and it is several days of work.

## Source tree

```
qt.props, QmlBrowser.sln, build.bat, run.bat     Windows build
CMakeLists.txt, CMakePresets.json, build-macos.sh macOS build
shared\        IpcChannel (JSON over a local socket), SharedPixels (shared-memory frames)
server\        QmlServer: main.cpp, HttpServer; wwwroot\ is the sample site
nodejs\        server.js, the same server on Node.js
client\        QmlBrowser
  TabManager, BrowserTab        tab processes, IPC, frames, IME state
  AppRegistry                   app://<name> to program, apps.json
  BrowserHistory                history and address-bar resolution
  TabViewport                   draws the frame, forwards input, stands in for the IME
  WindowFrame                   caption-less window frame (Windows)
  ui\                           chrome QML
renderer\      QmlRenderer
  OffscreenPage                 windowless Qt Quick rendering, input and IME replay
  PageView                      downloads and instantiates a document
  deploy\                       module lists for windeployqt
  ui\                           spinner, error page, notices
apphost\       QmlBrowserHost
  include\QmlBrowserHost.h      the public API
  src\BrowserBridge             socket and messages
  src\WindowCompositor          window stacking, frames
  src\InputInjector             pointer, keys, focus, IME, cursor
  src\OffscreenPlatform         Software mode setup
  src\NativePlatform            GPU mode setup
widgetapp\     WidgetApp: GalleryWindow and its pages
               (Controls, Paint, Picture, Vector, Video, Scene = 3D)
```

`GeneratedFiles\` folders hold `moc`/`rcc` output; `bin\` and `obj\` are build output.
