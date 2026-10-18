@echo off
rem Double-click installer for the Vive / Vive Pro update. Asks two questions, then runs
rem scripts\install_vive.ps1, which backs up every file it replaces into .\backups.
setlocal
echo CyberpunkVRPort 0.1.7-vive2 installer (HTC Vive / Vive Pro)
echo Close Cyberpunk 2077 before continuing.
echo.
set "GAME="
set /p "GAME=Cyberpunk 2077 folder (the one containing bin and archive): "
set "HS="
set /p "HS=Headset - type Vive or VivePro: "
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install_vive.ps1" -GameRoot "%GAME%" -Headset "%HS%" -DllPath "%~dp0CyberpunkVR_Stereo.dll" -BackupRoot "%~dp0backups"
echo.
pause
