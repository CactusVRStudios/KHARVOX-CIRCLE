[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$RunDirectory,
 [Parameter(Mandatory=$true)][string]$RangesFile,
 [Parameter(Mandatory=$true)][string]$Pipeline,
 [ValidateRange(0,65535)][int]$Pass=0,
 [ValidateRange(12,300)][int]$DurationSeconds=180,
 [ValidateRange(12,30)][int]$IntervalSeconds=12
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'npc-measurement-common.ps1')
$run=(Resolve-Path -LiteralPath $RunDirectory).Path
$launch=Get-Content (Join-Path $run 'launch.json') -Raw|ConvertFrom-Json
if(!$launch.diagnostic_gpu_bindings){throw 'Diagnostic GPU run required.'}
$ranges=@(Get-Content -LiteralPath $RangesFile|ForEach-Object {
 $a=$_.Trim() -split '\s+'
 [pscustomobject]@{buffer=$a[0];offset=[long]$a[1];bytes=[long]$a[2];generation=[long]$a[3]}
})
$count=[int][Math]::Ceiling($DurationSeconds/[double]$IntervalSeconds)
$log=Join-Path $run 'gpu-bindings.jsonl'
$existing=@(Read-NpcCaptureRows $log -SkipAddresses)
if(@($existing|Where-Object {$_.event -eq 'gpu_copy' -and $_.status -eq 'recorded'}).Count+(2*$ranges.Count+4)*$count -gt 1024){throw 'Insufficient immutable slots; reduce sample count or restart.'}
foreach($name in 'temporal-images.request','gpu-binding-inventory.request','gpu-binding-audit.request'){
 if(Test-Path (Join-Path $run $name)){throw "Pending request: $name"}
}
$lock=[IO.File]::Open((Join-Path $run 'npc-motion-series.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
$manifest=Join-Path $run ('npc-geometry-series-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.json')
$state=[ordered]@{release='r211';status='recording';pipeline=$Pipeline;pass=$Pass;startedUtc=[DateTime]::UtcNow.ToString('o');samples=@();error=$null}
$timer=[Diagnostics.Stopwatch]::StartNew()
try {
 for($i=0;$i -lt $count;$i++){
  while($timer.Elapsed.TotalSeconds -lt $i*$IntervalSeconds){Start-Sleep -Milliseconds 100}
  if(!(Get-Process -Id $launch.pid -ErrorAction SilentlyContinue)){throw 'Game exited.'}
  if(Test-Path (Join-Path $run 'npc-motion-series.stop')){throw 'Stopped by request.'}
  $offset=(Get-Item $log).Length
  & (Join-Path $PSScriptRoot 'request-gpu-bindings.ps1') -RunDirectory $run -RangesFile $RangesFile -Pipeline $Pipeline -Pass $Pass -PhysicalRanges | Out-Null
  $deadline=[DateTime]::UtcNow.AddSeconds(20);$complete=$false
  do {
   Start-Sleep -Milliseconds 500
   $rows=@(Read-NpcCaptureRows $log $offset -SkipAddresses)
   if(@($rows|Where-Object {$_.event -in @('capture_rejected','truncated','submit_error')}).Count){throw 'Rejected, truncated or failed GPU capture.'}
   $starts=@($rows|Where-Object event -eq 'start')
   if($starts.Count -gt 1){throw 'Concurrent captures.'}
   $copies=@($rows|Where-Object {$_.event -eq 'gpu_copy' -and $_.status -eq 'recorded'})
   $done=@($rows|Where-Object event -eq 'gpu_complete')
   if($starts.Count -eq 1 -and $copies.Count -ge 2*$ranges.Count+4 -and $done.Count -eq $copies.Count){
    $token=$copies[0].token;$left=@($copies|Where-Object eye -eq 0)[0].backend
    foreach($r in $ranges){foreach($eye in 0,1){foreach($phase in 'after_render_pass'){
     $c=@($copies|Where-Object {$_.buffer -eq $r.buffer -and $_.generation -eq $r.generation -and $_.copyOffset -eq $r.offset -and $_.copyBytes -eq $r.bytes -and $_.eye -eq $eye -and $_.phase -eq $phase})
     if($c.Count -ne 1 -or $c[0].token -ne $token -or $c[0].backend -ne $left+$eye){throw 'Incomplete or mixed stereo boundary data.'}
     $d=@($rows|Where-Object {$_.pipeline -eq $Pipeline -and (!$Pass -or $_.pass -eq $Pass) -and $_.cb -eq $c[0].cb -and $_.commandGeneration -eq $c[0].commandGeneration -and $_.token -eq $token -and $_.eye -eq $eye -and !$_.tagMismatch})
     $s=@($rows|Where-Object {$_.event -eq 'submit' -and $_.cb -eq $c[0].cb -and $_.commandGeneration -eq $c[0].commandGeneration -and !$_.descriptorChanged -and !$_.tagMismatch})
     if(!$d.Count -or !$s.Count){throw 'Missing consumer draw or stable submission.'}
    }}}
    $consumerDraws=@($rows|Where-Object {$_.pipeline -eq $Pipeline -and $_.liveIndirectArguments -and $_.token -eq $token})
    if($consumerDraws.Count -ne 2){throw 'Expected exactly one indirect consumer per eye; narrow the pipeline.'}
    foreach($draw in $consumerDraws){foreach($argument in $draw.argumentBindings){
     $captured=@($copies|Where-Object {$_.eye -eq $draw.eye -and $_.cb -eq $draw.cb -and $_.commandGeneration -eq $draw.commandGeneration -and $_.buffer -eq $argument.buffer -and $_.generation -eq $argument.generation -and $_.copyOffset -eq $argument.offset -and $_.copyBytes -eq $argument.size})
     if($captured.Count -ne 1){throw 'Live indirect arguments missing.'}
    }}
    foreach($d in $done){$f=Join-Path $run $d.file;if(!(Test-Path $f) -or (Get-Item $f).Length -ne $d.bytes){throw 'GPU payload incomplete.'}}
    $complete=$true
   }
  } while(!$complete -and [DateTime]::UtcNow -lt $deadline)
  if(!$complete){throw 'No complete consumer stereo capture within 20 seconds; inspect bindings/headset state.'}
  $state.samples+=@{sample=$i+1;session=$starts[0].session;token=$token;elapsedSeconds=$timer.Elapsed.TotalSeconds;copies=$copies.Count}
  $state|ConvertTo-Json -Depth 8|Set-Content $manifest -Encoding utf8
  Write-Output "GPU sample $($i+1)/${count}: both eyes, consumer pass completed."
 }
 while($timer.Elapsed.TotalSeconds -lt $DurationSeconds){Start-Sleep -Milliseconds 100}
 $state.status='complete'
} catch {$state.status='failed';$state.error=$_.Exception.Message;throw}
finally {
 $state.endedUtc=[DateTime]::UtcNow.ToString('o')
 $state|ConvertTo-Json -Depth 8|Set-Content $manifest -Encoding utf8
 $lock.Dispose()
 Write-Output "Geometry measurement $($state.status): $manifest"
}





