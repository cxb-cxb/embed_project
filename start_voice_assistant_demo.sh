#!/bin/sh
set -eu

PROJECT_ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
cd "$PROJECT_ROOT"

MODE="${MODE:-offline}"
CURRENT_PRODUCT="${CURRENT_PRODUCT:-}"
QUESTION="${QUESTION:-}"

ARGS="--mode $MODE"
if [ -n "$CURRENT_PRODUCT" ]; then
    ARGS="$ARGS --current-product $CURRENT_PRODUCT"
fi
if [ -n "$QUESTION" ]; then
    exec python3 tools/voice_retail_assistant.py $ARGS --question "$QUESTION"
fi

exec python3 tools/voice_retail_assistant.py $ARGS
