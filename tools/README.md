# Tools

## ADB

Windows ADB is included in `tools/adb`.

Common PowerShell commands from the project root:

```powershell
.\tools\scripts\adb-devices.ps1
.\tools\scripts\adb-shell.ps1
.\tools\scripts\adb-push-project.ps1
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
