param([string]$Snapshot,[int]$GameProcessId)
$ErrorActionPreference='Stop'

function Save-LowPerformanceSettings([string]$Root,[string]$Run,[string]$Directory) {
    $names=Get-Content -LiteralPath (Join-Path $Root 'profiles/low-performance-cvars.json') -Raw | ConvertFrom-Json
    $names=@($names | ForEach-Object {[string]$_})
    $files=@()
    foreach($name in @('TheGreatCircleConfig.cfg','TheGreatCircleConfig.local')){
        $path=Join-Path $Directory $name
        $lines=if(Test-Path -LiteralPath $path){@([IO.File]::ReadAllLines($path))}else{@()}
        $selected=@($lines | Where-Object {$_ -match '^\s*([^\s"]+)\s+' -and $matches[1] -in $names})
        $files+=@{path=$path;lines=$selected}
    }
    $target=Join-Path $Run 'low-performance-settings.json'
    @{names=$names;files=$files} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $target -Encoding UTF8
    return $target
}

function Restore-LowPerformanceSettings([string]$Path) {
    $saved=Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    foreach($file in $saved.files){
        if(!(Test-Path -LiteralPath $file.path)){continue}
        $before=[IO.File]::ReadAllText($file.path)
        $newline=if($before.Contains("`r`n")){"`r`n"}else{"`n"}
        $old=@{};foreach($line in $file.lines){if($line -match '^\s*([^\s"]+)\s+'){$old[$matches[1]]=[string]$line}}
        $seen=@{};$result=[Collections.Generic.List[string]]::new()
        foreach($line in [regex]::Split($before,'\r?\n')){
            if($line -match '^\s*([^\s"]+)\s+' -and $matches[1] -in $saved.names){
                $name=$matches[1]
                if($old.ContainsKey($name) -and !$seen.ContainsKey($name)){$result.Add($old[$name]);$seen[$name]=$true}
            }else{$result.Add($line)}
        }
        foreach($name in $old.Keys){if(!$seen.ContainsKey($name)){$result.Add($old[$name])}}
        $after=$result -join $newline
        if($after -ne $before){[IO.File]::WriteAllText($file.path,$after,[Text.UTF8Encoding]::new($false))}
    }
    Set-Content -LiteralPath ($Path+'.restored') -Value 'Preset keys restored; unrelated settings retained.'
}

if($Snapshot){
    try {
        $game=Get-Process -Id $GameProcessId -ErrorAction SilentlyContinue
        if($game){$game.WaitForExit()}
        Restore-LowPerformanceSettings $Snapshot
    }catch{$_ | Out-String | Set-Content -LiteralPath ($Snapshot+'.error');exit 1}
}
