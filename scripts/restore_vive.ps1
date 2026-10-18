param([Parameter(Mandatory=$true)][string]$ManifestPath)
$ErrorActionPreference = 'Stop'
if (Get-Process Cyberpunk2077 -ErrorAction SilentlyContinue) { throw 'Close Cyberpunk 2077 before restoring.' }
$ManifestPath = (Resolve-Path -LiteralPath $ManifestPath).Path
$backup = Split-Path $ManifestPath -Parent
$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
$root = (Resolve-Path -LiteralPath $manifest.GameRoot).Path.TrimEnd('\') + '\'
# Validate every path and backup before restoring any file. This deliberately restores
# the pre-test settings, including changes made from the mod's UI during testing.
foreach ($entry in $manifest.Files) {
    $target = [IO.Path]::GetFullPath((Join-Path $root $entry.RelativePath))
    $saved = [IO.Path]::GetFullPath((Join-Path $backup $entry.RelativePath))
    if (-not $target.StartsWith($root,[StringComparison]::OrdinalIgnoreCase) -or
        -not $saved.StartsWith($backup.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid path in restore manifest.' }
    if ($entry.Existed -and (Get-FileHash -LiteralPath $saved).Hash -ne $entry.OriginalHash) { throw "Backup hash mismatch: $saved" }
}
foreach ($entry in $manifest.Files) {
    $target = Join-Path $root $entry.RelativePath
    if ($entry.Existed) {
        Copy-Item -LiteralPath (Join-Path $backup $entry.RelativePath) -Destination $target -Force
        if ((Get-FileHash -LiteralPath $target).Hash -ne $entry.OriginalHash) { throw 'Restored file verification failed.' }
    } elseif (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
}
Write-Host 'Restored the plugin and settings from before the Vive test. Captured images remain available.'
