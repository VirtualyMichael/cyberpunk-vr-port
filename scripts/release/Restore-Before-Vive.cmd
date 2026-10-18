@echo off
rem Puts back the files from the most recent install in .\backups (game must be closed).
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -Command "$m = Get-ChildItem -LiteralPath '%~dp0backups' -Filter manifest.json -Recurse -ErrorAction Stop | Sort-Object LastWriteTime | Select-Object -Last 1; if (-not $m) { throw 'No backup found in .\backups.' }; Write-Host ('Restoring ' + $m.FullName); & '%~dp0scripts\restore_vive.ps1' -ManifestPath $m.FullName"
echo.
pause
