#!/bin/sh
set -eu

PROJECT_DIR="/userdata/Embed_project"
ENV_FILE="$PROJECT_DIR/config/voice_asr.env"
BEEP_WAV="/tmp/qsm_voice_ready_beep.wav"
WELCOME_MP3="$PROJECT_DIR/cache/welcome_tts.mp3"

if [ -f "$ENV_FILE" ]; then
    # shellcheck disable=SC1090
    . "$ENV_FILE"
fi

VOICE_WAKE_SECONDS="${VOICE_WAKE_SECONDS:-2}"
VOICE_COMMAND_SECONDS="${VOICE_COMMAND_SECONDS:-4}"
VOICE_SESSION_SECONDS="${VOICE_SESSION_SECONDS:-60}"
WAKE_ACK_TEXT="${WAKE_ACK_TEXT:-我在}"

trap 'echo; echo "Auto voice listener stopped."; exit 0' INT TERM

prepare_audio() {
    amixer -c 0 cset numid=1 "${VOICE_PLAYBACK_PATH:-3}" >/dev/null 2>&1 || true
    amixer -c 0 cset numid=3 "${VOICE_SPK_VOLUME:-180}" >/dev/null 2>&1 || true
    amixer -c 0 cset numid=4 "${VOICE_SPK_VOLUME:-180}" >/dev/null 2>&1 || true
    amixer -c 0 cset numid=5 "${VOICE_SPK_VOLUME:-180},${VOICE_SPK_VOLUME:-180}" >/dev/null 2>&1 || true
    amixer -c 0 cset numid=2 "${VOICE_MIC_PATH:-1}" >/dev/null 2>&1 || true
}

play_ready_beep() {
    if command -v sox >/dev/null 2>&1; then
        sox -n -r 48000 -b 16 -c 2 "$BEEP_WAV" synth 0.16 sine 880 gain -6 >/dev/null 2>&1 || true
    fi
    if [ -s "$BEEP_WAV" ]; then
        tinyplay "$BEEP_WAV" -D 0 -d 0 >/dev/null 2>&1 || aplay -D hw:0,0 "$BEEP_WAV" >/dev/null 2>&1 || true
    fi
}

play_cached_welcome() {
    [ -s "$WELCOME_MP3" ] || return 0

    wav="/tmp/qsm_voice_welcome.wav"
    rm -f "$wav"
    if command -v mpg123 >/dev/null 2>&1; then
        mpg123 -q --stereo -w "$wav" "$WELCOME_MP3" || true
    fi
    if [ ! -s "$wav" ] && command -v ffmpeg >/dev/null 2>&1; then
        ffmpeg -y -loglevel error -i "$WELCOME_MP3" -ac 2 -ar 48000 "$wav" || true
    fi
    if [ -s "$wav" ]; then
        tinyplay "$wav" -D 0 -d 0 >/dev/null 2>&1 || aplay -D hw:0,0 "$wav" >/dev/null 2>&1 || true
    fi
}

echo "Auto voice listener is starting. Wake word: 小智小智."
prepare_audio
if [ "${SKIP_BEEP:-1}" != "1" ]; then
    play_ready_beep
fi
if [ "${SKIP_WELCOME:-1}" != "1" ]; then
    play_cached_welcome
fi
"$PROJECT_DIR/scripts/run_voiceask_speaker.sh" --prepare-cache >/tmp/qsm_wake_ack_cache.log 2>&1 || true

echo "Auto voice listener is running. Say wake word first, then talk for ${VOICE_SESSION_SECONDS}s after '$WAKE_ACK_TEXT'. Press Ctrl+C to stop."

while true; do
    prepare_audio
    echo "Listening for wake word..."
    "$PROJECT_DIR/scripts/run_voiceask_speaker.sh" --wake-once "$VOICE_WAKE_SECONDS" || true
    sleep "${VOICE_LOOP_PAUSE_SECONDS:-1}"
done
