param(
    [int]$Seconds = 6,
    [ValidateSet("offline", "auto", "llm")]
    [string]$ReplyMode = "offline",
    [string]$CurrentProduct = ""
)

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $ProjectRoot "start_voice_mic_query_demo.ps1") `
    -Seconds $Seconds `
    -AsrProvider volcengine `
    -ReplyMode $ReplyMode `
    -CurrentProduct $CurrentProduct `
    -Loop
