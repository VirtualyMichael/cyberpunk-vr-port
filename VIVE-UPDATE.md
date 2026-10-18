# HTC Vive and Vive Pro support (0.1.7-vive2)

This update adds launcher presets for the **HTC Vive (Original)** and the **HTC Vive Pro**. It changes the HUD default for these headsets. It also adds a check that names the cause of the "blurry, zoomed-in image that only moves with your head" problem.

Based on upstream commit `a0c23fe` (0.1.7).

| | |
|---|---|
| Tested in the headset | **HTC Vive Pro**, SteamVR 2.17.10 (OpenXR), AMD RX 7900 XTX, driver 31.0.24002.92, 1800×2000 per eye |
| Built from the same measurements, not retested since the fix | HTC Vive (Original) |
| Not covered | HTC Vive Pro 2, Vive Cosmos, Vive Focus. They have different panels and FOV. See [Adding another headset](#adding-another-headset). |

## Installing

You need a working CyberpunkVRPort **0.1.7** install (RED4ext, CET, ArchiveXL and the rest of its requirements) and SteamVR set as your OpenXR runtime.

1. Close the game.
2. From PowerShell, in the folder containing this update:

   ```powershell
   & .\scripts\install_vive.ps1 -GameRoot 'C:\Path\To\Cyberpunk 2077' -Headset VivePro -DllPath .\CyberpunkVR_Stereo.dll
   ```

   Use `-Headset Vive` for the original Vive. The installer:
   - checks that your camera archive is the stock 0.1.7 one, and stops before changing anything if it isn't;
   - backs up every file it replaces and prints a restore manifest;
   - selects the headset, 1800×2000 per eye, SteamVR (OpenXR) and automatic FOV;
   - sets `xr_hud_stereo_depth=1` (see [HUD](#hud-looks-doubled-or-cross-eyed));
   - turns frame generation off.
3. Start SteamVR, then the game. The launcher should show your headset and **1800 x 2000**.

To undo, with the game closed:

```powershell
& .\scripts\restore_vive.ps1 -ManifestPath '<the manifest path the installer printed>'
```

### Resolution choices

| Headset | Launcher entry | Sizes offered (first = default) |
|---|---|---|
| Vive (Original), ID 15 | HTC Vive (Original) | 1800×2000, 1440×1600, 1260×1400, 1080×1200 |
| Vive Pro, ID 16 | HTC Vive Pro | 1800×2000, 2296×2552, 1440×1600, 1260×1400 |

All sizes have the 0.9 width:height shape that these lenses need. The launcher can only offer sizes that already exist in the mod's camera archive. That's why SteamVR's own 100% sizes (1852×2056 and 2016×2240) are not listed; the nearest authored sizes around them are.

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

> **Not yet proven:** the driver change and this build's start-up probe arrived in the same test session. The evidence points at the driver. Confirming it needs one run of the previous build (0.1.7-vive1) on the new driver; reports either way are welcome.

### HUD looks doubled or cross-eyed

In-game: **F10 → HUD → Stereo depth** (on). In the config file: `xr_hud_stereo_depth=1` in `bin\x64\vrport.ini`. The Vive installer sets it on.

- **On:** the HUD is one flat panel hanging about 1.5 m in front of you (`xr_hud_distance`). It shows real depth.
- **Off (upstream default):** each eye gets its own copy, shifted so the HUD sits at optical infinity, behind everything it covers. On a Vive this reads as a doubled, cross-eyed HUD as soon as you look at anything nearby.

`xr_hud_distance` and `xr_hud_fov` in the same menu set how far away and how large the panel is.

### Other checks

- Set SteamVR's per-app resolution for Cyberpunk to 100%. The mod picks its own render size.
- PCs with a CPU iGPU and a dedicated GPU: in Windows Settings → Display → Graphics, set `Cyberpunk2077.exe` to the dedicated GPU, as SteamVR's own programs already are.

## Adding another headset

A headset preset only chooses the per-eye render size. FOV, IPD, eye poses and controller bindings all come from the OpenXR runtime. The step-by-step method, with the Vive numbers worked through as an example, is the comment block **"HOW TO ADD A HEADSET"** above `kHmdPresets` in `src/Overlay/LauncherDialog.cpp`. In short:

1. Run once with any preset and copy the `OpenXRManager[FOV]: raw left=(...) right=(...)` line from `bin\x64\cyberpunkvrport.log`.
2. Compute `aspect = tan((|L|+R)/2) / tan(max(U,|D|))`.
3. Pick sizes with that aspect that exist in `vrcam.json`'s `components` list.
4. Add a resolution array and one `kHmdPresets` row with the next unused ID (17 and up). Never renumber existing IDs.
5. Extend `tools/launcher_tests/main.cpp` and run it.

## For developers

- `src/Overlay/LauncherDialog.cpp`: Vive (15) and Vive Pro (16) presets, plus the headset-authoring guide.
- `src/Runtimes/SteamVrSyncProbe.cpp`: the start-up keyed-mutex probe and the SteamVR delivery check, including what the disassembly of `vrclient_x64.dll` showed.
- `src/Runtimes/HudQuad.cpp`: notes on the two HUD placement modes.
- `include/Runtimes/RuntimeFovCorrection.hpp`, `src/Runtimes/OpenXRFrameLoop.cpp`: centred submitted FOV and aspect-correct menu quads (from 0.1.7-vive1).
- `src/Render/EyeImageCapture.cpp`, `scripts/capture_vive.ps1`: one-shot capture of both eye images for Vive presets.
- `tools/xr_probe/vive_pattern.cpp`: standalone D3D12/OpenXR checkerboard. Use it to check that the headset and runtime work without the game.
- Tests: `tools/launcher_tests` (preset IDs, ladders, save/reload) and `tools/vive_tests`.
