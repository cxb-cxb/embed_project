param(
    [int]$VoiceSeconds = 6,
    [ValidateSet("offline", "auto", "llm")]
    [string]$ReplyMode = "auto"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot

Write-Host ""
Write-Host "QSM Smart Retail Final Demo" -ForegroundColor Cyan
Write-Host ""
Write-Host "This launcher opens two PowerShell windows:" -ForegroundColor Yellow
Write-Host "  1. Visual recognition + LVDS overlay"
Write-Host "  2. Board microphone + ASR + smart reply"
Write-Host ""

$visual = Join-Path $ProjectRoot "start_visual_lvds_demo.ps1"
$voice = Join-Path $ProjectRoot "start_voice_mic_query_loop.ps1"
$runtimeDir = "D:\qsm_embed_dataset\lvds_overlay_runtime"
New-Item -ItemType Directory -Force -Path $runtimeDir | Out-Null

$visualLauncher = Join-Path $runtimeDir "start_visual_final_window.cmd"
$voiceLauncher = Join-Path $runtimeDir "start_voice_final_window.cmd"

@"
@echo off
chcp 65001 >nul
cd /d "$ProjectRoot"
echo Starting visual recognition + LVDS overlay...
powershell -NoProfile -ExecutionPolicy Bypass -NoExit -File "$visual"
echo.
echo Visual window ended. Press any key to close.
pause >nul
"@ | Set-Content -Path $visualLauncher -Encoding ASCII

@"
@echo off
chcp 65001 >nul
cd /d "$ProjectRoot"
echo Starting board microphone + ASR + smart reply...
powershell -NoProfile -ExecutionPolicy Bypass -NoExit -File "$voice" -Seconds $VoiceSeconds -ReplyMode $ReplyMode
echo.
echo Voice window ended. Press any key to close.
pause >nul
"@ | Set-Content -Path $voiceLauncher -Encoding ASCII

Start-Process -FilePath "cmd.exe" -ArgumentList @("/k", "`"$visualLauncher`"") -WorkingDirectory $ProjectRoot

Start-Sleep -Seconds 2

Start-Process -FilePath "cmd.exe" -ArgumentList @("/k", "`"$voiceLauncher`"") -WorkingDirectory $ProjectRoot

Write-Host "Demo windows started." -ForegroundColor Green
Write-Host "In the voice window, press Enter and ask the price question."
