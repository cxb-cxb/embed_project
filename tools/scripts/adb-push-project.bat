@echo off
setlocal
set "ROOT=%~dp0..\.."
set "ADB=%ROOT%\tools\adb\adb.exe"

"%ADB%" devices
"%ADB%" shell "mkdir -p /userdata/Embed_project/bin /userdata/Embed_project/data /userdata/Embed_project/scripts"

"%ADB%" push "%ROOT%\build_arm\embed_project" /userdata/Embed_project/bin/embed_project
"%ADB%" push "%ROOT%\build_arm\qr_scanner" /userdata/Embed_project/bin/qr_scanner
"%ADB%" push "%ROOT%\build_arm\qr_scanner_display" /userdata/Embed_project/bin/qr_scanner_display
"%ADB%" push "%ROOT%\data\products.csv" /userdata/Embed_project/data/products.csv
"%ADB%" push "%ROOT%\scripts\camera_preview_lvds.sh" /userdata/Embed_project/scripts/camera_preview_lvds.sh
"%ADB%" push "%ROOT%\scripts\qr_realtime_terminal.sh" /userdata/Embed_project/scripts/qr_realtime_terminal.sh
"%ADB%" push "%ROOT%\scripts\qr_realtime_lvds.sh" /userdata/Embed_project/scripts/qr_realtime_lvds.sh

"%ADB%" shell "chmod +x /userdata/Embed_project/bin/* /userdata/Embed_project/scripts/*.sh"
pause
