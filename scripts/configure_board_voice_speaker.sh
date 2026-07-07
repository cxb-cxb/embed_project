#!/bin/sh
set -eu

env_file="/userdata/Embed_project/config/voice_asr.env"

if [ ! -f "$env_file" ]; then
    echo "Missing $env_file"
    exit 1
fi

backup="$env_file.bak_$(date +%Y%m%d_%H%M%S 2>/dev/null || echo manual)"
cp "$env_file" "$backup" 2>/dev/null || true

tmp="${env_file}.tmp"
grep -v -E '^export (VOICE_PLAYBACK_PATH|VOICE_SPK_VOLUME|VOICE_CHANNEL|VOICE_GAIN_DB|VOICE_SECONDS|VOICE_WAKE_SECONDS|VOICE_COMMAND_SECONDS|VOICE_SESSION_SECONDS|WAKE_ACK_TEXT|SKIP_WELCOME)=' "$env_file" > "$tmp"
cat >> "$tmp" <<'EOF'
export VOICE_PLAYBACK_PATH=3
export VOICE_SPK_VOLUME=200
export VOICE_CHANNEL=left
export VOICE_GAIN_DB=1
export VOICE_SECONDS=4
export VOICE_WAKE_SECONDS=2
export VOICE_COMMAND_SECONDS=5
export VOICE_SESSION_SECONDS=120
export WAKE_ACK_TEXT="我在"
export SKIP_WELCOME=0
EOF
chmod 600 "$tmp"
mv "$tmp" "$env_file"

amixer -c 0 cset numid=1 3 >/dev/null 2>&1 || true
amixer -c 0 cset numid=5 200,200 >/dev/null 2>&1 || true
amixer -c 0 cset numid=3 200 >/dev/null 2>&1 || true
amixer -c 0 cset numid=4 200 >/dev/null 2>&1 || true
amixer -c 0 cset numid=2 1 >/dev/null 2>&1 || true

echo "Voice speaker config applied."
grep -E '^export (VOICE_PLAYBACK_PATH|VOICE_SPK_VOLUME|VOICE_CHANNEL|VOICE_GAIN_DB|VOICE_SECONDS|VOICE_WAKE_SECONDS|VOICE_COMMAND_SECONDS|VOICE_SESSION_SECONDS|WAKE_ACK_TEXT|SKIP_WELCOME)=' "$env_file"
