#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ASR_ENV="$PROJECT_DIR/config/voice_asr.env"

if [ -f "$ASR_ENV" ]; then
    # shellcheck disable=SC1090
    . "$ASR_ENV"
fi

if ! ping -c 1 -W 3 openspeech.bytedance.com >/dev/null 2>&1; then
    echo "Network is not ready. Trying Wi-Fi reconnect..."
    "$PROJECT_DIR/scripts/connect_wifi.sh" || true
fi

cd "$PROJECT_DIR"
exec "$PROJECT_DIR/bin/embed_project" "$PROJECT_DIR/data/products.csv"
