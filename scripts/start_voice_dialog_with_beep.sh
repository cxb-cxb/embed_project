#!/bin/sh
set -eu

PROJECT_DIR="/userdata/Embed_project"
BEEP_WAV="/tmp/qsm_voice_ready_beep.wav"

amixer -c 0 cset numid=1 3 >/dev/null 2>&1 || true
amixer -c 0 cset numid=5 255,255 >/dev/null 2>&1 || true
amixer -c 0 cset numid=3 255 >/dev/null 2>&1 || true
amixer -c 0 cset numid=4 255 >/dev/null 2>&1 || true

if command -v sox >/dev/null 2>&1; then
    sox -n -r 48000 -b 16 -c 2 "$BEEP_WAV" synth 0.18 sine 880 synth 0.18 sine 1320 delay 0 0 gain -1 >/dev/null 2>&1 || true
fi

if [ -s "$BEEP_WAV" ]; then
    tinyplay "$BEEP_WAV" -D 0 -d 0 >/dev/null 2>&1 || aplay -D hw:0,0 "$BEEP_WAV" >/dev/null 2>&1 || true
fi

exec "$PROJECT_DIR/scripts/run_voiceask_speaker.sh"
