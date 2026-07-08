#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ASR_ENV="$PROJECT_DIR/config/voice_asr.env"
BIN="$PROJECT_DIR/bin/embed_project"
CATALOG="$PROJECT_DIR/data/products.csv"
CACHE_DIR="$PROJECT_DIR/cache"
WELCOME_MP3="$CACHE_DIR/welcome_tts.mp3"
WAKE_ACK_MP3="$CACHE_DIR/wake_ack_tts.mp3"
WAKE_ACK_WAV="$CACHE_DIR/wake_ack_tts.wav"
WELCOME_TEXT="${WELCOME_TEXT:-欢迎来到智能售货机。}"
VOICE_STATE_FILE="${VOICE_STATE_FILE:-/tmp/qsm_retail_voice_state}"
PAYMENT_WAIT_FILE="${PAYMENT_WAIT_FILE:-/tmp/qsm_payment_waiting_method}"
PAYMENT_FINISHED_VOICE_FILE="${PAYMENT_FINISHED_VOICE_FILE:-/tmp/qsm_payment_finished_voice}"
TTS_PLAYING_FILE="${TTS_PLAYING_FILE:-/tmp/qsm_tts_playing}"
VOICE_WAKE_WORDS="${VOICE_WAKE_WORDS:-小智小智|小智|小知|小志|晓智|小芝|小只|智能售货机|售货机|信息机|智能售后}"
WAKE_ACK_TEXT="${WAKE_ACK_TEXT:-我在}"
PAYMENT_FINISHED_TEXT="${PAYMENT_FINISHED_TEXT:-支付已完成，购物车已清空，欢迎继续选购。}"

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
    text="$(printf '%s' "$1" | state_escape)"
    [ -n "$text" ] || text="$2"
    printf '%s\n' "$text"
}

voice_cart_command() {
    q="$(printf '%s' "$1" | tr 'A-Z' 'a-z')"
    case "$q" in
        *clear*|*empty*|*qingkong*|*清空*)
            printf 'clear\n'
            return
            ;;
        车|*checkout*|*pay*|*payment*|*jiesuan*|*结账*|*结帐*|*结算*|*结一下账*|*帮我结账*|*我要结账了*|*算账*|*算帐*|*算一下多少钱*|*支付*|*支付一下*|*买单*|*我要买单*|*付款*|*我要付款*|*可以付款了*|*付钱*|*付一下*|*买好了*|*一共*多少钱*|*总共*多少钱*|*合计*|*总价*)
            printf 'checkout\n'
            return
            ;;
    esac
    case "$q" in
        *add*cola*|*buy*cola*|*scan*cola*|*加入*可乐*|*买*可乐*|*我要*可乐*|*来个*可乐*|*拿一瓶*可乐*|*加一瓶*可乐*|*可乐*加入购物车*) printf 'add:cola\n'; return ;;
        *add*milk*|*buy*milk*|*scan*milk*|*加入*牛奶*|*买*牛奶*|*我要*牛奶*|*来个*牛奶*|*拿一瓶*牛奶*|*加一瓶*牛奶*|*牛奶*加入购物车*) printf 'add:milk\n'; return ;;
        *add*water*|*buy*water*|*scan*water*|*加入*矿泉水*|*加入*水*|*买*矿泉水*|*买*水*|*我要*矿泉水*|*我要*水*|*来个*矿泉水*|*来个*水*|*拿一瓶*矿泉水*|*拿一瓶*水*|*加一瓶*矿泉水*|*加一瓶*水*|*水*加入购物车*) printf 'add:water\n'; return ;;
        *add*bread*|*buy*bread*|*scan*bread*|*加入*面包*|*买*面包*|*我要*面包*|*来个*面包*|*拿一个*面包*|*加一个*面包*|*面包*加入购物车*) printf 'add:bread\n'; return ;;
        *add*noodle*|*buy*noodle*|*scan*noodle*|*加入*方便面*|*加入*泡面*|*买*方便面*|*买*泡面*|*我要*方便面*|*我要*泡面*|*来个*方便面*|*来个*泡面*|*加一桶*泡面*|*泡面*加入购物车*) printf 'add:noodle\n'; return ;;
        *add*chips*|*buy*chips*|*scan*chips*|*加入*薯片*|*买*薯片*|*我要*薯片*|*来个*薯片*|*拿一包*薯片*|*加一包*薯片*|*来一袋*薯片*|*薯片*加入购物车*) printf 'add:chips\n'; return ;;
        *add*biscuit*|*buy*biscuit*|*scan*biscuit*|*加入*饼干*|*买*饼干*|*我要*饼干*|*来个*饼干*|*拿一包*饼干*|*加一包*饼干*|*来一袋*饼干*|*饼干*加入购物车*) printf 'add:biscuit\n'; return ;;
        *add*toothpaste*|*buy*toothpaste*|*scan*toothpaste*|*加入*牙膏*|*买*牙膏*|*我要*牙膏*|*来个*牙膏*|*拿一支*牙膏*|*加一支*牙膏*|*牙膏*加入购物车*) printf 'add:toothpaste\n'; return ;;
        *add*tissue*|*buy*tissue*|*scan*tissue*|*加入*纸巾*|*买*纸巾*|*我要*纸巾*|*来个*纸巾*|*拿一包*纸巾*|*加一包*纸巾*|*来一袋*纸巾*|*纸巾*加入购物车*) printf 'add:tissue\n'; return ;;
        *add*soap*|*buy*soap*|*scan*soap*|*加入*香皂*|*加入*肥皂*|*买*香皂*|*买*肥皂*|*我要*香皂*|*我要*肥皂*|*来个*香皂*|*来个*肥皂*|*拿一个*香皂*|*加一个*香皂*|*香皂*加入购物车*) printf 'add:soap\n'; return ;;
    esac
    printf '\n'
}

