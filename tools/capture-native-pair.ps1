param([Parameter(Mandatory)][string]$RunDirectory)
$ErrorActionPreference='Stop'
$run=(Resolve-Path -LiteralPath $RunDirectory).Path
$launch=Get-Content -LiteralPath (Join-Path $run 'launch.json') -Raw | ConvertFrom-Json
if(!$launch.experimentalStereo){throw 'Explicit experimental launch required.'}
if(Test-Path -LiteralPath (Join-Path $run 'native-pair.rgba')){throw 'This run already has a static stereo pair.'}
function Read-Rig {
    try {Get-Content -LiteralPath (Join-Path $run 'native-pair-rig.json') -Raw -ErrorAction Stop | ConvertFrom-Json} catch {$null}
}
function Wait-Rig([int]$eye,[long]$minimum) {
    $deadline=[DateTime]::UtcNow.AddSeconds(40)
    do {
        $rig=Read-Rig
        if($rig -and $rig.native_pause -and $rig.mismatches -eq 0 -and $rig.verified_eye_copies[$eye] -ge $minimum){return $rig}
        Start-Sleep -Milliseconds 200
    } while([DateTime]::UtcNow -lt $deadline)
    throw 'Native pause/eye-copy prerequisite did not become ready. No pair submitted.'
}
foreach($eye in 0..1) {
    $name=if($eye -eq 0){'left'}else{'right'}
    $before=Read-Rig
    $previous=if($before){[long]$before.verified_eye_copies[$eye]}else{0L}
    & (Join-Path $PSScriptRoot 'set-experiment-camera.ps1') -RunDirectory $run -Operation $name
    $ready=Wait-Rig $eye ($previous+150)
    # Hold this native viewpoint for >150 copies before requesting the existing GPU capture.
    $requested=[DateTime]::Now
    & (Join-Path $PSScriptRoot 'set-experiment-camera.ps1') -RunDirectory $run -Operation $name -Capture
    $deadline=[DateTime]::UtcNow.AddSeconds(15)
    $sidecar=$null
    do {
        $sidecar=Get-ChildItem -LiteralPath (Join-Path $run 'captures') -Filter '*.txt' -File -ErrorAction SilentlyContinue | Where-Object LastWriteTime -ge $requested | Sort-Object LastWriteTime | Select-Object -First 1
        if(!$sidecar){Start-Sleep -Milliseconds 150}
    } while(!$sidecar -and [DateTime]::UtcNow -lt $deadline)
    if(!$sidecar){throw 'Synchronized GPU capture did not finish.'}
    $png=Join-Path $sidecar.DirectoryName ($sidecar.BaseName+'-mono.png')
    if(!(Test-Path -LiteralPath $png)){throw 'Completed image missing.'}
    $after=Wait-Rig $eye ($previous+150)
    if($ready.display_time -ne $after.display_time -or $ready.latched_owner -ne $after.latched_owner){throw 'Pause epoch changed during capture.'}
    Copy-Item -LiteralPath $png -Destination (Join-Path $run "native-$name.png")
    Copy-Item -LiteralPath $sidecar.FullName -Destination (Join-Path $run "native-$name-transport.txt")
    $after | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $run "native-$name-rig.json") -Encoding utf8
    Write-Host "Native $name capture completed; epoch $($after.display_time), verified copies $($after.verified_eye_copies[$eye])."
}
$python='C:\Users\Bernd\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
& $python (Join-Path $PSScriptRoot 'pack-native-pair.py') --left (Join-Path $run 'native-left.png') --right (Join-Path $run 'native-right.png') --left-rig (Join-Path $run 'native-left-rig.json') --right-rig (Join-Path $run 'native-right-rig.json') --out (Join-Path $run 'native-pair.rgba')
if($LASTEXITCODE -ne 0){throw 'Native pair validation/packing failed; no projection permitted.'}
& (Join-Path $PSScriptRoot 'set-experiment-camera.ps1') -RunDirectory $run -Operation none
