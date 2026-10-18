@echo off
rem Optional: 60-second checkerboard through SteamVR OpenXR, no game involved. Sharp squares and a
rem white cross in both eyes mean the headset, GPU and SteamVR work; any problem left is in the game.
setlocal
set "XR_RUNTIME_JSON="
for /f "tokens=2,*" %%a in ('reg query "HKCU\Software\Valve\Steam" /v SteamPath 2^>nul ^| find "SteamPath"') do set "STEAM=%%b"
if defined STEAM if exist "%STEAM%\steamapps\common\SteamVR\steamxr_win64.json" set "XR_RUNTIME_JSON=%STEAM%\steamapps\common\SteamVR\steamxr_win64.json"
echo Look for cyan squares (left eye), orange squares (right eye) and a white cross. Escape stops it.
"%~dp0tools\vive_pattern.exe"
echo.
pause
