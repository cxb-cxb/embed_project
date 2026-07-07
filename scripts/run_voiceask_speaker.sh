#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ASR_ENV="$PROJECT_DIR/config/voice_asr.env"
BIN="$PROJECT_DIR/bin/embed_project"
CATALOG="$PROJECT_DIR/data/products.csv"
CACHE_DIR="$PROJECT_DIR/cache"
WELCOME_MP3="$CACHE_DIR/welcome_tts.mp3"
WELCOME_TEXT="${WELCOME_TEXT:-欢迎来到智能售货机。}"
VOICE_STATE_FILE="${VOICE_STATE_FILE:-/tmp/qsm_retail_voice_state}"

if [ -f "$ASR_ENV" ]; then
    # shellcheck disable=SC1090
    . "$ASR_ENV"
fi

trap 'echo; echo "Voice assistant stopped."; exit 0' INT TERM

json_escape() {
    sed 's/\\/\\\\/g; s/"/\\"/g'
}

state_escape() {
    tr '\r\n' '  ' | sed 's/[[:space:]][[:space:]]*/ /g; s/^ //; s/ $//'
}

ui_safe_text() {
    fallback="$2"
    text="$(printf '%s' "$1" | state_escape)"
    if printf '%s' "$text" | LC_ALL=C grep -q '[^ -~]'; then
        printf '%s\n' "$fallback"
    else
        printf '%s\n' "$text"
    fi
}

voice_cart_command() {
    q="$(printf '%s' "$1" | tr 'A-Z' 'a-z')"
    case "$q" in
        *clear*|*empty*|*qingkong*|*清空*)
            printf 'clear\n'
            return
            ;;
    esac
    case "$q" in
        *add*cola*|*buy*cola*|*scan*cola*|*加入*可乐*|*买*可乐*) printf 'add:cola\n'; return ;;
        *add*milk*|*buy*milk*|*scan*milk*|*加入*牛奶*|*买*牛奶*) printf 'add:milk\n'; return ;;
        *add*water*|*buy*water*|*scan*water*|*加入*水*|*买*水*) printf 'add:water\n'; return ;;
        *add*bread*|*buy*bread*|*scan*bread*|*加入*面包*|*买*面包*) printf 'add:bread\n'; return ;;
        *add*noodle*|*buy*noodle*|*scan*noodle*|*加入*泡面*|*买*泡面*) printf 'add:noodle\n'; return ;;
        *add*chips*|*buy*chips*|*scan*chips*|*加入*薯片*|*买*薯片*) printf 'add:chips\n'; return ;;
        *add*biscuit*|*buy*biscuit*|*scan*biscuit*|*加入*饼干*|*买*饼干*) printf 'add:biscuit\n'; return ;;
        *add*toothpaste*|*buy*toothpaste*|*scan*toothpaste*|*加入*牙膏*|*买*牙膏*) printf 'add:toothpaste\n'; return ;;
        *add*tissue*|*buy*tissue*|*scan*tissue*|*加入*纸巾*|*买*纸巾*) printf 'add:tissue\n'; return ;;
        *add*soap*|*buy*soap*|*scan*soap*|*加入*香皂*|*买*香皂*) printf 'add:soap\n'; return ;;
    esac
    printf '\n'
}

write_voice_state() {
    question="$(ui_safe_text "$1" "VOICE INPUT")"
    answer="$(ui_safe_text "$2" "CHINESE REPLY PLAYED")"
    cart_cmd="$(printf '%s' "${3:-}" | state_escape)"
    tmp="${VOICE_STATE_FILE}.tmp"
    {
        printf 'QUESTION=%s\n' "$question"
        printf 'ANSWER=%s\n' "$answer"
        printf 'CART_CMD=%s\n' "$cart_cmd"
        printf 'UPDATED_MS=%s\n' "$(date +%s 2>/dev/null || echo 0)"
    } > "$tmp"
    mv "$tmp" "$VOICE_STATE_FILE"
}

prepare_speaker() {
    amixer -c 0 cset numid=1 "${VOICE_PLAYBACK_PATH:-10}" >/dev/null 2>&1 || true
    amixer -c 0 cset numid=5 "${VOICE_SPK_VOLUME:-200},${VOICE_SPK_VOLUME:-200}" >/dev/null 2>&1 || true
}

prepare_mic() {
    amixer -c 0 cset numid=2 "${VOICE_MIC_PATH:-1}" >/dev/null 2>&1 || true
}

ensure_dns() {
    {
        echo "nameserver 223.5.5.5"
        echo "nameserver 114.114.114.114"
        echo "nameserver 8.8.8.8"
    } > /etc/resolv.conf 2>/dev/null || true
}

