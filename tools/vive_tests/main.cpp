#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cmath>
#include <cstdarg>
#include "Runtimes/OpenXRInternal.hpp"
#include "Render/EyeImageCapture.hpp"
#include "Render/CommandResources.hpp"
#include "Runtimes/ControllerGrip.hpp"
using Microsoft::WRL::ComPtr;
volatile int g_verboseLog=0;
extern "C" int CyberpunkVR_RuntimeDiagnostics=0;
float forced{},gameH{},gameV{};
extern "C" float GetForcedFov(){return forced;}
extern "C" float GetGameRenderFovDeg(){return gameH;}
extern "C" float GetGameRenderVerticalFovDeg(){return gameV;}
void Log(const char* fmt,...){va_list args;va_start(args,fmt);vprintf(fmt,args);va_end(args);}
void Check(bool v,const char* reason){if(!v)throw std::runtime_error(reason);}
void Hr(HRESULT h){Check(SUCCEEDED(h),"Direct3D operation failed");}
void Near(float a,float b){Check(std::abs(a-b)<0.00001f,"projection mismatch");}
constexpr float rad=3.1415926535f/180.f;
void Projection() {
    const XrFovf raw[2]={{-54.487f*rad,51.257f*rad,55.744f*rad,-55.843f*rad},
                        {-51.278f*rad,54.291f*rad,55.573f*rad,-55.816f*rad}};
    gameH=108.974f;gameV=2*atan(tan(gameH*rad/2)/.9f)/rad;
    for(int eye=0;eye<2;++eye) {
        auto f=ApplyForcedProjectionFov(raw[eye],raw,eye,1800,2000);
        Near(f.angleLeft,-gameH*rad/2);Near(f.angleRight,gameH*rad/2);
        Near(f.angleUp,gameV*rad/2);Near(f.angleDown,-gameV*rad/2);
        Near(tan(f.angleRight)/tan(f.angleUp),.9f);
    }
    forced=100;auto f=ApplyForcedProjectionFov(raw[0],raw,0,1440,1600);
    Near(f.angleRight,50*rad);Near(tan(f.angleRight)/tan(f.angleUp),.9f);
    auto menu=MakeCenteredImageFov(65.4f,.9f);Near(tan(menu.angleRight)/tan(menu.angleUp),.9f);
    forced=0;gameH=gameV=0;
    f=ApplyForcedProjectionFov(raw[0],nullptr,0,0,0);
    Near(f.angleLeft,raw[0].angleLeft);Near(f.angleUp,raw[0].angleUp);
    const auto corrected=ComputeRuntimeFovCorrection(raw[0],raw[1]);
    f=ApplyForcedProjectionFov(raw[1],raw,1,1800,2000);Near(f.angleLeft,corrected.eye[1].angleLeft);
    std::cout<<"PASS actual submit FOV with measured Vive asymmetry, forced aspect, menu aspect and startup fallback\n";
}
void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};list->ResourceBarrier(1,&x);
}
std::vector<uint8_t> Read(const std::filesystem::path& p){
    std::ifstream f(p,std::ios::binary);Check(bool(f),"missing capture output");return {std::istreambuf_iterator<char>(f),{}};
}
void Capture(ID3D12Device* device,DXGI_FORMAT format,bool mismatch,unsigned test) {
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Hr(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> alloc;Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));
    ComPtr<ID3D12GraphicsCommandList> list;Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list)));
    ComPtr<ID3D12Resource> src[2],dst[2],upload[2];
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=36;d.Height=24;
    d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Format=format;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    for(unsigned eye=0;eye<2;++eye) {
        Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&src[eye])));
        auto target=d;target.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&target,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&dst[eye])));
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes{};device->GetCopyableFootprints(&d,0,1,0,&footprint,nullptr,nullptr,&bytes);
        D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=bytes;b.Height=1;b.DepthOrArraySize=1;b.MipLevels=1;b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES u{};u.Type=D3D12_HEAP_TYPE_UPLOAD;
        Hr(device->CreateCommittedResource(&u,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload[eye])));
        void* data{};D3D12_RANGE none{};Hr(upload[eye]->Map(0,&none,&data));
        for(unsigned y=0;y<24;++y)for(unsigned x=0;x<36;++x) {
            auto* p=static_cast<uint8_t*>(data)+y*footprint.Footprint.RowPitch+x*4;
            p[0]=uint8_t(x+eye*70);p[1]=uint8_t(y+20);p[2]=uint8_t(200-x);p[3]=255;
        }
        upload[eye]->Unmap(0,nullptr);
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=upload[eye].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;
        D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=src[eye].Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        Barrier(list.Get(),src[eye].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
    }
    for(unsigned eye=0;eye<2;++eye){
        list->CopyResource(dst[eye].Get(),src[mismatch && eye==1 ? 0 : eye].Get());
        Barrier(list.Get(),dst[eye].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    auto base=std::filesystem::current_path()/(L"capture-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(test));
    std::filesystem::create_directories(base);std::ofstream(base/L"vive-capture.request")<<"test";
    cvr::diagnostic::EyeImageCapture capture(base);
    Check(capture.TakeRequest() && !std::filesystem::exists(base/L"vive-capture.request"),"request was not consumed");
    ID3D12Resource* sources[]={src[0].Get(),src[1].Get()};ID3D12Resource* outputs[]={dst[0].Get(),dst[1].Get()};
    XrCompositionLayerProjectionView views[2]{};for(auto& v:views){v.fov=MakeCenteredImageFov(100,1.5f);v.pose.orientation.w=1;v.subImage.imageRect.extent={36,24};}
    Check(capture.Record(device,list.Get(),sources,outputs,views,test,100,76,1),"capture did not record");
    Check(!capture.Record(device,list.Get(),sources,outputs,views,test,100,76,1),"capture overwrote an in-flight readback");
    capture.Poll();Check(!std::filesystem::exists(base/L"vive-diagnostics"),"mapped before GPU submission");
    Hr(list->Close());ID3D12CommandList* lists[]={list.Get()};auto refs=cvr::gpu::PrepareCommandResources(1,lists);
    queue->ExecuteCommandLists(1,lists);cvr::gpu::SubmitCommandResources(queue.Get(),std::move(refs));
    ComPtr<ID3D12Fence> fence;Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    Hr(queue->Signal(fence.Get(),1));capture.Submitted(fence.Get(),1);
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(event!=nullptr,"event failed");Hr(fence->SetEventOnCompletion(1,event));
    const auto result=WaitForSingleObject(event,10000);CloseHandle(event);Check(result==WAIT_OBJECT_0,"GPU capture timed out");
    capture.Poll();cvr::gpu::CollectCommandResources();
    const auto folder=std::filesystem::directory_iterator(base/L"vive-diagnostics")->path();
    auto left=Read(folder/L"source-left.bmp"),right=Read(folder/L"source-right.bmp");
    Check(left.size()==54+36*24*4 && left[0]=='B' && left[1]=='M',"BMP encoding invalid");
    const bool rgba=format==DXGI_FORMAT_R8G8B8A8_UNORM;
    for(unsigned y=0;y<24;++y)for(unsigned x=0;x<36;++x){
        auto off=54+(y*36+x)*4;Check(left[off]==(rgba?200-x:x) && left[off+1]==y+20 && left[off+2]==(rgba?x:200-x),"row pitch or channel conversion broken");
    }
    Check(left==Read(folder/L"submitted-left.bmp"),"left eye copy differs");
    Check((right==Read(folder/L"submitted-right.bmp"))!=mismatch,"mismatch detector failed");
    const auto metadata=Read(folder/L"capture.json");const std::string json(metadata.begin(),metadata.end());
    Check(json.find(mismatch?"[true,false]":"[true,true]")!=std::string::npos,"capture metadata comparison wrong");
    std::cout<<"PASS GPU capture format="<<int(format)<<" deliberate-mismatch="<<mismatch<<" (row pitch, pixels, fence, metadata)\n";
}
// A controller's grip, corrected, must equal the Touch grip exactly: pose_actual * G_actual^-1 * G_touch is the
// Touch-equivalent pose of the SAME physical controller. Checked for an arbitrary device pose on both hands.
void GripCorrection() {
    const auto* index=cvr::grip::FindControllerGrip("/interaction_profiles/valve/index_controller");
    Check(index!=nullptr,"Index grip entry missing");
    Check(cvr::grip::FindControllerGrip("/interaction_profiles/oculus/touch_controller")==nullptr,"Touch must not be corrected");
    for(int hand=0;hand<2;++hand) {
        const XrQuaternionf deviceRot=[]{const float a=0.7f,b=-0.4f,c=1.1f;const XrQuaternionf qx{std::sin(a/2),0,0,std::cos(a/2)},qy{0,std::sin(b/2),0,std::cos(b/2)},qz{0,0,std::sin(c/2),std::cos(c/2)};return MultiplyQuat(MultiplyQuat(qy,qx),qz);}();
        const XrPosef device{deviceRot,{0.3f,1.2f,-0.5f}};
        // The grip each runtime would report = device * its local grip transform.
        auto gripOf=[&](const cvr::grip::GripLocal& g){
            const XrPosef local{cvr::grip::PitchQuat(g.pitchDeg),{g.origin[0],g.origin[1],g.origin[2]}};
            const auto off=RotateVector(device.orientation,local.position);
            return XrPosef{MultiplyQuat(device.orientation,local.orientation),{device.position.x+off.x,device.position.y+off.y,device.position.z+off.z}};
        };
        const auto reportedIndex=gripOf(index->grip[hand]);
        const auto wanted=gripOf(cvr::grip::kTouchGrip[hand]);
        const auto fixed=cvr::grip::ApplyGripCorrection(reportedIndex,cvr::grip::GripCorrection(index->grip[hand],cvr::grip::kTouchGrip[hand]));
        auto close=[](float a,float b){return std::abs(a-b)<0.00002f;};
        Check(close(fixed.position.x,wanted.position.x)&&close(fixed.position.y,wanted.position.y)&&close(fixed.position.z,wanted.position.z),"corrected Index grip position != Touch grip");
        const float dot=std::abs(fixed.orientation.x*wanted.orientation.x+fixed.orientation.y*wanted.orientation.y+fixed.orientation.z*wanted.orientation.z+fixed.orientation.w*wanted.orientation.w);
        Check(dot>0.999999f,"corrected Index grip orientation != Touch grip");
    }
    // The documented size of the difference: 10.9 degrees of pitch, about 4.6 cm of origin.
    const auto c=cvr::grip::GripCorrection(index->grip[0],cvr::grip::kTouchGrip[0]);
    const float deg=2*std::asin(std::abs(c.orientation.x))/rad;
    Check(std::abs(deg-10.9f)<0.01f,"Index->Touch pitch difference changed");
    std::cout<<"PASS Index grip correction maps the Index grip onto the Touch grip on both hands ("<<deg<<" deg pitch)\n";
}
int main()try {
    Projection();GripCorrection();ComPtr<ID3D12Debug> debug;
    if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter;Hr(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));
    DXGI_ADAPTER_DESC1 desc{};Hr(adapter->GetDesc1(&desc));std::wcout<<L"GPU: "<<desc.Description<<L"\n";
    ComPtr<ID3D12Device> device;Hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    Capture(device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,false,1);
    Capture(device.Get(),DXGI_FORMAT_B8G8R8A8_UNORM,true,2);
    ComPtr<ID3D12InfoQueue> info;
    if(SUCCEEDED(device.As(&info)))for(UINT64 n=0;n<info->GetNumStoredMessages();++n){
        SIZE_T size{};info->GetMessage(n,nullptr,&size);std::vector<uint8_t> data(size);auto* msg=reinterpret_cast<D3D12_MESSAGE*>(data.data());Hr(info->GetMessage(n,msg,&size));
        if(msg->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::cerr<<msg->pDescription<<'\n';throw std::runtime_error("D3D12 validation error");}
    }
    std::cout<<(debug?"PASS D3D12 debug validation":"SKIP D3D12 debug validation: Windows debug layer unavailable")<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
