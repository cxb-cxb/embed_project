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

Start-Process powershell -ArgumentList @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-NoExit",
    "-File", $visual
) -WorkingDirectory $ProjectRoot

Start-Sleep -Seconds 2

Start-Process powershell -ArgumentList @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-NoExit",
    "-File", $voice,
    "-Seconds", "$VoiceSeconds",
    "-ReplyMode", $ReplyMode
) -WorkingDirectory $ProjectRoot

Write-Host "Demo windows started." -ForegroundColor Green
Write-Host "In the voice window, press Enter and ask: 这个多少钱"
