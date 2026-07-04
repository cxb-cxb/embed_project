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

Write-Host "[demo] stopping camera and QR demo tasks"
& $adb shell "pkill -9 gst-launch-1.0 2>/dev/null || true; pkill -9 qr_scanner 2>/dev/null || true; pkill -9 qr_scanner_display 2>/dev/null || true"
& $adb shell "pgrep -af 'qr_scanner|gst-launch' || true"