voice_payment_method_command() {
    q="$(printf '%s' "$1" | tr 'A-Z' 'a-z')"
    case "$q" in
        *wechat*|*weixin*|*微信*|*微信支付*|*扫微信*|*用微信*)
            printf 'pay:wechat\n'
            return
            ;;
        *alipay*|*支付宝*|*支付寶*|*扫支付宝*|*用支付宝*|*支付宝支付*|*支付宝码*|*支付宝收款码*|\
        *宝支付*|*之付宝*|*支护宝*|*蓝色支付*|*用蓝色的*|*蓝色的码*|*扫蓝色的*)
            printf 'pay:alipay\n'
            return
            ;;
        *unionpay*|*union*pay*|*银联*|*雲閃付*|*云闪付*|*银联云闪付*|*用云闪付*|*用银联*)
            printf 'pay:unionpay\n'
            return
            ;;
    esac
    printf '\n'
}

product_from_retail_lexicon() {
    q="$(printf '%s' "$1" | tr 'A-Z' 'a-z')"
    case "$q" in
        *cola*|*coke*|*可乐*|*扣乐*|*可落*|*阔乐*|*汽水*) printf 'cola\n'; return ;;
        *milk*|*牛奶*|*奶*) printf 'milk\n'; return ;;
        *water*|*矿泉水*|*冰露*|*水*) printf 'water\n'; return ;;
        *bread*|*面包*|*吐司*) printf 'bread\n'; return ;;
        *noodle*|*方便面*|*泡面*|*杯面*|*桶面*) printf 'noodle\n'; return ;;
        *chips*|*薯片*|*薯条*) printf 'chips\n'; return ;;
        *biscuit*|*cookie*|*cookies*|*饼干*|*曲奇*) printf 'biscuit\n'; return ;;
        *toothpaste*|*牙膏*) printf 'toothpaste\n'; return ;;
        *tissue*|*纸巾*|*抽纸*) printf 'tissue\n'; return ;;
        *soap*|*香皂*|*肥皂*) printf 'soap\n'; return ;;
    esac
    printf '\n'
}

retail_lexicon_command() {
    q="$(printf '%s' "$1" | tr 'A-Z' 'a-z')"

    cmd="$(voice_payment_method_command "$q")"
    if [ -n "$cmd" ]; then
        printf '%s\n' "$cmd"
        return
    fi

    cmd="$(voice_cart_command "$q")"
    if [ -n "$cmd" ]; then
        printf '%s\n' "$cmd"
        return
    fi

    product="$(product_from_retail_lexicon "$q")"
    if [ -n "$product" ]; then
        case "$q" in
            *多少钱*|*价格*|*价钱*|*库存*|*还有*|*有吗*|*有么*|*有啥*|*推荐*|*介绍*)
                printf 'info:%s\n' "$product"
                ;;
            *加入*|*添加*|*购物车*|*来一个*|*来一*|*拿一个*|*拿一*|*买一个*|*买一*|*扫码*|*扫一下*|*)
                printf 'add:%s\n' "$product"
                ;;
        esac
        return
    fi

    case "$q" in
        *商品*|*有什么*|*有啥*|*推荐*|*卖什么*|*售货机*)
            printf 'info:products\n'
            return
            ;;
    esac

    printf '\n'
}

