#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <openxr/openxr.h>
#include <array>
#include <filesystem>
#include <string>
#include <utility>
#include <cstdint>

namespace cvr::diagnostic {
// One-shot, opt-in readback of the prepared eye images and the final XR images.
// No GPU waits and no changes to pixels. Kept off the normal frame path unless
// vive-capture.request exists beside the game executable.
class EyeImageCapture {
public:
    explicit EyeImageCapture(std::filesystem::path base = {}) : base_(std::move(base)) {}
    bool TakeRequest();
    bool Record(ID3D12Device*, ID3D12GraphicsCommandList*,
        ID3D12Resource* const sources[2], ID3D12Resource* const submitted[2],
        const XrCompositionLayerProjectionView views[2], uint64_t serial,
        float cameraH, float cameraV, float mainProjectionYY);
    void Submitted(ID3D12Fence*, uint64_t value);
    void Poll();
    void Reset();
private:
    struct Image {
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        D3D12_RESOURCE_DESC desc{};
        uint64_t bytes{};
    };
    void Paths();
    std::filesystem::path base_, output_;
    std::array<Image, 4> images_{};
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    uint64_t value_{}, lastRequestCheck_{};
    bool recorded_{};
    std::string metadata_;
};
}
