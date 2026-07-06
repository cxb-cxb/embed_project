param(
    [int]$Rounds = 0,
    [double]$Interval = 0.5,
    [int]$ClassifyEvery = 5,
    [double]$ClassifyPeriod = 1.0,
    [ValidateSet("preview", "fb", "kms")]
    [string]$DisplayMode = "preview",
    [switch]$SingleShot,
    [switch]$SkipChecks
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot
$env:PYTHONIOENCODING = "utf-8"
$OutputEncoding = [System.Text.UTF8Encoding]::new()
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new()

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Stop-Demo {
    param([string]$Message)
    Write-Host ""
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

function Find-Adb {
    $wingetAdb = Join-Path $HOME "AppData\Local\Microsoft\WinGet\Packages\Google.PlatformTools_Microsoft.Winget.Source_8wekyb3d8bbwe\platform-tools\adb.exe"
    if (Test-Path $wingetAdb) {
        return $wingetAdb
    }

    $pathAdb = Get-Command adb -ErrorAction SilentlyContinue
    if ($pathAdb) {
        return $pathAdb.Source
    }

    Stop-Demo "adb.exe was not found. Install Google Platform Tools or add adb to PATH."
}

function Find-Python {
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) {
        return $python.Source
    }

    Stop-Demo "python was not found in PATH."
}

$Adb = Find-Adb
$Python = Find-Python
$DemoScript = Join-Path $ProjectRoot "tools\live_lvds_product_overlay.py"

if (!(Test-Path $DemoScript)) {
    Stop-Demo "missing demo script: $DemoScript"
}

if (!$SkipChecks) {
    Write-Step "Checking Python dependencies"
    & $Python -c "import cv2, numpy, PIL" 2>$null
    if ($LASTEXITCODE -ne 0) {
        Stop-Demo "Python dependencies are missing. Required: opencv-python, numpy, pillow."
    }

    Write-Step "Checking script syntax"
    & $Python -m py_compile $DemoScript
    if ($LASTEXITCODE -ne 0) {
        Stop-Demo "Python syntax check failed."
    }

    Write-Step "Checking ADB device"
    $devices = & $Adb devices -l
    $devices | ForEach-Object { Write-Host $_ }
    $online = $devices | Where-Object { $_ -match "\sdevice\s" }
    if (!$online) {
        Stop-Demo "no online ADB device found. Check USB cable, board power, and normal ADB mode."
    }

    Write-Step "Checking LVDS state"
    $lvds = & $Adb shell "cat /sys/class/drm/card0-LVDS-1/status 2>/dev/null; cat /sys/class/drm/card0-LVDS-1/modes 2>/dev/null | head -1"
    $lvds | ForEach-Object { Write-Host $_ }
    if (($lvds -join "`n") -notmatch "connected") {
        Stop-Demo "LVDS is not reported as connected."
    }
}

Write-Step "Starting visual recognition LVDS demo"
if ($Rounds -eq 0) {
    Write-Host "Mode: continuous. Press Ctrl+C to stop." -ForegroundColor Yellow
} else {
    Write-Host "Mode: $Rounds rounds." -ForegroundColor Yellow
}
Write-Host "Interval: $Interval seconds"
Write-Host "Classify every: $ClassifyEvery frame(s)"
Write-Host "Classify period: $ClassifyPeriod seconds"
Write-Host "Display mode: $DisplayMode"
if ($SingleShot) {
    Write-Host "Capture mode: single-shot fallback" -ForegroundColor Yellow
} else {
    Write-Host "Capture mode: continuous camera stream" -ForegroundColor Green
}

$DemoArgs = @(
    $DemoScript,
    "--rounds", "$Rounds",
    "--interval", "$Interval",
    "--classify-every", "$ClassifyEvery",
    "--classify-period", "$ClassifyPeriod",
    "--display-mode", "$DisplayMode"
)
if ($SingleShot) {
    $DemoArgs += "--single-shot"
}

& $Python @DemoArgs