is_payment_reply_echo() {
    q="$(printf '%s' "$1" | tr 'A-Z' 'a-z')"
    case "$q" in
        *请选择微信支付、支付宝支付或银联云闪付*|*正在为您结账*请选择*|*正在为你结账*请选择*|\
        *好的*已为您打开*|*好的*已为你打开*|*已为您打开*收款码*|*已为你打开*收款码*|\
        *请扫码支付*)
            return 0
            ;;
        *濂界殑*|*宸蹭负鎮ㄦ墦寮*|*璇锋壂鐮佹敮浠*)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

is_wake_text() {
    q="$(printf '%s' "$1" | tr 'A-Z' 'a-z')"
    case "$q" in
        *语音助手*|*你好助手*|*小智*|*小知*|*小志*|*晓智*|*小芝*|*小只*|*智慧零售*|*售货机*|*智能售货机*|*信息机*|*智能售后*|\
        *voice*assistant*|*hello*assistant*|*xiao*zhi*|*xiaozhi*|*assistant*)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

extract_wake_command() {
    printf '%s' "$1" | tr 'A-Z' 'a-z' | sed \
        -e 's/语音助手//g' \
        -e 's/你好助手//g' \
        -e 's/小智//g' \
        -e 's/小知//g' \
        -e 's/小志//g' \
        -e 's/晓智//g' \
        -e 's/小芝//g' \
        -e 's/小只//g' \
        -e 's/智慧零售//g' \
        -e 's/智能售货机//g' \
        -e 's/售货机//g' \
        -e 's/信息机//g' \
        -e 's/智能售后//g' \
        -e 's/voice assistant//g' \
        -e 's/hello assistant//g' \
        -e 's/xiao zhi//g' \
        -e 's/xiaozhi//g' \
        -e 's/assistant//g' \
        -e 's/^[[:space:]，,。.!！?？：:、-]*//' \
        -e 's/[[:space:]，,。.!！?？：:、-]*$//'
}

write_voice_state() {
    question="$(printf '%s' "$1" | state_escape)"
    answer="$(printf '%s' "$2" | state_escape)"
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

contains_wake_word() {
    text="$1"
    old_ifs="$IFS"
    IFS='|'
    for word in $VOICE_WAKE_WORDS; do
        IFS="$old_ifs"
        [ -n "$word" ] || continue
        case "$text" in
            *"$word"*) return 0 ;;
        esac
        IFS='|'
    done
    IFS="$old_ifs"
    return 1
}

strip_wake_word() {
    text="$1"
    old_ifs="$IFS"
    IFS='|'
    for word in $VOICE_WAKE_WORDS; do
        IFS="$old_ifs"
        [ -n "$word" ] || continue
        text="$(printf '%s' "$text" | sed "s/$word//g")"
        IFS='|'
    done
    IFS="$old_ifs"
    printf '%s' "$text" | state_escape
}

