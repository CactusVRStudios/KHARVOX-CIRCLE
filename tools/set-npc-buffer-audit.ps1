param(
 [Parameter(Mandatory=$true)][string]$LogDirectory,
 [Parameter(Mandatory=$true)][ValidateSet('On','Off')][string]$State
)
$ErrorActionPreference='Stop'
$target=(Resolve-Path -LiteralPath $LogDirectory).Path
if(!(Get-ChildItem -LiteralPath $target -Filter 'circle-*.log' -File)){
 throw 'Select the run log directory containing circle-*.log.'
}
$value=if($State -eq 'On'){'1'}else{'0'}
[IO.File]::WriteAllText((Join-Path $target 'npc-buffer-audit.txt'),$value)
Write-Output "NPC buffer audit $State requested. Runtime poll applies within about one second; maximum 60000 events per process. No rendering comparison switches changed."
