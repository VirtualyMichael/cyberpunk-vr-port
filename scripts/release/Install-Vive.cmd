@echo off
rem Double-click installer for the Vive / Vive Pro update. Asks for the game folder and headset
rem (or takes them as arguments: Install-Vive.cmd "C:\...\Cyberpunk 2077" VivePro), then runs
rem scripts\install_vive.ps1, which backs up every file it replaces into .\backups.
setlocal
rem A PowerShell 7 parent leaves its module path behind, and Windows PowerShell then cannot
rem find built-ins such as Get-FileHash. Cleared, Windows PowerShell rebuilds its own.
set "PSModulePath="
echo CyberpunkVRPort 0.1.7-vive2 installer (HTC Vive / Vive Pro)
echo Close Cyberpunk 2077 before continuing.
echo.
set "GAME=%~1"
set "HS=%~2"
if not defined GAME set /p "GAME=Cyberpunk 2077 folder (the one containing bin and archive): "
if not defined HS set /p "HS=Headset - type Vive or VivePro: "
rem A pasted path often arrives in quotes; the script wants it bare.
set "GAME=%GAME:"=%"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install_vive.ps1" -GameRoot "%GAME%" -Headset "%HS%" -DllPath "%~dp0CyberpunkVR_Stereo.dll" -BackupRoot "%~dp0backups"
echo.
if "%~2"=="" pause