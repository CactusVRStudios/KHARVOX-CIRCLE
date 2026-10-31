param([string]$EmbeddedLauncher)
$ErrorActionPreference='Stop'
# CTest can inherit PowerShell 7's module search path while launching Windows
# PowerShell 5.1; use this host's utility module, including Get-FileHash.
Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1') -Force
$root=Split-Path -Parent $PSScriptRoot
$fixture=Join-Path $root ("logs/intro-integration-"+[guid]::NewGuid().ToString('N'))
if($EmbeddedLauncher){$fixture+=' space-$()-test';$assembly=[Reflection.Assembly]::LoadFrom([IO.Path]::GetFullPath($EmbeddedLauncher));$embedded=$assembly.GetType('Kharvox.Circle.EmbeddedLaunch')}
$null=New-Item -ItemType Directory -Path $fixture,(Join-Path $fixture 'bin'),(Join-Path $fixture 'game')
if(!$EmbeddedLauncher){
 $null=New-Item -ItemType Directory -Path (Join-Path $fixture 'tools'),(Join-Path $fixture 'profiles')
 foreach($name in @('launch.ps1','vr-intro.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $fixture "tools/$name")}
 Set-Content (Join-Path $fixture 'profiles/motor-builds.json') '{"profiles":[]}'
}
Set-Content (Join-Path $fixture 'runtime.json') '{}'
Set-Content (Join-Path $fixture 'bin/KharvoxCircle.dll') 'Never loaded: mock fixture.'
$source=@'
using System;
using System.IO;
using System.Threading;
using System.Diagnostics;
class MockIntroAndGame {
 static int Main() {
  var root=Directory.GetParent(AppDomain.CurrentDomain.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar)).FullName;
  var intro=Path.GetFileNameWithoutExtension(Process.GetCurrentProcess().MainModule.FileName).Contains("Intro");
  var events=Path.Combine(root,"events.txt");
  if(!intro){File.AppendAllText(events,"game\n");return 0;}
  File.AppendAllText(events,"intro-start\n");
  File.WriteAllText(Path.Combine(root,"intro-environment.txt"),
   (Environment.GetEnvironmentVariable("XR_RUNTIME_JSON")??"")+"\n"+
   (Environment.GetEnvironmentVariable("KHARVOX_CIRCLE_ENABLE_LAYER")??"")+"\n"+
   (Environment.GetEnvironmentVariable("CIRCLE_ENABLE_LOGGING")??"")+"\n"+
   (Environment.GetEnvironmentVariable("XR_ENABLE_API_LAYERS")??""));
  var mode=File.ReadAllText(Path.Combine(root,"intro-case.txt")).Trim();
  if(mode.StartsWith("cancel:")){
   using(var signal=EventWaitHandle.OpenExisting(mode.Substring(7)))signal.Set();
   Thread.Sleep(20000);
  }
  Thread.Sleep(150);
  File.AppendAllText(events,"intro-exit\n");
  return mode=="fail"?2:0;
 }
}
'@
$src=Join-Path $fixture 'Mock.cs';Set-Content $src $source
$exe=Join-Path $fixture 'bin/KHARVOX CIRCLE Intro.exe'
& "$env:WINDIR/Microsoft.NET/Framework64/v4.0.30319/csc.exe" /nologo /target:winexe ("/out:"+$exe) $src
if($LASTEXITCODE){throw 'Mock compilation failed.'}
Copy-Item $exe (Join-Path $fixture 'game/TheGreatCircle.exe')
$settingsPath=Join-Path $fixture 'settings.json'
function Require($ok,[string]$message){if(!$ok){throw $message}}
function Setup([string]$case,[object]$settings){
 Set-Content (Join-Path $fixture 'intro-case.txt') $case
 Set-Content (Join-Path $fixture 'events.txt') '' -NoNewline
 $complete=Get-Content (Join-Path $root 'profiles/launcher-defaults.json') -Raw | ConvertFrom-Json
 $complete | Add-Member -NotePropertyName vr_intro_started -NotePropertyValue $false -Force
 $complete | Add-Member -NotePropertyName disable_vr_intro -NotePropertyValue $false -Force
 foreach($key in $settings.Keys){$complete | Add-Member -NotePropertyName $key -NotePropertyValue $settings[$key] -Force}
 $complete | ConvertTo-Json -Depth 20 | Set-Content $settingsPath
}
function Launch([string]$mode='cinema',[string]$cancel=''){
 if(!$EmbeddedLauncher){& (Join-Path $fixture 'tools/launch.ps1') -Mode $mode -GameDirectory (Join-Path $fixture 'game') -RuntimeJson (Join-Path $fixture 'runtime.json') -SettingsFile $settingsPath -LaunchCancelEvent $cancel -MonitorSeconds 1;return}
 $arguments=New-Object 'System.Collections.Generic.Dictionary[string,object]'
 $arguments['PackageRoot']=$fixture;$arguments['Mode']=$mode;$arguments['GameDirectory']=(Join-Path $fixture 'game');$arguments['RuntimeJson']=(Join-Path $fixture 'runtime.json');$arguments['SettingsFile']=$settingsPath;$arguments['LaunchCancelEvent']=$cancel;$arguments['MonitorSeconds']=1
 $payload=$embedded.GetMethod('PayloadJson').Invoke($null,@([string]($arguments | ConvertTo-Json -Depth 20)))
 $info=$embedded.GetMethod('StartInfo').Invoke($null,@([string]$fixture))
 $process=[Diagnostics.Process]::Start($info)
 try {
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  $bytes=[Text.Encoding]::UTF8.GetBytes($payload);$process.StandardInput.BaseStream.Write($bytes,0,$bytes.Length);$process.StandardInput.Close()
  $process.WaitForExit()
  if($process.ExitCode){throw $stderr.Result}
  $stdout.Result -split '\r?\n'
 } finally {$process.Dispose()}
}
function Events { [IO.File]::ReadAllText((Join-Path $fixture 'events.txt')).Replace("`r",'').Trim() }
$oldLayer=$env:KHARVOX_CIRCLE_ENABLE_LAYER;$oldApi=$env:XR_ENABLE_API_LAYERS
try {
 $env:KHARVOX_CIRCLE_ENABLE_LAYER='1';$env:XR_ENABLE_API_LAYERS='test-layer-must-not-reach-intro'
 Setup 'ok' @{future_extension=@{nested='preserve'};enable_logging=$false}
 $output=@(Launch)
 Require ((Events) -eq "intro-start`nintro-exit`ngame") 'Game started before intro finished.'
 Require ($output -contains 'CIRCLE_INTRO_STARTED' -and $output -contains 'CIRCLE_INTRO_FINISHED') 'Missing UI progress messages.'
 $saved=Get-Content $settingsPath -Raw | ConvertFrom-Json
 Require ($saved.vr_intro_started -eq $true -and $saved.future_extension.nested -eq 'preserve') 'First-run persistence lost data.'
 $environment=[IO.File]::ReadAllLines((Join-Path $fixture 'intro-environment.txt'))
 Require ($environment[0] -eq (Join-Path $fixture 'runtime.json') -and [string]::IsNullOrEmpty($environment[1]) -and $environment[2] -eq '0' -and [string]::IsNullOrEmpty($environment[3])) 'Intro runtime/isolation/logging mismatch.'
 Require (!(Test-Path (Join-Path $fixture 'logs'))) 'Logging OFF created a session directory.'

 Setup 'fail' @{vr_intro_started=$true;disable_vr_intro=$true}
 $null=Launch
 Require ((Events) -eq 'game') 'Saved opt-out did not skip intro.'

 Setup 'ok' @{vr_intro_started=$false;disable_vr_intro=$true}
 $null=Launch
 Require ((Events) -eq "intro-start`nintro-exit`ngame") 'Unseen intro was skipped.'

 Setup 'ok' @{vr_intro_started=$true;disable_vr_intro=$false;enable_logging=$true}
 $null=Launch
 Require ((Events) -eq "intro-start`nintro-exit`ngame") 'Re-enabling intro did not play.'
 Require (([IO.File]::ReadAllLines((Join-Path $fixture 'intro-environment.txt')))[2] -eq '1') 'Logging ON not propagated.'

 Setup 'fail' @{}
 $failed=$false
 try{$null=Launch}catch{$failed=$_.Exception.Message -like '*did not complete*'}
 Require ($failed -and (Events) -eq "intro-start`nintro-exit") 'Failed intro launched game.'
 Require ((Get-Content $settingsPath -Raw | ConvertFrom-Json).vr_intro_started -eq $true) 'Failed attempt did not unlock retry/skip option.'

 Setup 'ok' @{disable_vr_intro='false'}
 $failed=$false;try{$null=Launch}catch{$failed=$_.Exception.Message -like '*JSON boolean*'}
 Require ($failed -and (Events) -eq '') 'Invalid intro setting accepted.'

 Setup 'ok' @{}
 Move-Item -LiteralPath $exe -Destination ($exe+'.absent')
 try{$failed=$false;try{$null=Launch}catch{$failed=$_.Exception.Message -like '*Intro is missing*'};Require ($failed -and (Events) -eq '') 'Missing intro launched game.'}
 finally{Move-Item -LiteralPath ($exe+'.absent') -Destination $exe}

 Setup 'fail' @{}
 $null=Launch 'off'
 Require ((Events) -eq 'game') 'Flat mode unexpectedly ran VR intro.'

 if($EmbeddedLauncher){
  Copy-Item -LiteralPath $exe -Destination (Join-Path $fixture 'bin/KharvoxCircleXrDiagnostic.exe')
  Setup 'fail' @{}
  $null=Launch 'diagnostic'
  Require ((Events) -eq 'game') 'Embedded Runtime Check failed or unexpectedly played intro.'
  Require (!(Test-Path (Join-Path $fixture 'tools')) -and !(Test-Path (Join-Path $fixture 'profiles'))) 'Embedded launcher extracted support files.'
 }

 $name='Local\CircleIntroTest-'+[guid]::NewGuid().ToString('N')
 $cancel=New-Object Threading.EventWaitHandle($false,[Threading.EventResetMode]::ManualReset,$name)
 try {
  Setup ('cancel:'+$name) @{}
  $failed=$false;try{$null=Launch 'cinema' $name}catch{$failed=$_.Exception.Message -like '*cancelled*'}
  Require ($failed -and (Events) -eq 'intro-start') 'Cancelled intro launched game.'
  Require (@(Get-Process | Where-Object {try{$_.Path -eq $exe}catch{$false}}).Count -eq 0) 'Cancelled intro was left running.'
 } finally {$cancel.Dispose()}
 'PASS: first run, ordered handoff, runtime/isolation, logging off/on, persistence, opt-out/re-enable, failure, missing binary, strict booleans, flat bypass and cancellation. No real game or headset started.' | Set-Content (Join-Path $fixture 'result.txt')
 Get-Content (Join-Path $fixture 'result.txt')
} finally {$env:KHARVOX_CIRCLE_ENABLE_LAYER=$oldLayer;$env:XR_ENABLE_API_LAYERS=$oldApi}
