# Tools

## ADB

Windows ADB is included in `tools/adb`.

Common PowerShell commands from the project root:

```powershell
.\tools\scripts\adb-devices.ps1
.\tools\scripts\adb-shell.ps1
.\tools\scripts\adb-push-project.ps1
.\tools\scripts\start-lvds-demo.ps1
.\tools\scripts\stop-lvds-demo.ps1
```

Common Windows batch commands:

```bat
tools\scripts\adb-devices.bat
tools\scripts\adb-shell.bat
tools\scripts\adb-push-project.bat
```

Direct usage:

```powershell
& ".\tools\adb\adb.exe" devices
& ".\tools\adb\adb.exe" shell
```

## LVDS demo one-click start

From the project root:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\scripts\start-lvds-demo.ps1
```

Use background mode when you only want to start the board demo and return to PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\scripts\start-lvds-demo.ps1 -Background
```

Stop camera and QR demo tasks:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\scripts\stop-lvds-demo.ps1
```

Generate the demo QR package:

```powershell
python -m pip install -r requirements-tools.txt
python tools\generate_demo_qr.py
```
