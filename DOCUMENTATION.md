# QML Browser

A browser whose documents are QML, not HTML. The window, the tabs, and the pages are three separate programs. There is no Chromium, no `QWebEngineView`, and no HTML parser. A page is compiled by the Qt Quick engine and drawn by the Qt Quick scene graph.

This file describes how the project is built, how a page is loaded, and what a document is allowed to do. `README.md` is the short version, including the Windows and macOS prerequisites.

The shipped build is Windows, Visual Studio, Qt 6.10.3. The HTTP servers and the QML documents are portable. The window frame, the child-window embedding, and the erase-background handling are Windows-only.

## The three programs

| Program | Project | What it does |
|---|---|---|
| `QmlServer.exe` | `server\` | Serves files from a document root over HTTP/1.1. |
| `QmlBrowser.exe` | `client\` | The window: tab strip, address bar, history, status line. It does not draw page content. |
| `QmlRenderer.exe` | `renderer\` | One process per tab. Downloads a document, compiles it, and draws it. |

`QmlBrowser.exe` starts `QmlRenderer.exe` itself. You start the server and the browser.

A Node.js server in `nodejs\` speaks the same HTTP as `QmlServer.exe` and serves the same files. It is a second implementation of the server, not a replacement. See [The Node server](#the-node-server).

```
QmlBrowser.exe                         QmlServer.exe  or  node server.js
  tab strip, address bar, history          HTTP GET/HEAD of *.qml
        |                                        ^
        |  QLocalSocket, one JSON object         |  QNetworkAccessManager
        |  per line                              |
        v                                        |
  QmlRenderer.exe  ---- HTTP GET --------------+
    one process per tab
    Windows: QQuickView reparented into the browser
    macOS: frameless tool window, moved by place
```

State (URL, title, HTTP status, shortcuts) travels on the local socket. On Windows the size and position of the page do not: the browser moves the renderer's native window with `SetWindowPos` and `ShowWindow`. On macOS that window id is not usable across processes, so the browser sends the rectangle in a `place` message.

## Requirements

### Windows

- Windows
- Qt 6 for MSVC x64. The tree is set up for Qt 6.10.3 at `C:\Qt\6.10.3\msvc2022_64`.
- Visual Studio 2022 or later with the Desktop development with C++ workload.

Point the build at a different Qt by setting `QtDir` before Visual Studio or `build.bat`, or by editing `QtDir` in `qt.props`:

```bat
set QtDir=C:\Qt\6.9.0\msvc2022_64
```

There is no CMake and no qmake step. `moc` and `rcc` are MSBuild custom build steps. After the link, `windeployqt` copies the Qt DLLs next to the executables. `build.bat` and `QmlBrowser.sln` are this configuration.

### macOS

- macOS 12.6, with Xcode 14.2. That is the newest Xcode this release of macOS can run. `CMAKE_OSX_DEPLOYMENT_TARGET` is `11.0`, the oldest macOS Qt 6.7 itself runs on.
- Qt 6.7 for macOS. Point `QTDIR` at that kit, usually `$HOME/Qt/6.7.3/macos`. Qt 6.8 and later need Xcode 15, which does not install here.
- CMake 3.22 or newer

`build.bat` and `QmlBrowser.sln` are the Windows build. `CMakeLists.txt` and `CMakePresets.json` are the macOS one. `./build-macos.sh` configures `build/macos-xcode` with the Xcode generator and compiles Debug. The generated schemes do not require a code-signing team. A `qt.conf` inside each bundle names `QTDIR`, so a Debug run from Xcode finds Qt.

Open `build/macos-xcode/QmlBrowser.xcodeproj`. Run the QmlServer scheme, then debug the QmlBrowser scheme. Qt Creator uses the `macos-debug` preset the same way: Debug, `CMAKE_PREFIX_PATH` from `QTDIR`.

On macOS the page is not a child window. The browser sends `{ "type": "place", "visible", "x", "y", "width", "height", "above" }` in global logical pixels. `above` is the browser `NSWindow`'s `windowNumber`. The renderer is a frameless `Qt::Tool` window at floating level, ordered above that window. `QmlRenderer.app` is an agent (`LSUIElement` in `renderer/Info.plist.in`), and the process sets the accessory activation policy, so a tab does not get a Dock icon. A show does not take key from the browser; the first click on the page does. The custom caption, `WM_ERASEBKGND`, and `WM_SETCURSOR` are still Windows-only.

The Node server runs there with Node.js 18 or newer and no packages:

```sh
node nodejs/server.js
node nodejs/server.js -help
node nodejs/server.js -public
```

Shared Qt behavior already applies on macOS: the address bar, a blank new tab, the server command line, HTTP, the page API, and the dark rectangle behind the page. The page window follows the viewport through `place`. These stay Windows-only:

- the custom caption (`WindowFrame`)
- embedding the renderer as a child window (`SetWindowPos` / `ShowWindow`)
- the `WM_ERASEBKGND` fill that hides the new-tab flash
- the `WM_SETCURSOR` handler that stops the resize pointer from sticking

On macOS the browser starts `QmlRenderer.app`. On Windows it starts `QmlRenderer.exe`.

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

Opening `QmlBrowser.sln` and building does the same thing as `build.bat`. Output goes to `bin\x64\Debug\` or `bin\x64\Release\`.

`run.bat` starts the server in its own console, waits about a second, then opens the browser on `http://127.0.0.1:8080/index.qml`.

