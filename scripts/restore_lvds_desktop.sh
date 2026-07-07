#!/bin/sh
set -eu

cd /userdata/Embed_project 2>/dev/null || true

STOP_FILE="/tmp/qr_scanner_display.stop"
echo "[desktop] stopping retail LVDS UI..."
touch "$STOP_FILE"

if [ -f /tmp/qr_scanner_display_watchdog.pid ]; then
  kill "$(cat /tmp/qr_scanner_display_watchdog.pid)" 2>/dev/null || true
fi
if [ -f /tmp/qr_scanner_display.pid ]; then
  kill "$(cat /tmp/qr_scanner_display.pid)" 2>/dev/null || true
fi

pkill -f "retail_lvds_watchdog" 2>/dev/null || true
pkill -9 qr_scanner_display 2>/dev/null || true
pkill -9 gst-launch-1.0 2>/dev/null || true

rm -f /tmp/qr_scanner_display.pid /tmp/qr_scanner_display_watchdog.pid "$STOP_FILE"

for b in /sys/class/backlight/*; do
  [ -e "$b" ] || continue
  [ -f "$b/bl_power" ] && echo 0 > "$b/bl_power" 2>/dev/null || true
  if [ -f "$b/brightness" ]; then
    max="$(cat "$b/max_brightness" 2>/dev/null || echo 255)"
    echo "$max" > "$b/brightness" 2>/dev/null || true
  fi
done

echo "[desktop] restarting weston..."
if [ -x /etc/init.d/S03weston ]; then
  /etc/init.d/S03weston restart || /etc/init.d/S03weston start || true
elif command -v weston >/dev/null 2>&1; then
  pkill -9 weston 2>/dev/null || true
  weston --log=/tmp/weston.log >/tmp/weston-start.log 2>&1 &
fi

sleep 1
echo "[desktop] done."
