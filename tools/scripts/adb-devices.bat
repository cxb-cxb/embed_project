@echo off
set "ADB=%~dp0..\adb\adb.exe"
"%ADB%" devices
pause