The first build runs `windeployqt`. Later builds skip it because of a stamp file, `bin\x64\<Config>\qtdeploy_*.stamp`. Delete that stamp to deploy again.

Pass a start URL as the browser's first argument. Anything that begins with `-` is ignored and the default page is used.

```bat
QmlBrowser.exe
QmlBrowser.exe http://127.0.0.1:9000/about.qml
```

The default start URL is `http://127.0.0.1:8080/index.qml`.

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

Shortcuts work while the page has keyboard focus. The renderer recognizes them and sends them to the browser instead of letting the page consume them. Esc is only taken while the chrome is actually hidden, so a page can use Esc the rest of the time.

Full screen and chrome-hide are independent. Hiding the bars, then going full screen, then pressing Esc leaves full screen and leaves the bars hidden. A second Esc shows the bars. While the chrome is off the screen, the page covers the browser window, so the "press Esc" reminder is drawn by the renderer.

A page can call `browser.crash()` to abort its own renderer. That tab shows a recovery page. Reload starts a new `QmlRenderer.exe` on the same history. Other tabs are unaffected.

### Address bar

What you type is resolved by `BrowserHistory::resolve`.

| Typed | Result |
|---|---|
| `about.qml`, `shared/Page.qml`, `/index.qml`, `./docs.qml` | Relative to the open page |
| `127.0.0.1`, `localhost`, `[::1]` | `http://` plus that host. If the open page is the same host and has a port, that port is kept, so `127.0.0.1` on port 8080 opens `http://127.0.0.1:8080/` |
| `127.0.0.1:9000`, `localhost:9000`, `example.com:8080` | `http://` plus host, port, and any path |
| `http://...`, `https://...`, `file://...`, `qrc:...` | Used as written. An empty HTTP or HTTPS path becomes `/` |
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

A served document cannot read local files on the machine running the browser. It can import only modules the client already has: Qt Quick modules linked into `QmlRenderer.exe`, and modules fetched by URL from the server.

The sample site:

| URL | File |
|---|---|
| `/` and `/index.qml` | `server\wwwroot\index.qml` |
| `/about.qml` | `server\wwwroot\about.qml` |
| `/docs.qml` | `server\wwwroot\docs.qml` |
| `/gallery.qml` | `server\wwwroot\gallery.qml` |
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
QmlRenderer.exe --channel <name> --tab <id> --parent <browser window id>
```

The renderer connects, then sends `hello` on the next event-loop turn, with its own window handle and process id. Sending it from inside `waitForConnected` would show the window from a nested loop, and AppKit would leave the tab blank. The view is frameless and its clear color is `#0e1017`. On Windows it reparents into the browser with `QWindow::fromWinId` and `QWindow::setParent`, and the browser shows and moves that child with `SetWindowPos` and `ShowWindow` after `hello`. On macOS that handle is not a cross-process window id, so the renderer stays its own window and applies `place` messages instead.

`QmlRenderer.exe` refuses to start without `--channel` (exit 2). If it cannot connect within 5 seconds it exits 1. If the browser disconnects, the renderer quits.

Closing a tab closes the socket. The renderer treats that as the request to exit. The browser does not post `WM_CLOSE`, because on Windows the renderer window is a child and would not receive it. If the process is still alive after 2 seconds, it is killed. The same grace period is used when the browser itself is shutting down.

If the renderer exits for any other reason, the tab is marked crashed. A crash exit says the process terminated unexpectedly. A normal exit reports the code. The child window is gone, so the recovery page drawn by the browser chrome is visible. Restart launches a new renderer into the same history.

### Messages

`IpcChannel` writes one compact JSON object and a newline. Compact JSON escapes newlines inside strings, so a message can carry a whole document. Unknown fields are ignored. An unknown `type` is ignored.

Browser to renderer:

| type | Fields | Effect |
|---|---|---|
| `navigate` | `url` string | Load that absolute URL. |
| `reload` | | Fetch the current URL again. |
| `stop` | | Abort the current download. |
| `chrome` | `fullScreen` bool, `chromeHidden` bool | Remember whether the chrome is off screen, and set the on-page notice. |
| `place` | `visible` bool, `x`, `y`, `width`, `height`, `above` number | macOS only. Move or hide the renderer's own window and stack it above `above` (the browser window number, or 0). Coordinates are global logical pixels. A repeated rectangle is not sent again unless placement is forced. Windows keeps using `SetWindowPos` and does not send this. |

