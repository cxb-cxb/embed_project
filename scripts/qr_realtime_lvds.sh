#!/bin/sh
set -eu

cd /userdata/Embed_project

LOG_FILE="${LOG_FILE:-/tmp/qsm_lvds_demo.log}"
CAMERA_DEV="${CAMERA_DEV:-/dev/video5}"
CAMERA_WIDTH="${CAMERA_WIDTH:-800}"
CAMERA_HEIGHT="${CAMERA_HEIGHT:-600}"

keep_backlight_on() {
  for b in /sys/class/backlight/*; do
    [ -e "$b" ] || continue
    if [ -f "$b/bl_power" ]; then
      echo 0 > "$b/bl_power" 2>/dev/null || true
    fi
    if [ -f "$b/brightness" ]; then
      max="$(cat "$b/max_brightness" 2>/dev/null || echo 255)"
      echo "$max" > "$b/brightness" 2>/dev/null || true
    fi
  done
}

cleanup_camera_users() {
  pkill -9 gst-launch-1.0 2>/dev/null || true
  pkill -9 camera_pgm_stream 2>/dev/null || true
  pkill -9 qr_scanner 2>/dev/null || true
  pkill -9 qr_scanner_display 2>/dev/null || true
}

cleanup_camera_users
keep_backlight_on

if ! pgrep rkaiq_3A_server >/dev/null 2>&1; then
  export AIQ_DIR=/etc/iqfiles
  rkaiq_3A_server >/tmp/rkaiq.log 2>&1 &
  sleep 1
fi

echo "[qr-lvds] start camera=${CAMERA_DEV} size=${CAMERA_WIDTH}x${CAMERA_HEIGHT} log=${LOG_FILE}"
echo "[qr-lvds] real barcode aliases:"
cat data/barcode_aliases.csv 2>/dev/null || true

trap 'echo "[qr-lvds] stop requested"; cleanup_camera_users; keep_backlight_on; exit 0' INT TERM

while true; do
  keep_backlight_on
  ./bin/qr_scanner_display -d "$CAMERA_DEV" -W "$CAMERA_WIDTH" -H "$CAMERA_HEIGHT" -c "$@" 2>&1 | tee -a "$LOG_FILE"
  echo "[qr-lvds] scanner exited; restarting in 1s" | tee -a "$LOG_FILE"
  cleanup_camera_users
  sleep 1
done
