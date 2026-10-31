param()
$ErrorActionPreference='Stop'
Import-Module Microsoft.PowerShell.Utility
$workspace=Split-Path -Parent $PSScriptRoot
$fixture=Join-Path $workspace ('logs/launcher-integration-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture,(Join-Path $fixture 'tools'),(Join-Path $fixture 'bin'),(Join-Path $fixture 'game') | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'launch.ps1') -Destination (Join-Path $fixture 'tools/launch.ps1')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'vr-intro.ps1') -Destination (Join-Path $fixture 'tools/vr-intro.ps1')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'low-performance-settings.ps1') -Destination (Join-Path $fixture 'tools/low-performance-settings.ps1')
Copy-Item -LiteralPath (Join-Path $workspace 'profiles') -Destination (Join-Path $fixture 'profiles') -Recurse
Set-Content -LiteralPath (Join-Path $fixture 'bin/KharvoxCircle.dll') -Value 'Test fixture only; never loaded.'
Set-Content -LiteralPath (Join-Path $fixture 'runtime.json') -Value '{}'
$source=@'
using System;
using System.IO;
using System.Collections;
using System.Web.Script.Serialization;
class MockGame {
 static void Main(string[] args) {
  File.WriteAllText("child-environment.json",new JavaScriptSerializer().Serialize(Environment.GetEnvironmentVariables()));
  File.WriteAllText("child-arguments.txt",String.Join(" ",args));
 }
}
'@
$sourcePath=Join-Path $fixture 'MockGame.cs'
Set-Content -LiteralPath $sourcePath -Value $source
$framework=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
& (Join-Path $framework 'csc.exe') /nologo /target:exe ('/reference:'+(Join-Path $framework 'System.Web.Extensions.dll')) ('/out:'+(Join-Path $fixture 'game/TheGreatCircle.exe')) $sourcePath
if($LASTEXITCODE -ne 0){throw 'Mock compilation failed.'}
# Only the isolated mock's metadata is substituted. The production build gate is unchanged.
$fixtureHash='16EAB3F4827D37EF93111173A831B0F668EF8BDFFA4442C740F2574F23DCC120'
function Get-FileHash { param($LiteralPath,$Algorithm) if($LiteralPath -ne (Join-Path $fixture 'game/TheGreatCircle.exe')){throw 'Unexpected hash request.'};[pscustomobject]@{Hash=$fixtureHash} }
function Get-Item { param($LiteralPath) if($LiteralPath -ne (Join-Path $fixture 'game/TheGreatCircle.exe')){throw 'Unexpected item request.'};[pscustomobject]@{VersionInfo=[pscustomobject]@{FileVersion='1,0,17,0'}} }
function Get-Process { param($Name,$ErrorAction) if($Name -ne 'TheGreatCircle'){throw 'Unexpected process request.'} }
function Require($condition,$message){if(!$condition){throw $message}}
 'param([string]$Destination,[switch]$VerifyOnly,[bool]$EnableLogging=$false); if(!$VerifyOnly){throw "Launch must verify offline"}; Set-Content -LiteralPath (Join-Path $PSScriptRoot "community-installer-called.txt") -Value $Destination' | Set-Content -LiteralPath (Join-Path $fixture 'tools/install-community-framegen.ps1')
$config=Get-Content -LiteralPath (Join-Path $workspace 'circle-settings.json') -Raw | ConvertFrom-Json
# The dedicated intro integration test covers first-run behavior; these existing
# render-mode tests represent a user who has already opted out of the intro.
$config | Add-Member -NotePropertyName vr_intro_started -NotePropertyValue $true -Force
$config | Add-Member -NotePropertyName disable_vr_intro -NotePropertyValue $true -Force
$config.vr_body_yaw_deadzone=31
$config.vr_body_yaw_follow_speed=140
$config.vr_body_yaw_max_residual=81
$config.vr_body_yaw_follow=$false
$config.vr_roomscale_debug=$true
$config | Add-Member -NotePropertyName enable_logging -NotePropertyValue $true -Force
$config.vr_movement_direction='offhand'
$config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
$previousCulture=[Threading.Thread]::CurrentThread.CurrentCulture
try {
 [Threading.Thread]::CurrentThread.CurrentCulture=[Globalization.CultureInfo]::GetCultureInfo('de-DE')
 # Cheat settings reach only the mock child; inherited enable flags are cleared.
 $cheatNames=@('invulnerable','stealth','stamina','oxygen','ammo','damage')
 $parentCheat=[Environment]::GetEnvironmentVariable('CIRCLE_CHEAT_DAMAGE')
 try {
  [Environment]::SetEnvironmentVariable('CIRCLE_CHEAT_DAMAGE','1')
  & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -MonitorSeconds 1
  $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
  foreach($name in $cheatNames){Require ($child.('CIRCLE_CHEAT_'+$name.ToUpperInvariant()) -eq '0') "Cheat default/inheritance failed: $name"}
  foreach($name in $cheatNames){$config | Add-Member -NotePropertyName ('cheat_'+$name) -NotePropertyValue $true -Force}
  $config | Add-Member -NotePropertyName cheat_damage_multiplier -NotePropertyValue 23 -Force
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -MonitorSeconds 1
  $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
  foreach($name in $cheatNames){Require ($child.('CIRCLE_CHEAT_'+$name.ToUpperInvariant()) -eq '1') "Cheat opt-in failed: $name"}
  Require ($child.CIRCLE_CHEAT_DAMAGE_MULTIPLIER -eq '23') 'Cheat multiplier/culture failed.'
  foreach($bad in @('99',$true,0,100,1.5)){
   $config.cheat_damage_multiplier=$bad
   $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
   $refused=$false
   try{& (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json')}catch{Require ($_.Exception.Message -like '*cheat_damage_multiplier*') 'Wrong cheat validation error.';$refused=$true}
   Require $refused 'Invalid cheat multiplier accepted.'
  }
  $config.cheat_damage_multiplier=99;$config.cheat_stealth='true'
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  $refused=$false
  try{& (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json')}catch{Require ($_.Exception.Message -like '*cheat_stealth*') 'Wrong cheat boolean error.';$refused=$true}
  Require $refused 'String cheat boolean accepted.'
 } finally {
  [Environment]::SetEnvironmentVariable('CIRCLE_CHEAT_DAMAGE',$parentCheat)
  foreach($name in $cheatNames){$config.PSObject.Properties.Remove('cheat_'+$name)}
  $config.PSObject.Properties.Remove('cheat_damage_multiplier')
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 }
 # Low Performance Mode is per-child, strict opt-in, and independent of Mono VR.
 # Logging-off must dominate legacy flags and inherited environment settings.
 $config | Add-Member -NotePropertyName vr_low_performance -NotePropertyValue $true -Force
 $config.PSObject.Properties.Remove('enable_logging')
 $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 $oldLogging=[Environment]::GetEnvironmentVariable('CIRCLE_ENABLE_LOGGING')
 try {
  $env:CIRCLE_ENABLE_LOGGING='1'
  foreach($extra in @(@{},@{EnableLogging=$false})){
   # Also verify an explicit off override defeats a saved true setting.
   if($extra.ContainsKey('EnableLogging')){
    $config | Add-Member -NotePropertyName enable_logging -NotePropertyValue $true -Force
    $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
   }
   $before=@(Get-ChildItem (Join-Path $fixture 'logs') -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName+':'+$_.LastWriteTimeUtc.Ticks }) -join '|'
   & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -Stereo -CaptureShaders -MonitorSeconds 1 @extra
   $after=@(Get-ChildItem (Join-Path $fixture 'logs') -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName+':'+$_.LastWriteTimeUtc.Ticks }) -join '|'
   $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
   Require ($child.CIRCLE_ENABLE_LOGGING -eq '0' -and !$child.KHARVOX_CIRCLE_LOG_DIR -and !$child.KHARVOX_CIRCLE_DUMP_SHADERS) 'Logging default/off leaked output environment.'
   Require ($before -eq $after) 'Logging off created or modified session logs.'
   Require ($child.CIRCLE_LOW_PERFORMANCE -eq '0') 'Removed low-performance setting reactivated.'
   Require ($child.CIRCLE_ROOMSCALE_DEBUG -eq '0') 'Logging off retained roomscale diagnostics.'
  }
 } finally {[Environment]::SetEnvironmentVariable('CIRCLE_ENABLE_LOGGING',$oldLogging)}
 $config | Add-Member -NotePropertyName enable_logging -NotePropertyValue 'true' -Force
 $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 $refused=$false
 try{& (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json')}catch{Require ($_.Exception.Message -like '*enable_logging*JSON boolean*') 'Wrong logging validation error.';$refused=$true}
 Require $refused 'Non-boolean logging setting accepted.'
 $config.enable_logging=$true
 $config.PSObject.Properties.Remove('vr_low_performance')
 $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 # Real launch-script/child-process coverage: opt-in, strict bool, override and
 # mode isolation. No real game or window is opened by this fixture.
 $parentHands=[Environment]::GetEnvironmentVariable('CIRCLE_HANDS_ONLY')
 try {
  $env:CIRCLE_HANDS_ONLY='1'
  $config.PSObject.Properties.Remove('vr_hands_only')
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -Stereo -MonitorSeconds 1
  $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
  Require ($child.CIRCLE_HANDS_ONLY -eq '0') 'Legacy Hands Only default/parent isolation failed.'
  Require ($child.CIRCLE_LOW_PERFORMANCE -eq '0') 'Legacy Low Performance Mode must default off.'
  Require ($child.CIRCLE_GI_HISTORY -eq '1') 'Stereo default omitted confirmed GI history isolation.'
  Require (!$child.CIRCLE_NRD_HISTORY -and !$child.CIRCLE_GI_HISTORY_PROBE) 'Unrequested lighting diagnostics leaked into normal launch.'
  $config | Add-Member -NotePropertyName vr_hands_only -NotePropertyValue $true -Force
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  foreach($extra in @(@{},@{HandsOnly=$false},@{Mode='off';Stereo=$false},@{Stereo=$false})){
   $launchFlags=@{Stereo=$true};foreach($key in $extra.Keys){$launchFlags[$key]=$extra[$key]}
   & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -MonitorSeconds 1 @launchFlags
   $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
   $expected=if($extra.Count -eq 0){'1'}else{'0'}
   $expectedGi=if($launchFlags.Stereo -and $extra.Mode -ne 'off'){'1'}else{$null}
   Require ($child.CIRCLE_GI_HISTORY -eq $expectedGi) 'GI history default escaped stereo mode boundary.'
   Require ($child.CIRCLE_HANDS_ONLY -eq $expected) 'Hands Only selection/override/mode gate failed.'
  }
  $config.vr_hands_only='false'
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  $refused=$false
  try{& (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -Stereo}catch{Require ($_.Exception.Message -like '*vr_hands_only*JSON boolean*') 'Wrong Hands Only validation error.';$refused=$true}
  Require $refused 'String Hands Only boolean accepted.'
  $config.vr_hands_only=$false
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 } finally {[Environment]::SetEnvironmentVariable('CIRCLE_HANDS_ONLY',$parentHands)}
 $parentLaser=[Environment]::GetEnvironmentVariable('CIRCLE_LASER_SIGHT')
 try {
  $env:CIRCLE_LASER_SIGHT='1'
  $config.PSObject.Properties.Remove('vr_laser_sight')
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -Stereo -MonitorSeconds 1
  $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
  Require ($child.CIRCLE_LASER_SIGHT -eq '0') 'Legacy Laser Sight default/parent isolation failed.'
  Require ($child.CIRCLE_GI_HISTORY -eq '1') 'Stereo default omitted confirmed GI history isolation.'
  Require (!$child.CIRCLE_NRD_HISTORY -and !$child.CIRCLE_GI_HISTORY_PROBE) 'Unrequested lighting diagnostics leaked into normal launch.'
  $config | Add-Member -NotePropertyName vr_laser_sight -NotePropertyValue $true -Force
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  foreach($extra in @(@{},@{LaserSight=$false},@{Mode='off';Stereo=$false},@{Stereo=$false})){
   $launchFlags=@{Stereo=$true};foreach($key in $extra.Keys){$launchFlags[$key]=$extra[$key]}
   & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -MonitorSeconds 1 @launchFlags
   $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
   $expected=if($extra.Count -eq 0){'1'}else{'0'}
   $expectedGi=if($launchFlags.Stereo -and $extra.Mode -ne 'off'){'1'}else{$null}
   Require ($child.CIRCLE_GI_HISTORY -eq $expectedGi) 'GI history default escaped stereo mode boundary.'
   Require ($child.CIRCLE_LASER_SIGHT -eq $expected) 'Laser Sight selection/override/mode gate failed.'
  }
  $config.vr_laser_sight='false'
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  $refused=$false
  try{& (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -Stereo}catch{Require ($_.Exception.Message -like '*vr_laser_sight*JSON boolean*') 'Wrong Laser Sight validation error.';$refused=$true}
  Require $refused 'String Laser Sight boolean accepted.'
  $config.vr_laser_sight=$false
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 } finally {[Environment]::SetEnvironmentVariable('CIRCLE_LASER_SIGHT',$parentLaser)}
 $parentPerf=[Environment]::GetEnvironmentVariable('CIRCLE_PERFORMANCE_OVERLAY')
 try {
  $env:CIRCLE_PERFORMANCE_OVERLAY='1'
  $config.PSObject.Properties.Remove('vr_performance_overlay')
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -Stereo -MonitorSeconds 1
  $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
  Require ($child.CIRCLE_PERFORMANCE_OVERLAY -eq '0') 'Legacy Performance overlay default/parent isolation failed.'
  $config | Add-Member -NotePropertyName vr_performance_overlay -NotePropertyValue $true -Force
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  foreach($extra in @(@{},@{PerformanceOverlay=$false},@{Mode='off';Stereo=$false},@{Stereo=$false})){
   $launchFlags=@{Stereo=$true};foreach($key in $extra.Keys){$launchFlags[$key]=$extra[$key]}
   & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -MonitorSeconds 1 @launchFlags
   $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
   $expected=if($extra.Count -eq 0){'1'}else{'0'}
   Require ($child.CIRCLE_PERFORMANCE_OVERLAY -eq $expected) 'Performance overlay selection/override/mode gate failed.'
  }
  $config.vr_performance_overlay='false'
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  $refused=$false
  try{& (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -Stereo}catch{Require ($_.Exception.Message -like '*vr_performance_overlay*JSON boolean*') 'Wrong Performance overlay validation error.';$refused=$true}
  Require $refused 'String Performance overlay boolean accepted.'
  $config.vr_performance_overlay=$false
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 } finally {[Environment]::SetEnvironmentVariable('CIRCLE_PERFORMANCE_OVERLAY',$parentPerf)}
 # Legacy settings must default to flat cinema even when the parent opted in.
 $parentMono=[Environment]::GetEnvironmentVariable('CIRCLE_MONO_VR')
 try {
  $env:CIRCLE_MONO_VR='1'
  $config.PSObject.Properties.Remove('vr_mono')
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -Stereo -MonitorSeconds 1
  $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
  Require ($child.CIRCLE_MONO_VR -eq '0') 'Missing Mono VR setting must ignore inherited parent selection.'
  $config | Add-Member -NotePropertyName vr_mono -NotePropertyValue $true -Force
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  foreach($extra in @(@{},@{MonoVR=$false},@{VRProjection='phase5'},@{Mode='off';Stereo=$false})){
   $launchFlags=@{Stereo=$true;StereoCinematics=$true};foreach($key in $extra.Keys){$launchFlags[$key]=$extra[$key]}
   & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -MonitorSeconds 1 @launchFlags
   $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
   $expected=if($extra.Count -eq 0){'1'}else{'0'}
   Require ($child.CIRCLE_MONO_VR -eq $expected) 'Mono VR selection/override/mode gate failed.'
   $argsText=Get-Content -LiteralPath (Join-Path $fixture 'game/child-arguments.txt') -Raw
   if($expected -eq '1' -or !$launchFlags.Stereo){
    Require ($argsText -notmatch 'r_streamlineDLSSGMode') 'Mono VR / flat must preserve native FG setting.'
   }else{
    Require ($argsText -match '\+r_streamlineDLSSGMode 0\b') 'Stereo must retain FG suppression.'
   }
   if($expected -eq '1'){
    Require ($child.CIRCLE_STEREO_CINEMATICS -eq '0') 'Mono VR must override stereo cinema.'
    Require ($child.CIRCLE_EXPERIMENTAL_DLSS -eq '1') 'Mono VR lost requested DLSS SR.'
   }
  }
  foreach($aaMode in @('off','native','dlss-sr')){
   & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -Stereo -MonoVR $true -Upscaling $aaMode -MonitorSeconds 1
   $argsText=Get-Content -LiteralPath (Join-Path $fixture 'game/child-arguments.txt') -Raw
   Require ($argsText -notmatch 'r_streamlineDLSSGMode') ('Mono FG incorrectly suppressed with '+$aaMode)
  }
  $config.vr_mono='false'
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  $refused=$false
  try{& (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -Stereo}catch{Require ($_.Exception.Message -like '*vr_mono*JSON boolean*') 'Wrong Mono VR validation error.';$refused=$true}
  Require $refused 'String Mono VR boolean accepted.'
  $config.vr_mono=$false
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 } finally {[Environment]::SetEnvironmentVariable('CIRCLE_MONO_VR',$parentMono)}
 $config | Add-Member -NotePropertyName vr_only_submit_real_frames -NotePropertyValue $true -Force
 $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 foreach($realArgs in @(@{Stereo=$true},@{Stereo=$true;OnlySubmitRealFrames=$false},@{})){
  & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -MonitorSeconds 1 @realArgs
  $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
  $expected=if($realArgs.Stereo -and !$realArgs.ContainsKey('OnlySubmitRealFrames')){'1'}else{'0'}
  Require ($child.CIRCLE_ONLY_SUBMIT_REAL_FRAMES -eq $expected) 'Real-only submission selection/override/mode gate failed.'
 }
 $config.vr_only_submit_real_frames='true'
 $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 $refused=$false
 try{& (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -Stereo}catch{Require ($_.Exception.Message -like '*vr_only_submit_real_frames*JSON boolean*') 'Wrong real-only validation error.';$refused=$true}
 Require $refused 'String real-only boolean accepted.'
 $config.vr_only_submit_real_frames=$false
 $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 $parentCinema=[Environment]::GetEnvironmentVariable('CIRCLE_STEREO_CINEMATICS')
 try {
  $env:CIRCLE_STEREO_CINEMATICS='1'
  $config.PSObject.Properties.Remove('vr_stereo_cinematics')
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -Stereo -MonitorSeconds 1
  $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
  Require ($child.CIRCLE_STEREO_CINEMATICS -eq '0') 'Legacy cinema default or parent isolation failed.'
  $config | Add-Member -NotePropertyName vr_stereo_cinematics -NotePropertyValue $true -Force
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  foreach($extra in @(@{},@{StereoCinematics=$false},@{VRProjection='phase5'},@{Mode='off';Stereo=$false})){
   $launchFlags=@{Stereo=$true};foreach($key in $extra.Keys){$launchFlags[$key]=$extra[$key]}
   & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -MonitorSeconds 1 @launchFlags
   $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
   $expected=if($extra.Count -eq 0){'1'}else{'0'}
   Require ($child.CIRCLE_STEREO_CINEMATICS -eq $expected) 'Cinema selection/override/mode gate failed.'
  }
  $config.vr_stereo_cinematics='false'
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  $refused=$false
  try{& (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -Stereo}catch{Require ($_.Exception.Message -like '*JSON boolean*') 'Wrong cinema validation error.';$refused=$true}
  Require $refused 'String cinema boolean was accepted.'
  $config.vr_stereo_cinematics=$false
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 } finally {[Environment]::SetEnvironmentVariable('CIRCLE_STEREO_CINEMATICS',$parentCinema)}
 & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -Stereo -ExperimentalHandIK -ExperimentalPunch -MonitorSeconds 1
 $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
 foreach($pair in @(@('CIRCLE_BODY_DEADZONE','31'),@('CIRCLE_BODY_SPEED','140'),@('CIRCLE_BODY_RESIDUAL','81'),@('CIRCLE_BODY_FOLLOW','0'),@('CIRCLE_BODY_NATIVE_TURN','1'),@('CIRCLE_ROOMSCALE','1'),@('CIRCLE_ROOMSCALE_DEBUG','1'),@('CIRCLE_MOVE_DIRECTION','offhand'),@('CIRCLE_ROOMSCALE_START','0.015'),@('CIRCLE_HAND_IK','1'),@('CIRCLE_PHYSICAL_PUNCH','1'),@('CIRCLE_SEQUENTIAL_STEREO','1'))){Require ($child.($pair[0]) -eq $pair[1]) ('Incorrect child environment: '+$pair[0])}
 & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -Stereo -ExperimentalPunch -MonitorSeconds 1
 $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
 Require ($null -eq $child.CIRCLE_HAND_IK -and $null -eq $child.CIRCLE_PHYSICAL_PUNCH) 'Punch was enabled without tracked hands.'
 # Exercise saved selections through the real launch script and child process.
 foreach($mode in @('off','native','dlss-sr','dlss-rr','game','fsr')){
  $config.upscaling=$mode
  $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  $beforeConfig=Get-Content -LiteralPath (Join-Path $fixture 'circle-settings.json') -Raw
  & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -Stereo -MonitorSeconds 1
  $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
  $argsText=Get-Content -LiteralPath (Join-Path $fixture 'game/child-arguments.txt') -Raw
  $aa=if($mode -like 'dlss-*'){2}elseif($mode -eq 'native'){1}elseif($mode -eq 'fsr'){3}else{0}
  Require ($argsText -match ('\+r_antialiasing '+$aa+'\b')) ('Wrong AA mode: '+$mode)
  Require ($argsText -match '\+r_streamlineDLSSGMode 0\b') 'Frame generation not disabled.'
  $pathTracing=0
  Require ($argsText -match ('\+rt_pathtracingEnabled '+$pathTracing+'\b')) ('Retired RR must not enable path tracing: '+$mode)
  Require (($child.CIRCLE_EXPERIMENTAL_DLSS -eq '1') -eq ($mode -like 'dlss-*')) ('Wrong SR hook gate: '+$mode)
  Require ($null -eq $child.CIRCLE_EXPERIMENTAL_RR) ('Wrong RR hook gate: '+$mode)
  Require ($argsText -match '\+rt_denoiserMode 1\b') ('Retired RR denoiser not reset: '+$mode)
  Require ((Get-Content -LiteralPath (Join-Path $fixture 'circle-settings.json') -Raw) -eq $beforeConfig) 'Launch modified saved settings.'
 }
 foreach($explicit in @('off','dlss-sr','dlss-rr')){
  & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -Stereo -Upscaling $explicit -MonitorSeconds 1
  $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
  Require (($child.CIRCLE_EXPERIMENTAL_DLSS -eq '1') -eq ($explicit -like 'dlss-*')) 'Explicit mode did not override saved game/off.'
 }
 & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -Stereo -Upscaling off -ExperimentalDLSSRR -MonitorSeconds 1
 $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
 Require ($null -eq $child.CIRCLE_EXPERIMENTAL_RR -and $child.CIRCLE_EXPERIMENTAL_DLSS -eq '1') 'Retired RR shortcut must migrate to SR.'
 $argsText=Get-Content -LiteralPath (Join-Path $fixture 'game/child-arguments.txt') -Raw
 Require ($argsText -match '\+rt_pathtracingEnabled 0\b' -and $argsText -match '\+rt_denoiserMode 1\b') 'Retired RR shortcut enabled RR/PT.'
 $refused=$false
 try{& (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -Stereo -ExperimentalDLSS -ExperimentalDLSSRR}catch{Require ($_.Exception.Message -like '*only one temporal*') 'Unexpected mutually-exclusive-mode error.';$refused=$true}
 Require $refused 'Conflicting temporal shortcuts were accepted.'
 $config.vr_roomscale_enabled=$false
 $config.vr_input_debug=$true
 $config.vr_pause_chord_window_ms=175
 $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -Stereo -MonitorSeconds 1
 $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
 Require ($child.CIRCLE_INPUT -eq '1' -and $null -eq $child.CIRCLE_ROOMSCALE -and $child.CIRCLE_BODY_NATIVE_TURN -eq '1') 'Disabling roomscale disabled native input or stick turning.'
 Require ($child.CIRCLE_INPUT_DEBUG -eq '1' -and $child.CIRCLE_PAUSE_WINDOW_MS -eq '175') 'New input settings did not reach child.'
 Require ($child.CIRCLE_DISABLE_FACIAL_HAIR -eq '0' -and $null -eq $child.CIRCLE_DISABLE_STRAND_HAIR) 'Retired beard suppression was enabled.'
 $config | Add-Member -NotePropertyName vr_disable_facial_hair -NotePropertyValue $false -Force
 $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -Stereo -MonitorSeconds 1
 $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
 Require ($child.CIRCLE_DISABLE_FACIAL_HAIR -eq '0') 'Cannot disable beard workaround for comparison.'
 $config.PSObject.Properties.Remove('vr_disable_facial_hair')
 $config | Add-Member -NotePropertyName vr_disable_strand_hair -NotePropertyValue $true -Force
 $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -Stereo -MonitorSeconds 1
 $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
 Require ($child.CIRCLE_DISABLE_FACIAL_HAIR -eq '0' -and $null -eq $child.CIRCLE_DISABLE_STRAND_HAIR) 'Legacy hair option re-enabled retired suppression.'
 # Integrated FG preserves the user's real-only choice, and cannot enter flat
 # or mono rendering. The fixture only starts MockGame, never the live game.
 $config.upscaling='dlss-sr'
 $config | Add-Member -NotePropertyName vr_dlss_frame_generation -NotePropertyValue $true -Force
 $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 foreach($case in @(
  @{Stereo=$true;MonoVR=$false;OnlySubmitRealFrames=$false},
  @{Stereo=$true;MonoVR=$false;OnlySubmitRealFrames=$true},
  @{Stereo=$true;MonoVR=$true;OnlySubmitRealFrames=$false},
  @{Stereo=$false;Mode='off'},
  @{Stereo=$true;VrDlssFrameGeneration=$false},
  @{Stereo=$true;MonoVR=$false;DiagnosticFramegenGpu=$true})){
  & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -MonitorSeconds 1 @case
  $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
  $expected=if($case.MonoVR -or $case.Mode -eq 'off' -or ($case.ContainsKey('VrDlssFrameGeneration') -and !$case.VrDlssFrameGeneration)){'0'}else{'1'}
  Require ($child.CIRCLE_VR_DLSS_FG -eq $expected) 'Integrated FG mode/explicit override failed.'
  if($case.ContainsKey('OnlySubmitRealFrames')){Require ($child.CIRCLE_ONLY_SUBMIT_REAL_FRAMES -eq $(if($case.OnlySubmitRealFrames){'1'}else{'0'})) 'Integrated FG changed the real-only preference.'}
  Require (!$child.KHARVOX_CIRCLE_D3D12_DEBUG) 'FG diagnostic enables D3D12 debug layer after game device creation.'
  Require (($child.CIRCLE_FG_DRED -eq '1') -eq [bool]$case.DiagnosticFramegenGpu) 'FG DRED diagnostic leaked or was omitted.'
 }
 # Community installer is stubbed only in this isolated child-process fixture.
 $config.upscaling='dlss-sr';$config.vr_dlss_frame_generation=$true
 $config | Add-Member -NotePropertyName vr_fg_backend -NotePropertyValue 'community' -Force
 $config | Add-Member -NotePropertyName vr_fg_multiplier -NotePropertyValue '4' -Force
 $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 foreach($case in @(@{Stereo=$true;MonoVR=$false},@{Stereo=$true;MonoVR=$true},@{Stereo=$false;Mode='off'},@{Stereo=$true;VrFgBackend='official';VrFgMultiplier=2})){
  & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -MonitorSeconds 1 @case
  $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
  $active=!$case.MonoVR -and $case.Mode -ne 'off'
  Require (($child.CIRCLE_VR_FG_COMMUNITY -eq '1') -eq $active) 'Community mode gate failed.'
  Require ($child.CIRCLE_VR_FG_MULTIPLIER -eq $(if($active -and !$case.ContainsKey('VrFgMultiplier')){'4'}else{'2'})) 'Multiplier override/isolation failed.'
  Require ((!!$child.CIRCLE_VR_COMMUNITY_DIR) -eq $active) 'Community runtime directory leaked.'
 }
 Require (Test-Path -LiteralPath (Join-Path $fixture 'tools/community-installer-called.txt')) 'Community setup was not called.'
 foreach($factor in @('adaptive','auto','auto-fixed','2.2','2.4','2.6','2.8','3.2','3.4','3.6','3.8')){
  $config.vr_fg_multiplier=$factor;$config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -MonitorSeconds 1 -Stereo
  $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
  Require ($child.CIRCLE_VR_FG_MULTIPLIER -eq $(if($factor -eq 'auto-fixed'){'auto-fixed'}else{'auto'})) 'FG mode identity/migration failed.'
  Require ($child.KHARVOX_CIRCLE_GPU_TIMING -eq 'sample') 'Automatic FG GPU sampling not enabled.'
 }
 foreach($invalid in @('5',$true,'3.0','3.5','2.1','4.2')){
  $config.vr_fg_multiplier=$invalid;$config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  $refused=$false;try{& (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -Stereo}catch{Require ($_.Exception.Message -like '*vr_fg_multiplier*') 'Wrong multiplier validation error.';$refused=$true}
  Require $refused 'Invalid FG multiplier accepted.'
 }
 $config.vr_fg_backend='official';$config.vr_fg_multiplier='2'
 # AMD uses its own request; selections, explicit override and mode isolation.
 $config.upscaling='fsr'
 $config | Add-Member -NotePropertyName vr_fsr_frame_generation -NotePropertyValue $true -Force
 $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 foreach($case in @(@{Stereo=$true;MonoVR=$false},@{Stereo=$true;MonoVR=$true},@{Stereo=$false;Mode='off'},@{Stereo=$true;VrFsrFrameGeneration=$false},@{Stereo=$true;Upscaling='native'})){
  & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -MonitorSeconds 1 @case
  $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
  $expected=if($case.MonoVR -or $case.Mode -eq 'off' -or $case.Upscaling -eq 'native' -or $case.ContainsKey('VrFsrFrameGeneration')){'0'}else{'1'}
  Require ($child.CIRCLE_VR_FSR_FG -eq $expected) 'AMD FG mode/override failed.'
  Require ($child.CIRCLE_VR_DLSS_FG -eq '0') 'AMD selection activated NVIDIA FG.'
 }
 foreach($factor in @('auto','auto-fixed')){
  $config.vr_fg_multiplier=$factor;$config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
  & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -MonitorSeconds 1 -Stereo -VrFgMultiplier $factor
  $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
  Require ($child.CIRCLE_VR_FG_MULTIPLIER -eq $factor -and $child.CIRCLE_VR_FSR_FG -eq '1' -and $child.KHARVOX_CIRCLE_GPU_TIMING -eq 'sample') 'AMD Auto mode/explicit override was lost.'
 }
 $config.vr_fg_multiplier='2'
 $config.vr_fsr_frame_generation='true'
 $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 $refused=$false
 try{& (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -Stereo}catch{Require ($_.Exception.Message -like '*vr_fsr_frame_generation*JSON boolean*') 'Wrong AMD validation error.';$refused=$true}
 Require $refused 'String AMD FG boolean accepted.'
 $config.vr_fsr_frame_generation=$false
 $config.vr_dlss_frame_generation='true'
 $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 $refused=$false
 try{& (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -Stereo}catch{Require ($_.Exception.Message -like '*vr_dlss_frame_generation*JSON boolean*') 'Wrong VR-FG validation error.';$refused=$true}
 Require $refused 'String FG boolean accepted.'
 $config.vr_dlss_frame_generation=$false
 # Store profiles use the same settings without leaking Steam context into GOG.
 $fixtureHash='68EF9BE325743E304062728F667FDC6B3FBA42243F9065A9A6B48B1B23418311'
 $config.game_directory=Join-Path $fixture 'game'
 $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -RuntimeJson (Join-Path $fixture 'runtime.json') -Stereo -MonitorSeconds 1
 $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
 Require ($child.SteamAppId -eq '2677660' -and $child.SteamGameId -eq '2677660') 'Steam profile did not supply its app context or saved directory.'
 $childArgs=Get-Content -LiteralPath (Join-Path $fixture 'game/child-arguments.txt') -Raw
 Require ($childArgs -match '\+r_motionblur 0' -and $childArgs -match '\+r_chromaticAberration 0') 'Steam VR comfort overrides missing.'
 $fixtureHash='16EAB3F4827D37EF93111173A831B0F668EF8BDFFA4442C740F2574F23DCC120'
 & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -RuntimeJson (Join-Path $fixture 'runtime.json') -Stereo -MonitorSeconds 1
 $child=Get-Content -LiteralPath (Join-Path $fixture 'game/child-environment.json') -Raw | ConvertFrom-Json
 Require ($null -eq $child.SteamAppId -and $null -eq $child.SteamGameId) 'Steam context leaked to GOG.'
 $childArgs=Get-Content -LiteralPath (Join-Path $fixture 'game/child-arguments.txt') -Raw
 Require ($childArgs -match '\+r_motionblur 0' -and $childArgs -match '\+r_chromaticAberration 0') 'GOG VR comfort overrides missing.'
 & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -Mode off -MonitorSeconds 1
 $childArgs=Get-Content -LiteralPath (Join-Path $fixture 'game/child-arguments.txt') -Raw
 Require ($childArgs -notmatch '\+r_motionblur|\+r_chromaticAberration') 'VR comfort override leaked into flat launch.'
 & (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -Mode cinema -RuntimeJson (Join-Path $fixture 'runtime.json') -MonitorSeconds 1
 $childArgs=Get-Content -LiteralPath (Join-Path $fixture 'game/child-arguments.txt') -Raw
 Require ($childArgs -match '\+r_motionblur 0' -and $childArgs -match '\+r_chromaticAberration 0') 'Comfort overrides depend on stereo/hand tracking.'
 $fixtureHash=('0'*64);$refused=$false
 try{& (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -Stereo}catch{Require ($_.Exception.Message -like '*explicitly verified Motor build*') 'Wrong unsupported-build error.';$refused=$true}
 Require $refused 'Unknown same-version executable accepted for stereo.'
 $fixtureHash='16EAB3F4827D37EF93111173A831B0F668EF8BDFFA4442C740F2574F23DCC120'
 $config.vr_body_yaw_max_residual=20
 $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'circle-settings.json')
 $refused=$false
 try{& (Join-Path $fixture 'tools/launch.ps1') -SettingsFile (Join-Path $fixture 'circle-settings.json') -GameDirectory (Join-Path $fixture 'game') -Stereo}catch{Require ($_.Exception.Message -like '*vr_body_yaw_max_residual*') 'Unexpected validation error.'; $refused=$true}
 Require $refused 'Invalid body yaw values were accepted.'
 'PASS: cinema default/migration, opt-in, override, mode gates and strict boolean; child-process environment, de-DE numeric serialization, body-follow-independent stick turning, hand/punch gates, AA/SR choices, retired RR migration, legacy migration, explicit overrides, exclusive test shortcuts, FG off, immutable settings, invalid-value refusal. Real game not launched.' | Set-Content -LiteralPath (Join-Path $fixture 'result.txt')
 Get-Content -LiteralPath (Join-Path $fixture 'result.txt')
} finally {[Threading.Thread]::CurrentThread.CurrentCulture=$previousCulture}
