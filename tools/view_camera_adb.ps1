$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Script = Join-Path $ProjectRoot "tools\view_camera_adb.py"

python $Script --width 800 --height 600
