[CmdletBinding()]
param([switch]$ValidateOnly)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$release=Join-Path $root 'releases/KHARVOX-CIRCLE-Phase8-r315'
$launcher=Join-Path $release 'tools/launch.ps1'
$settings=Join-Path $env:LOCALAPPDATA 'KHARVOX CIRCLE/circle-settings.json'
$runtime='D:\Games\dampf\steamapps\common\SteamVR\steamxr_win64.json'
foreach($path in @($launcher,$settings,$runtime)){
    if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing diagnostic dependency: $path"}
}
$arguments=@{
    Mode='cinema'; Stereo=$true; Runtime='active'; RuntimeJson=$runtime
    GameDirectory='D:\Games\dampf\steamapps\common\The Great Circle'
    SettingsFile=$settings; DiagnosticGpuBindings=$true; DiagnosticTemporalImages=$true
    TemporalImagePairs=12; Upscaling='dlss-sr'
}
if($ValidateOnly){
    [pscustomobject]@{launcher=$launcher;arguments=$arguments;stopsRunningGame=$false;changesSavedSettings=$false} | ConvertTo-Json -Depth 4
    return
}
if(Get-Process -Name TheGreatCircle -ErrorAction SilentlyContinue){
    throw 'Please exit the game normally before this diagnostic launch. No running process was stopped.'
}
& $launcher @arguments
