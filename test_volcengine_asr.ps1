param(
    [int]$Seconds = 5
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot
$env:PYTHONIOENCODING = "utf-8"
$OutputEncoding = [System.Text.UTF8Encoding]::new()
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new()

if (!$env:VOLCENGINE_ASR_API_KEY) {
    Write-Host "ERROR: 未检测到 VOLCENGINE_ASR_API_KEY。请先运行 .\configure_volcengine_asr.ps1" -ForegroundColor Red
    exit 1
}

python tools\voice_query_from_board_mic.py --seconds $Seconds --asr-provider volcengine --reply-mode offline
