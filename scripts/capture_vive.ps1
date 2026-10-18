param([Parameter(Mandatory=$true)][string]$GameRoot)
$ErrorActionPreference = 'Stop'
$target = Join-Path $GameRoot 'bin\x64\vive-capture.request'
if (-not (Test-Path -LiteralPath (Join-Path $GameRoot 'bin\x64\Cyberpunk2077.exe'))) { throw 'Game executable missing.' }
[IO.File]::WriteAllText($target, 'Capture next gameplay stereo pair.')
Write-Host 'Capture requested. With Vive selected and frame generation off, the next gameplay stereo pair will be saved under bin\x64\vive-diagnostics.'
