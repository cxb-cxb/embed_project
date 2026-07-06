#!/bin/sh
set -eu

PROJECT_DIR="${PROJECT_DIR:-/userdata/Embed_project}"
LOG_FILE="${LOG_FILE:-/tmp/qsm_lvds_demo.log}"
CAMERA_DEV="${CAMERA_DEV:-/dev/video5}"
CAMERA_WIDTH="${CAMERA_WIDTH:-800}"
CAMERA_HEIGHT="${CAMERA_HEIGHT:-600}"

echo "[demo] QSM368ZP-WF LVDS QR demo start"
echo "[demo] project: $PROJECT_DIR"
echo "[demo] log: $LOG_FILE"

if [ ! -x "$PROJECT_DIR/bin/qr_scanner_display" ]; then
  echo "[demo] missing executable: $PROJECT_DIR/bin/qr_scanner_display" >&2
  exit 1
fi

for b in /sys/class/backlight/*; do
  [ -e "$b" ] || continue
  if [ -f "$b/bl_power" ]; then
    echo 0 > "$b/bl_power" 2>/dev/null || true
  fi
  if [ -f "$b/brightness" ]; then
    max="$(cat "$b/max_brightness" 2>/dev/null || echo 255)"
    echo "$max" > "$b/brightness" 2>/dev/null || true
    echo "[demo] backlight $(basename "$b")=$max"
  fi
done

pkill -9 gst-launch-1.0 2>/dev/null || true
pkill -9 camera_pgm_stream 2>/dev/null || true
pkill -9 qr_scanner 2>/dev/null || true
pkill -9 qr_scanner_display 2>/dev/null || true
pkill -9 weston-desktop-shell 2>/dev/null || true
pkill -9 weston-keyboard 2>/dev/null || true
pkill -9 weston 2>/dev/null || true
sleep 1

if ! pgrep rkaiq_3A_server >/dev/null 2>&1; then
  export AIQ_DIR="${AIQ_DIR:-/etc/iqfiles}"
  rkaiq_3A_server >/tmp/rkaiq.log 2>&1 &
  sleep 1
fi

cd "$PROJECT_DIR"
echo "[demo] launching qr_scanner_display on $CAMERA_DEV ${CAMERA_WIDTH}x${CAMERA_HEIGHT}"
echo "[demo] scan QR payloads: product:cola, product:milk, product:bread, checkout, clear"

trap 'echo "[demo] stop requested"; exit 0' INT TERM

while true; do
  ./bin/qr_scanner_display -d "$CAMERA_DEV" -W "$CAMERA_WIDTH" -H "$CAMERA_HEIGHT" -c "$@"
  rc=$?
  echo "[demo] qr_scanner_display exited with code $rc; restarting in 2s"
  pkill -9 gst-launch-1.0 2>/dev/null || true
  pkill -9 camera_pgm_stream 2>/dev/null || true
  sleep 2
done
