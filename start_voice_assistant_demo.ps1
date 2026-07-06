param(
    [ValidateSet("offline", "auto", "llm")]
    [string]$Mode = "auto",
    [string]$CurrentProduct = "",
    [string]$Question = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot
$env:PYTHONIOENCODING = "utf-8"
$OutputEncoding = [System.Text.UTF8Encoding]::new()
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new()

function Stop-Demo {
    param([string]$Message)
    Write-Host ""
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

$Python = Get-Command python -ErrorAction SilentlyContinue
if (!$Python) {
    Stop-Demo "python was not found in PATH."
}

$AssistantScript = Join-Path $ProjectRoot "tools\voice_retail_assistant.py"
if (!(Test-Path $AssistantScript)) {
    Stop-Demo "missing assistant script: $AssistantScript"
}

& $Python.Source -m py_compile $AssistantScript
if ($LASTEXITCODE -ne 0) {
    Stop-Demo "Python syntax check failed."
}

$ArgsList = @($AssistantScript, "--mode", $Mode)
if ($CurrentProduct.Trim()) {
    $ArgsList += @("--current-product", $CurrentProduct.Trim())
}
if ($Question.Trim()) {
    $ArgsList += @("--question", $Question.Trim())
}

Write-Host ""
Write-Host "==> Starting terminal voice-query assistant" -ForegroundColor Cyan
Write-Host "Mode: $Mode"
if ($CurrentProduct.Trim()) {
    Write-Host "Current product: $($CurrentProduct.Trim())"
}
Write-Host ""

& $Python.Source @ArgsList
