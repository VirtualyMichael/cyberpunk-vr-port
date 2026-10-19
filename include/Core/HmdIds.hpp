#pragma once
// Launcher headset IDs that code OTHER than the launcher table needs to recognise.
//
// The ID is what the launcher saves as hmd_type in vrport-launcher.ini and what GetCurrentHmdType()
// returns. IDs are append-only: never renumber or reuse one. Upstream took 15 for the MeganeX in 0.1.8, so
// the Vive family sits at 16 and 17 -- a build from before that (0.1.7-vive2) saved 15 and 16, and a saved
// value that no longer matches its headset's ladder is moved to the nearest-cost entry by the launcher.
//
// The launcher's own table (src/Overlay/LauncherDialog.cpp, kHmdPresets) is the source of truth for every
// other headset; only add an ID here when some other file must test for it.
namespace cvr::hmd {
inline constexpr int kViveOriginal = 16;
inline constexpr int kVivePro = 17;
inline constexpr bool IsViveFamily(int hmdType) { return hmdType == kViveOriginal || hmdType == kVivePro; }
}
