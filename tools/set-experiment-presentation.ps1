param(
    [Parameter(Mandatory)][string]$RunDirectory,
    [ValidateSet('live','static')][string]$Output='live'
)
$ErrorActionPreference='Stop'
$run=(Resolve-Path -LiteralPath $RunDirectory).Path
$launch=Get-Content -LiteralPath (Join-Path $run 'launch.json') -Raw | ConvertFrom-Json
if(!$launch.experimentalStereo){throw 'Explicit experimental launch required.'}
$marker=Join-Path $run 'native-pair.hide'
if($Output -eq 'live'){[IO.File]::WriteAllText($marker,'live cinema')}
elseif(Test-Path -LiteralPath $marker){Remove-Item -LiteralPath $marker}
Write-Host "Experimental presentation: $Output. Static means the saved stereo pair; live means mono cinema."
