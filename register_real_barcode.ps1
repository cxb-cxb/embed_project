param(
    [Parameter(Mandatory = $true)]
    [string]$ProductId,

    [Parameter(Mandatory = $true)]
    [string]$Barcode
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$adb = Join-Path $projectRoot "tools\adb\adb.exe"
if (-not (Test-Path $adb)) {
    $adb = "adb"
}

Push-Location $projectRoot
try {
    python tools\register_real_barcode.py $ProductId $Barcode
    & $adb shell "mkdir -p /userdata/Embed_project/data" | Out-Host
    & $adb push "data\barcode_aliases.csv" "/userdata/Embed_project/data/barcode_aliases.csv" | Out-Host
    Write-Host "Barcode alias pushed to board."
    Write-Host "Restart scanner to reload aliases: sh scripts/qr_realtime_lvds.sh"
}
finally {
    Pop-Location
}