local_cart_reply() {
    case "$1" in
        checkout_pending)
            printf '%s\n' "好的，正在为您结账。请选择微信支付、支付宝支付或银联云闪付。"
            return 0
            ;;
        pay:wechat)
            printf '%s\n' "好的，已为您打开微信收款码，请扫码支付。"
            return 0
            ;;
        pay:alipay)
            printf '%s\n' "好的，已为您打开支付宝收款码，请扫码支付。"
            return 0
            ;;
        pay:unionpay)
            printf '%s\n' "抱歉，银联云闪付暂不可用，请选择微信支付或支付宝支付。"
            return 0
            ;;
        clear)
            printf '%s\n' "购物车已清空。"
            return 0
            ;;
        add:*)
            printf '%s\n' "好的，已加入购物车。"
            return 0
            ;;
        info:cola)
            printf '%s\n' "可乐3.50元，可以直接说把可乐加入购物车。"
            return 0
            ;;
        info:milk)
            printf '%s\n' "牛奶4.50元，可以直接说把牛奶加入购物车。"
            return 0
            ;;
        info:water)
            printf '%s\n' "矿泉水2.00元，可以直接说把水加入购物车。"
            return 0
            ;;
        info:bread)
            printf '%s\n' "面包5.00元，可以直接说把面包加入购物车。"
            return 0
            ;;
        info:noodle)
            printf '%s\n' "泡面4.00元，可以直接说把泡面加入购物车。"
            return 0
            ;;
        info:chips)
            printf '%s\n' "薯片6.00元，可以直接说把薯片加入购物车。"
            return 0
            ;;
        info:biscuit)
            printf '%s\n' "饼干5.50元，可以直接说把饼干加入购物车。"
            return 0
            ;;
        info:toothpaste)
            printf '%s\n' "牙膏8.00元，可以直接说把牙膏加入购物车。"
            return 0
            ;;
        info:tissue)
            printf '%s\n' "纸巾4.00元，可以直接说把纸巾加入购物车。"
            return 0
            ;;
        info:soap)
            printf '%s\n' "香皂3.00元，可以直接说把香皂加入购物车。"
            return 0
            ;;
        info:products)
            printf '%s\n' "目前有可乐、牛奶、水、面包、泡面、薯片、饼干、牙膏、纸巾和香皂。"
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

start_async_local_cart_reply_tts() {
    answer="$1"
    printf "%s\n" "$$" > "$TTS_PLAYING_FILE"
    nohup "$PROJECT_DIR/scripts/run_voiceask_speaker.sh" --speak-local-reply "$answer" \
        >/tmp/qsm_local_cart_tts.log 2>&1 </dev/null &
}

play_local_cart_reply() {
    answer="$1"
    mode="${VOICE_FAST_CART_REPLY:-async}"
    case "$mode" in
        0|off|sync)
            play_text_tts "$answer" || true
            ;;
        skip|silent)
            echo "Skipping cloud TTS for fast local cart reply."
            ;;
        async|1|on|*)
            echo "Starting async local cart reply TTS."
            start_async_local_cart_reply_tts "$answer"
            ;;
    esac
}

consume_payment_finished_voice_prompt() {
    label="${1:-voice}"
    if [ -f "$PAYMENT_FINISHED_VOICE_FILE" ]; then
        rm -f "$PAYMENT_FINISHED_VOICE_FILE"
        echo "Payment finished voice prompt before ${label}."
        play_local_cart_reply "$PAYMENT_FINISHED_TEXT" || true
        return 0
    fi
    return 1
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
    if ! ping -c 1 -W 2 openspeech.bytedance.com >/dev/null 2>&1; then
        echo "网络暂未连通，本次仍继续录音识别；如 ASR 失败请检查 Wi-Fi。"
        if [ "${VOICE_TRY_WIFI_RECONNECT:-0}" = "1" ]; then
            "$PROJECT_DIR/scripts/connect_wifi.sh" || true
        fi
        ensure_dns
    fi
}

with_tts_playback_lock() {
    tts_lock_owner="$$"
    printf "%s\n" "$tts_lock_owner" > "$TTS_PLAYING_FILE"
    set +e
    "$@"
    rc=$?
    set -e
    sleep "${VOICE_TTS_POST_DELAY_SECONDS:-2.5}"
    clear_tts_playback_lock_if_owner "$tts_lock_owner"
    return "$rc"
}

clear_tts_playback_lock_if_owner() {
    owner="$1"
    current_owner="$(cat "$TTS_PLAYING_FILE" 2>/dev/null || true)"
    if [ "$current_owner" = "$owner" ]; then
        rm -f "$TTS_PLAYING_FILE"
    fi
}

