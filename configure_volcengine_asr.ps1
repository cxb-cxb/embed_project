param(
    [string]$ApiKey = "",
    [string]$AppId = "",
    [string]$ResourceId = "volc.bigasr.auc_turbo",
    [string]$Endpoint = "https://openspeech.bytedance.com/api/v3/auc/bigmodel/recognize/flash"
)

$ErrorActionPreference = "Stop"

if (!$ApiKey.Trim()) {
    $secure = Read-Host "请输入火山引擎豆包语音 ASR API Key / Access Token" -AsSecureString
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

if ($ApiKey.Trim().StartsWith("ark-")) {
    Write-Host ""
    Write-Host "WARNING: 当前输入看起来像火山方舟/大模型推理 API Key，而不是豆包语音 ASR 的 X-Api-Key。" -ForegroundColor Yellow
    Write-Host "如果后续测试返回 Invalid X-Api-Key，请到 火山引擎控制台 -> 豆包语音 获取 ASR 专用 X-Api-Key。"
    Write-Host ""
}

[Environment]::SetEnvironmentVariable("VOLCENGINE_ASR_API_KEY", $ApiKey, "User")
[Environment]::SetEnvironmentVariable("VOLCENGINE_ASR_APP_ID", $AppId, "User")
[Environment]::SetEnvironmentVariable("VOLCENGINE_ASR_RESOURCE_ID", $ResourceId, "User")
[Environment]::SetEnvironmentVariable("VOLCENGINE_ASR_ENDPOINT", $Endpoint, "User")

$env:VOLCENGINE_ASR_API_KEY = $ApiKey
$env:VOLCENGINE_ASR_APP_ID = $AppId
$env:VOLCENGINE_ASR_RESOURCE_ID = $ResourceId
$env:VOLCENGINE_ASR_ENDPOINT = $Endpoint

Write-Host ""
Write-Host "火山引擎豆包语音 ASR 配置完成。" -ForegroundColor Green
Write-Host "VOLCENGINE_ASR_ENDPOINT=$Endpoint"
Write-Host "VOLCENGINE_ASR_RESOURCE_ID=$ResourceId"
if ($AppId.Trim()) {
    Write-Host "VOLCENGINE_ASR_APP_ID=$AppId"
} else {
    Write-Host "VOLCENGINE_ASR_APP_ID=未设置，新版控制台 API Key 模式通常可以为空"
}
Write-Host "VOLCENGINE_ASR_API_KEY=已保存到当前 Windows 用户环境变量"
Write-Host ""
Write-Host "建议重新打开 PowerShell 后运行 .\test_volcengine_asr.ps1"
