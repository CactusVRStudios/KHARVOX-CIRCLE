param([string]$SettingsFile)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(!$SettingsFile){$SettingsFile=Join-Path $root 'profiles/launcher-defaults.json'}
$null=Get-Content -LiteralPath $SettingsFile -Raw | ConvertFrom-Json
$resources=@()
$embedded=[ordered]@{
 'assets/Kharvox Crilce Logo Banner.png'='banner.png'
 'assets/ko-fi-support.png'='support.png'
 'assets/circle-controller-interaction-en.png'='interaction.png'
 'assets/circle-controller-combat-en.png'='combat.png'
 'tools/launch.ps1'='launch.ps1'
 'tools/settings-path.ps1'='settings-path.ps1'
 'tools/vr-intro.ps1'='vr-intro.ps1'
 'tools/test-xr.ps1'='test-xr.ps1'
 'tools/install-community-framegen.ps1'='install-community-framegen.ps1'
 'profiles/motor-builds.json'='motor-builds.json'
}
foreach($entry in $embedded.GetEnumerator()){$resources+=('/resource:'+(Join-Path $root $entry.Key)+',Circle.'+$entry.Value)}
$resources+=('/resource:'+([IO.Path]::GetFullPath($SettingsFile))+',Circle.defaults.json')
$noticeFiles=@('LICENSE','licenses/renderer-components-MIT.txt','licenses/renderer-components-NOTICES.md','licenses/ARGENT-MIT.txt','third_party/fidelityfx/LICENSE.txt','licenses/frame-generation-MIT.txt','third_party/NVIDIA-DLSS-SDK-LICENSE.txt','licenses/VRIntro-OpenXR-Apache-2.0.txt','licenses/VRIntro-jsoncpp-LICENSE.txt','licenses/community-framegen-notices.txt')
$notices=($noticeFiles | ForEach-Object {"`r`n===== $_ =====`r`n"+[IO.File]::ReadAllText((Join-Path $root $_))}) -join "`r`n"
$resourceDir=Join-Path $root 'build/launcher-resources'
[IO.Directory]::CreateDirectory($resourceDir) | Out-Null
$noticePath=Join-Path $resourceDir 'notices.txt'
[IO.File]::WriteAllText($noticePath,$notices,(New-Object Text.UTF8Encoding($false)))
$resources+=('/resource:'+$noticePath+',Circle.notices.txt')
$framework=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
$wpf=Join-Path $framework 'WPF'
$compiler=Join-Path $framework 'csc.exe'
if(!(Test-Path -LiteralPath $compiler)){throw 'The Windows .NET Framework 4 compiler is required.'}
$references=@('System.dll','System.Core.dll','System.Web.Extensions.dll') | ForEach-Object { '/reference:'+(Join-Path $framework $_) }
$references+=@('PresentationCore.dll','PresentationFramework.dll','WindowsBase.dll','System.Xaml.dll') | ForEach-Object {
 $path=Join-Path $wpf $_
 if(!(Test-Path -LiteralPath $path)){$path=Join-Path $framework $_}
 '/reference:'+$path
}
& $compiler /nologo /target:winexe /platform:x64 /optimize+ ('/out:'+(Join-Path $root 'CircleLauncher.exe')) @references @resources (Join-Path $root 'src/launcher/CircleLauncher.cs') (Join-Path $root 'src/launcher/EmbeddedResources.cs')
if($LASTEXITCODE -ne 0){throw 'Circle launcher compilation failed.'}
Write-Output (Join-Path $root 'CircleLauncher.exe')
