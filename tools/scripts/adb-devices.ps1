$ErrorActionPreference = "Stop"

$adb = Join-Path $PSScriptRoot "..\adb\adb.exe"
& $adb devices
