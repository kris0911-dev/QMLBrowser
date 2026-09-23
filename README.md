# QMLBrowser

A minimal desktop web browser built with **Qt 6 Quick (QML)** and **Qt WebEngine**.
It provides a toolbar (back / forward / reload), an address bar with search
fallback, a load-progress indicator, and a full Chromium-based `WebEngineView`.

## Project layout

| Path | Description |
| --- | --- |
| `src/main.cpp` | Application entry point; initializes Qt WebEngine and loads the QML UI. |
| `qml/Main.qml` | The browser UI (toolbar, address bar, `WebEngineView`). |
| `qml.qrc` | Qt resource file bundling the QML into the binary. |
| `CMakeLists.txt` | CMake build definition. |
| `.cursor/` | Cloud Agent environment configuration (`install.sh`, `start.sh`). |
| `scripts/run.sh` | Helper to launch the built browser on the headless display. |

## Prerequisites

- Qt 6 (Base, Declarative/Quick, Quick Controls, WebEngine) development packages
- A C++17 compiler (GCC) and CMake ≥ 3.16

On Ubuntu 24.04 the full dependency set is installed by
[`.cursor/install.sh`](.cursor/install.sh).

## Build

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build
```

> Note: on Ubuntu `/usr/bin/c++` may resolve to `clang++`, which cannot find
> `libstdc++`. Qt is built with GCC, so configure with `gcc`/`g++` as shown above.

## Run

### On a machine with a display

```bash
./build/QMLBrowser
```

### Headless (Cloud Agent / server)

A headless X server is required. `.cursor/start.sh` starts `Xvfb` on `:99`:

```bash
bash .cursor/start.sh          # starts Xvfb :99 (idempotent)
DISPLAY=:99 scripts/run.sh     # launch the browser
DISPLAY=:99 scripts/run.sh https://example.com
```

`scripts/run.sh` sets software OpenGL (`LIBGL_ALWAYS_SOFTWARE=1`) and the Chromium
flags required inside a container (`--no-sandbox --disable-gpu`).

## Usage

- Type a URL or search terms in the address bar and press <kbd>Enter</kbd>.
  Bare hostnames get an `https://` prefix; free text falls back to a web search.
- Use the ◀ / ▶ buttons to move through history and ↻ to reload (✕ to stop while loading).
