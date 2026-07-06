param(
    [int]$Port = 8787
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot
$env:PYTHONIOENCODING = "utf-8"
$OutputEncoding = [System.Text.UTF8Encoding]::new()
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()

foreach ($name in @("VOLCENGINE_ASR_API_KEY", "VOLCENGINE_ASR_RESOURCE_ID", "VOLCENGINE_ASR_ENDPOINT")) {
    if (!(Get-Item "Env:$name" -ErrorAction SilentlyContinue)) {
        $userValue = [Environment]::GetEnvironmentVariable($name, "User")
        if ($userValue) {
            Set-Item "Env:$name" $userValue
        }
    }
}

if (!$env:VOLCENGINE_ASR_API_KEY) {
    Write-Host "ERROR: VOLCENGINE_ASR_API_KEY is not configured." -ForegroundColor Red
    exit 1
}
if (!$env:VOLCENGINE_ASR_RESOURCE_ID) {
    $env:VOLCENGINE_ASR_RESOURCE_ID = "volc.bigasr.auc_turbo"
}
if (!$env:VOLCENGINE_ASR_ENDPOINT) {
    $env:VOLCENGINE_ASR_ENDPOINT = "https://openspeech.bytedance.com/api/v3/auc/bigmodel/recognize/flash"
}

$adb = Join-Path $HOME "AppData\Local\Microsoft\WinGet\Packages\Google.PlatformTools_Microsoft.Winget.Source_8wekyb3d8bbwe\platform-tools\adb.exe"
if (!(Test-Path $adb)) { $adb = "adb" }

& $adb reverse "tcp:$Port" "tcp:$Port"
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: adb reverse failed." -ForegroundColor Red
    exit 1
}

$boardEnv = @"
export VOLCENGINE_ASR_API_KEY='proxy'
export VOLCENGINE_ASR_RESOURCE_ID='volc.bigasr.auc_turbo'
export VOLCENGINE_ASR_ENDPOINT='http://127.0.0.1:$Port/asr'
"@
$tmp = Join-Path $env:TEMP "qsm_voice_env_proxy.txt"
$boardEnv | Set-Content -Path $tmp -Encoding ASCII
& $adb push $tmp /userdata/Embed_project/.voice_env | Out-Host
& $adb shell "chmod 600 /userdata/Embed_project/.voice_env" | Out-Host

Write-Host ""
Write-Host "ASR proxy is ready for board script." -ForegroundColor Cyan
Write-Host "Keep this window open. On the board, run:"
Write-Host "cd /userdata/Embed_project && ./start_board_voice_assistant.sh"
Write-Host ""

$env:QSM_ASR_PROXY_PORT = "$Port"
python tools\volcengine_asr_proxy.py
