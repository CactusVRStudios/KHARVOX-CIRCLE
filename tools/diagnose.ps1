param(
    [ValidateSet('cinema','attach','off')][string]$Mode = 'cinema',
    [int]$MonitorSeconds = 60,
    [string]$RunDirectory
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (!$RunDirectory) {
    & (Join-Path $PSScriptRoot 'launch.ps1') -Mode $Mode -MonitorSeconds $MonitorSeconds
    $RunDirectory = (Get-ChildItem -LiteralPath (Join-Path $root 'logs') -Directory -Filter "*-$Mode" | Sort-Object Name | Select-Object -Last 1).FullName
}
$launchPath = Join-Path $RunDirectory 'launch.json'
$launch = if (Test-Path -LiteralPath $launchPath) { Get-Content -LiteralPath $launchPath -Raw | ConvertFrom-Json } else { $null }
if (!$launch -and (Test-Path -LiteralPath (Join-Path $RunDirectory 'result.json'))) {
    $launch = Get-Content -LiteralPath (Join-Path $RunDirectory 'result.json') -Raw | ConvertFrom-Json
    $launch | Add-Member -NotePropertyName mode -NotePropertyValue diagnostic
}
$text = (@(Get-ChildItem -LiteralPath $RunDirectory -Filter 'circle-*.log' -File | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join "`n")
$profiles=Get-Content -LiteralPath (Join-Path $root 'profiles/motor-builds.json') -Raw | ConvertFrom-Json
$profilePatterns=@($profiles.profiles | ForEach-Object {[regex]::Escape($_.name)}) -join '|'
$summary = [ordered]@{
    runDirectory = $RunDirectory; mode = $launch.mode; pid = $launch.pid
    aliveAfterMonitor = $launch.aliveAfterMonitor; exitCode = $launch.exitCode
    layerLoaded = $text -match 'KHARVOX CIRCLE layer loaded'
    gamePresents = $text -match 'motor: successful game presents'
    mandatoryRtExtensions = ($text -match 'game device extension: VK_KHR_ray_tracing_pipeline') -and ($text -match 'game device extension: VK_KHR_acceleration_structure') -and ($text -match 'game device extension: VK_KHR_ray_query')
    observedRtDispatch = $text -match 'motor workload: first vkCmdTraceRaysKHR'
    observedAccelerationStructureBuild = $text -match 'motor workload: first vkCmdBuildAccelerationStructuresKHR'
    xrSessionBegun = $text -match 'xr: xrBeginSession: 0'
    gameCopiedIntoRing = $text -match 'presenter: first copy into the ring'
    gameConsumedByXr = $text -match 'xr: first game frame on the screen'
    gameImageLayerSubmitted = $text -match 'motor: first game image layer submitted successfully'
    diagnosticStereoSubmitted = $text -match 'diagnostic: stereo pattern submitted successfully'
    predictedPoseObserved = $text -match 'diagnostic: valid predicted left-eye pose'
    buildProfile = $launch.buildProfile; storefront = $launch.storefront
    nativeStateObserverValidated = $text -match ('hybrid: ('+$profilePatterns+')')
    nativeMainMenuObserved = $text -match 'hybrid: native state 0 valid yes -> MAIN_MENU / CINEMA_QUAD'
    nativeLoadingObserved = $text -match 'hybrid: native state 1 valid yes -> LOADING / LOADING_QUAD'
    unknownIngameKeptInCinema = $text -match 'hybrid: native state 2 valid yes -> UNKNOWN / CINEMA_QUAD'
    safeStereoRequestDenied = $text -match 'hybrid: F7 stereo request denied'
    loadingCardSubmitted = $text -match 'hybrid: stable loading card submitted successfully'
    skipLogoIntrosRequested = $launch.skipLogoIntros
    nativeGameStereo = $false; gameCamera6DoF = $false
    humanHeadsetVerification = 'not inferable from logs; see docs/headset-verification.json'
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $RunDirectory 'summary.json') -Encoding utf8
$summary | ConvertTo-Json
