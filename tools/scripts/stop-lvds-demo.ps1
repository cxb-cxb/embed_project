param(
    [switch]$NoDesktop
)

$ErrorActionPreference = "Stop"

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$bundledAdb = Join-Path $projectRoot "tools\adb\adb.exe"

if (Test-Path $bundledAdb) {
    try {
        & $bundledAdb version | Out-Null
        $adb = $bundledAdb
    } catch {
        $adb = (Get-Command adb -ErrorAction Stop).Source
    }
} else {
    $adb = (Get-Command adb -ErrorAction Stop).Source
}

Write-Host "[demo] stopping LVDS, camera, and QR demo tasks"
$stopTasks = @'
for pattern in scripts/demo_lvds_start.sh qr_scanner_display qr_scanner gst-launch-1.0 camera_pgm_stream; do
  for pid in $(pgrep -f "$pattern" 2>/dev/null || true); do
    [ "$pid" = "$$" ] && continue
    kill -9 "$pid" 2>/dev/null || true
  done
done
sleep 1
'@
& $adb shell $stopTasks | Out-Host

if (!$NoDesktop) {
    Write-Host "[demo] restoring Weston desktop on LVDS"
    $restoreDesktop = @'
killall weston 2>/dev/null || true
pkill -9 weston-desktop-shell 2>/dev/null || true
pkill -9 weston-keyboard 2>/dev/null || true
pkill -9 weston-simple-shm 2>/dev/null || true
rm -f /tmp/runtime-root/wayland-0 /tmp/runtime-root/wayland-0.lock /tmp/weston.log /tmp/weston_nohup.out
mkdir -p /tmp/runtime-root
chmod 700 /tmp/runtime-root
for b in /sys/class/backlight/*; do
  [ -e "$b" ] || continue
  [ -f "$b/bl_power" ] && echo 0 > "$b/bl_power" 2>/dev/null || true
  if [ -f "$b/brightness" ]; then
    max="$(cat "$b/max_brightness" 2>/dev/null || echo 255)"
    echo "$max" > "$b/brightness" 2>/dev/null || true
  fi
done
export XDG_RUNTIME_DIR=/tmp/runtime-root
nohup /usr/bin/weston --backend=drm-backend.so --log=/tmp/weston.log >/tmp/weston_nohup.out 2>&1 &
sleep 2
ps -ef | grep weston | grep -v grep || true
'@
    & $adb shell $restoreDesktop | Out-Host
}

& $adb shell "ps -ef | grep -E 'demo_lvds_start|qr_scanner|gst-launch|camera_pgm' | grep -v grep | grep -v pgrep || true" | Out-Host
