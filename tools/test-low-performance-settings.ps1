$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'low-performance-settings.ps1')
$fixture=Join-Path $root ('logs/low-performance-restore-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$config=Join-Path $fixture 'TheGreatCircleConfig.local'
[IO.File]::WriteAllText($config,"r_lodScaleRaytracing `"0.5`"`r`notherSetting `"before`"`r`n")
$saved=Save-LowPerformanceSettings $root $fixture $fixture
[IO.File]::WriteAllText($config,"r_lodScaleRaytracing `"0.1`"`r`notherSetting `"after`"`r`nr_enableDynamicGI `"0`"`r`n")
Restore-LowPerformanceSettings $saved
$actual=[IO.File]::ReadAllText($config)
$expected="r_lodScaleRaytracing `"0.5`"`r`notherSetting `"after`"`r`n"
if($actual -cne $expected){throw 'Restore changed unrelated settings or retained an injected value.'}
Restore-LowPerformanceSettings $saved
if([IO.File]::ReadAllText($config) -cne $expected){throw 'Restore is not idempotent.'}
'PASS: targeted restoration preserves unrelated edits, removes injected keys, retains line endings, and is idempotent.'