ensure_network() {
    ensure_dns
    if ! ping -c 1 -W 3 openspeech.bytedance.com >/dev/null 2>&1; then
        echo "Network is not ready. Trying Wi-Fi reconnect..."
        "$PROJECT_DIR/scripts/connect_wifi.sh" || true
        ensure_dns
    fi
}

dechunk_http_body() {
    raw="$1"
    out="$2"

    if ! grep -qi '^Transfer-Encoding: chunked' "$raw"; then
        sed '1,/^\r*$/d' "$raw" > "$out"
        return
    fi

    sed '1,/^\r*$/d' "$raw" | {
        while IFS= read -r size_line; do
            size_hex="$(printf '%s' "$size_line" | tr -d '\r' | sed 's/;.*//')"
            [ -n "$size_hex" ] || continue
            size=$((0x$size_hex))
            [ "$size" -gt 0 ] || break
            dd bs=1 count="$size" 2>/dev/null
            dd bs=1 count=2 >/dev/null 2>&1 || true
        done
    } > "$out"
}

extract_tts_data_to_mp3() {
    raw="$1"
    mp3="$2"
    body="/tmp/embed_tts_response.dechunked.jsonl"
    b64="/tmp/embed_tts_audio_fixed.b64"

    [ -s "$raw" ] || return 1
    dechunk_http_body "$raw" "$body"
    sed -n 's/.*"data"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$body" | tr -d '\r\n' > "$b64"
    [ -s "$b64" ] || return 1
    base64 -d "$b64" > "$mp3"
    [ -s "$mp3" ] || return 1
}

decode_latest_tts() {
    mp3="/tmp/embed_tts_reply_fixed.mp3"
    extract_tts_data_to_mp3 /tmp/embed_tts_response.json "$mp3" || return 1
    printf '%s\n' "$mp3"
}

play_mp3() {
    mp3="$1"
    raw_wav="/tmp/embed_tts_reply_raw.wav"
    wav="/tmp/embed_tts_reply_fixed.wav"
    channel="${VOICE_CHANNEL:-left}"
    prepare_speaker
    echo "Speaker playback: $mp3 channel=$channel"
    rm -f "$raw_wav" "$wav"
    if mpg123 -q --stereo -w "$raw_wav" "$mp3" && [ -s "$raw_wav" ]; then
        case "$channel" in
            right|R|r)
                pan='pan=stereo|c0=0*c0|c1=c1'
                ;;
            both|stereo|B|b)
                pan='pan=stereo|c0=c0|c1=c1'
                ;;
            left|L|l|*)
                pan='pan=stereo|c0=c0|c1=0*c1'
                ;;
        esac
        if sox "$raw_wav" "$wav" highpass 200 lowpass 5000 gain "${VOICE_GAIN_DB:-7}" \
            compand 0.03,0.15 -70,-70,-30,-18,-16,-12,-8,-7 -7 -90 0.1 2>/dev/null \
            && [ -s "$wav" ] && [ "$(wc -c < "$wav" | tr -d ' ')" -ge 1024 ]; then
            :
        elif ! ffmpeg -y -loglevel error -i "$raw_wav" -af "volume=${VOICE_GAIN_DB:-7}dB" -ac 2 -ar 48000 "$wav" \
            || [ ! -s "$wav" ] || [ "$(wc -c < "$wav" | tr -d ' ')" -lt 1024 ]; then
            cp "$raw_wav" "$wav"
        fi
        tinyplay "$wav" -D 0 -d 0 || aplay -D hw:0,0 "$wav"
    elif ffmpeg -y -loglevel error -i "$mp3" -ac 2 -ar 48000 "$wav" && [ -s "$wav" ]; then
        tinyplay "$wav" -D 0 -d 0 || aplay -D hw:0,0 "$wav"
    else
        mpg123 -q -a hw:0,0 "$mp3" || mpg123 -q "$mp3"
    fi
}

play_latest_tts() {
    mp3="$(decode_latest_tts)" || {
        echo "TTS audio was not generated; terminal reply is still available."
        return 1
    }
    play_mp3 "$mp3"
}

