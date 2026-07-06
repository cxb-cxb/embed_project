#!/bin/sh
set -eu

DURATION="${1:-3}"
OUT="${2:-/tmp/qsm_mic_test.wav}"

echo "Enable RK809 main microphone capture path..."
amixer -c 0 cset name='Capture MIC Path' 'Main Mic' >/dev/null

echo "Current capture path:"
amixer -c 0 sget 'Capture MIC Path' 2>/dev/null || true

echo "Recording ${DURATION}s to ${OUT}. Please speak near the onboard microphone."
rm -f "$OUT"
arecord -D hw:0,0 -f S16_LE -c 2 -r 16000 -d "$DURATION" "$OUT"

echo "Recorded file:"
ls -l "$OUT"
echo "Done. Pull it with: adb pull $OUT"
