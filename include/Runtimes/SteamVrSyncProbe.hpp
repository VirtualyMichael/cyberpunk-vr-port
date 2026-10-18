#pragma once

struct ID3D12Device;
struct ID3D12CommandQueue;

namespace cvr {
// Logs, as [VIVE-SYNC] lines, whether this process can build the keyed-mutex shared texture
// SteamVR's compositor client needs before it accepts a submitted frame. See the .cpp.
void RunSteamVrSyncProbe(const char* when, ID3D12Device* gameDevice, ID3D12CommandQueue* gameQueue);

// Once per session, ~20 s after it begins: reads SteamVR's own client log for THIS process
// (Steam\logs\xrclient_Cyberpunk2077.txt) and logs a [VIVE-SYNC] WARNING if SteamVR refused our
// frames. SteamVR does not report that refusal through OpenXR -- xrEndFrame keeps succeeding -- so
// this is the only place the port can see it. Silent when the log is absent (not SteamVR).
void ScheduleSteamVrDeliveryCheck();
}
