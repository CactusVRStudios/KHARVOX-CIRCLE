$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'settings-path.ps1')
$testRoot=Join-Path ([IO.Path]::GetTempPath()) ('CircleSettingsPS-'+[guid]::NewGuid().ToString('N'))
try {
    $old=Join-Path $testRoot 'KHARVOX-CIRCLE-old'
    $next=Join-Path $testRoot 'KHARVOX-CIRCLE-new'
    [IO.Directory]::CreateDirectory($old)|Out-Null
    [IO.Directory]::CreateDirectory($next)|Out-Null
    $shared=Join-Path $testRoot 'user/circle-settings.json'
    $original='{"vr_render_scale":1.25,"future_key":{"keep":true}}'
    [IO.File]::WriteAllText((Join-Path $old 'circle-settings.json'),$original)
    [IO.File]::WriteAllText((Join-Path $old 'circle-settings.json.bak'),'{}')
    [IO.File]::WriteAllText((Join-Path $next 'circle-settings.defaults.json'),'{}')
    $resolved=Resolve-CircleSettingsPath -Root $next -SharedPath $shared
    if($resolved -ne $shared -or [IO.File]::ReadAllText($shared) -ne $original){throw 'Previous release migration failed'}
    [IO.File]::WriteAllText((Join-Path $next 'circle-settings.json'),'{}')
    $null=Resolve-CircleSettingsPath -Root $next -SharedPath $shared
    if([IO.File]::ReadAllText($shared) -ne $original){throw 'Shared settings overwritten'}
    [IO.File]::WriteAllText($shared,'{broken')
    $null=Resolve-CircleSettingsPath -Root $next -SharedPath $shared
    if([IO.File]::ReadAllText($shared) -ne '{broken'){throw 'Invalid shared data silently replaced'}
    $invalid=Join-Path $testRoot 'invalid'
    [IO.Directory]::CreateDirectory($invalid)|Out-Null
    [IO.File]::WriteAllText((Join-Path $invalid 'circle-settings.json'),'[]')
    $absent=Join-Path $testRoot 'absent.json'
    $rejected=$false
    try {$null=Resolve-CircleSettingsPath -Root $invalid -SharedPath $absent}catch{$rejected=$true}
    if(!$rejected -or (Test-Path -LiteralPath $absent)){throw 'Invalid migration accepted'}
    Write-Output 'PASS: shared settings migration, precedence, unknown keys and invalid-file preservation.'
} finally {
    $resolvedRoot=[IO.Path]::GetFullPath($testRoot)
    $tempBase=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
    if(!$resolvedRoot.StartsWith($tempBase,[StringComparison]::OrdinalIgnoreCase) -or !(Split-Path -Leaf $resolvedRoot).StartsWith('CircleSettingsPS-')){throw 'Unexpected test cleanup path'}
    if(Test-Path -LiteralPath $resolvedRoot){Remove-Item -LiteralPath $resolvedRoot -Recurse -Force}
}
