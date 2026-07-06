param(
    [int]$VoiceSeconds = 6,
    [ValidateSet("offline", "auto", "llm")]
    [string]$ReplyMode = "auto",
    [ValidateSet("terminal", "wav", "board")]
    [string]$VoiceOutput = "terminal",
    [ValidateSet("qr-cart", "visual-preview", "visual-overlay")]
    [string]$LvdsMode = "qr-cart"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot

Write-Host ""
Write-Host "QSM Smart Retail Final Demo" -ForegroundColor Cyan
Write-Host ""
Write-Host "This launcher opens two PowerShell windows:" -ForegroundColor Yellow
Write-Host "  1. LVDS retail display"
Write-Host "  2. Board microphone + ASR + smart reply"
Write-Host ""
Write-Host "LVDS mode: $LvdsMode"
Write-Host ""

$visual = Join-Path $ProjectRoot "start_visual_lvds_demo.ps1"
$qrLvds = Join-Path $ProjectRoot "tools\scripts\start-lvds-demo.ps1"
$voice = Join-Path $ProjectRoot "start_voice_mic_query_loop.ps1"

function New-EncodedPowerShellCommand {
    param([string]$Command)
    $bytes = [System.Text.Encoding]::Unicode.GetBytes($Command)
    return [Convert]::ToBase64String($bytes)
}

$lvdsCommand = switch ($LvdsMode) {
    "qr-cart" {
@"
Set-Location -LiteralPath '$ProjectRoot'
Write-Host 'Starting QR cart + LVDS realtime display...' -ForegroundColor Cyan
Write-Host 'Scan product QR codes to add items. Scan checkout or clear for cart actions.' -ForegroundColor Yellow
try {
    & '$qrLvds'
} catch {
    Write-Host ''
    Write-Host "LVDS QR CART ERROR: `$(`$_.Exception.Message)" -ForegroundColor Red
}
Write-Host ''
Write-Host 'LVDS QR cart window ended. Press Enter to close.'
Read-Host
"@
    }
    "visual-preview" {
@"
Set-Location -LiteralPath '$ProjectRoot'
Write-Host 'Starting visual recognition + LVDS realtime preview...' -ForegroundColor Cyan
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
    }
    "visual-overlay" {
@"
Set-Location -LiteralPath '$ProjectRoot'
Write-Host 'Starting visual recognition + LVDS overlay...' -ForegroundColor Cyan
try {
    & '$visual' -DisplayMode fb
} catch {
    Write-Host ''
    Write-Host "VISUAL OVERLAY ERROR: `$(`$_.Exception.Message)" -ForegroundColor Red
}
Write-Host ''
Write-Host 'Visual overlay window ended. Press Enter to close.'
Read-Host
"@
    }
}

$voiceCommand = @"
Set-Location -LiteralPath '$ProjectRoot'
Write-Host 'Starting board microphone + ASR + smart reply...' -ForegroundColor Cyan
try {
    & '$voice' -Seconds $VoiceSeconds -ReplyMode '$ReplyMode' -VoiceOutput '$VoiceOutput'
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
    "-EncodedCommand", (New-EncodedPowerShellCommand $lvdsCommand)
) -WorkingDirectory $ProjectRoot

Start-Sleep -Seconds 2

Start-Process -FilePath "powershell.exe" -ArgumentList @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-NoExit",
    "-EncodedCommand", (New-EncodedPowerShellCommand $voiceCommand)
) -WorkingDirectory $ProjectRoot

Write-Host "Demo windows started." -ForegroundColor Green
Write-Host "QR mode: scan product QR codes to add items to cart."
Write-Host "Voice mode: in the voice window, press Enter and ask a question."
