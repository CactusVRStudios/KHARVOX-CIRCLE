@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Launch-Circle.ps1" -Stereo -ExperimentalHandIK -ExperimentalPunch %*
if errorlevel 1 pause
