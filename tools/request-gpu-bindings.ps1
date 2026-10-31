[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RunDirectory,
    [string]$RangesFile,
    [switch]$Inventory,
    [ValidatePattern('^(0x)?[0-9a-fA-F]+$')][string]$Pipeline,
    [switch]$PhysicalRanges,
    [ValidateRange(0,65535)][int]$Pass = 0,
    [switch]$ViewConstants,
    [switch]$BeforePass,
    [switch]$SkinningInputs,
    [ValidateRange(0,1024)][int]$SkinningBatch=0,
    [switch]$NpcConsumers,
    [switch]$PairedNpcConsumers,
    [ValidateRange(0,16384)][int]$RenderWidth=0,
    [ValidateRange(0,16384)][int]$RenderHeight=0,
    [uint32]$AttachmentSignature=0,
    [ValidateRange(0,151552)][uint32]$MvpIndexPlusOne=0
)
$ErrorActionPreference='Stop'
$run=(Resolve-Path -LiteralPath $RunDirectory).Path
$launch=Get-Content -LiteralPath (Join-Path $run 'launch.json') -Raw | ConvertFrom-Json
if(!$launch.diagnostic_gpu_bindings){throw 'This run was not launched with -DiagnosticGpuBindings.'}
if($Inventory -and $RangesFile){throw 'Choose inventory OR targeted ranges.'}
if(!$Inventory -and !$RangesFile){throw 'r188 requires -Inventory (no copies) or -RangesFile with verified current-run ranges.'}
if($Inventory -and $PhysicalRanges){throw 'Inventory performs no physical-range copies.'}
if($PhysicalRanges -and !$Pipeline){throw 'Physical ranges require a verified current-run pipeline and a requested descriptor-bound anchor buffer.'}
if($BeforePass -and (!$Pipeline -or !$Pass -or $Inventory -or $ViewConstants)){throw 'BeforePass requires explicit pipeline/pass/ranges and cannot combine with ViewConstants.'}
if($ViewConstants -and (!$Pipeline -or !$Pass -or $Inventory)){throw 'ViewConstants requires targeted ranges, an explicit pipeline and pass.'}
if($Pass -and !$Pipeline){throw 'A pass filter requires a verified pipeline.'}
if($MvpIndexPlusOne -and (!$PairedNpcConsumers -or !$ViewConstants -or $Inventory)){throw 'MVP capture requires paired NPC consumers and live view constants.'}
if($Pipeline){
    $catalog=Join-Path $run 'shaders/pipelines.jsonl'
    $wanted=[Convert]::ToUInt64(($Pipeline -replace '^0x',''),16)
    if(!$wanted){throw 'Pipeline must be nonzero.'}
    $found=$false
    foreach($row in Get-Content -LiteralPath $catalog){
        $entry=$row|ConvertFrom-Json
        if($entry.pipeline -and [Convert]::ToUInt64(($entry.pipeline -replace '^0x',''),16) -eq $wanted){$found=$true;break}
    }
    if(!$found){throw 'Pipeline is absent from this run shader catalog.'}
}
if($SkinningInputs){
    if(!$Pipeline -or $Inventory -or ($Pass -and !$PairedNpcConsumers) -or $BeforePass -or ($ViewConstants -and !$PairedNpcConsumers)){throw 'SkinningInputs requires targeted compute pipeline and ranges.'}
    if(!($entry.stages | Where-Object { $_.module -eq '2d357153fa50c442' })){throw 'SkinningInputs requires the verified 2d357153fa50c442 writer.'}
}
$ranges=Join-Path $run 'gpu-binding-ranges.txt'
if($RangesFile){
    $inputPath=(Resolve-Path -LiteralPath $RangesFile).Path
    $rangeLines=@(Get-Content -LiteralPath $inputPath)
    if($rangeLines.Count -lt 1 -or $rangeLines.Count -gt 192){throw 'Specify 1–192 ranges, each at most 64 KiB.'}
    foreach($line in $rangeLines){
        if($line -notmatch '^\s*(?:0x)?[0-9a-fA-F]+\s+(\d+)\s+(\d+)\s+(\d+)\s*$'){throw 'Ranges: hex VkBuffer, decimal offset, byte count, resource generation.'}
        $offset=[uint64]$Matches[1];$size=[uint64]$Matches[2]
        if(!$size -or $size -gt 65536 -or $offset%4 -or $size%4 -or $offset -gt [uint64]::MaxValue-$size){throw 'Ranges must be 4-byte aligned, at most 64 KiB, and cannot overflow.'}
    }
    Copy-Item -LiteralPath $inputPath -Destination $ranges -Force
} elseif(Test-Path -LiteralPath $ranges) {
    Remove-Item -LiteralPath $ranges
}
$requestName=if($Inventory){'gpu-binding-inventory.request'}else{'gpu-binding-audit.request'}
if($NpcConsumers -and !$Inventory){throw 'NpcConsumers is metadata-only.'}
$selection=Join-Path $run 'gpu-binding-pipeline.txt'
if($Pipeline -or $NpcConsumers){
    $filterPipeline=if($Pipeline){$Pipeline}else{'0'}
    [IO.File]::WriteAllText($selection,('{0} {1} {2} {3} {4} {5} {6} {7} {8} {9} {10} {11} {12}' -f $filterPipeline,[int]$PhysicalRanges.IsPresent,$Pass,[int]$ViewConstants.IsPresent,[int]$BeforePass.IsPresent,[int]$SkinningInputs.IsPresent,$SkinningBatch,[int]$NpcConsumers.IsPresent,[int]$PairedNpcConsumers.IsPresent,$RenderWidth,$RenderHeight,$AttachmentSignature,$MvpIndexPlusOne))
} elseif(Test-Path -LiteralPath $selection){Remove-Item -LiteralPath $selection}
[IO.File]::WriteAllText((Join-Path $run $requestName),'capture')
Write-Output 'Requested a single-pair targeted audit. Inventory copies no GPU payloads. Verify completion, both eyes and ownership before interpreting differences.'
