#!/bin/sh
set -eu

PROJECT_DIR="/userdata/Embed_project"
SCRIPT_DIR="$PROJECT_DIR/scripts"
VOICE_LOG="/tmp/qsm_auto_voice.log"
VOICE_PID="/tmp/qsm_auto_voice.pid"
VOICE_STATE="/tmp/qsm_retail_voice_state"
PAYMENT_WAIT="/tmp/qsm_payment_waiting_method"

cd "$PROJECT_DIR"

echo "[mission] stopping old voice processes..."
killall start_voice_auto_listen.sh run_voiceask_speaker.sh embed_project arecord tinyplay aplay mpg123 2>/dev/null || true
rm -f "$VOICE_STATE" "$PAYMENT_WAIT"

echo "[mission] applying speaker and microphone config..."
if [ -f "$SCRIPT_DIR/configure_board_voice_speaker.sh" ]; then
    sh "$SCRIPT_DIR/configure_board_voice_speaker.sh" || true
fi

echo "[mission] starting retail screen..."
sh "$SCRIPT_DIR/start_retail_lvds_ui.sh"

echo "[mission] starting voice listener..."
rm -f "$VOICE_LOG" "$VOICE_PID"
nohup sh "$SCRIPT_DIR/start_voice_auto_listen.sh" > "$VOICE_LOG" 2>&1 &
echo "$!" > "$VOICE_PID"

sleep 2

echo "[mission] run_mission is ready."
echo "[mission] screen_log=/userdata/Embed_project/qr_scanner_display.log"
echo "[mission] voice_pid=$(cat "$VOICE_PID" 2>/dev/null || echo starting)"
echo "[mission] voice_log=$VOICE_LOG"
tail -20 "$VOICE_LOG" 2>/dev/null || true
