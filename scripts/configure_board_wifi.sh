#!/bin/sh
set -eu

ssid="${1:-}"
password="${2:-}"
env_file="/userdata/Embed_project/config/wifi.env"

if [ -z "$ssid" ] || [ -z "$password" ]; then
    echo "Usage: $0 <ssid> <password>"
    exit 1
fi

mkdir -p /userdata/Embed_project/config
tmp="${env_file}.tmp"
cat > "$tmp" <<EOF
export WIFI_IFACE='wlan0'
export WIFI_SSID='$ssid'
export WIFI_PASSWORD='$password'
EOF
chmod 600 "$tmp"
mv "$tmp" "$env_file"

echo "Wi-Fi config updated for SSID: $ssid"
