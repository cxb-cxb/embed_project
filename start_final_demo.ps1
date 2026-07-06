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

function New-EncodedPowerShellCommand {
    param([string]$Command)
    $bytes = [System.Text.Encoding]::Unicode.GetBytes($Command)
    return [Convert]::ToBase64String($bytes)
}

$visualCommand = @"
Set-Location -LiteralPath '$ProjectRoot'
Write-Host 'Starting visual recognition + LVDS overlay...' -ForegroundColor Cyan
try {
    & '$visual'
} catch {
    Write-Host ''
    Write-Host "VISUAL ERROR: `$(`$_.Exception.Message)" -ForegroundColor Red
}
Write-Host ''
Write-Host 'Visual window ended. Press Enter to close.'
Read-Host
"@

$voiceCommand = @"
Set-Location -LiteralPath '$ProjectRoot'
Write-Host 'Starting board microphone + ASR + smart reply...' -ForegroundColor Cyan
try {
    & '$voice' -Seconds $VoiceSeconds -ReplyMode '$ReplyMode'
} catch {
    Write-Host ''
    Write-Host "VOICE ERROR: `$(`$_.Exception.Message)" -ForegroundColor Red
}
Write-Host ''
Write-Host 'Voice window ended. Press Enter to close.'
Read-Host
"@

Start-Process -FilePath "powershell.exe" -ArgumentList @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-NoExit",
    "-EncodedCommand", (New-EncodedPowerShellCommand $visualCommand)
) -WorkingDirectory $ProjectRoot

Start-Sleep -Seconds 2

Start-Process -FilePath "powershell.exe" -ArgumentList @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-NoExit",
    "-EncodedCommand", (New-EncodedPowerShellCommand $voiceCommand)
) -WorkingDirectory $ProjectRoot

Write-Host "Demo windows started." -ForegroundColor Green
Write-Host "In the voice window, press Enter and ask the price question."
