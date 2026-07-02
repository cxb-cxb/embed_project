#!/bin/sh
set -eu

cd /userdata/Embed_project

pkill -9 gst-launch-1.0 2>/dev/null || true
pkill -9 qr_scanner 2>/dev/null || true
pkill -9 qr_scanner_display 2>/dev/null || true

if ! pgrep rkaiq_3A_server >/dev/null 2>&1; then
  export AIQ_DIR=/etc/iqfiles
  rkaiq_3A_server >/tmp/rkaiq.log 2>&1 &
  sleep 1
fi

exec ./bin/qr_scanner -d /dev/video5 -W 800 -H 600 -t -p 5 "$@"
