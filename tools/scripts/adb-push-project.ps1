$ErrorActionPreference = "Stop"

$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$adb = Join-Path $projectRoot "tools\adb\adb.exe"

& $adb devices
& $adb shell "mkdir -p /userdata/Embed_project/bin /userdata/Embed_project/data /userdata/Embed_project/scripts"

& $adb push (Join-Path $projectRoot "build_arm\embed_project") /userdata/Embed_project/bin/embed_project
& $adb push (Join-Path $projectRoot "build_arm\qr_scanner") /userdata/Embed_project/bin/qr_scanner
& $adb push (Join-Path $projectRoot "build_arm\qr_scanner_display") /userdata/Embed_project/bin/qr_scanner_display
& $adb push (Join-Path $projectRoot "data\products.csv") /userdata/Embed_project/data/products.csv
& $adb push (Join-Path $projectRoot "scripts\camera_preview_lvds.sh") /userdata/Embed_project/scripts/camera_preview_lvds.sh
& $adb push (Join-Path $projectRoot "scripts\qr_realtime_terminal.sh") /userdata/Embed_project/scripts/qr_realtime_terminal.sh
& $adb push (Join-Path $projectRoot "scripts\qr_realtime_lvds.sh") /userdata/Embed_project/scripts/qr_realtime_lvds.sh

& $adb shell "chmod +x /userdata/Embed_project/bin/* /userdata/Embed_project/scripts/*.sh"
