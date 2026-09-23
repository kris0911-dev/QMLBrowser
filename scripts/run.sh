#!/usr/bin/env bash
# Launch QMLBrowser against the headless X server started by .cursor/start.sh.
# Usage: scripts/run.sh [url]
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$REPO_ROOT/build/QMLBrowser"

if [ ! -x "$BIN" ]; then
  echo "QMLBrowser is not built. Run: cmake --build build (see README)." >&2
  exit 1
fi

export DISPLAY="${DISPLAY:-:99}"
export LIBGL_ALWAYS_SOFTWARE=1
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/runtime-$(id -un)}"
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"

# Chromium (WebEngine) cannot use its sandbox or a GPU inside the container.
export QTWEBENGINE_CHROMIUM_FLAGS="${QTWEBENGINE_CHROMIUM_FLAGS:---no-sandbox --disable-gpu --disable-dev-shm-usage}"

exec "$BIN" "$@"