request_tts_text() {
    text="$1"
    raw="$2"
    req="$3"

    tts_url="${VOLCANO_TTS_URL:-https://openspeech.bytedance.com/api/v3/tts/unidirectional}"
    tts_cluster="${VOLCANO_TTS_CLUSTER:-seed-tts-2.0}"
    tts_voice="${VOLCANO_TTS_VOICE_TYPE:-zh_female_vv_uranus_bigtts}"

    if [ -z "${VOLCANO_APP_ID:-}" ] || [ -z "${VOLCANO_ACCESS_TOKEN:-}" ]; then
        return 1
    fi

    escaped_text="$(printf '%s' "$text" | json_escape)"
    printf '{"req_params":{"text":"%s","speaker":"%s","audio_params":{"format":"mp3","sample_rate":48000},"additions":"{}"}}\n' \
        "$escaped_text" "$tts_voice" > "$req"

    len="$(wc -c < "$req" | tr -d ' ')"
    host="$(echo "$tts_url" | sed 's#https://\([^/]*\).*#\1#')"
    path="$(echo "$tts_url" | sed 's#https://[^/]*##')"

    {
        printf 'POST %s HTTP/1.1\r\n' "$path"
        printf 'Host: %s\r\n' "$host"
        printf 'Content-Type: application/json\r\n'
        printf 'X-Api-App-Id: %s\r\n' "$VOLCANO_APP_ID"
        printf 'X-Api-Access-Key: %s\r\n' "$VOLCANO_ACCESS_TOKEN"
        printf 'X-Api-Resource-Id: %s\r\n' "$tts_cluster"
        printf 'X-Api-Request-Id: qsm368-tts-shell\r\n'
        printf 'X-Api-Sequence: -1\r\n'
        printf 'Content-Length: %s\r\n' "$len"
        printf 'Connection: close\r\n'
        printf '\r\n'
        cat "$req"
    } | openssl s_client -quiet -connect "$host:443" -servername "$host" > "$raw" 2>/tmp/embed_tts_shell_ssl.log || true
}

play_text_tts() {
    text="$1"
    raw="/tmp/embed_tts_open_response.http"
    req="/tmp/embed_tts_open_request.json"
    mp3="/tmp/embed_tts_open_reply.mp3"

    request_tts_text "$text" "$raw" "$req" || return 1
    extract_tts_data_to_mp3 "$raw" "$mp3" || return 1
    play_mp3 "$mp3"
}

request_open_chat() {
    question="$1"
    raw="/tmp/embed_open_chat_response.http"
    body="/tmp/embed_open_chat_response.json"
    req="/tmp/embed_open_chat_request.json"

    [ -n "${ARK_API_KEY:-}" ] || return 1
    [ -n "${ARK_MODEL:-}" ] || return 1
    ark_base="${ARK_BASE_URL:-https://ark.cn-beijing.volces.com/api/v3}"
    host="$(echo "$ark_base" | sed 's#https://\([^/]*\).*#\1#')"
    base_path="$(echo "$ark_base" | sed 's#https://[^/]*##')"
    path="${base_path%/}/chat/completions"
    q_escaped="$(printf '%s' "$question" | json_escape)"

    cat > "$req" <<EOF
{"model":"$ARK_MODEL","messages":[{"role":"system","content":"You are a voice assistant inside a smart vending machine. Open chat is allowed. Always reply in Simplified Chinese. Keep the answer natural, short, and suitable for speech playback, preferably under 80 Chinese characters."},{"role":"user","content":"$q_escaped"}],"temperature":0.7}
EOF

    len="$(wc -c < "$req" | tr -d ' ')"
    {
        printf 'POST %s HTTP/1.1\r\n' "$path"
        printf 'Host: %s\r\n' "$host"
        printf 'Content-Type: application/json\r\n'
        printf 'Authorization: Bearer %s\r\n' "$ARK_API_KEY"
        printf 'Content-Length: %s\r\n' "$len"
        printf 'Connection: close\r\n'
        printf '\r\n'
        cat "$req"
    } | openssl s_client -quiet -connect "$host:443" -servername "$host" > "$raw" 2>/tmp/embed_open_chat_ssl.log || true

    sed '1,/^\r*$/d' "$raw" > "$body"
    answer="$(sed -n 's/.*"content"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$body" | tail -1)"
    answer="$(printf '%s' "$answer" | sed 's/\\"/"/g; s/\\n/ /g; s/\\\\/\\/g')"
    [ -n "$answer" ] || return 1
    printf '%s\n' "$answer"
}

is_retail_question() {
    q="$1"
    case "$q" in
        *鐗涘ザ*|*鍙箰*|*闈㈠寘*|*鐭挎硥姘?|*姘?|*钖墖*|*楗煎共*|*鏂逛究闈?|*娉￠潰*|*鐗欒啅*|*绾稿肪*|*棣欑殏*|*鑲ョ殏*|\
        *milk*|*cola*|*coke*|*bread*|*water*|*chips*|*cookie*|*cookies*|*biscuit*|*noodle*|*toothpaste*|*tissue*|*soap*|\
        *澶氬皯閽?|*浠锋牸*|*鍞环*|*搴撳瓨*|*璐墿杞?|*缁撹处*|*鍟嗗搧*|*鎺ㄨ崘*|*鎼厤*|*鐗圭偣*)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

