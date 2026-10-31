param([string]$PackageRoot)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if(!$PackageRoot){$PackageRoot=$root}
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class CircleResourceProbe {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern IntPtr LoadLibraryEx(string path,IntPtr file,uint flags);
 [DllImport("kernel32.dll",EntryPoint="FindResourceW",SetLastError=true)] public static extern IntPtr FindResource(IntPtr module,IntPtr name,IntPtr type);
 [DllImport("kernel32.dll")] public static extern IntPtr LoadResource(IntPtr module,IntPtr resource);
 [DllImport("kernel32.dll")] public static extern IntPtr LockResource(IntPtr resource);
 [DllImport("kernel32.dll")] public static extern uint SizeofResource(IntPtr module,IntPtr resource);
 [DllImport("kernel32.dll")] public static extern bool FreeLibrary(IntPtr module);
}
'@
function Require($ok,[string]$message){if(!$ok){throw $message}}
function Hash([byte[]]$bytes){$sha=[Security.Cryptography.SHA256]::Create();try{[Convert]::ToBase64String($sha.ComputeHash($bytes))}finally{$sha.Dispose()}}
$assembly=[Reflection.Assembly]::LoadFrom((Join-Path $PackageRoot 'CircleLauncher.exe'))
foreach($entry in @(@('banner.png','assets/Kharvox Crilce Logo Banner.png'),@('support.png','assets/ko-fi-support.png'))){
 $stream=$assembly.GetManifestResourceStream('Circle.'+$entry[0]);Require ($null -ne $stream) ('Missing embedded '+$entry[0])
 try{$data=New-Object IO.MemoryStream;$stream.CopyTo($data);Require ((Hash $data.ToArray()) -eq (Hash ([IO.File]::ReadAllBytes((Join-Path $root $entry[1]))))) 'Launcher image differs from source';$data.Dispose()}finally{$stream.Dispose()}
}
$defaultsStream=$assembly.GetManifestResourceStream('Circle.defaults.json');$reader=New-Object IO.StreamReader($defaultsStream)
try{$defaults=$reader.ReadToEnd() | ConvertFrom-Json;Require ($defaults.enable_logging -eq $false -and $defaults.vr_intro_started -eq $false -and $defaults.disable_vr_intro -eq $false) 'Incorrect embedded defaults'}finally{$reader.Dispose()}
foreach($name in @('launch.ps1','vr-intro.ps1','settings-path.ps1','test-xr.ps1','install-community-framegen.ps1','motor-builds.json')){
 $stream=$assembly.GetManifestResourceStream('Circle.'+$name);Require ($null -ne $stream -and $stream.Length -gt 0) ('Missing embedded '+$name);$stream.Dispose()
}
$module=[CircleResourceProbe]::LoadLibraryEx((Join-Path $PackageRoot 'bin/KharvoxCircle.dll'),[IntPtr]::Zero,0x22)
Require ($module -ne [IntPtr]::Zero) 'Cannot load DLL resources (data-only).'
try {
 foreach($entry in @(@(301,'circle-controller-interaction-en.png'),@(302,'circle-controller-combat-en.png'))){
  $resource=[CircleResourceProbe]::FindResource($module,[IntPtr]$entry[0],[IntPtr]10);Require ($resource -ne [IntPtr]::Zero) 'Missing controller guide resource'
  $size=[CircleResourceProbe]::SizeofResource($module,$resource)
  $memory=[CircleResourceProbe]::LockResource([CircleResourceProbe]::LoadResource($module,$resource))
  $bytes=New-Object byte[] $size;[Runtime.InteropServices.Marshal]::Copy($memory,$bytes,0,$size)
  Require ((Hash $bytes) -eq (Hash ([IO.File]::ReadAllBytes((Join-Path $root ('assets/'+$entry[1])))))) 'Controller guide differs from source'
  $stream=New-Object IO.MemoryStream(,$bytes);$bitmap=[Drawing.Image]::FromStream($stream)
  try{Require ($bitmap.Width -gt 0 -and $bitmap.Height -gt 0 -and $bitmap.Width -le 4096 -and $bitmap.Height -le 4096) 'Guide cannot be decoded'}finally{$bitmap.Dispose();$stream.Dispose()}
 }
}finally{[CircleResourceProbe]::FreeLibrary($module) | Out-Null}
'PASS: launcher images, defaults, launch policies, and both DLL controller guide resources are embedded and match source assets.'
