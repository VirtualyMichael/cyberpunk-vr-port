# Installs the Vive build over an existing CyberpunkVRPort 0.1.7 install, with a byte-for-byte backup
# that restore_vive.ps1 can put back. -Headset picks the launcher preset (see LauncherDialog.cpp):
#   Vive    -> hmd_type 15, HTC Vive (Original)
#   VivePro -> hmd_type 16, HTC Vive Pro
# Both default to 1800x2000 per eye and to xr_hud_stereo_depth=1 (HUD as one real panel in front of
# you; 0 shows it at optical infinity, which reads as a doubled, cross-eyed HUD on these headsets).
param(
    [Parameter(Mandatory=$true)][string]$GameRoot,
    [ValidateSet('Vive','VivePro')][string]$Headset = 'Vive',
    [string]$DllPath = (Join-Path $PSScriptRoot '..\build\bin\red4ext\plugins\CyberpunkVR_Stereo\Release\CyberpunkVR_Stereo.dll'),
    [string]$BackupRoot = (Join-Path $PSScriptRoot '..\..\vive-backups')
)
$ErrorActionPreference = 'Stop'
if (Get-Process Cyberpunk2077 -ErrorAction SilentlyContinue) { throw 'Close Cyberpunk 2077 before installing.' }
$GameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
$DllPath = (Resolve-Path -LiteralPath $DllPath).Path
if (-not (Test-Path -LiteralPath (Join-Path $GameRoot 'bin\x64\Cyberpunk2077.exe'))) { throw 'Game executable missing.' }
$archive = Join-Path $GameRoot 'archive\pc\mod\cyberpunkvrport.archive'
if ((Get-FileHash -LiteralPath $archive).Hash -ne 'EDED1057AA7DF6E8DA31CB77F9BF06ABC7B06B675F852C8FF3E2AEFB7D31445C') {
    throw 'This update requires the checked 0.1.7 camera archive. Installation stopped before changing files.'
}
$utf8 = New-Object Text.UTF8Encoding $false
function Set-IniValues([string]$Text, [System.Collections.IDictionary]$Values) {
    foreach ($key in $Values.Keys) {
        $pattern = '(?m)^\s*' + [regex]::Escape($key) + '\s*=.*$'
        $line = $key + '=' + $Values[$key]
        if ([regex]::IsMatch($Text, $pattern)) { $Text = [regex]::Replace($Text, $pattern, $line) }
        else { $Text = $Text.TrimEnd() + "`r`n" + $line + "`r`n" }
    }
    return $Text
}
$launcherRel = 'bin\x64\vrport-launcher.ini'
$configRel = 'bin\x64\vrport.ini'
$cameraRel = 'bin\x64\plugins\cyber_engine_tweaks\mods\CyberpunkVRPort_Stereo\vrcam.json'
$pluginRel = 'red4ext\plugins\CyberpunkVR_Stereo\CyberpunkVR_Stereo.dll'
$hmdType = if ($Headset -eq 'VivePro') { '16' } else { '15' }
$launcher = Set-IniValues ([IO.File]::ReadAllText((Join-Path $GameRoot $launcherRel))) ([ordered]@{
    width='1800'; height='2000'; hmd_type=$hmdType; debug='0'; show_launcher='1'
})
$config = Set-IniValues ([IO.File]::ReadAllText((Join-Path $GameRoot $configRel))) ([ordered]@{
    xr_runtime='1'; xr_force_fov='0.000'; xr_world_scale='1.000'; xr_ipd_scale='1.000';
    xr_stereo_scale='1.000'; xr_mono_submit='1'; xr_input_actions='1'; xr_render_pose_submit='1';
    xr_pose_lag='1'; xr_framegen='0'; xr_motion_predict_ms='0.00'; xr_menu_rect='0'; first_launch='0';
    xr_hud_stereo_depth='1'
})
$cameraText = [IO.File]::ReadAllText((Join-Path $GameRoot $cameraRel))
$camera = $cameraText | ConvertFrom-Json
if ($camera.components -notcontains 'vrcam_1800x2000') { throw '1800x2000 camera missing from installed catalogue.' }
$cameraText = [regex]::Replace($cameraText, '("component"\s*:\s*")[^"]*(")', '${1}vrcam_1800x2000${2}')
$cameraText = [regex]::Replace($cameraText, '("virtualCamera"\s*:\s*")[^"]*(")', '${1}vrcam_feed_1800x2000${2}')
$backup = Join-Path ([IO.Path]::GetFullPath($BackupRoot)) ('before-vive-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$entries = @()
foreach ($rel in @($pluginRel,$launcherRel,$configRel,$cameraRel)) {
    $target = Join-Path $GameRoot $rel
    $saved = Join-Path $backup $rel
    $exists = Test-Path -LiteralPath $target
    $hash = $null
    if ($exists) {
        New-Item -ItemType Directory -Path (Split-Path $saved -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $target -Destination $saved
        $hash = (Get-FileHash -LiteralPath $target).Hash
        if ((Get-FileHash -LiteralPath $saved).Hash -ne $hash) { throw 'Backup verification failed.' }
    }
    $entries += [pscustomobject]@{RelativePath=$rel; Existed=$exists; OriginalHash=$hash; InstalledHash=$null}
}
$manifest = [pscustomobject]@{GameRoot=$GameRoot; Build='0.1.7-vive2'; Files=$entries}
$manifestPath = Join-Path $backup 'manifest.json'
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 5), $utf8)
try {
    Copy-Item -LiteralPath $DllPath -Destination (Join-Path $GameRoot $pluginRel) -Force
    [IO.File]::WriteAllText((Join-Path $GameRoot $launcherRel), $launcher, $utf8)
    [IO.File]::WriteAllText((Join-Path $GameRoot $configRel), $config, $utf8)
    [IO.File]::WriteAllText((Join-Path $GameRoot $cameraRel), $cameraText, $utf8)
    if ((Get-FileHash -LiteralPath (Join-Path $GameRoot $pluginRel)).Hash -ne (Get-FileHash -LiteralPath $DllPath).Hash) { throw 'Installed DLL differs from the built DLL.' }
    foreach ($entry in $entries) { $entry.InstalledHash = (Get-FileHash -LiteralPath (Join-Path $GameRoot $entry.RelativePath)).Hash }
    [IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 5), $utf8)
} catch {
    foreach ($entry in $entries) {
        $target = Join-Path $GameRoot $entry.RelativePath
        if ($entry.Existed) { Copy-Item -LiteralPath (Join-Path $backup $entry.RelativePath) -Destination $target -Force }
        elseif (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
    }
    throw
}
Write-Host "Installed 0.1.7-vive2: $Headset (hmd_type $hmdType), 1800x2000, SteamVR OpenXR, HUD as a panel, frame generation off."
Write-Host "Verified DLL SHA256: $((Get-FileHash -LiteralPath $DllPath).Hash)"
Write-Host "Restore manifest: $manifestPath"
