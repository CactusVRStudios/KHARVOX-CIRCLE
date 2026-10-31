param(
    [Parameter(Mandatory)][string]$RunDirectory,
    [ValidateSet('none','yaw','pitch','roll','position','left','right')][string]$Operation='none',
    [switch]$Capture
)
$ErrorActionPreference='Stop'
$run=(Resolve-Path -LiteralPath $RunDirectory).Path
$launch=Get-Content -LiteralPath (Join-Path $run 'launch.json') -Raw | ConvertFrom-Json
if(!$launch.experimentalStereo) {throw 'This run did not explicitly enable experimental stereo.'}
$temporary=Join-Path $run 'experiment-camera.tmp'
[IO.File]::WriteAllText($temporary,$Operation)
Move-Item -LiteralPath $temporary -Destination (Join-Path $run 'experiment-camera.txt') -Force
if($Capture) {[IO.File]::WriteAllText((Join-Path $run 'experiment-capture.request'),'capture')}
Write-Host "Experimental private-camera operation: $Operation. Original camera memory is preserved."