Renderer to browser:

| type | Fields | Effect |
|---|---|---|
| `hello` | `tab` number, `winId` number, `pid` number | The renderer window exists and can be placed. On Windows that window is the child. On macOS it is the tool window. |
| `state` | `status`, `title`, `url`, `httpStatus`, `progress`, `error`, `bytes` | Update the tab strip, address bar, and status line. Sent at most once per event-loop turn. |
| `source` | `text` | The source panel. |
| `navigate` | `url` | The page called `browser.navigate`. The browser owns history and sends a `navigate` back. |
| `fullscreen` | `on` | The page called `browser.setFullScreen`. |
| `shortcut` | `key` | A browser shortcut was pressed while the page had focus. `key` is `Ctrl+T`, `Ctrl+W`, `Ctrl+Tab`, `Ctrl+Shift+Tab`, `Ctrl+R`, `Ctrl+L`, `Ctrl+U`, `Ctrl+Shift+F`, `Alt+Left`, `Alt+Right`, `F5`, `F11`, or `Escape`. |

`status` in `state` matches `PageView`: 0 null, 1 loading, 2 ready, 3 error. `bytes` is the character length of the source.

`TabViewport` publishes the page rectangle in device pixels on `QQuickWindow::afterAnimating`. On Windows that moves the child window when the window is resized, when the bars show or hide, or when the DPI scale changes. On macOS `syncPlacement` also runs when the browser window moves, because the page is a separate window and an unchanged viewport rectangle still has a new screen position. Only the active tab's renderer is shown.

## The window, on Windows

`WindowFrame` keeps `WS_CAPTION` and `WS_THICKFRAME`. `WM_NCCALCSIZE` insets the client area by the resize border only, so the caption area becomes client pixels and the tab strip can draw there. `WM_NCHITTEST` classifies that frame itself:

- the real non-client border returns the resize codes
- empty space on the tab strip returns `HTCAPTION`
- everything else in the client returns `HTCLIENT`

`Qt.FramelessWindowHint` is not used on the browser. It would remove the resize borders, and those borders have to sit outside the client area because the renderer covers whatever the chrome paints in the page rectangle.

The renderer is a child window. Windows forwards `WM_SETCURSOR` for that child to the parent, and the parent's class cursor is null, so a size cursor from the frame edge would stick after the bars hide and the page meets the border. The renderer handles `WM_SETCURSOR` on `HTCLIENT` and sets the arrow, unless the page has set its own cursor (a link, a splitter). It also paints `#0e1017` on `WM_ERASEBKGND`. The browser frame paints `#0b0d13` on erase, and the QML content area has a rectangle of `#0e1017` behind the viewport. Those three are what stop the white flash when a new tab hides the previous child before the new renderer has drawn.

The renderer window sits above anything the chrome draws in the same rectangle. Hints that must stay visible over the page are either the status line or a notice drawn by the renderer. The crash page is visible only while that tab has no live child window.

On macOS none of this native frame, cursor, or erase handling runs. The QML underlay (`#0e1017`) is still there. The page is a separate frameless tool window. `place` moves it and orders it above the browser at floating window level. Showing it activates the browser process again so the tab does not steal the menu bar. The first mouse press on the page is what makes that window key.

## Source tree

```
qt.props                 Windows Qt location, warnings, output directories
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

server\                  QmlServer.exe
  main.cpp               command line and document-root search
  HttpServer.h/.cpp      GET/HEAD, keep-alive, types, listings, errors
  wwwroot\               the site the server reads
    index.qml about.qml gallery.qml docs.qml
    shared\              qmldir, Page.qml, Card.qml, NavLink.qml
    assets\logo.svg

nodejs\
  package.json           no dependencies
  server.js              the same HTTP server on Node.js

client\                  QmlBrowser.exe
  main.cpp               start URL, type registration, WindowFrame
  WindowFrame.h/.cpp     caption-less frame, Windows only
  TabManager.h/.cpp      local server, tab list, placement
  BrowserTab.h/.cpp      one renderer process, history, IPC
  BrowserHistory.h/.cpp  back/forward and address-bar resolution
  TabViewport.h/.cpp     the rectangle the active renderer fills
  ui\Browser.qml         chrome
  ui\TabButton.qml ui\ToolButton.qml
  resources.qrc          chrome compiled into the executable

renderer\                QmlRenderer.exe
  main.cpp               child window on Windows, place and AppKit stacking on macOS, IPC, shortcuts
  Info.plist.in          LSUIElement so the macOS renderer bundle has no Dock icon
  PageView.h/.cpp        download and instantiate one document
  ui\Renderer.qml        page, spinner, error page, full-screen notice
  resources.qrc
```

`client\GeneratedFiles\` and `renderer\GeneratedFiles\` are `moc` and `rcc` output. `bin\` and `obj\` are build output.
