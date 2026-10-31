# Shared reads must not deny the game's asynchronous diagnostic writer.
function Read-NpcCaptureRows([string]$Path,[long]$Offset=0,[switch]$SkipAddresses) {
    if(!(Test-Path -LiteralPath $Path)){return}
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    try {
        $null=$stream.Seek($Offset,[IO.SeekOrigin]::Begin)
        $reader=[IO.StreamReader]::new($stream)
        $text=$reader.ReadToEnd()
        # Ignore an unfinished trailing record; reread from Offset on next poll.
        $end=$text.LastIndexOf("`n")
        if($end -ge 0){foreach($line in $text.Substring(0,$end).Split("`n")){
            if($line.Trim() -and !($SkipAddresses -and $line.StartsWith('{"event":"buffer_address"'))){ $line | ConvertFrom-Json }
        }}
    } finally { $stream.Dispose() }
}
function Test-NpcCaptureRows($Rows,[int]$Session) {
    $r=@($Rows | Where-Object session -eq $Session)
    if(@($r|Where-Object {$_.event -in @('truncated','submit_error','capture_rejected')}).Count){throw 'Capture rejected/truncated or queue submission failed.'}
    $c=@($r|Where-Object event -eq 'temporal_camera')
    if($c.Count -ne 2){return $false}
    $l=@($c|Where-Object eye -eq 0);$right=@($c|Where-Object eye -eq 1)
    if($l.Count -ne 1 -or $right.Count -ne 1 -or !$l[0].token -or $l[0].token -ne $right[0].token -or $l[0].backend+1 -ne $right[0].backend){throw 'Mixed or nonadjacent stereo camera identities.'}
    foreach($eye in 0,1){
        $camera=@($c|Where-Object eye -eq $eye)[0]
        $eval=@($r|Where-Object {$_.event -eq 'temporal_evaluate' -and $_.eye -eq $eye})
        if($eval.Count -ne 1){return $false}
        if($eval[0].result -ne 0){throw 'Temporal evaluation failed.'}
        $identities=@()
        foreach($kind in 'depth','motion','input','output'){
            $im=@($r|Where-Object {$_.event -eq 'temporal_image' -and $_.eye -eq $eye -and $_.kind -eq $kind})
            if(!$im.Count){return $false}
            if($im.Count -ne 1 -or $im[0].status -ne 'recorded'){throw "Invalid $eye/$kind image."}
            $x=$im[0]
            if($x.token -ne $camera.token -or $x.backend -ne $camera.backend -or $x.after -ne ($kind -eq 'output')){throw 'Mixed image identity/stage.'}
            $identities+= "$($x.cb)/$($x.commandGeneration)"
            $done=@($Rows|Where-Object {$_.event -eq 'gpu_complete' -and $_.captureSession -eq $Session -and $_.id -eq $x.id})
            if(!$done.Count){return $false}
            if($done.Count -ne 1 -or $done[0].bytes -ne $x.bytes -or $done[0].file -ne "gpu-buffer-$($x.id).bin"){throw 'Invalid GPU completion.'}
        }
        if(@($identities|Select-Object -Unique).Count -ne 1){throw 'Stages from different command recordings.'}
        $submits=@($r|Where-Object {$_.event -eq 'submit' -and "$($_.cb)/$($_.commandGeneration)" -eq $identities[0]})
        if($submits.Count -ne 1){return $false}
    }
    return $true
}

