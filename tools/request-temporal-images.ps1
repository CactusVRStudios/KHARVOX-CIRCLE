param(
 [Parameter(Mandatory=$true)][string]$RunDirectory,
 [ValidateRange(0,1)][double]$CenterX=0.5,
 [ValidateRange(0,1)][double]$CenterY=0.5
)
$ErrorActionPreference='Stop'
$run=(Resolve-Path -LiteralPath $RunDirectory).Path
$launch=Get-Content -LiteralPath (Join-Path $run 'launch.json') -Raw | ConvertFrom-Json
if(!$launch.diagnostic_temporal_images){throw 'Requires a launch with -DiagnosticTemporalImages.'}
$text=$CenterX.ToString('R',[Globalization.CultureInfo]::InvariantCulture)+' '+$CenterY.ToString('R',[Globalization.CultureInfo]::InvariantCulture)
[IO.File]::WriteAllText((Join-Path $run 'temporal-images.request'),$text)
Write-Output 'Requested one stereo pair, up to 512x512 pixels per resource at the selected normalized image center. Inspect temporal_image statuses and GPU completions; an armed request is not a successful capture.'
