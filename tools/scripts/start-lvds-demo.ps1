param(
    [switch]$Background
)

$ErrorActionPreference = "Stop"

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$bundledAdb = Join-Path $projectRoot "tools\adb\adb.exe"

function Resolve-Adb {
    if (Test-Path $bundledAdb) {
        try {
            & $bundledAdb version | Out-Null
            return $bundledAdb
        } catch {
            Write-Warning "Bundled adb is not usable, falling back to adb from PATH."
        }
    }

    $cmd = Get-Command adb -ErrorAction Stop
    return $cmd.Source
}

$adb = Resolve-Adb
Write-Host "[demo] adb: $adb"

& $adb start-server | Out-Host
$devices = & $adb devices -l
$devices | Out-Host
if (-not (($devices -join "`n") -match "\bdevice\b")) {
    throw "No ADB device found. Check USB data cable, normal boot mode, and run adb devices again."
}

Write-Host "[demo] preparing /userdata/Embed_project"
& $adb shell "mkdir -p /userdata/Embed_project/scripts /userdata/Embed_project/bin /userdata/Embed_project/data" | Out-Host

$scriptFiles = @{
    "scripts\demo_lvds_start.sh" = "/userdata/Embed_project/scripts/demo_lvds_start.sh"
    "scripts\qr_realtime_lvds.sh" = "/userdata/Embed_project/scripts/qr_realtime_lvds.sh"
    "scripts\camera_preview_lvds.sh" = "/userdata/Embed_project/scripts/camera_preview_lvds.sh"
}

foreach ($entry in $scriptFiles.GetEnumerator()) {
    $local = Join-Path $projectRoot $entry.Key
    if (Test-Path $local) {
        & $adb push $local $entry.Value | Out-Host
    }
}

if (Test-Path (Join-Path $projectRoot "data\products.csv")) {
    & $adb push (Join-Path $projectRoot "data\products.csv") "/userdata/Embed_project/data/products.csv" | Out-Host
}

$qrScannerDisplay = Join-Path $projectRoot "build_arm\qr_scanner_display"
if (!(Test-Path $qrScannerDisplay)) {
    $qrScannerDisplay = Join-Path $projectRoot "patched\qr_scanner_display"
}
if (Test-Path $qrScannerDisplay) {
    & $adb push $qrScannerDisplay "/userdata/Embed_project/bin/qr_scanner_display" | Out-Host
}

& $adb shell "chmod +x /userdata/Embed_project/scripts/*.sh /userdata/Embed_project/bin/* 2>/dev/null || true" | Out-Host

if ($Background) {
    Write-Host "[demo] starting in background. Log: /tmp/qsm_lvds_demo.log"
    & $adb shell "pkill -9 gst-launch-1.0 2>/dev/null || true; pkill -9 qr_scanner 2>/dev/null || true; pkill -9 qr_scanner_display 2>/dev/null || true; rm -f /tmp/qsm_lvds_demo.log" | Out-Host
    Start-Sleep -Seconds 2
    & $adb shell "cd /userdata/Embed_project; setsid sh scripts/demo_lvds_start.sh >/tmp/qsm_lvds_demo.log 2>&1 </dev/null & sleep 1" | Out-Host
    Start-Sleep -Seconds 3
    & $adb shell "pgrep -af qr_scanner_display; echo --- log ---; tail -40 /tmp/qsm_lvds_demo.log 2>/dev/null || true" | Out-Host
} else {
    Write-Host "[demo] starting foreground demo. Press Ctrl+C to stop."
    & $adb shell "cd /userdata/Embed_project && sh scripts/demo_lvds_start.sh"
}
