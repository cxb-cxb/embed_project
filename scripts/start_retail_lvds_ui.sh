#!/bin/sh
set -eu

cd /userdata/Embed_project

pkill -9 gst-launch-1.0 2>/dev/null || true
pkill -9 qr_scanner_display 2>/dev/null || true
pkill -f "retail_lvds_watchdog" 2>/dev/null || true
pkill -9 weston-desktop-shell 2>/dev/null || true
pkill -9 weston-keyboard 2>/dev/null || true
pkill -9 weston 2>/dev/null || true

LOG_FILE="${LOG_FILE:-/userdata/Embed_project/qr_scanner_display.log}"
CAMERA_DEV="${CAMERA_DEV:-/dev/video5}"
CAMERA_WIDTH="${CAMERA_WIDTH:-800}"
CAMERA_HEIGHT="${CAMERA_HEIGHT:-600}"
STOP_FILE="/tmp/qr_scanner_display.stop"

for b in /sys/class/backlight/*; do
  [ -e "$b" ] || continue
  [ -f "$b/bl_power" ] && echo 0 > "$b/bl_power" 2>/dev/null || true
  if [ -f "$b/brightness" ]; then
    max="$(cat "$b/max_brightness" 2>/dev/null || echo 255)"
    echo "$max" > "$b/brightness" 2>/dev/null || true
  fi
done

# Some older runs can survive without a visible process name while still holding
# the camera fd. Release only processes that are currently using this camera.
for fd in /proc/[0-9]*/fd/*; do
  [ -e "$fd" ] || continue
  target="$(readlink "$fd" 2>/dev/null || true)"
  [ "$target" = "$CAMERA_DEV" ] || continue
  pid="${fd#/proc/}"
  pid="${pid%%/*}"
  [ "$pid" = "$$" ] && continue
  kill -9 "$pid" 2>/dev/null || true
done

rm -f /tmp/qr_scanner_display.pid /tmp/qr_scanner_display_watchdog.pid "$STOP_FILE" "$LOG_FILE"
trap '' HUP
if command -v nohup >/dev/null 2>&1; then
  nohup sh -c "
    echo retail_lvds_watchdog
    while [ ! -f '$STOP_FILE' ]; do
      /userdata/Embed_project/bin/qr_scanner_display \
        -d '$CAMERA_DEV' -W '$CAMERA_WIDTH' -H '$CAMERA_HEIGHT' -c \
        >> '$LOG_FILE' 2>&1 &
      child=\$!
      echo \$child > /tmp/qr_scanner_display.pid
      wait \$child
      rc=\$?
      [ -f '$STOP_FILE' ] && break
      echo '[retail-ui] qr_scanner_display exited rc='\"\$rc\"', restart in 1s' >> '$LOG_FILE'
      sleep 1
    done
  " >> "$LOG_FILE" 2>&1 &
else
  sh -c "
    echo retail_lvds_watchdog
    while [ ! -f '$STOP_FILE' ]; do
      /userdata/Embed_project/bin/qr_scanner_display \
        -d '$CAMERA_DEV' -W '$CAMERA_WIDTH' -H '$CAMERA_HEIGHT' -c \
        >> '$LOG_FILE' 2>&1 &
      child=\$!
      echo \$child > /tmp/qr_scanner_display.pid
      wait \$child
      rc=\$?
      [ -f '$STOP_FILE' ] && break
      echo '[retail-ui] qr_scanner_display exited rc='\"\$rc\"', restart in 1s' >> '$LOG_FILE'
      sleep 1
    done
  " >> "$LOG_FILE" 2>&1 &
fi
echo "$!" > /tmp/qr_scanner_display_watchdog.pid
sleep 1

echo "[retail-ui] watchdog=$(cat /tmp/qr_scanner_display_watchdog.pid) pid=$(cat /tmp/qr_scanner_display.pid 2>/dev/null || echo starting) log=$LOG_FILE"
