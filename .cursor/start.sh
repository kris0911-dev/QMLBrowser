#!/usr/bin/env bash
# Per-boot runtime setup for QMLBrowser.
# Starts a headless X server (Xvfb) on :99 so the Qt/QML GUI (and Chromium-based
# WebEngine) can render without a physical display. Idempotent and safe to re-run.
set -euo pipefail

DISPLAY_NUM=":99"
SCREEN_GEOMETRY="1280x800x24"
LOCK_FILE="/tmp/.X99-lock"

if [ -e "$LOCK_FILE" ] && pgrep -x Xvfb >/dev/null 2>&1; then
  echo "==> Xvfb already running on ${DISPLAY_NUM}"
else
  echo "==> Starting Xvfb on ${DISPLAY_NUM} (${SCREEN_GEOMETRY})"
  rm -f "$LOCK_FILE" 2>/dev/null || true
  Xvfb "$DISPLAY_NUM" -screen 0 "$SCREEN_GEOMETRY" -ac +extension GLX +render -noreset \
    > /tmp/xvfb.log 2>&1 &

  for _ in $(seq 1 30); do
    if xdpyinfo -display "$DISPLAY_NUM" >/dev/null 2>&1; then
      break
    fi
    sleep 0.2
  done
fi

if xdpyinfo -display "$DISPLAY_NUM" >/dev/null 2>&1; then
  echo "==> Display ${DISPLAY_NUM} is ready"
else
  echo "!! Display ${DISPLAY_NUM} did not become ready" >&2
  exit 1
fi