run_open_chat_reply() {
    question="$1"
    [ -n "$question" ] || return 1
    echo "Open chat question: $question"
    answer="$(request_open_chat "$question" 2>/dev/null || true)"
    [ -n "$answer" ] || return 1
    echo "Assistant: $answer"
    write_voice_state "$question" "$answer" "$(voice_cart_command "$question")"
    play_text_tts "$answer" || true
}

build_welcome_tts() {
    mkdir -p "$CACHE_DIR"
    raw="/tmp/embed_tts_welcome_response.http"
    req="/tmp/embed_tts_welcome_request.json"

    request_tts_text "$WELCOME_TEXT" "$raw" "$req" || return 1
    extract_tts_data_to_mp3 "$raw" "$WELCOME_MP3"
}

play_welcome() {
    if [ "${SKIP_WELCOME:-0}" = "1" ]; then
        return 0
    fi
    if [ ! -s "$WELCOME_MP3" ]; then
        echo "Generating welcome voice..."
        build_welcome_tts || {
            echo "Welcome voice generation skipped."
            return 0
        }
    fi
    play_mp3 "$WELCOME_MP3" || true
}

run_embed_command() {
    cmd="$1"
    log="/tmp/embed_project_voiceask.log"
    rm -f /tmp/embed_tts_response.json /tmp/embed_tts_audio.b64 /tmp/embed_tts_reply.mp3 \
        /tmp/embed_tts_response.dechunked.jsonl /tmp/embed_tts_audio_fixed.b64 \
        /tmp/embed_tts_reply_fixed.mp3 /tmp/embed_tts_reply_fixed.wav \
        /tmp/embed_tts_open_response.http /tmp/embed_tts_open_reply.mp3 "$log"

    case "$cmd" in
        voiceask*)
            ensure_dns
            prepare_mic
            echo "Please speak now. Recording ${VOICE_SECONDS:-8} seconds..."
            ;;
    esac

    printf '%s\nexit\n' "$cmd" | "$BIN" "$CATALOG" > "$log" 2>&1 || true
    question="$(sed -n 's/.*Recognized:[[:space:]]*\(.*\)/\1/p' "$log" | tail -1)"
    [ -n "$question" ] || question="$(printf '%s' "$cmd" | sed 's/^ask[[:space:]]*//; s/^voiceask[[:space:]]*[0-9]*//')"

    if [ -n "$question" ] && run_open_chat_reply "$question"; then
        sed -n '/Recognized:/p' "$log"
        return
    fi

    sed \
        -e '/^Speaking\.\.\.$/d' \
        -e '/base64: invalid input/d' \
        -e '/TTS failed\./d' \
        -e '/Audio playback failed\./d' \
        "$log"

    if [ "${OPEN_CHAT_MODE:-auto}" = "all" ]; then
        run_open_chat_reply "$question" && return
    elif ! is_retail_question "$question"; then
        run_open_chat_reply "$question" && return
    elif grep -q 'Sorry, I only handle retail product queries' "$log"; then
        run_open_chat_reply "$question" && return
        echo "Open chat failed; using built-in reply audio."
    fi

    answer="$(awk '/Recognized:/{seen=1; next} seen && length($0)>0 && $0 !~ /^>/ {print; exit}' "$log" 2>/dev/null || true)"
    [ -n "$answer" ] || answer="$(awk 'length($0)>0 && $0 !~ /^>/ && $0 !~ /^Embed_project/ && $0 !~ /^Commands:/ {last=$0} END{print last}' "$log" 2>/dev/null || true)"
    [ -n "$answer" ] || answer="Voice command received."
    write_voice_state "$question" "$answer" "$(voice_cart_command "$question")"
    play_latest_tts || true
}

ensure_network
prepare_speaker

case "${1:-}" in
    --once)
        seconds="${2:-${VOICE_SECONDS:-8}}"
        run_embed_command "voiceask $seconds"
        exit 0
        ;;
    '')
        ;;
    *)
        run_embed_command "$*"
        exit 0
        ;;
esac

echo "QSM voice assistant with speaker is ready."
echo "Press Enter to record from microphone, or type: ask milk price"
echo "Type q and press Enter to quit."
echo "Audio path: ${VOICE_PLAYBACK_PATH:-10}, channel: ${VOICE_CHANNEL:-left}"
play_welcome

input_device="/dev/stdin"
if [ -r /dev/tty ]; then
    input_device="/dev/tty"
fi

while true; do
    printf '\nvoice> '
    if ! read -r line < "$input_device"; then
        echo
        echo "Input closed. Voice assistant stopped."
        break
    fi
    case "$line" in
        q|quit|exit) break ;;
        '') line="voiceask ${VOICE_SECONDS:-8}" ;;
    esac
    run_embed_command "$line"
    echo "Ready for next question. Press Enter to speak again, or type q to quit."
done
