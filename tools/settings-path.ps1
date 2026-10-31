function Resolve-CircleSettingsPath {
    param([Parameter(Mandatory=$true)][string]$Root,
          [string]$SharedPath=(Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'KHARVOX CIRCLE/circle-settings.json'))
    if(Test-Path -LiteralPath $SharedPath -PathType Leaf){return $SharedPath}
    $source=Join-Path $Root 'circle-settings.json'
    if(!(Test-Path -LiteralPath $source -PathType Leaf)){
        $parent=Split-Path -Parent $Root.TrimEnd('\','/')
        $candidates=@(Get-ChildItem -LiteralPath $parent -Directory -Filter 'KHARVOX-CIRCLE*' -ErrorAction SilentlyContinue | ForEach-Object {
            $file=Join-Path $_.FullName 'circle-settings.json'
            if(Test-Path -LiteralPath $file -PathType Leaf){Get-Item -LiteralPath $file}
        })
        $candidate=$candidates | Sort-Object @{Expression={Test-Path -LiteralPath ($_.FullName+'.bak')};Descending=$true},@{Expression={$_.LastWriteTimeUtc};Descending=$true},FullName | Select-Object -First 1
        $source=if($candidate){$candidate.FullName}else{Join-Path $Root 'circle-settings.defaults.json'}
    }
    $text=if(Test-Path -LiteralPath $source -PathType Leaf){[IO.File]::ReadAllText($source)}else{'{}'}
    $value=ConvertFrom-Json -InputObject $text -ErrorAction Stop
    if(!$text.TrimStart().StartsWith('{') -or $null -eq $value){throw "Settings must contain a JSON object: $source"}
    [IO.Directory]::CreateDirectory((Split-Path -Parent $SharedPath)) | Out-Null
    $temporary=$SharedPath+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
    try {
        [IO.File]::WriteAllText($temporary,$text,(New-Object Text.UTF8Encoding($false)))
        try {[IO.File]::Move($temporary,$SharedPath)}catch{if(!(Test-Path -LiteralPath $SharedPath -PathType Leaf)){throw}}
    }finally{if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary}}
    return $SharedPath
}
