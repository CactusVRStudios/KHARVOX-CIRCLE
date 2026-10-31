[CmdletBinding()]
param([string]$Destination,[switch]$VerifyOnly,[bool]$EnableLogging=$false)
$ErrorActionPreference='Stop'
if(!$Destination){$Destination=Join-Path (Split-Path -Parent $PSScriptRoot) 'runtimes/dlss-unlocked-0.4.0'}
# Fetch the specific backend used by DLSS-Unlocked, without its desktop proxy,
# OptiScaler, registry edits, SR replacements or unrelated render injectors.
$revision='6dcfd54c51e7fc2eb4fe25e5bb16044809c647b5'
$base="https://raw.githubusercontent.com/sdli1995/dlssg_for_sm86/$revision"
$files=[ordered]@{
 'version.dll'='7B3F90435BE4F94348BF8D128C84871C7F904C704F7690601A263277C327A7B2'
 'dlssg_sm86/backend/sm86_backend.dll'='30FFC6E09A347CCDE87ECC6FA54FD7818F5ED8A9353F5C31C2A90835C7F0CA9B'
 'dlssg_sm86/kernels/fg_kernels.dll'='1394C2D5D9CC8938BA3E52B043048D20D6D4C13A019C161B11A3DFD16343EC24'
 'dlssg_sm86/runtime/nvngx_dlssg.dll'='CADEE1AA6DB3907BABDC3285DA2AEBC4553D92B89C38BA3EAE8ABD8BBA1BEA72'
}
$Destination=[IO.Path]::GetFullPath($Destination)
foreach($profile in @('rtx20-30','rtx40')) {
 foreach($entry in $files.GetEnumerator()) {
  $target=Join-Path (Join-Path $Destination $profile) $entry.Key
  if(Test-Path -LiteralPath $target) {
   if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $entry.Value){throw "Community runtime changed: $target. Move that runtime folder aside before reinstalling."}
   continue
  }
  if($VerifyOnly){throw "Bundled community runtime missing: $target. Extract the complete CIRCLE offline package again."}
  New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
  $temp=$target+'.'+[guid]::NewGuid().ToString('N')+'.download'
  try {
   $cached=Join-Path (Join-Path $Destination 'rtx20-30') $entry.Key
   if($profile -eq 'rtx40' -and (Test-Path -LiteralPath $cached)){Copy-Item -LiteralPath $cached -Destination $temp}
   else {Invoke-WebRequest -UseBasicParsing -Uri "$base/$($entry.Key)" -OutFile $temp}
   if((Get-FileHash -LiteralPath $temp -Algorithm SHA256).Hash -ne $entry.Value){throw "Community runtime checksum mismatch: $($entry.Key)"}
   Move-Item -LiteralPath $temp -Destination $target
  } finally {if(Test-Path -LiteralPath $temp){Remove-Item -LiteralPath $temp}}
 }
 $force=if($profile -eq 'rtx40'){1}else{0}
 # No spoofing to the game or SR. The forced SM86 route on Ada is experimental.
 $loggingLevel=if($EnableLogging){2}else{0}
 $config=@"
[General]
Enabled=1
[FrameGeneration]
Engine=DLSS
MaxGeneratedFrames=3
[DLSSG]
Optimized=1
Preset=Auto
[Compatibility]
SpoofArchToGame=0
ForceSM86Route=$force
AccelerateNewerGpus=0
[Runtime]
Mode=Bundled
[SuperResolution]
Mode=Off
[Logging]
Level=$loggingLevel
Directory=logs
"@
 $ini=Join-Path (Join-Path $Destination $profile) 'dlssg_sm86.ini'
 [IO.File]::WriteAllText($ini,$config,[Text.Encoding]::ASCII)
}
Write-Output "Community DLSS runtime verified: $Destination (upstream $revision)"

