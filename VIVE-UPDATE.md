# HTC Vive and Vive Pro support (0.1.8-vive1)

This adds launcher presets for the **HTC Vive (Original)** and the **HTC Vive Pro**, usable Valve Index (Knuckles) controllers on those headsets, and a check that names the cause of the "blurry, zoomed-in image that only moves with your head" problem.

Based on upstream **0.1.8** (commit `9aa2601`). It is a small layer on top of that release: one replacement `CyberpunkVR_Stereo.dll`, a few settings, and scripts.

| | |
|---|---|
| Tested in the headset on 0.1.7 | **HTC Vive Pro**, SteamVR 2.17.10 (OpenXR), AMD RX 7900 XTX, driver 31.0.24002.92 |
| Needs a test on 0.1.8 | everything below. 0.1.8 changes the renderer (asymmetric projection), so earlier results do not carry over. |
| Not covered | HTC Vive Pro 2, Vive Cosmos, Vive Focus. They have different panels and FOV. See [Adding another headset](#adding-another-headset). |

## Installing

1. Install **upstream CyberpunkVRPort 0.1.8** first, exactly as its own `INSTALL.txt` says (extract it into the game folder, and delete the three old 0.1.7 archives it lists). You need its prerequisites: RED4ext, CET, redscript, TweakXL, ArchiveXL and Codeware. Start SteamVR before the game.
2. Close the game. Extract this release's zip to any folder.
3. Double-click **`Install-Vive.cmd`**. Paste your Cyberpunk 2077 folder (the one that contains `bin` and `archive`), then type `Vive` or `VivePro`.

   Or, from PowerShell in the extracted folder:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\scripts\install_vive.ps1 -GameRoot 'C:\Path\To\Cyberpunk 2077' -Headset VivePro -DllPath .\CyberpunkVR_Stereo.dll -BackupRoot .\backups
   ```

   The installer:
   - checks that your camera archive is the stock 0.1.8 one, and stops before changing anything if it isn't;
   - backs up every file it replaces and prints a restore manifest;
   - selects the headset, 1816×1976 per eye, SteamVR (OpenXR) and automatic FOV;
   - sets `xr_hud_stereo_depth=1`, turns frame generation off, and leaves your other settings alone.
4. Start SteamVR, then the game. The launcher should show your headset and **1816 x 1976**.

To undo, close the game and double-click **`Restore-Before-Vive.cmd`**. It restores the most recent backup in `.\backups`. Alternatively, run `scripts\restore_vive.ps1 -ManifestPath '<the manifest path the installer printed>'`.

Optional: **`Run-Headset-Test.cmd`** shows a 60-second checkerboard through SteamVR without the game. If the squares are sharp, your headset, GPU and SteamVR are fine. **`Capture-Eye-Images.cmd`** saves both eye images for a bug report.

### Headset IDs and resolution choices

Upstream 0.1.8 took ID 15 for the MeganeX superlight 8K, so the Vive presets use **16 and 17**. IDs are saved in `vrport-launcher.ini`; a save from the earlier 0.1.7 Vive build (15 and 16) is not carried over, so run the installer again or pick your headset in the launcher.

| Headset | ID | Sizes offered (first = default) |
|---|---|---|
| HTC Vive (Original) | 16 | 1816×1976, 1456×1584, 1272×1384, 1088×1184 |
| HTC Vive Pro | 17 | 1816×1976, 2064×2232, 1456×1584, 1272×1384 |

The 0.1.8 launcher can only offer sizes that exist in its camera archive, and each Vive size here costs the same number of pixels as the native Vive size it replaces: 1816×1976 ≈ 1800×2000 (the size both headsets were tested at), 1456×1584 ≈ 1440×1600 (the Vive Pro panel), 1088×1184 ≈ 1080×1200 (the OG panel) and 2064×2232 ≈ 2016×2240 (SteamVR's own 100% on a Vive Pro).

They are the Valve Index sizes. A Vive Pro's measured lens window has a width/height of about **0.897**, the Index's is **0.920**, and no 0.897-shaped size exists in the archive yet, so the pixels come out about 2.5% wider than square. If Vive-shaped sizes are added with `tools/gen_vrcam_assets.py`, switch the ladders to them.

## Troubleshooting

### The headset shows a blurry, zoomed-in mess that still moves when you turn your head

**SteamVR is refusing the game's frames.** It keeps showing one old startup frame and reprojecting it as you move. That's why the image "tracks" but has no detail. No FOV or resolution setting changes it.

How it was found:

- SteamVR's compositor log (`Steam\logs\vrcompositor.txt`) counted **6 frames** for a 90-second session. A standalone OpenXR test on the same PC sent 5,375.
- SteamVR's client log for the game (`Steam\logs\xrclient_Cyberpunk2077.txt`) showed
  `Failed to create sync texture. Ensure application was built using DXGI 1.1 or later`.
  SteamVR prints this only once. After that it rejects every frame, while still telling the game that each frame succeeded.
- The failing sessions ran **AMD driver 32.0.31041.1004**. This is the same driver named in upstream issues [#78](https://github.com/dariulone/cyberpunk-vr-port/issues/78) and [#97](https://github.com/dariulone/cyberpunk-vr-port/issues/97). The first working session ran **31.0.24002.92**, with 9,284 frames delivered and no sync-texture error.

This build checks for the problem automatically. About 20 seconds into a VR session, look in `bin\x64\cyberpunkvrport.log` for:

- `[VIVE-SYNC] SteamVR accepted this game's frames` — delivery is fine.
- `[VIVE-SYNC] WARNING: SteamVR REFUSED this game's frames` — change your GPU driver version (update or roll back), then restart SteamVR and the game.

Startup also logs `[VIVE-SYNC]` lines that repeat SteamVR's D3D11 keyed-mutex steps inside the game. Include them in any bug report.

> **Not yet proven:** the driver change and the start-up probe arrived in the same test session. The evidence points at the driver. Confirming it needs one run of an older build on the new driver; reports either way are welcome.

### HUD looks doubled or cross-eyed

Upstream 0.1.8 turned **HUD stereo depth** on by default, which is the fix: the HUD is one flat panel hanging about 1.5 m in front of you instead of two copies at optical infinity. If you upgraded with an older `vrport.ini` that has `xr_hud_stereo_depth=0`, the Vive installer sets it to 1. It is also under **F10 → HUD → Stereo depth**. `xr_hud_distance` and `xr_hud_fov` set how far away and how large the panel is.

0.1.8 also lists fixes for popups and quest markers that doubled the picture. If some in-world markers (shop or objective markers) still look doubled on a Vive, please report it with a `Capture-Eye-Images.cmd` capture taken about 3 m from the marker.

### Other checks

- Set SteamVR's per-app resolution for Cyberpunk to 100%. The mod picks its own render size.
- PCs with a CPU iGPU and a dedicated GPU: in Windows Settings → Display → Graphics, set `Cyberpunk2077.exe` to the dedicated GPU, as SteamVR's own programs already are.

## Valve Index (Knuckles) controllers

Vive and Vive Pro owners often use Index controllers. Two things were wrong with them, and both are handled in this build.

**There is no usable menu button.** Index's button that looks like a menu button is `system`, which SteamVR keeps for its own dashboard. The game never receives it, so "press Start to continue" and the pause menu were unreachable. **Press either trackpad firmly** and the game sees Start.

| Index control | In game |
|---|---|
| Trigger, grip, thumbstick, thumbstick click | as on Touch |
| A / B, left hand | X / Y |
| A / B, right hand | A / B |
| **Either trackpad, pressed** | **Start / menu** |
| Left stick click + right stick | D-pad (existing chord) |
| Both stick clicks | the mod's F10 overlay (existing) |

You can also rebind per game in SteamVR: the controller bindings page for Cyberpunk 2077.

**Hands were at the wrong angle.** Every hand, arm and weapon calculation was tuned on Touch controllers. SteamVR places the Index grip differently: 9.7° pitch and about 14 cm back from the tracking origin, against Touch's 20.6° and about 10 cm. The build now asks the runtime which controller is in your hand and shifts the Index grip to the Touch-equivalent pose. The numbers come from SteamVR's own render-model files, and `tools/vive_tests` checks that the corrected Index grip equals the Touch grip exactly.

Look for these lines in `bin\x64\cyberpunkvrport.log`:

- `[GRIP] controller profile /interaction_profiles/valve/index_controller ... grip corrected` — the correction is active.
- `[GRIP] hand N raw grip-vs-aim angle X deg` — a measurement. SteamVR's data predicts about **50°** for Index and **60°** for Touch. If an Index reports something else, the table in `include/Runtimes/ControllerGrip.hpp` doesn't match what your runtime sends; please post the number.

If the hands still look rotated, the **Wrist** sliders in the F10 overlay still apply on top. 0.1.8 stores wrist rotations as offsets from its tuned base, so they read 0 at the start.

To add another controller, add one entry to `kControllerGrips` in `ControllerGrip.hpp` (the file's header explains where to find the numbers).

## Adding another headset

A headset preset only chooses the per-eye render size. FOV, IPD, eye poses and controller bindings all come from the OpenXR runtime. Since 0.1.8 each eye renders exactly its own lens window, so the number that matters is the window's shape. The step-by-step method, with the Vive numbers worked through, is the comment block **"HOW TO ADD A HEADSET"** above `kHmdPresets` in `src/Overlay/LauncherDialog.cpp`. In short:

1. Run once with any preset and copy the `OpenXRManager[FOV]: raw left=(...) right=(...)` line from `bin\x64\cyberpunkvrport.log`.
2. Compute `window AR = (tan(outer) + tan(inner)) / (tan(up) + tan(down))`.
3. Pick sizes with that shape that exist in `vrcam.json`'s `components` list, or add new ones with `tools/gen_vrcam_assets.py` and a WolvenKit repack.
4. Add a resolution array and one `kHmdPresets` row with the next unused ID (18 and up). Never renumber existing IDs.
5. Extend `tools/launcher_tests/main.cpp` and run it.

## For developers

- `src/Overlay/LauncherDialog.cpp`, `include/Core/HmdIds.hpp`: Vive (16) and Vive Pro (17) presets, plus the headset-authoring guide.
- `src/Runtimes/SteamVrSyncProbe.cpp`: the start-up keyed-mutex probe and the SteamVR delivery check, including what the disassembly of `vrclient_x64.dll` showed.
- `src/Runtimes/HudQuad.cpp`: notes on the two HUD placement modes.
- `include/Runtimes/ControllerGrip.hpp`: per-controller grip correction (Index today), with the recipe for adding more; Index bindings are in `OpenXRManager.cpp`.
- `include/Runtimes/RuntimeFovCorrection.hpp`, `src/Runtimes/OpenXRFrameLoop.cpp`: aspect-correct menu quads, which upstream still draws square.
- `src/Render/EyeImageCapture.cpp`, `scripts/capture_vive.ps1`: one-shot capture of both eye images for Vive presets.
- `tools/xr_probe/vive_pattern.cpp`: standalone D3D12/OpenXR checkerboard. Use it to check that the headset and runtime work without the game.
- Tests: `tools/launcher_tests` (preset IDs, ladders, save/reload, migration of old saves) and `tools/vive_tests`.
