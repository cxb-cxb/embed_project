param(
    [int]$Seconds = 6,
    [ValidateSet("offline", "auto", "llm")]
    [string]$ReplyMode = "offline",
    [string]$CurrentProduct = "",
    [ValidateSet("terminal", "wav", "board")]
    [string]$VoiceOutput = "terminal",
    [ValidateSet("Main Mic", "Hands Free Mic")]
    [string]$MicPath = "Main Mic",
    [int]$MinRms = 35
)

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $ProjectRoot "start_voice_mic_query_demo.ps1") `
    -Seconds $Seconds `
    -AsrProvider volcengine `
    -ReplyMode $ReplyMode `
    -CurrentProduct $CurrentProduct `
    -VoiceOutput $VoiceOutput `
    -MicPath $MicPath `
    -MinRms $MinRms `
    -Loop
