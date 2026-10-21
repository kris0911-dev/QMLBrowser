# QML Browser

A browser whose documents are QML, not HTML. The server, the window, and each tab are separate programs. There is no Chromium, no `QWebEngineView`, and no HTML parser. A page is compiled by the Qt Quick engine and drawn by the Qt Quick scene graph.

This file describes how the project is built, how a page is loaded, and what a document is allowed to do. `README.md` is the short version, including the Windows and macOS prerequisites.

The shipped build is Windows, Visual Studio, Qt 6.10.3. macOS builds with CMake and Xcode against Qt 6.7. The HTTP servers and the QML documents are portable. The window frame is Windows-only. Page drawing is the same on every platform: a tab's process has no window, and the browser composites its frames.

## The programs

| Program | Project | What it does |
|---|---|---|
| `QmlServer.exe` | `server\` | Serves files from a document root over HTTP/1.1. |
| `QmlBrowser.exe` | `client\` | The window: tab strip, address bar, history, status line. It does not draw page content. |
| `QmlRenderer.exe` | `renderer\` | One process per QML tab. Downloads a document, compiles it, and draws it. |
| `WidgetApp.exe` | `widgetapp\` | One process per `app://widgets` tab. A Qt Widgets application that draws into the tab instead of a window of its own. |

