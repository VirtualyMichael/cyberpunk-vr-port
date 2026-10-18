#include "Render/EyeImageCapture.hpp"
#include "Render/CommandResources.hpp"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <limits>
#include <stdexcept>

extern void Log(const char*, ...);
namespace cvr::diagnostic {
namespace {
bool Rgba(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_R8G8B8A8_TYPELESS || f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
}
bool Bgra(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_B8G8R8A8_TYPELESS || f == DXGI_FORMAT_B8G8R8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
}
void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* r,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    list->ResourceBarrier(1, &b);
}
void WriteBmp(const std::filesystem::path& path, UINT width, UINT height, const std::vector<uint8_t>& bgra) {
    BITMAPFILEHEADER file{}; BITMAPINFOHEADER info{};
    file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(bgra.size());
    info.biSize = sizeof(info); info.biWidth = static_cast<LONG>(width);
    info.biHeight = -static_cast<LONG>(height); info.biPlanes = 1; info.biBitCount = 32;
    info.biCompression = BI_RGB; info.biSizeImage = static_cast<DWORD>(bgra.size());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info), sizeof(info));
    out.write(reinterpret_cast<const char*>(bgra.data()), bgra.size());
    if (!out) throw std::runtime_error("cannot write diagnostic BMP");
}
}
void EyeImageCapture::Paths() {
    if (!base_.empty()) return;
    wchar_t path[32768]{};
    const auto n = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (!n || n == std::size(path)) throw std::runtime_error("executable path unavailable");
    base_ = std::filesystem::path(path).parent_path();
}
bool EyeImageCapture::TakeRequest() {
    if (recorded_ || fence_) return false;
    const auto now = GetTickCount64();
    if (lastRequestCheck_ && now - lastRequestCheck_ < 500) return false;
    lastRequestCheck_ = now;
    try {
        Paths();
        // Delete consumes one request. Record logs a rejection instead of retrying
        // every frame if the format or dimensions cannot be captured safely.
        return DeleteFileW((base_ / L"vive-capture.request").c_str()) != FALSE;
    } catch (const std::exception& e) {
        Log("[VIVE-CAPTURE] request failed: %s\n", e.what()); return false;
    }
}
bool EyeImageCapture::Record(ID3D12Device* device, ID3D12GraphicsCommandList* list,
    ID3D12Resource* const sources[2], ID3D12Resource* const submitted[2],
    const XrCompositionLayerProjectionView views[2], uint64_t serial,
    float cameraH, float cameraV, float mainProjectionYY) {
    if (!device || !list || recorded_ || fence_) return false;
    try {
        Paths(); images_ = {};
        ID3D12Resource* resources[] = {sources[0], sources[1], submitted[0], submitted[1]};
        uint64_t total{};
        for (unsigned i = 0; i < 4; ++i) {
            if (!resources[i]) throw std::runtime_error("missing eye image");
            auto& image = images_[i]; image.desc = resources[i]->GetDesc();
            const auto& d = image.desc;
            if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.DepthOrArraySize != 1 ||
                d.MipLevels != 1 || d.SampleDesc.Count != 1 || !d.Width || !d.Height ||
                !(Rgba(d.Format) || Bgra(d.Format)))
                throw std::runtime_error("capture requires single-sample 8-bit RGBA/BGRA eye images");
            device->GetCopyableFootprints(&d, 0, 1, 0, &image.footprint, nullptr, nullptr, &image.bytes);
            total += image.bytes;
            if (total > 128ull * 1024 * 1024) throw std::runtime_error("readback exceeds 128 MiB limit; lower resolution for capture");
            D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC buffer{}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width = image.bytes; buffer.Height = 1; buffer.DepthOrArraySize = 1;
            buffer.MipLevels = 1; buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&image.readback))))
                throw std::runtime_error("readback allocation failed");
            if (!cvr::gpu::KeepCommandResources(list, {resources[i], image.readback.Get()}))
                throw std::runtime_error("cannot retain capture resources through GPU completion");
        }
        output_ = base_ / L"vive-diagnostics" / (L"frame-" + std::to_wstring(serial) + L"-" + std::to_wstring(GetTickCount64()));
        std::ostringstream json; json << std::setprecision(9);
        json << "{\n  \"serial\": " << serial << ",\n  \"camera_horizontal_deg\": " << cameraH
             << ",\n  \"camera_vertical_deg\": " << cameraV << ",\n  \"observed_main_projection_yy\": " << mainProjectionYY
             << ",\n  \"note\": \"prepared sources versus final swapchains; not a raw engine render-target capture\",\n  \"views\": [";
        for (unsigned i = 0; i < 2; ++i) {
            const auto& v = views[i];
            json << (i ? "," : "") << "\n    {\"fov_radians_LRUD\": [" << v.fov.angleLeft << ',' << v.fov.angleRight << ',' << v.fov.angleUp << ',' << v.fov.angleDown
                 << "], \"position\": [" << v.pose.position.x << ',' << v.pose.position.y << ',' << v.pose.position.z
                 << "], \"orientation_xyzw\": [" << v.pose.orientation.x << ',' << v.pose.orientation.y << ',' << v.pose.orientation.z << ',' << v.pose.orientation.w
                 << "], \"rect_xywh\": [" << v.subImage.imageRect.offset.x << ',' << v.subImage.imageRect.offset.y << ',' << v.subImage.imageRect.extent.width << ',' << v.subImage.imageRect.extent.height << "]}";
        }
        json << "\n  ],\n  \"images\": [";
        for (unsigned i = 0; i < 4; ++i) {
            const auto& d = images_[i].desc;
            json << (i ? "," : "") << "{\"width\":" << d.Width << ",\"height\":" << d.Height << ",\"dxgi_format\":" << int(d.Format) << '}';
        }
        json << "],\n"; metadata_ = json.str();
        // Allocation and metadata preparation finish before recording any GPU work.
        for (unsigned i = 0; i < 4; ++i) {
            if (i >= 2) Transition(list, resources[i], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION from{}; from.pResource = resources[i]; from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION to{}; to.pResource = images_[i].readback.Get();
            to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint = images_[i].footprint;
            list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
            if (i >= 2) Transition(list, resources[i], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
        recorded_ = true;
        Log("[VIVE-CAPTURE] recorded stereo frame %llu; awaiting GPU completion\n", static_cast<unsigned long long>(serial));
        return true;
    } catch (const std::exception& e) {
        images_ = {}; Log("[VIVE-CAPTURE] rejected: %s\n", e.what()); return false;
    }
}
void EyeImageCapture::Submitted(ID3D12Fence* fence, uint64_t value) {
    if (!recorded_ || fence_ || !fence) return;
    fence_ = fence; value_ = value;
}
void EyeImageCapture::Reset() {
    // CommandResources retains recorded GPU references until completion.
    if (recorded_) Log("[VIVE-CAPTURE] pending capture cancelled before readback\n");
    images_ = {}; fence_.Reset(); recorded_ = false; value_ = 0;
}
void EyeImageCapture::Poll() {
    if (!recorded_ || !fence_) return;
    const auto complete = fence_->GetCompletedValue();
    if (complete < value_) return;
    try {
        if (complete == std::numeric_limits<uint64_t>::max()) throw std::runtime_error("device removed before capture completed");
        std::filesystem::create_directories(output_);
        std::array<std::vector<uint8_t>, 4> pixels;
        const wchar_t* names[] = {L"source-left.bmp", L"source-right.bmp", L"submitted-left.bmp", L"submitted-right.bmp"};
        for (unsigned i = 0; i < 4; ++i) {
            auto& image = images_[i]; const auto w = static_cast<UINT>(image.desc.Width), h = image.desc.Height;
            pixels[i].resize(size_t(w) * h * 4);
            void* data{}; const D3D12_RANGE read{0, static_cast<SIZE_T>(image.bytes)};
            if (FAILED(image.readback->Map(0, &read, &data)) || !data) throw std::runtime_error("readback mapping failed");
            for (UINT y = 0; y < h; ++y) {
                const auto* row = static_cast<const uint8_t*>(data) + image.footprint.Offset + size_t(y) * image.footprint.Footprint.RowPitch;
                auto* dst = pixels[i].data() + size_t(y) * w * 4;
                for (UINT x = 0; x < w; ++x) {
                    dst[x*4] = row[x*4 + (Rgba(image.desc.Format) ? 2 : 0)];
                    dst[x*4+1] = row[x*4+1]; dst[x*4+2] = row[x*4 + (Rgba(image.desc.Format) ? 0 : 2)];
                    dst[x*4+3] = row[x*4+3];
                }
            }
            const D3D12_RANGE none{}; image.readback->Unmap(0, &none);
            WriteBmp(output_ / names[i], w, h, pixels[i]);
        }
        const bool equalLeft = images_[0].desc.Width == images_[2].desc.Width && images_[0].desc.Height == images_[2].desc.Height && pixels[0] == pixels[2];
        const bool equalRight = images_[1].desc.Width == images_[3].desc.Width && images_[1].desc.Height == images_[3].desc.Height && pixels[1] == pixels[3];
        std::ofstream json(output_ / L"capture.json");
        json << metadata_ << "  \"source_equals_submitted\": [" << (equalLeft ? "true" : "false") << ',' << (equalRight ? "true" : "false") << "]\n}\n";
        json.close(); if (!json) throw std::runtime_error("cannot write capture metadata");
        Log("[VIVE-CAPTURE] saved %ls; exact source/final pixels left=%d right=%d\n", output_.c_str(), equalLeft, equalRight);
    } catch (const std::exception& e) { Log("[VIVE-CAPTURE] save failed: %s\n", e.what()); }
    images_ = {}; fence_.Reset(); recorded_ = false;
}
}
