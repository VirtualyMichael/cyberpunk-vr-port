// Independent D3D12/OpenXR display test. No game hooks or API-layer registration.
#define XR_USE_GRAPHICS_API_D3D12
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <vector>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <algorithm>
using Microsoft::WRL::ComPtr;
void XR(XrResult r,const char* label){if(XR_FAILED(r)){printf("FAIL %s: %d\n",label,r);throw std::runtime_error(label);}}
void DX(HRESULT r){if(FAILED(r)){printf("FAIL D3D12: %08X\n",unsigned(r));throw std::runtime_error("D3D12");}}
int main()try {
    setvbuf(stdout,nullptr,_IONBF,0);
    printf("Vive independent D3D12/OpenXR checkerboard. Runs for 60 seconds. ESC exits.\n");
    const char* ext=XR_KHR_D3D12_ENABLE_EXTENSION_NAME;
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};strcpy_s(ci.applicationInfo.applicationName,"Vive D3D12 Pattern");
    ci.applicationInfo.apiVersion=XR_CURRENT_API_VERSION;ci.enabledExtensionCount=1;ci.enabledExtensionNames=&ext;
    XrInstance instance{};XR(xrCreateInstance(&ci,&instance),"create instance");
    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};XR(xrGetInstanceProperties(instance,&props),"runtime properties");printf("Runtime: %s\n",props.runtimeName);
    XrSystemGetInfo sys{XR_TYPE_SYSTEM_GET_INFO};sys.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system{};XR(xrGetSystem(instance,&sys,&system),"get headset");
    PFN_xrGetD3D12GraphicsRequirementsKHR requirements{};
    XR(xrGetInstanceProcAddr(instance,"xrGetD3D12GraphicsRequirementsKHR",reinterpret_cast<PFN_xrVoidFunction*>(&requirements)),"get graphics requirements function");
    XrGraphicsRequirementsD3D12KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};XR(requirements(instance,system,&req),"graphics requirements");
    ComPtr<IDXGIFactory4> factory;DX(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter;DX(factory->EnumAdapterByLuid(req.adapterLuid,IID_PPV_ARGS(&adapter)));
    DXGI_ADAPTER_DESC1 ad{};DX(adapter->GetDesc1(&ad));printf("Adapter: %ls\n",ad.Description);
    ComPtr<ID3D12Device> device;DX(D3D12CreateDevice(adapter.Get(),req.minFeatureLevel,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};DX(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    XrGraphicsBindingD3D12KHR bind{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};bind.device=device.Get();bind.queue=queue.Get();
    XrSessionCreateInfo sc{XR_TYPE_SESSION_CREATE_INFO};sc.next=&bind;sc.systemId=system;
    XrSession session{};XR(xrCreateSession(instance,&sc,&session),"create D3D12 session");
    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};spaceInfo.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;spaceInfo.poseInReferenceSpace.orientation.w=1;
    XrSpace space{};XR(xrCreateReferenceSpace(session,&spaceInfo,&space),"create local space");
    uint32_t n{};XR(xrEnumerateSwapchainFormats(session,0,&n,nullptr),"formats count");std::vector<int64_t> formats(n);
    XR(xrEnumerateSwapchainFormats(session,n,&n,formats.data()),"formats");
    const auto fmt=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    if(std::find(formats.begin(),formats.end(),fmt)==formats.end())throw std::runtime_error("runtime lacks RGBA8 SRGB");
    constexpr int w=1800,h=2000;
    std::array<XrSwapchain,2> swapchains{};std::array<std::vector<XrSwapchainImageD3D12KHR>,2> images;
    for(int e=0;e<2;++e){
        XrSwapchainCreateInfo s{XR_TYPE_SWAPCHAIN_CREATE_INFO};s.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        s.format=fmt;s.sampleCount=1;s.width=w;s.height=h;s.faceCount=s.arraySize=s.mipCount=1;
        XR(xrCreateSwapchain(session,&s,&swapchains[e]),"create eye swapchain");
        XR(xrEnumerateSwapchainImages(swapchains[e],0,&n,nullptr),"image count");images[e].assign(n,{XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        XR(xrEnumerateSwapchainImages(swapchains[e],n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(images[e].data())),"images");
    }
    ComPtr<ID3D12CommandAllocator> allocator;DX(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;DX(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));DX(list->Close());
    ComPtr<ID3D12DescriptorHeap> heap;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=2;DX(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
    ComPtr<ID3D12Fence> fence;DX(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));uint64_t serial{};
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("fence event");
    bool running=false,done=false,exitAsked=false,stopping=false;uint64_t rendered{};
    const auto start=GetTickCount64();
    while(!done && GetTickCount64()-start<65000){
        XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
        while(xrPollEvent(instance,&ev)==XR_SUCCESS){
            if(ev.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){
                auto state=reinterpret_cast<XrEventDataSessionStateChanged*>(&ev)->state;printf("Session state: %d\n",int(state));
                if(state==XR_SESSION_STATE_READY && !running){XrSessionBeginInfo b{XR_TYPE_SESSION_BEGIN_INFO};b.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;XR(xrBeginSession(session,&b),"begin session");running=true;}
                if(state==XR_SESSION_STATE_STOPPING){stopping=true;done=true;}
                if(state==XR_SESSION_STATE_EXITING || state==XR_SESSION_STATE_LOSS_PENDING)done=true;
            }
            ev={XR_TYPE_EVENT_DATA_BUFFER};
        }
        if(done)break;
        if(!exitAsked && (GetTickCount64()-start>60000 || (GetAsyncKeyState(VK_ESCAPE)&0x8000))){XR(xrRequestExitSession(session),"request exit");exitAsked=true;}
        if(!running){Sleep(5);continue;}
        XrFrameState fs{XR_TYPE_FRAME_STATE};XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};XR(xrWaitFrame(session,&wait,&fs),"wait frame");
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};XR(xrBeginFrame(session,&begin),"begin frame");
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;locate.displayTime=fs.predictedDisplayTime;locate.space=space;
        XrViewState state{XR_TYPE_VIEW_STATE};XrView views[2]={{XR_TYPE_VIEW},{XR_TYPE_VIEW}};uint32_t count{};
        XR(xrLocateViews(session,&locate,&state,2,&count,views),"locate views");
        XrCompositionLayerProjectionView pv[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
        const auto valid=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
        const bool draw=fs.shouldRender && count==2 && (state.viewStateFlags&valid)==valid;
        if(draw){
            DX(allocator->Reset());DX(list->Reset(allocator.Get(),nullptr));
            for(int e=0;e<2;++e){
                uint32_t index{};XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};XR(xrAcquireSwapchainImage(swapchains[e],&acquire,&index),"acquire image");
                XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wi.timeout=2000000000;
                const auto waited=xrWaitSwapchainImage(swapchains[e],&wi);if(waited!=XR_SUCCESS)throw std::runtime_error("image wait failed/timed out");
                D3D12_RENDER_TARGET_VIEW_DESC rd{};rd.Format=fmt;rd.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
                auto handle=heap->GetCPUDescriptorHandleForHeapStart();handle.ptr+=e*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
                device->CreateRenderTargetView(images[e][index].texture,&rd,handle);
                const float dark[4]={.015f,.015f,.015f,1},white[4]={.8f,.8f,.8f,1};
                const float tint[4]={e?.5f:0.f,e?.15f:.4f,e?0.f:.5f,1};
                list->ClearRenderTargetView(handle,dark,0,nullptr);
                for(int y=0;y<10;++y)for(int x=0;x<8;++x)if((x+y)%2==0){D3D12_RECT r{x*w/8,y*h/10,(x+1)*w/8,(y+1)*h/10};list->ClearRenderTargetView(handle,tint,1,&r);}
                D3D12_RECT cross[2]={{w/2-3,0,w/2+3,h},{0,h/2-3,w,h/2+3}};list->ClearRenderTargetView(handle,white,2,cross);
                pv[e].pose=views[e].pose;pv[e].fov=views[e].fov;pv[e].subImage={swapchains[e],{{0,0},{w,h}},0};
            }
            DX(list->Close());ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);DX(queue->Signal(fence.Get(),++serial));
            DX(fence->SetEventOnCompletion(serial,event));if(WaitForSingleObject(event,2000)!=WAIT_OBJECT_0)throw std::runtime_error("GPU fence timeout");
            for(auto s:swapchains){XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};XR(xrReleaseSwapchainImage(s,&release),"release image");}
        }
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};layer.space=space;layer.viewCount=2;layer.views=pv;
        const auto* ptr=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=fs.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount=draw?1:0;end.layers=draw?&ptr:nullptr;XR(xrEndFrame(session,&end),"end frame");
        if(draw && rendered++==0)printf("PATTERN VISIBLE: cyan left eye, orange right eye, crisp checkerboards and white cross.\n");
    }
    if(stopping)XR(xrEndSession(session),"end session");
    for(auto s:swapchains)XR(xrDestroySwapchain(s),"destroy swapchain");
    xrDestroySpace(space);xrDestroySession(session);xrDestroyInstance(instance);CloseHandle(event);
    printf("Completed %llu rendered frames. Visual confirmation still required.\n",static_cast<unsigned long long>(rendered));
    return rendered?0:2;
}catch(const std::exception& e){printf("Stopped: %s\n",e.what());return 1;}
