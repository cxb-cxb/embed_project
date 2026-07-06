param(
    [int]$Seconds = 5,
    [ValidateSet("offline", "auto", "llm")]
    [string]$ReplyMode = "offline",
    [ValidateSet("volcengine", "openai")]
    [string]$AsrProvider = "volcengine",
    [string]$CurrentProduct = "",
    [string]$AsrText = "",
    [switch]$Loop
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot
$env:PYTHONIOENCODING = "utf-8"
$OutputEncoding = [System.Text.UTF8Encoding]::new()
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new()

foreach ($name in @(
    "VOLCENGINE_ASR_API_KEY",
    "VOLCENGINE_ASR_RESOURCE_ID",
    "VOLCENGINE_ASR_ENDPOINT",
    "VOLCENGINE_ASR_APP_ID",
    "RETAIL_LLM_API_KEY",
    "RETAIL_LLM_BASE_URL",
    "RETAIL_LLM_MODEL"
)) {
    if (!(Get-Item "Env:$name" -ErrorAction SilentlyContinue)) {
        $userValue = [Environment]::GetEnvironmentVariable($name, "User")
        if ($userValue) {
            Set-Item "Env:$name" $userValue
        }
    }
}

$Script = Join-Path $ProjectRoot "tools\voice_query_from_board_mic.py"
python -m py_compile $Script

$ArgsList = @($Script, "--seconds", "$Seconds", "--reply-mode", $ReplyMode, "--asr-provider", $AsrProvider)
if ($CurrentProduct.Trim()) {
    $ArgsList += @("--current-product", $CurrentProduct.Trim())
}
if ($AsrText.Trim()) {
    $ArgsList += @("--asr-text", $AsrText.Trim())
}

if (!$Loop) {
    python @ArgsList
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "QSM voice assistant loop is ready." -ForegroundColor Cyan
Write-Host "Press Enter to record and answer. Type q then Enter to quit."
Write-Host "Recording seconds: $Seconds"
Write-Host ""

while ($true) {
    $cmd = Read-Host "voice"
    if ($cmd -in @("q", "quit", "exit")) {
        break
    }
    python @ArgsList
    Write-Host ""
}
