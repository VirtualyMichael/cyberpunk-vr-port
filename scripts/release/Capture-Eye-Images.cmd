@echo off
rem Requests a one-shot capture of both eye images (saved under bin\x64\vive-diagnostics) for bug reports.
rem Stand still looking at the problem, then run this while the game is running.
setlocal
set "PSModulePath="
set "GAME=%~1"
if not defined GAME set /p "GAME=Cyberpunk 2077 folder: "
set "GAME=%GAME:"=%"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\capture_vive.ps1" -GameRoot "%GAME%"
if "%~1"=="" pause
