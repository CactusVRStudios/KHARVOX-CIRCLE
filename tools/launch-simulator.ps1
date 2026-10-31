param(
    [string]$SimulatorDirectory = 'D:\bin',
    [string]$SettingsFile,
    [switch]$DiagnosticTemporalImages,
    [ValidateRange(2,32)][int]$TemporalImagePairs = 2,
    [int]$MonitorSeconds = 10
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$runtimeDll = (Resolve-Path -LiteralPath (Join-Path $SimulatorDirectory 'openxr_simulator.dll')).Path
$logs = Join-Path $root 'logs'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$manifest = Join-Path $logs 'openxr-simulator-absolute.json'
@{file_format_version='1.0.0';runtime=@{library_path=$runtimeDll}} | ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding ascii
if (!$SettingsFile) { $SettingsFile = Join-Path $root 'circle-settings.json' }
$previous = [Environment]::GetEnvironmentVariable('SIMXR_VK_NO_TIMELINE','Process')
try {
    # Documented simulator fallback: serialize Vulkan/D3D12 through CPU waits.
    $env:SIMXR_VK_NO_TIMELINE = '1'
    & (Join-Path $PSScriptRoot 'launch.ps1') -Stereo -ExperimentalHandIK -ExperimentalPunch -DiagnosticGpuBindings -DiagnosticTemporalImages:$DiagnosticTemporalImages -TemporalImagePairs $TemporalImagePairs -SimulatorDesktopInput -RuntimeJson $manifest -SettingsFile $SettingsFile -MonitorSeconds $MonitorSeconds
} finally {
    [Environment]::SetEnvironmentVariable('SIMXR_VK_NO_TIMELINE',$previous,'Process')
}
