// SteamVrSyncProbe -- repeats, inside this process, the steps SteamVR's compositor client takes to
// build the keyed-mutex "sync texture" it needs before it will show a submitted frame.
//
// WHY IT EXISTS -- the "zoomed in, blurry, colours move with your head" Vive report.
//
// In every failing session SteamVR's own OpenXR client log (Steam/logs/xrclient_Cyberpunk2077.txt)
// said "Failed to create sync texture. Ensure application was built using DXGI 1.1 or later", and
// vrcompositor.txt counted SIX presents for a ninety-second session (the standalone vive_pattern test
// on the same machine: 5,375). Disassembly of vrclient_x64.dll shows that message is printed ONCE and
// every later submit is then refused with VRCompositorError 106, while xrEndFrame keeps returning
// XR_SUCCESS to us. The headset was showing one stale startup frame, reprojected as the head moved --
// which is exactly a blurry smear that "tracks", and why no FOV/projection change ever affected it.
//
// The fault was outside the port: the failing sessions ran AMD driver 32.0.31041.1004 (the same
// driver named in upstream issues #78 and #97); the first working session ran 31.0.24002.92, with
// 9,284 presents and no sync-texture error. This file keeps two cheap checks so the next person
// hitting it gets a log line instead of a blurry headset:
//   * RunSteamVrSyncProbe repeats the D3D11 keyed-mutex steps in-process and logs each HRESULT;
//   * ScheduleSteamVrDeliveryCheck reads SteamVR's own verdict back out of its client log.

#include "Runtimes/SteamVrSyncProbe.hpp"
#include "Core/VrCoreShared.hpp"

#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <cstdio>
#include <cwchar>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "advapi32.lib")

namespace cvr {
namespace {

template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

void ProbeKeyedMutexTexture(const char* when, ID3D11Device* device, const char* label, UINT misc) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = 16; td.Height = 16; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.MiscFlags = misc;
    ID3D11Texture2D* tex = nullptr;
    const HRESULT hrTex = device->CreateTexture2D(&td, nullptr, &tex);
    HRESULT hrMutex = E_FAIL, hrHandle = E_FAIL, hrAcquire = E_FAIL;
    if (SUCCEEDED(hrTex) && tex) {
        IDXGIKeyedMutex* mutex = nullptr;
        hrMutex = tex->QueryInterface(IID_PPV_ARGS(&mutex));
        if (mutex) {
            hrAcquire = mutex->AcquireSync(0, 0);
            if (SUCCEEDED(hrAcquire)) mutex->ReleaseSync(0);
        }
        SafeRelease(mutex);
        HANDLE shared = nullptr;
        if (misc & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) {
            IDXGIResource1* r1 = nullptr;
            hrHandle = tex->QueryInterface(IID_PPV_ARGS(&r1));
            if (r1) hrHandle = r1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &shared);
            SafeRelease(r1);
            if (shared) CloseHandle(shared);
        } else {
            IDXGIResource* r = nullptr;
            hrHandle = tex->QueryInterface(IID_PPV_ARGS(&r));
            if (r) hrHandle = r->GetSharedHandle(&shared);
            SafeRelease(r);
        }
    }
    SafeRelease(tex);
    Log("[VIVE-SYNC] %s %s: CreateTexture2D=0x%08X keyedMutex=0x%08X acquire=0x%08X sharedHandle=0x%08X\n",
        when, label, (unsigned)hrTex, (unsigned)hrMutex, (unsigned)hrAcquire, (unsigned)hrHandle);
}

}  // namespace

