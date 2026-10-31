param(
    [int]$Seconds = 180,
    [ValidateSet('auto','vdxr','active','meta')][string]$Runtime = 'auto',
    [string]$RuntimeJson,
    [bool]$EnableLogging=$false,
    [string]$PackageRoot
)
$ErrorActionPreference = 'Stop'
$root = if($PackageRoot){[IO.Path]::GetFullPath($PackageRoot)}else{Split-Path -Parent $PSScriptRoot}
$run = Join-Path $root ('logs/' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-xr-diagnostic')
if($EnableLogging){New-Item -ItemType Directory -Force -Path $run | Out-Null}
$vdxr = 'C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json'
$meta = 'C:\Program Files\Oculus\Support\oculus-runtime\oculus_openxr_64.json'
if ($RuntimeJson) { $manifest = (Resolve-Path -LiteralPath $RuntimeJson).Path }
elseif ($Runtime -eq 'meta') { $manifest = $meta }
elseif ($Runtime -eq 'vdxr') { $manifest = $vdxr }
else { $manifest = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Khronos\OpenXR\1' -ErrorAction SilentlyContinue).ActiveRuntime }
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = Join-Path $root 'bin/KharvoxCircleXrDiagnostic.exe'
$start.WorkingDirectory = Join-Path $root 'bin'
$start.Arguments = [string]$Seconds
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.EnvironmentVariables['CIRCLE_ENABLE_LOGGING']=if($EnableLogging){'1'}else{'0'}
$start.EnvironmentVariables.Remove('KHARVOX_CIRCLE_LOG_DIR') | Out-Null
if($EnableLogging){$start.EnvironmentVariables['KHARVOX_CIRCLE_LOG_DIR'] = $run}
$start.EnvironmentVariables['KHARVOX_CIRCLE_MODE'] = 'diagnostic'
if ($manifest) { $start.EnvironmentVariables['XR_RUNTIME_JSON'] = $manifest }
Write-Host "XR diagnostic for $Seconds seconds: red L left, blue R right. Logging: $EnableLogging"
$process = [Diagnostics.Process]::Start($start)
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
$process.WaitForExit()
if($EnableLogging){
$stdout.Result | Set-Content -LiteralPath (Join-Path $run 'stdout.txt') -Encoding utf8
$stderr.Result | Set-Content -LiteralPath (Join-Path $run 'stderr.txt') -Encoding utf8
[ordered]@{ pid = $process.Id; exitCode = $process.ExitCode; runtime = $manifest; seconds = $Seconds; logDirectory = $run } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'result.json') -Encoding utf8
}else{Write-Output $stdout.Result; Write-Output $stderr.Result}
Write-Host "XR diagnostic exit: $($process.ExitCode)"
if ($process.ExitCode -ne 0) { throw "XR diagnostic failed: $($stderr.Result)" }
