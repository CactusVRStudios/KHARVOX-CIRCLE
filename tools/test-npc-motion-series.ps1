param([string]$FixtureDirectory)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'npc-measurement-common.ps1')
$rows=@();$id=0
foreach($eye in 0,1){
 $rows+=[pscustomobject]@{event='temporal_camera';session=1;eye=$eye;token=1234;backend=10+$eye}
 $rows+=[pscustomobject]@{event='temporal_evaluate';session=1;eye=$eye;result=0}
 $rows+=[pscustomobject]@{event='submit';session=1;cb="cb$eye";commandGeneration=100+$eye}
 foreach($kind in 'depth','motion','input','output'){
  $id++
  $rows+=[pscustomobject]@{event='temporal_image';session=1;eye=$eye;token=1234;backend=10+$eye;kind=$kind;status='recorded';id=$id;bytes=1024;after=($kind -eq 'output');cb="cb$eye";commandGeneration=100+$eye}
  $rows+=[pscustomobject]@{event='gpu_complete';session=1;captureSession=1;id=$id;bytes=1024;file="gpu-buffer-$id.bin"}
 }
}
if(!(Test-NpcCaptureRows $rows 1)){throw 'Valid pair rejected'}
if(Test-NpcCaptureRows @($rows|Where-Object { !($_.event -eq 'gpu_complete' -and $_.id -eq 8) }) 1){throw 'Missing GPU completion accepted'}
if(Test-NpcCaptureRows @($rows|Where-Object { !($_.event -eq 'submit' -and $_.cb -eq 'cb1') }) 1){throw 'Missing submission accepted'}
foreach($fault in 'token','backend','command','stage','completion','truncated'){
 $copy=@(($rows|ConvertTo-Json -Depth 5)|ConvertFrom-Json)
 switch($fault){
  token {($copy|Where-Object {$_.event -eq 'temporal_camera' -and $_.eye -eq 1}).token=1235}
  backend {($copy|Where-Object {$_.event -eq 'temporal_camera' -and $_.eye -eq 1}).backend=12}
  command {($copy|Where-Object {$_.event -eq 'temporal_image' -and $_.id -eq 8}).commandGeneration=999}
  stage {($copy|Where-Object {$_.event -eq 'temporal_image' -and $_.id -eq 8}).after=$false}
  completion {($copy|Where-Object {$_.event -eq 'gpu_complete' -and $_.id -eq 8}).bytes=1000}
  truncated {$copy+=[pscustomobject]@{event='truncated';session=1}}
 }
 $rejected=$false
 try {$null=Test-NpcCaptureRows $copy 1} catch {$rejected=$true}
 if(!$rejected){throw "Fault accepted: $fault"}
}
$tmp=Join-Path ([IO.Path]::GetTempPath()) ('npc-series-test-'+[Guid]::NewGuid().ToString()+'.jsonl')
$writer=[IO.File]::Open($tmp,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite)
try {
 $bytes=[Text.Encoding]::UTF8.GetBytes("{`"event`":`"first`"}`n{`"event`":")
 $writer.Write($bytes,0,$bytes.Length);$writer.Flush()
 $read=@(Read-NpcCaptureRows $tmp)
 if($read.Count -ne 1 -or $read[0].event -ne 'first'){throw 'Partial-record handling failed'}
 $bytes=[Text.Encoding]::UTF8.GetBytes("`"second`"}`n")
 $writer.Write($bytes,0,$bytes.Length);$writer.Flush()
 $read=@(Read-NpcCaptureRows $tmp)
 if($read.Count -ne 2){throw 'Concurrent writer/read sharing failed'}
} finally {$writer.Dispose();Remove-Item -LiteralPath $tmp}
Write-Output 'PASS: valid pair, incomplete data, six corrupted cases, shared writer and partial JSON records.'
if($FixtureDirectory){
 @([pscustomobject]@{event='temporal_start';session=1})+$rows|ForEach-Object {$_|ConvertTo-Json -Compress}|Set-Content -LiteralPath (Join-Path $FixtureDirectory 'fixture.jsonl')
}
