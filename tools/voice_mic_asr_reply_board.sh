#!/bin/sh
set -eu

PROJECT_ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
ENV_FILE="$PROJECT_ROOT/.voice_env"
ASSISTANT="$PROJECT_ROOT/tools/voice_retail_assistant_board.sh"

if [ -f "$ENV_FILE" ]; then
    # shellcheck disable=SC1090
    . "$ENV_FILE"
fi

ASR_ENDPOINT="${VOLCENGINE_ASR_ENDPOINT:-https://openspeech.bytedance.com/api/v3/auc/bigmodel/recognize/flash}"
ASR_RESOURCE_ID="${VOLCENGINE_ASR_RESOURCE_ID:-volc.bigasr.auc_turbo}"
RECORD_SECONDS="${RECORD_SECONDS:-6}"
CURRENT_PRODUCT="${CURRENT_PRODUCT:-}"

if [ -z "${VOLCENGINE_ASR_API_KEY:-}" ]; then
    echo "ERROR: VOLCENGINE_ASR_API_KEY is not configured."
    echo "Create $ENV_FILE with: export VOLCENGINE_ASR_API_KEY='your_asr_key'"
    exit 1
fi

if [ ! -x "$ASSISTANT" ]; then
    echo "ERROR: missing executable assistant script: $ASSISTANT"
    exit 1
fi

json_escape() {
    sed 's/\\/\\\\/g; s/"/\\"/g'
}

extract_text() {
    sed -n 's/.*"text"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$1" | head -1
}

post_asr_request() {
    req="$1"
    raw="$2"
    hdr="$3"
    resp="$4"

    len="$(wc -c < "$req" | tr -d ' ')"

    case "$ASR_ENDPOINT" in
        http://*)
            wget -q -S -T 60 -O "$resp" \
                --header "Content-Type: application/json" \
                --header "X-Api-Key: $VOLCENGINE_ASR_API_KEY" \
                --header "X-Api-Resource-Id: $ASR_RESOURCE_ID" \
                --header "X-Api-Request-Id: $uid" \
                --header "X-Api-Sequence: -1" \
                --post-file "$req" \
                "$ASR_ENDPOINT" 2>"$hdr" || true
            return
            ;;
    esac

    host="$(echo "$ASR_ENDPOINT" | sed 's#https://\([^/]*\).*#\1#')"
    path="$(echo "$ASR_ENDPOINT" | sed 's#https://[^/]*##')"

    {
        printf 'POST %s HTTP/1.1\r\n' "$path"
        printf 'Host: %s\r\n' "$host"
        printf 'Content-Type: application/json\r\n'
        printf 'X-Api-Key: %s\r\n' "$VOLCENGINE_ASR_API_KEY"
        printf 'X-Api-Resource-Id: %s\r\n' "$ASR_RESOURCE_ID"
        printf 'X-Api-Request-Id: %s\r\n' "$uid"
        printf 'X-Api-Sequence: -1\r\n'
        printf 'Content-Length: %s\r\n' "$len"
        printf 'Connection: close\r\n'
        printf '\r\n'
        cat "$req"
    } | openssl s_client -quiet -connect "$host:443" -servername "$host" > "$raw" 2>"$hdr.openssl" || true

    sed -n '1,/^\r*$/p' "$raw" > "$hdr"
    sed '1,/^\r*$/d' "$raw" > "$resp"
}

run_once() {
    stamp="$(date +%s)"
    wav="/tmp/qsm_voice_query_${stamp}.wav"
    b64="/tmp/qsm_voice_query_${stamp}.b64"
    req="/tmp/qsm_voice_query_${stamp}.json"
    resp="/tmp/qsm_voice_query_${stamp}.resp.json"
    hdr="/tmp/qsm_voice_query_${stamp}.headers.txt"
    raw="/tmp/qsm_voice_query_${stamp}.raw.http"

    echo ""
    echo "请靠近板载麦克风说话，开始录音 ${RECORD_SECONDS} 秒..."
    amixer -c 0 cset name='Capture MIC Path' 'Main Mic' >/dev/null
    arecord -D hw:0,0 -f S16_LE -c 2 -r 16000 -d "$RECORD_SECONDS" "$wav" >/dev/null 2>&1

    base64 "$wav" | tr -d '\n' > "$b64"
    uid="$(printf 'qsm_%s' "$stamp" | json_escape)"

    printf '{"user":{"uid":"%s"},"audio":{"data":"' "$uid" > "$req"
    cat "$b64" >> "$req"
    printf '","language":"zh"},"request":{"model_name":"bigmodel","enable_itn":true,"enable_punc":true}}\n' >> "$req"

    post_asr_request "$req" "$raw" "$hdr" "$resp"

    status="$(sed -n 's/.*[Xx]-[Aa]pi-[Ss]tatus-[Cc]ode:[[:space:]]*\([^[:space:]]*\).*/\1/p' "$hdr" | tail -1 | tr -d '\r')"
    message="$(sed -n 's/.*[Xx]-[Aa]pi-[Mm]essage:[[:space:]]*\(.*\)/\1/p' "$hdr" | tail -1 | tr -d '\r')"

    if [ "$status" != "20000000" ]; then
        echo "ASR识别失败: code=${status:-unknown}, msg=${message:-unknown}"
        echo "录音文件保存在板子: $wav"
        return 1
    fi

    text="$(extract_text "$resp" | tr -d '\r')"
    if [ -z "$text" ]; then
        echo "ASR没有返回有效文字。响应文件: $resp"
        return 1
    fi

    echo "客户语音识别结果> $text"
    if [ -n "$CURRENT_PRODUCT" ]; then
        CURRENT_PRODUCT="$CURRENT_PRODUCT" "$ASSISTANT" "$text" | sed -n '/^助手>/p'
    else
        "$ASSISTANT" "$text" | sed -n '/^助手>/p'
    fi
}

echo "QSM voice assistant is ready."
echo "按回车开始一次语音查询，输入 q 后回车退出。"
echo "默认每次录音 ${RECORD_SECONDS} 秒。"

if [ "${1:-}" = "--once" ]; then
    run_once
    exit $?
fi

while true; do
    printf "\nvoice> "
    if ! read -r cmd; then
        break
    fi
    case "$cmd" in
        q|quit|exit) break ;;
    esac
    run_once || true
done
