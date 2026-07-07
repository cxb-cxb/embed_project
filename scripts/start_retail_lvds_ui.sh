#!/bin/sh
set -eu

cd /userdata/Embed_project

pkill -9 gst-launch-1.0 2>/dev/null || true
pkill -9 qr_scanner_display 2>/dev/null || true

LOG_FILE="${LOG_FILE:-/userdata/Embed_project/qr_scanner_display.log}"
CAMERA_DEV="${CAMERA_DEV:-/dev/video5}"
CAMERA_WIDTH="${CAMERA_WIDTH:-800}"
CAMERA_HEIGHT="${CAMERA_HEIGHT:-600}"

for b in /sys/class/backlight/*; do
  [ -e "$b" ] || continue
  [ -f "$b/bl_power" ] && echo 0 > "$b/bl_power" 2>/dev/null || true
  if [ -f "$b/brightness" ]; then
    max="$(cat "$b/max_brightness" 2>/dev/null || echo 255)"
    echo "$max" > "$b/brightness" 2>/dev/null || true
  fi
done

rm -f /tmp/qr_scanner_display.pid "$LOG_FILE"
start-stop-daemon -S -b -m -p /tmp/qr_scanner_display.pid \
  -x /userdata/Embed_project/bin/qr_scanner_display -- \
  -d "$CAMERA_DEV" -W "$CAMERA_WIDTH" -H "$CAMERA_HEIGHT" -c

echo "[retail-ui] started pid=$(cat /tmp/qr_scanner_display.pid) log=$LOG_FILE"
