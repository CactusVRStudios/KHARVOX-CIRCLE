[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$RunDirectory,
 [ValidateRange(6,180)][int]$DurationSeconds=180,
 [ValidateRange(6,30)][int]$IntervalSeconds=6,
 [ValidateRange(0,1)][double]$CenterX=0.5,
 [ValidateRange(0,1)][double]$CenterY=0.5
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'npc-measurement-common.ps1')
$run=(Resolve-Path -LiteralPath $RunDirectory).Path
$launch=Get-Content -LiteralPath (Join-Path $run 'launch.json') -Raw|ConvertFrom-Json
if(!$launch.diagnostic_temporal_images -or !$launch.diagnostic_gpu_bindings){throw 'Launch with DiagnosticTemporalImages first.'}
$samples=[int][Math]::Ceiling($DurationSeconds/[double]$IntervalSeconds)
$log=Join-Path $run 'gpu-bindings.jsonl'
$existing=@(Read-NpcCaptureRows $log)
$used=@($existing|Where-Object event -eq 'temporal_start').Count
if($used+$samples -gt $launch.temporal_image_pairs){throw 'Insufficient reserved image pairs; restart the diagnostic release.'}
foreach($name in 'temporal-images.request','gpu-binding-inventory.request','gpu-binding-audit.request'){
 if(Test-Path -LiteralPath (Join-Path $run $name)){throw "Pending request: $name"}
}
$lock=[IO.File]::Open((Join-Path $run 'npc-motion-series.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
$manifest=Join-Path $run ('npc-motion-series-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.json')
$state=[ordered]@{release='r207';status='recording';startedUtc=[DateTime]::UtcNow.ToString('o');durationSeconds=$DurationSeconds;intervalSeconds=$IntervalSeconds;center=@($CenterX,$CenterY);classification='unclassified: infer standing/moving from recorded data';samples=@();error=$null}
$timer=[Diagnostics.Stopwatch]::StartNew()
$request=Join-Path $run 'temporal-images.request'
try {
 $state|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $manifest -Encoding utf8
 for($i=0;$i -lt $samples;$i++){
  while($timer.Elapsed.TotalSeconds -lt $i*$IntervalSeconds){Start-Sleep -Milliseconds 100}
  if(!(Get-Process -Id $launch.pid -ErrorAction SilentlyContinue)){throw 'Game process exited.'}
  if(Test-Path -LiteralPath (Join-Path $run 'npc-motion-series.stop')){throw 'Stopped by request.'}
  $offset=if(Test-Path -LiteralPath $log){(Get-Item -LiteralPath $log).Length}else{0L}
  $requested=[DateTime]::UtcNow.ToString('o')
  $elapsed=$timer.Elapsed.TotalSeconds
  & (Join-Path $PSScriptRoot 'request-temporal-images.ps1') -RunDirectory $run -CenterX $CenterX -CenterY $CenterY | Out-Null
  $deadline=[DateTime]::UtcNow.AddSeconds(20);$complete=$false;$session=0
  do {
   Start-Sleep -Milliseconds 500
   $rows=@(Read-NpcCaptureRows $log $offset)
   $start=@($rows|Where-Object event -eq 'temporal_start')
   if($start.Count -gt 1){throw 'Concurrent capture requests; attribution ambiguous.'}
   if($start.Count){$session=[int]$start[0].session;$complete=Test-NpcCaptureRows $rows $session}
   if($complete){break}
  } while([DateTime]::UtcNow -lt $deadline)
  if(!$complete){throw "Sample $($i+1), session ${session}: no complete stereo data within 20 seconds."}
  foreach($done in @($rows|Where-Object {$_.event -eq 'gpu_complete' -and $_.captureSession -eq $session})){
   $file=Join-Path $run $done.file
   if(!(Test-Path -LiteralPath $file) -or (Get-Item -LiteralPath $file).Length -ne $done.bytes){throw 'GPU payload missing or wrong length.'}
  }
  $c=@($rows|Where-Object {$_.event -eq 'temporal_camera' -and $_.eye -eq 0})[0]
  $state.samples+= [ordered]@{sample=$i+1;session=$session;requestedUtc=$requested;elapsedSeconds=$elapsed;token=$c.token;leftBackend=$c.backend;complete=$true}
  $state|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $manifest -Encoding utf8
  Write-Output "Sample $($i+1)/${samples}: session $session, both eyes and all eight GPU images complete."
 }
 while($timer.Elapsed.TotalSeconds -lt $DurationSeconds){Start-Sleep -Milliseconds 100}
 $state.status='complete'
} catch { $state.status='failed';$state.error=$_.Exception.Message;throw }
finally {
 if(Test-Path -LiteralPath $request){Remove-Item -LiteralPath $request}
 $state.endedUtc=[DateTime]::UtcNow.ToString('o')
 $state.elapsedSeconds=$timer.Elapsed.TotalSeconds
 $state|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $manifest -Encoding utf8
 $lock.Dispose()
 Write-Output "Measurement $($state.status): $manifest"
}
