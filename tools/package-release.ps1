param([Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9-]+$')][string]$Name,[string]$SettingsFile)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$destination=Join-Path $root ('releases/'+$Name)
$zip=$destination+'.zip'
if((Test-Path -LiteralPath $destination) -or (Test-Path -LiteralPath $zip)){throw 'Release already exists; choose a new name.'}
if((Get-FileHash -LiteralPath (Join-Path $root 'bin/CircleFidelityFX_DX12.dll') -Algorithm SHA256).Hash -ne '12A5081257EC95B0B53AD51B4A87FB3C03F97FE0BBB59F9496968F8D50EF93A6'){throw 'FidelityFX runtime hash mismatch.'}
# Compile all UI assets, defaults, build catalog, launch policies and attribution
# into the launcher. No resource or script extraction is required at runtime.
& (Join-Path $PSScriptRoot 'build-launcher.ps1') -SettingsFile $SettingsFile
& (Join-Path $PSScriptRoot 'install-community-framegen.ps1') -VerifyOnly -EnableLogging $false
# Runtime-only allowlist. Developer diagnostics and documentation stay local.
$files=@('CircleLauncher.exe','bin/KharvoxCircle.dll','bin/openxr_loader.dll',
 'bin/VK_LAYER_KHARVOX_CIRCLE.json','bin/KharvoxCircleXrDiagnostic.exe',
 'bin/CircleFidelityFX_DX12.dll','bin/CircleAdaptiveFG.dll','bin/KHARVOX CIRCLE Intro.exe')
foreach($profile in @('rtx20-30','rtx40')){
 foreach($file in @('version.dll','dlssg_sm86/backend/sm86_backend.dll','dlssg_sm86/kernels/fg_kernels.dll','dlssg_sm86/runtime/nvngx_dlssg.dll','dlssg_sm86.ini')){
  $files+="runtimes/dlss-unlocked-0.4.0/$profile/$file"
 }
}
foreach($file in $files){if(!(Test-Path -LiteralPath (Join-Path $root $file) -PathType Leaf)){throw "Missing runtime file: $file"}}
foreach($file in $files){
 $target=Join-Path $destination $file
 [IO.Directory]::CreateDirectory((Split-Path -Parent $target)) | Out-Null
 Copy-Item -LiteralPath (Join-Path $root $file) -Destination $target
}
$inventory=@(Get-ChildItem -LiteralPath $destination -Recurse -File | ForEach-Object {
 [pscustomobject]@{path=$_.FullName.Substring($destination.Length+1);bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
if($inventory.Count -ne $files.Count){throw 'Unexpected release file count.'}
foreach($item in $inventory){
 if([IO.Path]::GetExtension($item.path) -notin @('.exe','.dll','.json','.ini')){throw "Non-runtime artifact leaked into release: $($item.path)"}
 if($item.path -match '(^|[\\/])(docs|tools|assets|licenses|third_party|profiles)([\\/]|$)'){throw "Development directory leaked: $($item.path)"}
}
Compress-Archive -LiteralPath $destination -DestinationPath $zip -CompressionLevel Optimal
$audit=Join-Path $root ('logs/'+$Name+'-manifest.json')
[pscustomobject]@{name=$Name;created=(Get-Date).ToString('o');files=$inventory;zipSha256=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash;headsetValidation='pending'} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $audit -Encoding utf8
Write-Output $destination
Write-Output $zip
