#!/bin/sh
set -eu

PROJECT_DIR="${PROJECT_DIR:-/userdata/Embed_project}"
INIT_SCRIPT="${INIT_SCRIPT:-/etc/init.d/S99embed_project}"

if [ "$(id -u 2>/dev/null || echo 1)" != "0" ]; then
    echo "Please run as root on the board."
    exit 1
fi

mount -o remount,rw / 2>/dev/null || true
mkdir -p "$(dirname "$INIT_SCRIPT")"

cat > "$INIT_SCRIPT" <<'INIT_EOF'
#!/bin/sh

PROJECT_DIR="/userdata/Embed_project"
BOOT_LOG="/tmp/qsm_run_mission_boot.log"
BOOT_PID="/tmp/qsm_run_mission_boot.pid"
BOOT_DELAY="${QSM_BOOT_DELAY_SECONDS:-8}"

start_demo() {
    if [ -f "$BOOT_PID" ]; then
        old_pid="$(cat "$BOOT_PID" 2>/dev/null || true)"
        if [ -n "$old_pid" ] && kill -0 "$old_pid" 2>/dev/null; then
            echo "Embed_project boot launcher already running: $old_pid"
            return 0
        fi
    fi

    rm -f /tmp/qr_scanner_display.stop
    {
        echo "[$(date '+%Y-%m-%d %H:%M:%S' 2>/dev/null || echo boot)] Embed_project autostart begin"
        sleep "$BOOT_DELAY"
        cd "$PROJECT_DIR" || exit 1
        sh "$PROJECT_DIR/scripts/run_mission.sh"
    } >> "$BOOT_LOG" 2>&1 &
    echo "$!" > "$BOOT_PID"
    echo "Embed_project autostart scheduled, pid=$(cat "$BOOT_PID"), log=$BOOT_LOG"
}

stop_demo() {
    if [ -f "$BOOT_PID" ]; then
        old_pid="$(cat "$BOOT_PID" 2>/dev/null || true)"
        if [ -n "$old_pid" ]; then
            kill "$old_pid" 2>/dev/null || true
        fi
        rm -f "$BOOT_PID"
    fi
    pkill -f "scripts/start_voice_auto_listen.sh" 2>/dev/null || true
    pkill -f "scripts/run_voiceask_speaker.sh" 2>/dev/null || true
    pkill -f "/userdata/Embed_project/bin/embed_project" 2>/dev/null || true
    pkill -f "qr_scanner_display" 2>/dev/null || true
    pkill -f "tinycap" 2>/dev/null || true
    pkill -f "tinyplay" 2>/dev/null || true
    pkill -f "aplay" 2>/dev/null || true
    pkill -f "mpg123" 2>/dev/null || true
    echo "Embed_project stopped."
}

case "${1:-start}" in
    start)
        start_demo
        ;;
    stop)
        stop_demo
        ;;
    restart)
        stop_demo
        sleep 1
        start_demo
        ;;
    status)
        if [ -f "$BOOT_PID" ] && kill -0 "$(cat "$BOOT_PID" 2>/dev/null)" 2>/dev/null; then
            echo "Embed_project boot launcher running: $(cat "$BOOT_PID")"
        else
            echo "Embed_project boot launcher is not running."
        fi
        ;;
    *)
        echo "Usage: $0 {start|stop|restart|status}"
        exit 1
        ;;
esac
INIT_EOF

chmod +x "$INIT_SCRIPT"

if command -v update-rc.d >/dev/null 2>&1; then
    update-rc.d "$(basename "$INIT_SCRIPT")" defaults >/dev/null 2>&1 || true
fi

echo "Installed Embed_project boot autostart: $INIT_SCRIPT"
echo "Start now: $INIT_SCRIPT start"
echo "Log file: /tmp/qsm_run_mission_boot.log"
