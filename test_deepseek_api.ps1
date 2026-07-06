param(
    [string]$Question = "牛奶适合早餐吗"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot
$env:PYTHONIOENCODING = "utf-8"
$OutputEncoding = [System.Text.UTF8Encoding]::new()
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new()

if (!$env:RETAIL_LLM_API_KEY) {
    Write-Host "ERROR: 未检测到 RETAIL_LLM_API_KEY。请先运行 .\configure_deepseek_api.ps1" -ForegroundColor Red
    exit 1
}

if (!$env:RETAIL_LLM_BASE_URL) {
    $env:RETAIL_LLM_BASE_URL = "https://api.deepseek.com"
}
if (!$env:RETAIL_LLM_MODEL) {
    $env:RETAIL_LLM_MODEL = "deepseek-v4-flash"
}

Write-Host "Testing DeepSeek API..." -ForegroundColor Cyan
Write-Host "Base URL: $env:RETAIL_LLM_BASE_URL"
Write-Host "Model: $env:RETAIL_LLM_MODEL"
Write-Host ""

python tools\voice_retail_assistant.py --mode llm --question $Question
