param(
    [string]$ApiKey = "",
    [string]$Model = "deepseek-v4-flash",
    [string]$BaseUrl = "https://api.deepseek.com"
)

$ErrorActionPreference = "Stop"

if (!$ApiKey.Trim()) {
    $secure = Read-Host "请输入 DeepSeek API Key" -AsSecureString
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try {
        $ApiKey = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
    } finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
    }
}

if (!$ApiKey.Trim()) {
    Write-Host "ERROR: API Key 不能为空。" -ForegroundColor Red
    exit 1
}

[Environment]::SetEnvironmentVariable("RETAIL_LLM_API_KEY", $ApiKey, "User")
[Environment]::SetEnvironmentVariable("RETAIL_LLM_BASE_URL", $BaseUrl, "User")
[Environment]::SetEnvironmentVariable("RETAIL_LLM_MODEL", $Model, "User")

$env:RETAIL_LLM_API_KEY = $ApiKey
$env:RETAIL_LLM_BASE_URL = $BaseUrl
$env:RETAIL_LLM_MODEL = $Model

Write-Host ""
Write-Host "DeepSeek API 配置完成。" -ForegroundColor Green
Write-Host "RETAIL_LLM_BASE_URL=$BaseUrl"
Write-Host "RETAIL_LLM_MODEL=$Model"
Write-Host "RETAIL_LLM_API_KEY=已保存到当前 Windows 用户环境变量"
Write-Host ""
Write-Host "注意：已经打开的旧 PowerShell 窗口可能读取不到新的用户环境变量，建议重新打开一个 PowerShell 后测试。"