wait_for_tts_playback_idle() {
    label="${1:-voice}"
    waited=0
    while [ -f "$TTS_PLAYING_FILE" ]; do
        if [ "$waited" -eq 0 ]; then
            echo "Waiting for TTS playback to finish before recording ${label}..." >&2
        fi
        sleep 0.2
        waited=$((waited + 1))
        if [ "$waited" -ge "${VOICE_TTS_LOCK_MAX_TICKS:-80}" ]; then
            echo "TTS playback lock timeout; clearing stale lock." >&2
            rm -f "$TTS_PLAYING_FILE"
            break
        fi
    done
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

play_mp3_unlocked() {
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

play_mp3() {
    with_tts_playback_lock play_mp3_unlocked "$@"
}

play_wav_file_unlocked() {
    wav="$1"
    prepare_speaker
    echo "Speaker playback: $wav channel=${VOICE_CHANNEL:-left}"
    tinyplay "$wav" -D 0 -d 0 || aplay -D hw:0,0 "$wav"
}

play_wav_file() {
    with_tts_playback_lock play_wav_file_unlocked "$@"
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

build_wake_ack_audio() {
    mkdir -p "$CACHE_DIR"
    raw="/tmp/embed_tts_wake_ack_response.http"
    req="/tmp/embed_tts_wake_ack_request.json"
    raw_wav="/tmp/embed_tts_wake_ack_raw.wav"

    request_tts_text "$WAKE_ACK_TEXT" "$raw" "$req" || return 1
    extract_tts_data_to_mp3 "$raw" "$WAKE_ACK_MP3" || return 1
    rm -f "$raw_wav" "$WAKE_ACK_WAV"

    if command -v mpg123 >/dev/null 2>&1; then
        mpg123 -q --stereo -w "$raw_wav" "$WAKE_ACK_MP3" || true
    fi
    if [ ! -s "$raw_wav" ] && command -v ffmpeg >/dev/null 2>&1; then
        ffmpeg -y -loglevel error -i "$WAKE_ACK_MP3" -ac 2 -ar 48000 "$raw_wav" || true
    fi
    [ -s "$raw_wav" ] || return 1

    case "${VOICE_CHANNEL:-left}" in
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
    if command -v sox >/dev/null 2>&1; then
        sox "$raw_wav" "$WAKE_ACK_WAV" gain "${VOICE_GAIN_DB:-1}" 2>/dev/null || cp "$raw_wav" "$WAKE_ACK_WAV"
    else
        cp "$raw_wav" "$WAKE_ACK_WAV"
    fi
    [ -s "$WAKE_ACK_WAV" ]
}

play_wake_ack() {
    if [ ! -s "$WAKE_ACK_WAV" ]; then
        echo "Building cached wake acknowledgement..."
        build_wake_ack_audio || true
    fi
    if [ -s "$WAKE_ACK_WAV" ]; then
        play_wav_file "$WAKE_ACK_WAV" || true
    else
        play_text_tts "${WAKE_ACK_TEXT}" || true
    fi
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
    if is_payment_reply_echo "$question"; then
        echo "Ignoring payment reply echo."
        return 0
    fi
    lexicon_cmd="$(retail_lexicon_command "$question")"
    if [ -n "$lexicon_cmd" ]; then
        if [ "$lexicon_cmd" = "checkout" ]; then
            lexicon_cmd="checkout_pending"
            : > "$PAYMENT_WAIT_FILE"
        elif [ "${lexicon_cmd#info:}" != "$lexicon_cmd" ]; then
            :
        else
            rm -f "$PAYMENT_WAIT_FILE"
        fi
        answer="$(local_cart_reply "$lexicon_cmd")" || return 1
        case "$lexicon_cmd" in
            info:*) state_cmd="" ;;
            *) state_cmd="$lexicon_cmd" ;;
        esac
        echo "Retail lexicon hit: question=$question command=$lexicon_cmd"
        echo "Retail command: $lexicon_cmd"
        echo "Assistant: $answer"
        write_voice_state "$question" "$answer" "$state_cmd"
        play_local_cart_reply "$answer" || true
        return 0
    fi
    cart_cmd="$(voice_payment_method_command "$question")"
    if [ -n "$cart_cmd" ]; then
        rm -f "$PAYMENT_WAIT_FILE"
        answer="$(local_cart_reply "$cart_cmd")" || return 1
        echo "Retail command: $cart_cmd"
        echo "Assistant: $answer"
        write_voice_state "$question" "$answer" "$cart_cmd"
        play_local_cart_reply "$answer" || true
        return 0
    fi
    if [ -f "$PAYMENT_WAIT_FILE" ]; then
        cart_cmd="$(voice_payment_method_command "$question")"
        if [ -n "$cart_cmd" ]; then
            rm -f "$PAYMENT_WAIT_FILE"
            answer="$(local_cart_reply "$cart_cmd")" || return 1
            echo "Retail command: $cart_cmd"
            echo "Assistant: $answer"
            write_voice_state "$question" "$answer" "$cart_cmd"
            play_local_cart_reply "$answer" || true
            return 0
        fi
    fi
    cart_cmd="$(voice_cart_command "$question")"
    if [ -n "$cart_cmd" ]; then
        if [ "$cart_cmd" = "checkout" ]; then
            cart_cmd="checkout_pending"
            : > "$PAYMENT_WAIT_FILE"
        else
            rm -f "$PAYMENT_WAIT_FILE"
        fi
        answer="$(local_cart_reply "$cart_cmd")" || return 1
        echo "Retail command: $cart_cmd"
        echo "Assistant: $answer"
        write_voice_state "$question" "$answer" "$cart_cmd"
        play_local_cart_reply "$answer" || true
        return 0
    fi
    echo "Open chat question: $question"
    answer="$(request_open_chat "$question" 2>/dev/null || true)"
    [ -n "$answer" ] || return 1
    echo "Assistant: $answer"
    write_voice_state "$question" "$answer" ""
    play_text_tts "$answer" || true
}

recognize_voice_once() {
    seconds="$1"
    label="${2:-voice}"
    log="/tmp/embed_project_${label}.log"
    rm -f "$log"

    consume_payment_finished_voice_prompt "$label" || true
    wait_for_tts_playback_idle "$label"
    ensure_dns
    prepare_mic
    echo "Listening for ${label}. Recording ${seconds} seconds..." >&2
    printf 'voiceask %s\nexit\n' "$seconds" | "$BIN" "$CATALOG" > "$log" 2>&1 || true
    sed -n 's/.*Recognized:[[:space:]]*\(.*\)/\1/p' "$log" | tail -1
}

run_voice_question_once() {
    seconds="${1:-${VOICE_COMMAND_SECONDS:-7}}"
    question="$(recognize_voice_once "$seconds" command)"
    if [ -z "$question" ]; then
        echo "No speech recognized in active session."
        return 0
    fi

    echo "Recognized: $question"
    run_open_chat_reply "$question" || true
}

run_active_session() {
    session_seconds="${VOICE_SESSION_SECONDS:-60}"
    now="$(date +%s 2>/dev/null || echo 0)"
    session_end=$((now + session_seconds))

    echo "Active voice session started for ${session_seconds} seconds."
    while true; do
        now="$(date +%s 2>/dev/null || echo 0)"
        [ "$now" -lt "$session_end" ] || break
        wait_for_tts_playback_idle active_session
        echo "Listening for question in active session..."
        run_voice_question_once "${VOICE_COMMAND_SECONDS:-7}"
        sleep "${VOICE_SESSION_LOOP_PAUSE_SECONDS:-1}"
    done
    echo "Active voice session ended. Returning to wake word mode."
}

run_wake_once() {
    wake_seconds="${1:-${VOICE_WAKE_SECONDS:-5}}"
    wake_text="$(recognize_voice_once "$wake_seconds" wake)"

    if [ -z "$wake_text" ]; then
        echo "No wake word recognized."
        return 0
    fi

    echo "Wake candidate: $wake_text"
    if ! is_wake_text "$wake_text"; then
        if [ -n "$(voice_payment_method_command "$wake_text")" ] ||
           [ -n "$(voice_cart_command "$wake_text")" ]; then
            echo "Retail command detected without wake word."
            run_open_chat_reply "$wake_text" || true
            run_active_session
            return 0
        fi
        echo "Wake word not detected."
        return 0
    fi

    command_text="$(extract_wake_command "$wake_text")"
    echo "Wake word detected."
    write_voice_state "唤醒成功" "$WAKE_ACK_TEXT" ""
    play_wake_ack

    if [ -n "$command_text" ] && [ "$(printf '%s' "$command_text" | wc -c | tr -d ' ')" -ge 4 ]; then
        run_open_chat_reply "$command_text" || true
    fi

    echo "Please ask your question now."
    run_active_session
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

play_wake_beep() {
    beep="${VOICE_WAKE_BEEP_WAV:-/tmp/qsm_voice_wake_beep.wav}"
    prepare_speaker
    if command -v sox >/dev/null 2>&1; then
        sox -n -r 48000 -b 16 -c 2 "$beep" synth 0.18 sine 1200 gain -4 >/dev/null 2>&1 || true
    fi
    if [ -s "$beep" ]; then
        tinyplay "$beep" -D 0 -d 0 >/dev/null 2>&1 || aplay -D hw:0,0 "$beep" >/dev/null 2>&1 || true
    else
        printf '\a'
    fi
}

run_embed_command() {
    cmd="$1"
    mode="${2:-normal}"
    log="/tmp/embed_project_voiceask.log"
    require_wake=0
    rm -f /tmp/embed_tts_response.json /tmp/embed_tts_audio.b64 /tmp/embed_tts_reply.mp3 \
        /tmp/embed_tts_response.dechunked.jsonl /tmp/embed_tts_audio_fixed.b64 \
        /tmp/embed_tts_reply_fixed.mp3 /tmp/embed_tts_reply_fixed.wav \
        /tmp/embed_tts_open_response.http /tmp/embed_tts_open_reply.mp3 "$log"

    case "$cmd" in
        voiceask*)
            require_wake=1
            [ "$mode" = "no_wake" ] && require_wake=0
            wait_for_tts_playback_idle "$mode"
            ensure_dns
            prepare_mic
            if [ "$mode" = "wake_only" ]; then
                echo "请说唤醒词“小智小智”。录音 ${VOICE_WAKE_SECONDS:-5} 秒..."
            elif [ "$require_wake" -eq 1 ]; then
                echo "请先说唤醒词“小智小智”，再提出问题。录音 ${VOICE_SECONDS:-8} 秒..."
            else
                echo "请开始提问。录音 ${VOICE_SECONDS:-8} 秒..."
            fi
            display_seconds="$(printf '%s' "$cmd" | sed -n 's/^voiceask[[:space:]]*\([0-9][0-9]*\).*/\1/p')"
            [ -n "$display_seconds" ] || display_seconds="${VOICE_SECONDS:-8}"
            echo "Please speak now. Recording ${display_seconds} seconds..."
            ;;
    esac

    printf '%s\nexit\n' "$cmd" | "$BIN" "$CATALOG" > "$log" 2>&1 || true
    question="$(sed -n 's/.*Recognized:[[:space:]]*\(.*\)/\1/p' "$log" | tail -1)"
    if [ -z "$question" ] && [ "$require_wake" -eq 0 ]; then
        question="$(printf '%s' "$cmd" | sed 's/^ask[[:space:]]*//; s/^voiceask[[:space:]]*[0-9]*//')"
    fi

    if [ -z "$question" ]; then
        echo "未识别到有效语音，请靠近麦克风后重试。"
        return 1
    fi

    if [ "$require_wake" -eq 1 ]; then
        if ! contains_wake_word "$question"; then
            echo "已听到语音，但未检测到唤醒词“小智小智”，本次不回复。"
            return 1
        fi
        question="$(strip_wake_word "$question")"
        [ -n "$question" ] || question="请问有什么可以帮您？"
    fi

    if [ "$mode" = "wake_only" ]; then
        echo "唤醒成功，请听到提示音后再提问。"
        printf '%s\n' "$question" > /tmp/qsm_last_wake_text
        return 0
    fi

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
    case "$answer" in
        *"Speech recognition failed or returned empty text."*|"")
            answer="语音识别失败，请靠近麦克风后重试。"
            ;;
    esac
    [ -n "$answer" ] || answer="语音指令已收到。"
    write_voice_state "$question" "$answer" "$(voice_cart_command "$question")"
    play_latest_tts || true
}

echo "QSM voice assistant with wake word is starting. Wake word: 小智小智"
ensure_network
prepare_speaker

case "${1:-}" in
    --speak-local-reply)
        shift
        with_tts_playback_lock play_text_tts "$*" || true
        exit 0
        ;;
    --prepare-cache)
        build_wake_ack_audio || true
        exit 0
        ;;
    --payment-finished-prompt)
        consume_payment_finished_voice_prompt manual || true
        exit 0
        ;;
    --wake-once)
        seconds="${2:-${VOICE_WAKE_SECONDS:-5}}"
        run_wake_once "$seconds"
        exit 0
        ;;
    --once)
        seconds="${2:-${VOICE_SECONDS:-8}}"
        wake_seconds="${VOICE_WAKE_SECONDS:-5}"
        if run_embed_command "voiceask $wake_seconds" wake_only; then
            play_wake_beep
            run_embed_command "voiceask $seconds" no_wake
        fi
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
