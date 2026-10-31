# Shared by the graphical launcher and Start-VR.cmd. No registry or runtime changes.
function Set-CircleIntroStarted {
    param([Parameter(Mandatory=$true)][string]$SettingsFile)
    # Re-read rather than replacing the launch snapshot: preserve unrelated settings.
    $current=Get-Content -LiteralPath $SettingsFile -Raw | ConvertFrom-Json
    $current | Add-Member -NotePropertyName vr_intro_started -NotePropertyValue $true -Force
    $temp=$SettingsFile+'.'+[guid]::NewGuid().ToString('N')+'.tmp'
    try {
        [IO.File]::WriteAllText($temp,($current | ConvertTo-Json -Depth 40),(New-Object Text.UTF8Encoding($false)))
        [IO.File]::Replace($temp,$SettingsFile,$SettingsFile+'.bak')
    } finally {if(Test-Path -LiteralPath $temp){Remove-Item -LiteralPath $temp}}
}

function Invoke-CircleVrIntro {
    param([Parameter(Mandatory=$true)][string]$Root,
          [Parameter(Mandatory=$true)][string]$SettingsFile,
          [string]$RuntimeJson,[bool]$EnableLogging=$false,
          [Threading.EventWaitHandle]$CancelEvent)
    $settings=Get-Content -LiteralPath $SettingsFile -Raw | ConvertFrom-Json
    foreach($key in @('vr_intro_started','disable_vr_intro')){
        if($null -ne $settings.$key -and $settings.$key -isnot [bool]){throw "$key must be a JSON boolean."}
    }
    if($CancelEvent -and $CancelEvent.WaitOne(0)){throw 'Game launch cancelled.'}
    # As in ARGENT, disabling is available only after an intro process has started.
    if($settings.vr_intro_started -eq $true -and $settings.disable_vr_intro -eq $true){return}
    $exe=Join-Path $Root 'bin/KHARVOX CIRCLE Intro.exe'
    if(!(Test-Path -LiteralPath $exe -PathType Leaf)){throw 'CIRCLE VR Intro is missing. Extract the complete release; Indiana Jones was not started.'}
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$exe
    $info.WorkingDirectory=Split-Path -Parent $exe
    $info.UseShellExecute=$false
    $info.CreateNoWindow=$true
    $info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    # Do not inject game-specific layers or inherited diagnostics into the intro.
    @($info.EnvironmentVariables.Keys) | Where-Object {
        $_ -like 'CIRCLE_*' -or $_ -like 'KHARVOX_CIRCLE_*' -or $_ -like 'ARGENT_*' -or
        $_ -like 'VK_*LAYER*' -or $_ -in @('XR_ENABLE_API_LAYERS','XR_API_LAYER_PATH','XR_RUNTIME_JSON')
    } | ForEach-Object {$info.EnvironmentVariables.Remove($_) | Out-Null}
    if($RuntimeJson){$info.EnvironmentVariables['XR_RUNTIME_JSON']=$RuntimeJson}
    $info.EnvironmentVariables['CIRCLE_ENABLE_LOGGING']=if($EnableLogging){'1'}else{'0'}
    $intro=$null
    try {
        $intro=[Diagnostics.Process]::Start($info)
        Set-CircleIntroStarted -SettingsFile $SettingsFile
        Write-Output 'CIRCLE_INTRO_STARTED'
        while(!$intro.WaitForExit(100)){
            if($CancelEvent -and $CancelEvent.WaitOne(0)){throw 'Game launch cancelled.'}
        }
        if($CancelEvent -and $CancelEvent.WaitOne(0)){throw 'Game launch cancelled.'}
        if($intro.ExitCode -ne 0){throw "VR intro did not complete (exit $($intro.ExitCode)). Indiana Jones was not started. Retry, or select Disable VR Intro under Rendering."}
        # Process exit also waits for OpenXR/session teardown, GPU and audio cleanup.
        Write-Output 'CIRCLE_INTRO_FINISHED'
    } finally {
        if($intro){
            if(!$intro.HasExited){
                $null=$intro.CloseMainWindow()
                if(!$intro.WaitForExit(8000)){$intro.Kill();$intro.WaitForExit()}
            }
            $intro.Dispose()
        }
    }
}