`QmlBrowser.exe` starts `QmlRenderer.exe` and `WidgetApp.exe` itself, picking one from the tab's URL. You start the server and the browser. `WidgetApp.exe` speaks the renderer's protocol. See [App tabs](#app-tabs-appwidgets). **Other Qt applications cannot be hosted yet.** See [Hosting other Qt applications](#hosting-other-qt-applications) for the recommended way to add that.

A Node.js server in `nodejs\` speaks the same HTTP as `QmlServer.exe` and serves the same files. It is a second implementation of the server, not a replacement. See [The Node server](#the-node-server).

```
QmlBrowser.exe                         QmlServer.exe  or  node server.js
  tab strip, address bar, history          HTTP GET/HEAD of *.qml
  composites each tab's frame
        |                                        ^
        |  QLocalSocket, one JSON object         |  QNetworkAccessManager
        |  per line                              |
        v                                        |
  QmlRenderer.exe  ---- HTTP GET --------------+
    one process per tab, no window
    rasterizes into shared memory

  WidgetApp.exe          (app://widgets tabs, same socket protocol)
    Qt Widgets on the offscreen platform, no window
    paints its windows into shared memory
```

State (URL, title, HTTP status, shortcuts, input-method state) travels on the local socket. Frames travel in shared memory: the renderer writes a bitmap and sends `frame`, and the browser draws it inside its own window. `viewport` carries the page size. There is no second native window to move.

## Requirements

### Windows

- Windows
- Qt 6 for MSVC x64. The tree is set up for Qt 6.10.3 at `C:\Qt\6.10.3\msvc2022_64`.
- Visual Studio 2022 or later with the Desktop development with C++ workload.

Point the build at a different Qt by setting `QtDir` before Visual Studio or `build.bat`, or by editing `QtDir` in `qt.props`:

```bat
set QtDir=C:\Qt\6.9.0\msvc2022_64
```

The kit needs the Qt Base modules (Core, Gui, Network, Widgets) and Qt Declarative (Qml, Quick); the default installer selection has them. Two add-on modules are used when present and skipped otherwise. `WidgetApp.vcxproj` checks for their `.lib` files:

| Module | Without it |
|---|---|
| Qt SVG | The app's Vectors page shows a note in place of its SVG item. |
| Qt Multimedia | The app's Video page plays only its generated clip, and File › Open video is disabled. |

`WidgetApp` includes Qt's QPA headers (`qpa/qwindowsysteminterface.h`), which live under `include\QtGui\<version>\QtGui`. `qt.props` derives `<version>` from the folder above `QtDir`, so `C:\Qt\6.10.3\msvc2022_64` gives `6.10.3`. For a kit laid out differently, also set `QtVersion`:

```bat
set QtDir=D:\sdk\qt-msvc
set QtVersion=6.10.3
```

The build stops with a message naming the folder if those headers are not where `qt.props` expects them.

There is no CMake and no qmake step. `moc` and `rcc` are MSBuild custom build steps. After the link, `windeployqt` copies the Qt DLLs next to the executables. `WidgetApp.vcxproj` also copies `plugins\platforms\qoffscreen.dll` (`qoffscreend.dll` in Debug) into `bin\x64\<Config>\platforms\`, because the app runs on that platform in a tab and `windeployqt` only deploys `qwindows.dll`. `build.bat` and `QmlBrowser.sln` are this configuration; building `QmlBrowser` builds `QmlRenderer` and `WidgetApp` first.

### macOS

- macOS 12.6, with Xcode 14.2. That is the newest Xcode this release of macOS can run. `CMAKE_OSX_DEPLOYMENT_TARGET` is `11.0`, the oldest macOS Qt 6.7 itself runs on.
- Qt 6.7 for macOS. Point `QTDIR` at that kit, usually `$HOME/Qt/6.7.3/macos`. Qt 6.8 and later need Xcode 15, which does not install here.
- CMake 3.22 or newer

`build.bat` and `QmlBrowser.sln` are the Windows build. `CMakeLists.txt` and `CMakePresets.json` are the macOS one. `./build-macos.sh` configures `build/macos-xcode` with the Xcode generator and compiles Debug. The generated schemes do not require a code-signing team. A `qt.conf` inside each bundle names `QTDIR`, so a Debug run from Xcode finds Qt.

Open `build/macos-xcode/QmlBrowser.xcodeproj`. Run the QmlServer scheme, then debug the QmlBrowser scheme. Qt Creator uses the `macos-debug` preset the same way: Debug, `CMAKE_PREFIX_PATH` from `QTDIR`.

On macOS the page is not a window. The renderer is an agent (`LSUIElement` in `renderer/Info.plist.in`) and sets the accessory activation policy, so a tab does not get a Dock icon. `WidgetApp.app` keeps an ordinary `Info.plist`, so it has a Dock icon when you start it on its own; hosted, it runs on the offscreen platform and never contacts the Dock. The custom caption stays Windows-only.

`build-macos.sh` builds `QmlServer`, `QmlBrowser`, `QmlRenderer` and `WidgetApp`. CMake reports at configure time whether it found Qt SVG and Qt Multimedia for the app (`WidgetApp: Qt SVG 1, Qt Multimedia 0`). The Qt 6.7.3 kit installed here has Qt SVG; Qt Multimedia is a separate component in the Qt maintenance tool.

The Node server runs there with Node.js 18 or newer and no packages:

```sh
node nodejs/server.js
node nodejs/server.js -help
node nodejs/server.js -public
```

Shared Qt behavior already applies on macOS: the address bar, a blank new tab, the server command line, HTTP, the page API, and offscreen frame compositing. The custom caption (`WindowFrame`) stays Windows-only.

### Either platform

Node.js 18 or newer is enough for the optional Node server. It does not replace the Qt programs.

## Build and run

On Windows:

```bat
build.bat              REM Debug, the default
build.bat Release
run.bat                REM starts QmlServer.exe, then QmlBrowser.exe
run.bat Release
```

Opening `QmlBrowser.sln` and building does the same thing as `build.bat`. Output goes to `bin\x64\Debug\` or `bin\x64\Release\`: `QmlServer.exe`, `QmlBrowser.exe`, `QmlRenderer.exe`, `WidgetApp.exe`, the Qt DLLs, and `platforms\` with `qwindows` and `qoffscreen`.

`run.bat` starts the server in its own console, waits about a second, then opens the browser on `http://127.0.0.1:8080/index.qml`.

The first build runs `windeployqt`. Later builds skip it because of a stamp file, `bin\x64\<Config>\qtdeploy_*.stamp`. Delete that stamp to deploy again.

Pass a start URL as the browser's first argument. Anything that begins with `-` is ignored and the default page is used.

```bat
QmlBrowser.exe
QmlBrowser.exe http://127.0.0.1:9000/about.qml
```

The default start URL is `http://127.0.0.1:8080/index.qml`. `QmlBrowser.exe app://widgets` starts on the widget app instead, and does not need the server.

`WidgetApp.exe` also runs on its own, as a normal window on the native platform, which is useful for comparing it with the hosted copy:

```bat
WidgetApp.exe
WidgetApp.exe --page video
```

Chrome QML (`client\ui\`, `renderer\ui\`) is compiled into the executable by `rcc`. A change there needs a rebuild of that program. A change under `server\wwwroot` does not: the server reads the file on every request, and the renderer asks the network for every load (`QNetworkRequest::AlwaysNetwork`). Press F5 after editing a page.

## Using the browser

The window has no stock title bar. The tab strip is the caption. Drag empty space on the strip to move the window, double-click it to maximize, and use the minimize, maximize, and close buttons at the right. Windows still owns the resize border, snapping, and the shadow. Full screen turns that frame off. That caption exists only on Windows.

The first tab loads the start URL. A later tab opens blank, with the address bar focused and empty, until you type an address.

Each tab has its own back/forward stack. The status line shows the HTTP status, the document size in characters, and the process id of the renderer you are looking at.

| Shortcut | Action |
|---|---|
| Ctrl+T | New blank tab |
| Ctrl+W | Close the current tab |
| Ctrl+Tab / Ctrl+Shift+Tab | Next / previous tab |
| Ctrl+L | Focus the address bar |
| F5 / Ctrl+R | Reload |
| Alt+Left / Alt+Right | Back / forward |
| Ctrl+U | Show or hide the document source |
| F11 | Full screen. Esc also leaves it |
| Ctrl+Shift+F | Hide the tab strip, address bar, and status line. Esc brings them back |
| Enter in the address bar | Go to the typed address |
| Esc in the address bar | Restore the field to the current URL |

Shortcuts work while the page has keyboard focus. They are Qt Quick shortcuts on the browser window, so they run there instead of being delivered to the page. Esc is only taken while the chrome is actually hidden, so a page can use Esc the rest of the time.

Full screen and chrome-hide are independent. Hiding the bars, then going full screen, then pressing Esc leaves full screen and leaves the bars hidden. A second Esc shows the bars. While the chrome is off the screen, the "press Esc" reminder is drawn into the page frame.

The ▣ button on the right of the toolbar opens the widget app in a new tab (`app://widgets`). The home page has a card with links to it and to its pages.

Typing with an input method (Pinyin, Kotoeri and the like) works in page text fields and in the widget app. The composition is drawn inside the field and the candidate window sits at the caret. See [Input methods](#input-methods).

A page can call `browser.crash()` to abort its own renderer. That tab shows a recovery page. Reload starts a new `QmlRenderer.exe` on the same history. Other tabs are unaffected.

### Address bar

What you type is resolved by `BrowserHistory::resolve`.

| Typed | Result |
|---|---|
| `about.qml`, `shared/Page.qml`, `/index.qml`, `./docs.qml` | Relative to the open page |
| `127.0.0.1`, `localhost`, `[::1]` | `http://` plus that host. If the open page is the same host and has a port, that port is kept, so `127.0.0.1` on port 8080 opens `http://127.0.0.1:8080/` |
| `127.0.0.1:9000`, `localhost:9000`, `example.com:8080` | `http://` plus host, port, and any path |
| `http://...`, `https://...`, `file://...`, `qrc:...` | Used as written. An empty HTTP or HTTPS path becomes `/` |
| `app://widgets`, `app://widgets/video` | The Qt Widgets app, optionally on one of its pages: `controls`, `paint`, `pictures`, `vectors`, `video` |
| empty | Ignored |
| the URL already open | Reload, no new history entry |

A bare hostname with no port, such as `example.com` or `docs`, stays a relative document. `about.qml` must not be turned into a website.

Going to a new URL drops the forward stack. Back and forward move an index. They do not fetch until the history change is sent to the renderer as a `navigate` message.

## Writing a page

A page is a `.qml` file whose root is an `Item` (usually a `Rectangle`). The renderer sizes that root to the viewport.

```qml
import QtQuick

Rectangle {
    property string title: "Hello"

    color: "#101218"

    Text {
        anchors.centerIn: parent
        text: "Served from " + browser.url
        color: "white"
    }

    MouseArea {
        anchors.fill: parent
        onClicked: browser.navigate("about.qml")
    }
}
```

`title` on the root object is the tab title and the window caption (`Title — QML Browser`). If it is missing, the tab shows the host, or "New tab" when the address is empty.

`browser` is the C++ `PageView` of this tab, placed in a private context. The page cannot see the browser chrome, and the chrome cannot see objects the page creates.

| Member | Meaning |
|---|---|
| `browser.navigate(target)` | Resolve `target` against the current document URL and ask the browser to go there. This pushes history. |
| `browser.reload()` | Fetch the current document again. |
| `browser.stop()` | Abort the in-flight download. |
| `browser.url` | Absolute URL of the current document. |
| `browser.httpStatus` | HTTP status of the last response, or 0 when there was none. |
| `browser.status` | `PageView.Null`, `Loading`, `Ready`, or `Error`. |
| `browser.progress` | Download fraction from 0 to 1. |
| `browser.errorString` | Compile errors or a transport failure with an empty body. |
| `browser.sourceText` | The source that was compiled. |
| `browser.pageTitle` | The root item's `title` property. |
| `browser.setFullScreen(on)` | Ask the browser window to enter or leave full screen. |
| `browser.crash()` | Abort this renderer process. |

Relative URLs, images, and `import "shared"` resolve against the document URL passed to `QQmlComponent::setData`. A folder with a `qmldir` is a remote module. `server\wwwroot\shared\` (`Page`, `Card`, `NavLink`) is the one shipped with the site.

Text fields need nothing special for input methods. A field reports its state through the normal `inputMethodQuery` path, and `inputMethodHints` are honoured: `Qt.ImhHiddenText`, `Qt.ImhDigitsOnly` and `Qt.ImhFormattedNumbersOnly` keep the IME off, as they would in a local application. `echoMode: TextInput.Password` does the same.

A served document cannot read local files on the machine running the browser. It can import only modules the client already has: Qt Quick modules linked into `QmlRenderer.exe`, and modules fetched by URL from the server.

The sample site:

| URL | File |
|---|---|
| `/` and `/index.qml` | `server\wwwroot\index.qml` |
| `/about.qml` | `server\wwwroot\about.qml` |
| `/docs.qml` | `server\wwwroot\docs.qml` |
| `/gallery.qml` | `server\wwwroot\gallery.qml` |
| `/ime.qml` | `server\wwwroot\ime.qml`, input-method test bench built from `TextInput` and `TextEdit`, with a live state panel and an event log |
| `/ime-controls.qml` | `server\wwwroot\ime-controls.qml`, the same checks with Qt Quick Controls (`TextField`, `TextArea`, editable `ComboBox` and `SpinBox`) |
| `/shared/` | A generated directory listing. The folder has no `index.qml`. |
| `/assets/logo.svg` | `server\wwwroot\assets\logo.svg` |

## How a page load works

1. The browser sends `{ "type": "navigate", "url": "..." }` to that tab's renderer.
2. `PageView` issues `GET` with `QNetworkAccessManager`. The user agent is `QmlBrowser/1.0 (Qt <version>)`. Redirects are allowed only when they are not less safe than the original request. Cache control is `AlwaysNetwork`.
3. A transport error with an empty body becomes the error page. Any other response, including a 404, is compiled. This server's error bodies are QML, so a missing file still draws a page.
4. `QQmlComponent::setData(body, documentUrl)` compiles the source. The URL is what makes relative imports hit the server.
5. The component is created in a private `QQmlContext` whose `browser` property is this `PageView`. The item is parented into the viewport and resized with it.
6. `QQmlComponent` errors are shown as the error page, one line per diagnostic (`line:column description`).
7. The renderer sends a coalesced `state` message so the tab strip, address bar, and status line can update. Source text is a separate `source` message.

The QML engine also requests `qmldir` next to the document. The server log shows `404 /qmldir` for a file that is not a directory module. That probe is normal.

An open page stays on screen if the server later exits. The scene is already built inside the renderer. The next navigation or reload fails until a server is listening again.

## The HTTP server

`QmlServer.exe` and `nodejs\server.js` implement the same behavior. Both are static file servers. They do not run QML.

### Command line

One dash and two dashes are the same option. `-public` is one option, not the short option `-p` plus the value `ublic`.

```bat
QmlServer.exe
QmlServer.exe -help
QmlServer.exe -port 9000
QmlServer.exe -root D:\my\qml\site -address 0.0.0.0
QmlServer.exe -public

node nodejs\server.js
node nodejs\server.js -help
node nodejs\server.js -port 8090
node nodejs\server.js -public
```

| Option | Meaning |
|---|---|
| `-h`, `-help`, `-?` | Print help and exit 0. |
| `-v`, `-version` | Print `QmlServer 1.0` and exit 0. |
| `-p`, `-port <port>` | Port from 1 to 65535. Default 8080. |
| `-r`, `-root <directory>` | Document root. |
| `-a`, `-address <ip>` | Bind address. Default `127.0.0.1`. `*` and `0.0.0.0` mean every interface. `localhost` means `127.0.0.1`. |
| `-public` | Same as `-address 0.0.0.0`. Cannot be combined with `-address`. |

A bad argument prints the error and the full help. Unknown options and a bad port or address exit with code 2 on the Node server and with code 1 (parse errors) or 2 (bad port, address, or missing root) on the Qt server. If the port is already taken, both print `Cannot listen on <host>:<port>` and exit 1.

They cannot share a port. Run them together by giving the second one `-port`:

```bat
QmlServer.exe -port 8080
node nodejs\server.js -port 8090
```

The browser does not care which process answered. Open `http://127.0.0.1:8090/index.qml` to use the Node server.

On startup the server prints the document root, the listen URL, and the home page, then logs every response. Ctrl+C stops it.

### Document root

Unless `-root` is set, the root is chosen in this order:

1. The `QMLSERVER_ROOT` environment variable.
2. A `wwwroot` directory next to the executable (Qt) or next to `server.js` (Node).
3. Walk up at most six directories looking for `server/wwwroot`. This is why `bin\x64\Release\QmlServer.exe` serves the files you edit in the source tree.
4. `wwwroot` under the current working directory.

The process exits if that directory does not exist.

### Requests

HTTP/1.1. `GET` and `HEAD` only. Any other method is `405` with a QML body, and the connection is closed.

The header block must end with `\r\n\r\n`. More than 16 KiB of headers without that terminator is `414`, then the socket is closed. A request line with fewer than three tokens is `400`, then the socket is closed.

Keep-alive is the default for HTTP/1.1. A request line ending in `1.0` defaults to close. A `Connection` header whose value is exactly `close` (trimmed, lowercased) forces close. Any other `Connection` value keeps the socket open, including `keep-alive` on HTTP/1.0. Several complete requests in one buffer are answered in order.

The target must start with `/`. The query string, from `?` to the end, is discarded. The path is percent-decoded. A `..` segment is `403` with the text "Path traversal is not allowed." and the connection stays open. After cleaning, a path that is not under the document root is `403` with "Outside of the document root."

| Situation | Status |
|---|---|
| File exists | 200, body is the file |
| Directory contains `index.qml` | 200, body is that file |
| Directory has no `index.qml` | 200, a generated QML listing |
| Missing file | 404, a QML error page |
| Directory read or file read fails | 500 |
| Method other than GET or HEAD | 405, connection closed |

`HEAD` sends the same headers as `GET`, including `Content-Length` of the full body, and no body.

Response headers:

```
HTTP/1.1 <code> <reason>
Server: QmlServer/1.0 (Qt <version>)     or, from Node: QmlServer/1.0 (Node)
Date: <UTC>
Content-Type: <see below>
Content-Length: <bytes>
Cache-Control: no-cache, no-store, must-revalidate
Connection: keep-alive                   or close
```

The `Server` header is the only intentional difference between the two implementations, so a response tells you which process answered. The browser does not read it.

Content types by suffix, case-insensitive. A missing or unknown suffix, including a bare `qmldir`, is `text/plain; charset=utf-8`.

| Suffix | Content-Type |
|---|---|
| qml | `text/x-qml; charset=utf-8` |
| js, mjs | `text/javascript; charset=utf-8` |
| json | `application/json; charset=utf-8` |
| txt | `text/plain; charset=utf-8` |
| html | `text/html; charset=utf-8` |
| css | `text/css; charset=utf-8` |
| svg | `image/svg+xml` |
| png | `image/png` |
| jpg, jpeg | `image/jpeg` |
| gif | `image/gif` |
| webp | `image/webp` |
| ico | `image/vnd.microsoft.icon` |
| ttf | `font/ttf` |
| otf | `font/otf` |

Error pages and directory listings are `text/x-qml; charset=utf-8`. A listing sorts directories first, then by name. Directory rows end in `/` and are labeled `dir`. Files are labeled `<size> B`. Every listing except `/` has a `../` row. Each row calls `browser.navigate` with that name.

The access log is local time, one line per response:

```
hh:mm:ss  METHOD  CODE  SIZE B  target
23:21:31  GET    200     7714 B  /index.qml
```

`METHOD` is padded to 5 characters and `SIZE` to 7. `SIZE` is the body size even for `HEAD`. An empty target is printed as `-`.

Sockets are opened with `TCP_NODELAY`.

## The Node server

`nodejs\server.js` is a single file and uses only the Node `net` and `fs` modules, so framing, pipelining, `HEAD`, and the log line can match the Qt server. `npm start` from `nodejs\` runs it. Node 18 or newer is enough. The same file runs on macOS.

It looks for `server\wwwroot` by walking up from the `nodejs` folder, which is one level under the repository. The Qt executable looks up from `bin\x64\<Config>\`. Both land on the same directory.

Percent-decoding uses `decodeURIComponent`. A broken `%` sequence is `400`. The Qt server's decoder is more lenient and leaves invalid sequences in place. Paths that both decode the same way produce the same status and body.

## How a tab works

`QmlBrowser` listens on a `QLocalServer` named `qmlbrowser-<pid>-<uuid>`. That name is a pipe on Windows and a socket file elsewhere. Creating a tab starts:

```
QmlRenderer.exe --channel <name> --tab <id>
```

On macOS the browser starts `QmlRenderer.app`. On Windows it starts `QmlRenderer.exe`. The renderer connects, then sends `hello` on the next event-loop turn, with its process id. It does not create a window. `QQuickRenderControl` rasterizes the Qt Quick scene into an offscreen texture. The pixels are copied into a shared-memory bitmap and announced with `frame`. `TabViewport` uploads that bitmap into the browser window's scene graph.

`QmlRenderer.exe` refuses to start without `--channel` (exit 2). If it cannot connect within 5 seconds it exits 1. If the browser disconnects, the renderer quits.

Closing a tab closes the socket. The renderer treats that as the request to exit. If the process is still alive after 2 seconds, it is killed. The same grace period is used when the browser itself is shutting down.

If the renderer exits for any other reason, the tab is marked crashed. A crash exit says the process terminated unexpectedly. A normal exit reports the code. The recovery page drawn by the browser chrome covers that tab. Restart launches a new renderer into the same history.

### Messages

`IpcChannel` writes one compact JSON object and a newline. Compact JSON escapes newlines inside strings, so a message can carry a whole document. Unknown fields are ignored. An unknown `type` is ignored.

Browser to renderer:

| type | Fields | Effect |
|---|---|---|
| `navigate` | `url` string | Load that absolute URL. |
| `reload` | | Fetch the current URL again. |
| `stop` | | Abort the current download. |
| `chrome` | `fullScreen` bool, `chromeHidden` bool | Set the on-page notice. |
| `viewport` | `visible` bool, `width`, `height`, `dpr` | Logical page size and device pixel ratio. A hidden tab is snapshotted when the size changes and otherwise does not draw. A repeated value is not sent again. |
| `input` | `kind` plus event fields | A pointer, wheel, key, focus, or input-method event in viewport coordinates. See the table below. |
| `frameAck` | `seq` number | The browser has copied that frame. The renderer may reuse the slot. |

Renderer to browser:

| type | Fields | Effect |
|---|---|---|
| `hello` | `tab` number, `pid` number | The renderer is connected and can take a viewport. |
| `frame` | `seq`, `width`, `height`, `stride`, `format`, `shm` | A new bitmap is in the shared-memory segment named `shm`. `width` and `height` are device pixels. `format` 1 is premultiplied RGBA, 2 is premultiplied BGRA. |
| `cursor` | `shape` number | `Qt::CursorShape` for the page under the pointer. |
| `state` | `status`, `title`, `url`, `httpStatus`, `progress`, `error`, `bytes` | Update the tab strip, address bar, and status line. Sent at most once per event-loop turn. |
| `source` | `text` | The source panel. |
| `navigate` | `url` | The page called `browser.navigate`. The browser owns history and sends a `navigate` back. |
| `fullscreen` | `on` | The page called `browser.setFullScreen`. |
| `ime` | `serial`, `enabled`, and when enabled `hints`, `readOnly`, `cursorRect`, `anchorRect`, `clipRect`, `text`, `selection`, `cursor`, `anchor`, `absolute`, `before`, `after` | Input-method state of the focused item. Rectangles are `[x, y, width, height]` in viewport coordinates. Sent only when something changed. `serial` changes when a different item takes focus. |

`input` kinds:

| kind | Fields | Notes |
|---|---|---|
| `mouse` | `action` (`press`, `release`, `move`, `dbl`), `x`, `y`, `button`, `buttons`, `mods` | `move` is sent while a button is down. |
| `hover` | `action` (`move`, `leave`), `x`, `y`, `mods` | Pointer moves with no button down. |
| `wheel` | `x`, `y`, `px`, `py`, `ax`, `ay`, `buttons`, `mods`, `phase`, `inverted` | Pixel and angle deltas. |
| `key` | `action` (`press`, `release`), `key`, `text`, `mods`, `repeat`, `scan`, `vk`, `nativeMods` | Browser shortcuts never arrive here. |
| `focus` | `on` | The page area gained or lost keyboard focus. |
| `ime` | `preedit`, `commit`, `replaceStart`, `replaceLength`, `attributes` | One `QInputMethodEvent`. `attributes` holds the cursor, selection and preedit formats (`underline`, `underlineColor`, `foreground`, `background`). |

`WidgetApp` answers the same messages. Its `state` always has status 2 and no HTTP fields. It sends `navigate` when the user picks a page inside the app, so the browser records a history entry; the `navigate` that comes back is ignored because the app is already there. Reloading an app tab restarts the process instead of sending `reload`, and `stop` is ignored because nothing is ever loading.

`status` in `state` matches `PageView`: 0 null, 1 loading, 2 ready, 3 error. `bytes` is the character length of the source.

`TabViewport` publishes the logical page size and the window's device pixel ratio on `QQuickWindow::afterAnimating`. That covers resizes, the bars showing or hiding, and DPI changes. Only the active tab's renderer is asked to keep drawing. Two shared-memory slots are in flight at once, so the renderer can prepare the next frame while the browser still holds the previous one.

## Input methods

The platform input method (the macOS text input system, Windows IMM/TSF) talks to the browser window, because that is the only window. The text field is in another process, so `TabViewport` stands in for it:

1. The tab process reports the focused item's state as an `ime` message: whether it takes text, its hints, the caret and anchor rectangles mapped to viewport coordinates, and the surrounding text. The renderer re-reads it after every input event and every frame, so scrolling moves the candidate window with the caret. Only changes are sent.
2. `TabViewport` sets `ItemAcceptsInputMethod` from `enabled` and answers `inputMethodQuery` from that state. macOS only routes keys through the input method when the focus object reports `ImEnabled`, so this is what turns composition on. A password field, a digits-only field or a button keeps it off.
3. Each `QInputMethodEvent` the platform delivers (preedit, commit, replacement range, cursor and format attributes) is forwarded as `input` with kind `ime` and replayed into the focused item.
4. When focus moves to another item while text is being composed, the tab process commits the pending preedit into the item that lost focus and bumps `serial`. On the new serial the browser resets the platform input method and drops the commit that reset produces, so the text is inserted once.

The candidate window position comes from the last report, so it can trail the caret by one message while typing quickly.

`/ime.qml` and `/ime-controls.qml` exercise this with every kind of field. The Controls page of the widget app has a line edit, an editable combo box, a rich-text editor and an editable table for the same purpose.

## App tabs (app://widgets)

`app://widgets` is a native Qt Widgets application, `WidgetApp`, hosted in a tab. It is a `QMainWindow` with a menu bar, a tool bar, a status bar and five pages: standard controls, freehand painting, raster pictures, vector graphics (`QGraphicsView`, plus an SVG item when Qt SVG is available) and video. The video page plays a generated test clip. With Qt Multimedia in the kit it can also play a file, through a `QVideoSink` rather than `QVideoWidget`, because `QVideoWidget` draws on a native surface that `QWidget::render` cannot capture.

`BrowserTab` picks the program from the URL. When the current history entry moves between a QML document and `app://widgets`, whether through a link, the address bar, back or forward, the tab retires its process and starts the other one. The new process gets the URL after `hello`, as a renderer does. Reload restarts the app, since there is nothing to fetch again. Any other `app://` host goes to the renderer, which shows its error page.

Hosted, the app runs on Qt's `offscreen` platform, so none of its windows reach the screen. That covers the main window and also menus, combo box lists, tooltips, the date popup, and dialogs such as `QMessageBox`, `QColorDialog` and the non-native `QFileDialog`. `BrowserBridge` keeps the visible top-level widgets in stacking order and paints each one with `QWidget::render` into the shared-memory frame, at the browser's device pixel ratio. Dialogs get a drawn title strip and shadow, and a modal one dims what is behind it. The offscreen screen is resized to the viewport, so Qt keeps popups inside the tab.

`input` messages are replayed through `QWindowSystemInterface`, synchronously, into the top-level window under the pointer, or into the active popup. Qt then does its normal work: double clicks, enter and leave, hover, tooltips, popup dismissal, modal blocking and shortcuts. The `dbl` action is dropped because Qt derives the double click from two presses. Input-method events and the `ime` state work as for a QML page, with rectangles mapped to page coordinates. `cursor` reports the cursor of the widget under the pointer, or of the pressed widget during a drag.

Two details matter because widgets run nested event loops (`QDialog::exec`, `QMenu::exec`):

- Incoming messages are handled through a queued connection. `QLocalSocket` does not emit `readyRead` again while a `readyRead` handler is still running, so a click that opens a dialog would otherwise cut the app off from the browser until the dialog closed.
- The implicit mouse grab ends before a release is delivered, because the release that opens a modal dialog does not return until the dialog closes.

Picking a page inside the app sends `navigate` with `app://widgets/<page>`, so the browser adds a history entry the way a single-page web app routes. View source shows the app's widget tree.

Limits: the offscreen platform has its own clipboard, so copy and paste between the app and other programs does not work. Drag and drop is limited to inside the app. Dialogs cannot be dragged. Content that `QWidget::render` cannot capture, such as `QOpenGLWidget`, `QQuickWidget` or `QVideoWidget`, stays blank.

Run without `--channel`, `WidgetApp` is an ordinary desktop application on the native platform. `--page <name>` opens a page.

On Windows the offscreen platform draws text with a FreeType font database rather than DirectWrite. That database scans `QT_QPA_FONTDIR`, or `<Qt>\lib\fonts`, which a Qt install does not have, and it skips `.ttc` collections, which is how Windows ships its Chinese and Japanese fonts. Hosted on Windows, the app therefore points `QT_QPA_FONTDIR` at `%WINDIR%\Fonts` (unless it is already set) and registers every `.ttc` in that folder at startup. Scanning that folder makes the first start of an app tab take a moment longer. On macOS the offscreen platform uses Core Text and needs neither step.

### Hosting other Qt applications

An app tab hosts one program today: `WidgetApp`. Three things tie it to that program:

- `BrowserBridge` is compiled into `WidgetApp`. It is not a library or a plugin, so another program cannot use it without being rebuilt with it.
- The bridge takes a `GalleryWindow*`. It uses it in three places, to map `app://widgets/<page>` to a page and back. The rest of the bridge works for any `QWidget` windows: compositing, popups, dialogs, input, cursor and input method.
- `BrowserTab::engineFor` maps the host `widgets` to the executable `WidgetApp` and knows no other.

What can be shown in a tab depends on how it is drawn:

| Content | Can it be shown in a tab? |
|---|---|
| A QML document served over HTTP | Yes. That is what a normal tab is. `QmlRenderer` draws it with `QQuickRenderControl`. |
| A Qt Widgets application | Yes, once it is linked with the bridge. The bridge captures widgets with `QWidget::render`. |
| A Qt Quick application (its own executable, `main()` and C++ types) | Not yet. `QmlRenderer` can load a `.qml` file but not the C++ types the application registers. The widget bridge cannot capture a `QQuickWindow`. |
| `QQuickWidget`, `QOpenGLWidget`, `QVideoWidget` inside a Widgets app | No. `QWidget::render` does not capture GPU surfaces, so they stay blank. |
| A program that does not use Qt | No. That would need window capture and input injection at the operating-system level, which is outside this design. |

> [!IMPORTANT]
> **Recommendation: make the bridge a Qt platform plugin (`-platform qmlbrowser`).**
>
> Qt loads a platform plugin before the application creates its first window. The plugin can give every window a backing store in shared memory and feed input in through `QWindowSystemInterface`, which is exactly what `BrowserBridge` does now. The difference is that the bridge would no longer live in the application:
>
> - **Unmodified Qt executables run in a tab.** The browser starts them with `-platform qmlbrowser` plus the channel arguments. They are not rebuilt or relinked.
> - **Widgets and Qt Quick are both covered.** Widgets draw into the backing store as they do now. Qt Quick works with its software renderer (`QT_QUICK_BACKEND=software`). A Qt Quick app that needs the GPU renderer requires extra work in the plugin: an offscreen graphics surface and a readback of each frame.
> - **Window stacking, popups and modality move into the plugin.** It sees every window being created, shown, raised and closed, so it does not have to infer them from events the way the bridge does.
> - **The browser side stays almost the same.** `engineFor` looks the URL up in a table of registered applications instead of a single hard-coded name.
>
> The costs:
>
> - A platform plugin uses Qt's private API, so it must be built against the exact Qt version of every application it hosts: 6.10.3 for the Windows build, 6.7 for the macOS one.
> - It is several days of work, against about one day for the shorter option below.
>
> **If the only goal is to host a few Widgets applications whose source you control**, do the shorter option instead:
>
> 1. Remove the three `GalleryWindow` uses from `BrowserBridge` and make page routing an optional callback.
> 2. Build `BrowserBridge`, `IpcChannel` and `SharedPixels` as a static library.
> 3. Let each application call one function from `main()` when it sees `--channel`.
> 4. Map `app://<name>` to an executable through a configuration file read by `BrowserTab`.
>
> This keeps the code that has already been tested. It does not help with Qt Quick applications or programs you cannot rebuild.

## The window, on Windows

`WindowFrame` keeps `WS_CAPTION` and `WS_THICKFRAME`. `WM_NCCALCSIZE` insets the client area by the resize border only, so the caption area becomes client pixels and the tab strip can draw there. `WM_NCHITTEST` classifies that frame itself:

- the real non-client border returns the resize codes
- empty space on the tab strip returns `HTCAPTION`
- everything else in the client returns `HTCLIENT`

`Qt.FramelessWindowHint` is not used on the browser. It would remove the resize borders, and those borders have to sit outside the client area so the tab strip can occupy the caption.

The page is part of this window's scene graph, so it moves with the frame. The content area paints `#0e1017` until the first frame arrives. The crash page is a rectangle drawn over the viewport. The full-screen reminder is drawn into the page frame via the `chrome` message. Cursor changes from the page are applied to the viewport item.

On macOS the same compositing path runs. The custom caption does not.

## Source tree

```
qt.props                 Windows Qt location and version, warnings, output directories
QmlBrowser.sln            Windows solution
build.bat                msbuild Debug or Release, x64
run.bat                  QmlServer.exe, then QmlBrowser.exe
CMakeLists.txt           CMake build. Apple requires Qt 6.7; other platforms require Qt 6.10
CMakePresets.json        macos-debug (Ninja) and macos-xcode, both Debug, deployment target 11.0
build-macos.sh           configure the Xcode Debug build from QTDIR (Qt 6.7.3)
README.md                short overview and prerequisites
DOCUMENTATION.md         this file

shared\
  IpcChannel.h/.cpp      newline-delimited JSON over QLocalSocket
  SharedPixels.h/.cpp    shared-memory frame bitmap

server\                  QmlServer.exe
  main.cpp               command line and document-root search
  HttpServer.h/.cpp      GET/HEAD, keep-alive, types, listings, errors
  wwwroot\               the site the server reads
    index.qml about.qml gallery.qml docs.qml
    ime.qml              input-method test bench, core Qt Quick fields
    ime-controls.qml     the same with Qt Quick Controls
    shared\              qmldir, Page.qml, Card.qml, NavLink.qml
    assets\logo.svg

nodejs\
  package.json           no dependencies
  server.js              the same HTTP server on Node.js

client\                  QmlBrowser.exe
  main.cpp               start URL, type registration, WindowFrame
  WindowFrame.h/.cpp     caption-less frame, Windows only
  TabManager.h/.cpp      local server, tab list, viewport size
  BrowserTab.h/.cpp      one tab process (renderer or app), history, IPC, frames, IME state
  BrowserHistory.h/.cpp  back/forward and address-bar resolution
  TabViewport.h/.cpp     draws the active frame, forwards input, stands in for the IME
  ui\Browser.qml         chrome
  ui\TabButton.qml ui\ToolButton.qml
  resources.qrc          chrome compiled into the executable

widgetapp\               WidgetApp.exe, the app://widgets tab
  main.cpp               hosted (offscreen platform) or standalone start, dark Fusion theme
  BrowserBridge.h/.cpp   renderer protocol for widgets: compositing, input, IME, cursor
  GalleryWindow.h/.cpp   QMainWindow, menus, tool bar, page routing
  ControlsPage.h/.cpp    buttons, menus, dialogs, text input, ranges, item views
  PaintPage.h/.cpp       freehand canvas
  PicturePage.h/.cpp     generated pictures with zoom, pan and rotation
  VectorPage.h/.cpp      QGraphicsView scene, optional SVG item
  VideoPage.h/.cpp       generated clip, optional Qt Multimedia playback
  WidgetApp.vcxproj      Windows project; also copies the offscreen platform plugin

renderer\                QmlRenderer.exe
  main.cpp               offscreen frame loop, IPC
  OffscreenPage.h/.cpp   windowless Qt Quick rasterization, input and IME replay
  Info.plist.in          LSUIElement so the macOS renderer bundle has no Dock icon
  PageView.h/.cpp        download and instantiate one document
  ui\Renderer.qml        page, spinner, error page, full-screen notice
  resources.qrc
```

`client\GeneratedFiles\`, `renderer\GeneratedFiles\` and `widgetapp\GeneratedFiles\` are `moc` and `rcc` output. `bin\` and `obj\` are build output.