void RunSteamVrSyncProbe(const char* when, ID3D12Device* gameDevice, ID3D12CommandQueue* gameQueue) {
    __try {
        [&]() {
            const LUID gameLuid = gameDevice ? gameDevice->GetAdapterLuid() : LUID{};
            IDXGIFactory1* factory1 = nullptr;
            const HRESULT hrF1 = CreateDXGIFactory1(IID_PPV_ARGS(&factory1));
            IDXGIFactory4* factory4 = nullptr;
            const HRESULT hrF2 = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory4));
            Log("[VIVE-SYNC] %s CreateDXGIFactory1=0x%08X CreateDXGIFactory2=0x%08X gameLuid=%08X:%08X\n",
                when, (unsigned)hrF1, (unsigned)hrF2,
                (unsigned)gameLuid.HighPart, (unsigned)gameLuid.LowPart);
            IDXGIAdapter1* match = nullptr;
            if (factory1) {
                for (UINT i = 0;; ++i) {
                    IDXGIAdapter1* adapter = nullptr;
                    if (factory1->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND || !adapter) break;
                    DXGI_ADAPTER_DESC1 d{};
                    adapter->GetDesc1(&d);
                    const bool same = d.AdapterLuid.HighPart == gameLuid.HighPart &&
                                      d.AdapterLuid.LowPart == gameLuid.LowPart;
                    Log("[VIVE-SYNC] %s adapter[%u] \"%ls\" luid=%08X:%08X vram=%lluMB flags=0x%X%s\n",
                        when, i, d.Description, (unsigned)d.AdapterLuid.HighPart,
                        (unsigned)d.AdapterLuid.LowPart,
                        (unsigned long long)(d.DedicatedVideoMemory >> 20), d.Flags,
                        same ? "  <- game device" : "");
                    if (same && !match) { match = adapter; adapter = nullptr; }
                    SafeRelease(adapter);
                }
            }
            if (match) {
                const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
                ID3D11Device* d11 = nullptr;
                D3D_FEATURE_LEVEL got{};
                const HRESULT hrDev = D3D11CreateDevice(match, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                    D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2, D3D11_SDK_VERSION, &d11, &got, nullptr);
                Log("[VIVE-SYNC] %s D3D11CreateDevice(game adapter)=0x%08X level=0x%X\n",
                    when, (unsigned)hrDev, (unsigned)got);
                if (d11) {
                    ProbeKeyedMutexTexture(when, d11, "own D3D11 keyedmutex", D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX);
                    ProbeKeyedMutexTexture(when, d11, "own D3D11 keyedmutex+nthandle",
                        D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE);
                    ProbeKeyedMutexTexture(when, d11, "own D3D11 shared", D3D11_RESOURCE_MISC_SHARED);
                }
                SafeRelease(d11);
            }
            if (gameDevice && gameQueue) {
                ID3D11Device* on12 = nullptr;
                IUnknown* queues[] = {gameQueue};
                const HRESULT hrOn12 = D3D11On12CreateDevice(gameDevice, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                    nullptr, 0, queues, 1, 0, &on12, nullptr, nullptr);
                Log("[VIVE-SYNC] %s D3D11On12CreateDevice(game device)=0x%08X\n", when, (unsigned)hrOn12);
                if (on12) ProbeKeyedMutexTexture(when, on12, "11on12 keyedmutex", D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX);
                SafeRelease(on12);
            }
            SafeRelease(match);
            SafeRelease(factory4);
            SafeRelease(factory1);
        }();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[VIVE-SYNC] %s probe raised exception 0x%08X\n", when, GetExceptionCode());
    }
}

}  // namespace cvr

namespace cvr {
namespace {

// Steam's install folder from the registry (HKCU\Software\Valve\Steam, SteamPath). SteamVR writes
// its per-application OpenXR client logs to <Steam>\logs\xrclient_<exe name>.txt.
bool SteamLogPath(wchar_t* out, DWORD outChars) {
    wchar_t steam[MAX_PATH]{};
    DWORD bytes = sizeof(steam);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ,
                     nullptr, steam, &bytes) != ERROR_SUCCESS) return false;
    wchar_t exe[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return false;
    const wchar_t* name = wcsrchr(exe, L'\\');
    name = name ? name + 1 : exe;
    wchar_t stem[MAX_PATH]{};
    wcsncpy_s(stem, name, _TRUNCATE);
    if (wchar_t* dot = wcsrchr(stem, L'.')) *dot = 0;
    return swprintf_s(out, outChars, L"%s\\logs\\xrclient_%s.txt", steam, stem) > 0;
}

DWORD WINAPI DeliveryCheckThread(void*) {
    Sleep(20000);   // past session start and the first submits, where SteamVR prints its verdict
    wchar_t path[MAX_PATH * 2]{};
    if (!SteamLogPath(path, _countof(path))) return 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 0;   // not SteamVR, or it logs elsewhere: say nothing
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    // The current session is at the end of the file; 256 KB covers it with room to spare.
    const LONGLONG want = size.QuadPart < (256 << 10) ? size.QuadPart : (256 << 10);
    LARGE_INTEGER at{};
    at.QuadPart = size.QuadPart - want;
    std::string text(static_cast<size_t>(want), '\0');
    DWORD got = 0;
    const bool ok = SetFilePointerEx(file, at, nullptr, FILE_BEGIN) &&
                    ReadFile(file, text.data(), static_cast<DWORD>(want), &got, nullptr);
    CloseHandle(file);
    if (!ok) return 0;
    text.resize(got);
    // Only THIS process's block counts: earlier sessions in the same file are history.
    char marker[64]{};
    sprintf_s(marker, "startup with PID=%lu,", GetCurrentProcessId());
    const size_t start = text.rfind(marker);
    if (start == std::string::npos) {
        Log("[VIVE-SYNC] SteamVR client log has no entry for this process yet; delivery not checked.\n");
        return 0;
    }
    if (text.find("Failed to create sync texture", start) != std::string::npos) {
        Log("[VIVE-SYNC] WARNING: SteamVR REFUSED this game's frames (\"Failed to create sync texture\" in "
            "Steam\\logs\\xrclient_*.txt). The headset will show a blurry, stale image that only moves with "
            "your head. This is outside the mod: it was seen with AMD driver 32.0.31041.1004 and cleared "
            "on 31.0.24002.92. Update or roll back the GPU driver, then restart SteamVR and the game.\n");
    } else {
        Log("[VIVE-SYNC] SteamVR accepted this game's frames (no sync-texture error for this process).\n");
    }
    return 0;
}

}  // namespace

void ScheduleSteamVrDeliveryCheck() {
    static LONG s_scheduled = 0;
    if (InterlockedExchange(&s_scheduled, 1)) return;
    if (HANDLE t = CreateThread(nullptr, 0, DeliveryCheckThread, nullptr, 0, nullptr)) CloseHandle(t);
}

}  // namespace cvr
