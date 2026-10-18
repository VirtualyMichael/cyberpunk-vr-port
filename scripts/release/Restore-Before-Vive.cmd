@echo off
rem Puts back the files from the most recent install in .\backups (game must be closed).
setlocal
rem A PowerShell 7 parent leaves its module path behind, and Windows PowerShell then cannot
rem find built-ins such as Get-FileHash. Cleared, Windows PowerShell rebuilds its own.
set "PSModulePath="
powershell -NoProfile -ExecutionPolicy Bypass -Command "$m = Get-ChildItem -LiteralPath '%~dp0backups' -Filter manifest.json -Recurse -ErrorAction Stop | Sort-Object LastWriteTime | Select-Object -Last 1; if (-not $m) { throw 'No backup found in .\backups.' }; Write-Host ('Restoring ' + $m.FullName); & '%~dp0scripts\restore_vive.ps1' -ManifestPath $m.FullName"
echo.
if not "%~1"=="nopause" pause
