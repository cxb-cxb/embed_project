#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WIFI_ENV="$PROJECT_DIR/config/wifi.env"

if [ -f "$WIFI_ENV" ]; then
    # shellcheck disable=SC1090
    . "$WIFI_ENV"
fi

WIFI_IFACE="${WIFI_IFACE:-wlan0}"
WIFI_SSID="${WIFI_SSID:-}"
WIFI_PASSWORD="${WIFI_PASSWORD:-}"
connect_timeout="${WIFI_CONNECT_TIMEOUT_SECONDS:-75}"

if [ -z "$WIFI_SSID" ] || [ -z "$WIFI_PASSWORD" ]; then
    echo "Missing WIFI_SSID or WIFI_PASSWORD in $WIFI_ENV"
    exit 1
fi

mkdir -p /var/run/wpa_supplicant "$PROJECT_DIR/config"
killall wpa_supplicant 2>/dev/null || true
killall udhcpc 2>/dev/null || true

ifconfig wlan1 down 2>/dev/null || true
ifconfig "$WIFI_IFACE" up

{
    echo "ctrl_interface=/var/run/wpa_supplicant"
    echo "update_config=1"
    echo "country=CN"
    wpa_passphrase "$WIFI_SSID" "$WIFI_PASSWORD"
} > "$PROJECT_DIR/config/wpa_supplicant.conf"
chmod 600 "$PROJECT_DIR/config/wpa_supplicant.conf"

wpa_supplicant -B -i "$WIFI_IFACE" -c "$PROJECT_DIR/config/wpa_supplicant.conf"
waited=0
wpa_state=""
while [ "$waited" -lt "$connect_timeout" ]; do
    sleep 1
    waited=$((waited + 1))
    wpa_state="$(wpa_cli -p /var/run/wpa_supplicant -i "$WIFI_IFACE" status | sed -n 's/^wpa_state=//p')"
    [ "$wpa_state" = "COMPLETED" ] && break
done

if [ "$wpa_state" != "COMPLETED" ]; then
    echo "Wi-Fi association failed. Current state: $wpa_state"
    wpa_cli -p /var/run/wpa_supplicant -i "$WIFI_IFACE" scan_results || true
    exit 1
fi

udhcpc -i "$WIFI_IFACE" -q -n -t 8
ifconfig "$WIFI_IFACE"
