#include "NativeBridge.h"
#include "PairHistory.h"
#include "ControllerInput.h"
#include "DirectionConfig.h"
#include <windows.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <wrl/client.h>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <filesystem>
#include <fstream>
#include <mutex>

using Microsoft::WRL::ComPtr;
namespace NativeBridge {
namespace {
std::recursive_mutex mutex;
Transport::Header* trackingChannel{};
HANDLE frameReadyEvent{};
Transport::RenderInfo renderInfo{};
uint64_t trackingId{};
void Log(const char* fmt, ...) {
    char line[1024]; va_list args; va_start(args,fmt);
    vsnprintf_s(line,sizeof(line),_TRUNCATE,fmt,args); va_end(args);
    FILE* f{}; if (!fopen_s(&f,"DeusExHRVR.log","a")) {
        fprintf(f,"[%llu] %s\n",GetTickCount64(),line); fclose(f);
    }
}
bool Good(XrResult r,const char* op) {
    if (XR_FAILED(r)) { Log("%s failed: %d",op,r); return false; }
    return true;
}
struct Bridge {
    XrInstance instance{}; XrSystemId system{}; XrSession session{};
    XrSpace space{}, headSpace{}; XrSwapchain swapchain{};
    XrSessionState state=XR_SESSION_STATE_UNKNOWN;
    bool running=false, anchorValid=false, failed=false;
    bool levelScreen=true; // Local patch: [VR] LevelScreen, place the virtual screen level
    UINT width{}, height{}; DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    std::vector<XrSwapchainImageD3D11KHR> images;
    XrPosef anchor{{0,0,0,1},{0,0,-2}};
    uint64_t pairs=0, sourceFrame=0, nextRetry=0;
    HeadsetDisplay::Settings display{};
    PFN_xrGetDisplayRefreshRateFB getRefresh{};
    uint64_t rateTick=0,ratePairs=0;
    XrDuration lastPeriod{};
    PairHistory history;
    ControllerInput controller;
    bool historyArmed{},f10Down{};
    IDXGISwapChain* owner{};
    // Local patch (frozen-view reset): while the game's camera is stopped (in-game
    // menu, loading screens) every frame repeats one head pose, so the image stays
    // where it froze and the headset's reset cannot bring it in front. On a reset,
    // turn and move that frozen view onto the current head, like the virtual screen.
    bool resetPending{};XrTime resetTime{};
    uint64_t frozenId{};uint32_t frozenRepeats{};
    bool frozenShift{};float frozenTurn{};XrVector3f frozenFrom{},frozenTo{};

    // Heading about +Y of an orientation, from its forward (-Z) axis, or from its
    // right axis when looking straight up or down.
    static float Heading(const XrQuaternionf& q) {
        float fx=-2.f*(q.x*q.z+q.w*q.y),fz=-(1.f-2.f*(q.x*q.x+q.y*q.y));
        if(std::hypot(fx,fz)<.001f){float rx=1.f-2.f*(q.y*q.y+q.z*q.z),rz=2.f*(q.x*q.z-q.w*q.y);return std::atan2(-rz,rx);}
        return std::atan2(-fx,-fz);
    }
    void FrozenReset(const Transport::Tracking& t,XrTime now,bool recenter) {
        if(t.id!=frozenId){frozenId=t.id;frozenRepeats=0;frozenShift=false;}
        else ++frozenRepeats;
        if(recenter){resetPending=true;resetTime=now;}
        if(!resetPending || now<resetTime)return;
        resetPending=false;
        if(!frozenRepeats)return; // a live view already follows the reset on its next frame
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        if(!Good(xrLocateSpace(headSpace,space,now,&loc),"xrLocateSpace") || (loc.locationFlags&3)!=3)return;
        XrQuaternionf frozen{};memcpy(&frozen,&t.head.orientation,sizeof(frozen));
        frozenTurn=Heading(loc.pose.orientation)-Heading(frozen);
        memcpy(&frozenFrom,&t.head.position,sizeof(frozenFrom));
        frozenTo=loc.pose.position;frozenShift=true;
        Log("Frozen view re-placed after reset: pose=%llu turned %.1f deg",t.id,double(frozenTurn)*57.29577951);
    }
    // Turn a pose about the frozen head by frozenTurn and move that head onto the
    // current one. Offsets from the head (the eyes) are kept exactly.
    XrPosef Shift(XrPosef p) const {
        float c=std::cos(frozenTurn),s=std::sin(frozenTurn);
        float dx=p.position.x-frozenFrom.x,dy=p.position.y-frozenFrom.y,dz=p.position.z-frozenFrom.z;
        p.position={frozenTo.x+c*dx+s*dz,frozenTo.y+dy,frozenTo.z-s*dx+c*dz};
        XrQuaternionf r{0,std::sin(frozenTurn*.5f),0,std::cos(frozenTurn*.5f)},q=p.orientation;
        p.orientation={r.w*q.x+r.x*q.w+r.y*q.z-r.z*q.y,r.w*q.y-r.x*q.z+r.y*q.w+r.z*q.x,
                       r.w*q.z+r.x*q.y-r.y*q.x+r.z*q.w,r.w*q.w-r.x*q.x-r.y*q.y-r.z*q.z};
        return p;
    }

    void DestroySwapchain() {
        images.clear();
        if(swapchain) xrDestroySwapchain(swapchain);
        swapchain=XR_NULL_HANDLE; width=height=0;
    }
    void Reset() {
        history.Reset();historyArmed=false;f10Down=false;
        DestroySwapchain();
        controller.Reset();
        if(headSpace) xrDestroySpace(headSpace);
        if(space) xrDestroySpace(space);
        if(session) xrDestroySession(session);
        if(instance) xrDestroyInstance(instance);
        headSpace=space=XR_NULL_HANDLE; session=XR_NULL_HANDLE; instance=XR_NULL_HANDLE;
        system=XR_NULL_SYSTEM_ID; running=anchorValid=false;
        context.Reset(); device.Reset(); owner=nullptr;
        state=XR_SESSION_STATE_UNKNOWN;
        display={};getRefresh=nullptr;lastPeriod=0;rateTick=ratePairs=0;
    }
    bool Init(ID3D11Device* dev) {
        uint32_t n=0;
        if(!Good(xrEnumerateInstanceExtensionProperties(nullptr,0,&n,nullptr),"xrEnumerateInstanceExtensionProperties"))return false;
        std::vector<XrExtensionProperties> ext(n,{XR_TYPE_EXTENSION_PROPERTIES});
        if(!Good(xrEnumerateInstanceExtensionProperties(nullptr,n,&n,ext.data()),"xrEnumerateInstanceExtensionProperties"))return false;
        if(std::none_of(ext.begin(),ext.end(),[](const auto& e){return !strcmp(e.extensionName,XR_KHR_D3D11_ENABLE_EXTENSION_NAME);})) {
            Log("Runtime lacks XR_KHR_D3D11_enable"); return false;
        }
        std::vector<const char*> enabled{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
        bool refreshExtension=std::any_of(ext.begin(),ext.end(),[](const auto& e){return !strcmp(e.extensionName,XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);});
        if(refreshExtension)enabled.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
        XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(ci.applicationInfo.applicationName,"DeusExHRVR native stereo");
        ci.applicationInfo.applicationVersion=1; ci.applicationInfo.apiVersion=XR_API_VERSION_1_0;
        ci.enabledExtensionCount=uint32_t(enabled.size()); ci.enabledExtensionNames=enabled.data();
        if(!Good(xrCreateInstance(&ci,&instance),"xrCreateInstance"))return false;
        XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
        if(Good(xrGetInstanceProperties(instance,&ip),"xrGetInstanceProperties"))Log("%u-bit runtime: %s",unsigned(sizeof(void*)*8),ip.runtimeName);
        XrSystemGetInfo gi{XR_TYPE_SYSTEM_GET_INFO}; gi.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        if(!Good(xrGetSystem(instance,&gi,&system),"xrGetSystem (headset must be connected)"))return false;
        XrViewConfigurationView viewConfig[2]{{XR_TYPE_VIEW_CONFIGURATION_VIEW},{XR_TYPE_VIEW_CONFIGURATION_VIEW}};
        uint32_t views=0;
        if(!Good(xrEnumerateViewConfigurationViews(instance,system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,2,&views,viewConfig),"xrEnumerateViewConfigurationViews") || views!=2)return false;
        display.magic=HeadsetDisplay::Magic;
        for(auto& v:viewConfig){display.width=std::max(display.width,v.recommendedImageRectWidth);display.height=std::max(display.height,v.recommendedImageRectHeight);}
        if(!HeadsetDisplay::Valid(display)){Log("Headset dimensions exceed the native pair texture limits");return false;}
        PFN_xrGetD3D11GraphicsRequirementsKHR requirements{};
        if(!Good(xrGetInstanceProcAddr(instance,"xrGetD3D11GraphicsRequirementsKHR",reinterpret_cast<PFN_xrVoidFunction*>(&requirements)),"xrGetInstanceProcAddr"))return false;
        XrGraphicsRequirementsD3D11KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
        if(!Good(requirements(instance,system,&req),"xrGetD3D11GraphicsRequirementsKHR"))return false;
        ComPtr<ID3D11Device> queryDevice;
        if(!dev) {
            ComPtr<IDXGIFactory1> factory;ComPtr<IDXGIAdapter1> selected;
            if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return false;
            for(UINT i=0;;++i){
                ComPtr<IDXGIAdapter1> candidate;if(factory->EnumAdapters1(i,&candidate)==DXGI_ERROR_NOT_FOUND)break;
                DXGI_ADAPTER_DESC1 ad{};
                if(candidate && SUCCEEDED(candidate->GetDesc1(&ad)) && !memcmp(&ad.AdapterLuid,&req.adapterLuid,sizeof(LUID))){selected=candidate;break;}
            }
            if(!selected || FAILED(D3D11CreateDevice(selected.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&queryDevice,nullptr,nullptr)))return false;
            dev=queryDevice.Get();
        }
        ComPtr<IDXGIDevice> dxdev; ComPtr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC ad{};
        if(FAILED(dev->QueryInterface(IID_PPV_ARGS(&dxdev))) || FAILED(dxdev->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&ad)))return false;
        if(memcmp(&ad.AdapterLuid,&req.adapterLuid,sizeof(LUID)) || dev->GetFeatureLevel()<req.minFeatureLevel) {
            Log("Game device does not satisfy headset adapter/feature-level requirement"); return false;
        }
        XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR}; binding.device=dev;
        XrSessionCreateInfo si{XR_TYPE_SESSION_CREATE_INFO}; si.next=&binding; si.systemId=system;
        if(!Good(xrCreateSession(instance,&si,&session),"xrCreateSession"))return false;
        if(refreshExtension && XR_SUCCEEDED(xrGetInstanceProcAddr(instance,"xrGetDisplayRefreshRateFB",reinterpret_cast<PFN_xrVoidFunction*>(&getRefresh))) && getRefresh)
            getRefresh(session,&display.refreshHz);
        Log("Headset recommended eye=%ux%u refresh=%.3f Hz (0=not exposed; use xrWaitFrame timing)",display.width,display.height,display.refreshHz);
        XrReferenceSpaceCreateInfo ri{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        ri.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL; ri.poseInReferenceSpace.orientation.w=1;
        if(!Good(xrCreateReferenceSpace(session,&ri,&space),"xrCreateReferenceSpace LOCAL"))return false;
        ri.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
        if(!Good(xrCreateReferenceSpace(session,&ri,&headSpace),"xrCreateReferenceSpace VIEW"))return false;
        wchar_t config[MAX_PATH]{};GetFullPathNameW(L"DeusExHRVR.ini",MAX_PATH,config,nullptr);
        levelScreen=GetPrivateProfileIntW(L"VR",L"LevelScreen",1,config)!=0;
        Log("Virtual screen placement: %s",levelScreen?"level (LevelScreen=1)":"follows head tilt (LevelScreen=0)");
        if(trackingChannel && DirectionConfig::MotionEnabled(config)) {
            bool ok=controller.Init(instance,session);Log("Motion controller aim and Xbox buttons: %s",ok?"ready":"unavailable; native input retained");
            if(!ok)controller.Reset();
        }
        device=dev; dev->GetImmediateContext(&context);
        Log("OpenXR session created. F6 switches experimental engine tracking; untracked frames use the stereo screen.");
        return true;
    }
    bool Poll() {
        XrEventDataBuffer e{XR_TYPE_EVENT_DATA_BUFFER}; XrResult r;
        while((r=xrPollEvent(instance,&e))==XR_SUCCESS) {
            if(e.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                auto& s=*reinterpret_cast<XrEventDataSessionStateChanged*>(&e);
                if(s.session==session) {
                    state=s.state; Log("OpenXR session state=%d",state);
                    if(state==XR_SESSION_STATE_READY && !running) {
                        XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO}; bi.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        running=Good(xrBeginSession(session,&bi),"xrBeginSession");
                        if(!running)return false;
                    }
                    if(state==XR_SESSION_STATE_STOPPING && running) {
                        Good(xrEndSession(session),"xrEndSession"); running=false;
                    }
                    if(state==XR_SESSION_STATE_LOSS_PENDING || state==XR_SESSION_STATE_EXITING)return false;
                }
            } else if(e.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) return false;
            else if(e.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
                anchorValid=false;
                resetPending=true;resetTime=reinterpret_cast<XrEventDataReferenceSpaceChangePending*>(&e)->changeTime;
            }
            e={XR_TYPE_EVENT_DATA_BUFFER};
        }
        return r==XR_EVENT_UNAVAILABLE || Good(r,"xrPollEvent");
    }
    // The native pair holds display-encoded pixels: it is what the desktop
    // path presents to an sRGB monitor. Prefer the sRGB sibling of the same
    // typeless family for the OpenXR swapchain so the runtime compositor
    // applies the correct transfer function; a bit-exact family copy needs
    // no conversion. Fall back to the native format only if the runtime
    // offers no sRGB variant (image may then render dark).
    static DXGI_FORMAT SrgbSibling(DXGI_FORMAT f) {
        switch(f) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        default: return f; // already sRGB, or formats with no sRGB variant (float, 10:10:10:2)
        }
    }
    bool EnsureSwapchain(const D3D11_TEXTURE2D_DESC& d,UINT h) {
        DXGI_FORMAT want=SrgbSibling(d.Format);
        if(swapchain && width==d.Width && height==h && (format==want||format==d.Format))return true;
        DestroySwapchain();
        uint32_t n=0;
        if(!Good(xrEnumerateSwapchainFormats(session,0,&n,nullptr),"xrEnumerateSwapchainFormats"))return false;
        std::vector<int64_t> formats(n);
        if(!Good(xrEnumerateSwapchainFormats(session,n,&n,formats.data()),"xrEnumerateSwapchainFormats"))return false;
        auto supported=[&](DXGI_FORMAT f){return std::find(formats.begin(),formats.end(),int64_t(f))!=formats.end();};
        if(!supported(want)) {
            if(want!=d.Format && supported(d.Format)) {
                Log("Runtime lacks sRGB swapchain format %d; using native linear %d (image may appear dark)",want,d.Format);
                want=d.Format;
            } else {
                Log("Native texture format %d (sRGB variant %d) unsupported by runtime; no reinterpretation",d.Format,want); return false;
            }
        } else if(want!=d.Format) {
            Log("Using sRGB swapchain format %d for native format %d (gamma-correct compositor sampling)",want,d.Format);
        }
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags=XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        ci.format=want; ci.sampleCount=1; ci.width=d.Width; ci.height=h; ci.faceCount=1; ci.arraySize=2; ci.mipCount=1;
        if(!Good(xrCreateSwapchain(session,&ci,&swapchain),"xrCreateSwapchain"))return false;
        if(!Good(xrEnumerateSwapchainImages(swapchain,0,&n,nullptr),"xrEnumerateSwapchainImages"))return false;
        images.assign(n,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        if(!Good(xrEnumerateSwapchainImages(swapchain,n,&n,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"xrEnumerateSwapchainImages"))return false;
        width=d.Width; height=h; format=want;
        Log("Native pair swapchain: %ux%u per eye, arraySize=2 format=%d images=%u",width,height,format,n);
        return true;
    }
    void Capture(ID3D11Texture2D* pair,UINT h,uint64_t id) {
        D3D11_TEXTURE2D_DESC d{}; pair->GetDesc(&d);
        if(d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
           d.Format!=DXGI_FORMAT_B8G8R8A8_UNORM && d.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)return;
        d.Usage=D3D11_USAGE_STAGING; d.BindFlags=0; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ; d.MiscFlags=0;
        ComPtr<ID3D11Texture2D> staging;
        if(FAILED(device->CreateTexture2D(&d,nullptr,&staging)))return;
        context->CopyResource(staging.Get(),pair);
        D3D11_MAPPED_SUBRESOURCE map{};
        if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)))return;
        std::error_code ec; std::filesystem::create_directory("DeusExHRVR-captures",ec);
        for(UINT eye=0;eye<2;eye++) {
            char name[180]; sprintf_s(name,"DeusExHRVR-captures/pair-%llu-%s.bmp",id,eye?"right":"left");
            std::ofstream out(name,std::ios::binary);
            BITMAPFILEHEADER fh{}; BITMAPINFOHEADER ih{};
            fh.bfType=0x4d42; fh.bfOffBits=sizeof(fh)+sizeof(ih); fh.bfSize=fh.bfOffBits+d.Width*h*4;
            ih.biSize=sizeof(ih); ih.biWidth=d.Width; ih.biHeight=-LONG(h); ih.biPlanes=1; ih.biBitCount=32; ih.biCompression=BI_RGB;
            out.write(reinterpret_cast<const char*>(&fh),sizeof(fh)); out.write(reinterpret_cast<const char*>(&ih),sizeof(ih));
            std::vector<uint8_t> row(d.Width*4);
            bool rgba=d.Format==DXGI_FORMAT_R8G8B8A8_UNORM || d.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            for(UINT y=0;y<h;y++) {
                memcpy(row.data(),static_cast<const uint8_t*>(map.pData)+(eye*h+y)*map.RowPitch,row.size());
                if(rgba)for(UINT x=0;x<d.Width;x++)std::swap(row[x*4],row[x*4+2]);
                out.write(reinterpret_cast<const char*>(row.data()),row.size());
            }
        }
        context->Unmap(staging.Get(),0); Log("Captured both native eye regions from sourceFrame=%llu",id);
    }
    void Frame(ID3D11Texture2D* pair,UINT h,bool swap,bool capture,bool recenter) {
        D3D11_TEXTURE2D_DESC d{}; pair->GetDesc(&d);
        bool f10=(GetAsyncKeyState(VK_F10)&0x8000)!=0;
        if(f10&&!f10Down){historyArmed=!historyArmed;if(!historyArmed)history.Reset();Log("Rolling native-pair history %s (F8 saves previous 360 frames)",historyArmed?"armed":"off");}
        f10Down=f10;
        if(capture && history.Count()) {
            char folder[180];sprintf_s(folder,"DeusExHRVR-captures/history-%lu-%llu",GetCurrentProcessId(),sourceFrame);
            Log("History save: frames=%u success=%d folder=%s",history.Count(),int(history.Save(context.Get(),folder)),folder);
        }
        if(capture)Capture(pair,h,sourceFrame);
        if(!running){Sleep(10);return;}
        XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO}; XrFrameState fs{XR_TYPE_FRAME_STATE};
        if(!Good(xrWaitFrame(session,&wi,&fs),"xrWaitFrame")) {failed=true; return;}
        if(fs.predictedDisplayPeriod!=lastPeriod) {
            lastPeriod=fs.predictedDisplayPeriod;
            if(getRefresh)getRefresh(session,&display.refreshHz);
            Log("Runtime pacing: period=%.3f ms applicationTarget=%.3f Hz displayRefresh=%.3f Hz",double(lastPeriod)/1e6,lastPeriod>0?1e9/double(lastPeriod):0.,display.refreshHz);
        }
        Transport::Tracking tracking{};tracking.id=++trackingId;tracking.tick=GetTickCount64();
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime=fs.predictedDisplayTime;locate.space=space;
        XrViewState viewsState{XR_TYPE_VIEW_STATE};XrView views[2]{{XR_TYPE_VIEW},{XR_TYPE_VIEW}};uint32_t viewCount=0;
        XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
        if(XR_SUCCEEDED(xrLocateViews(session,&locate,&viewsState,2,&viewCount,views))&&viewCount==2&&
           (viewsState.viewStateFlags&3)==3&&XR_SUCCEEDED(xrLocateSpace(headSpace,space,fs.predictedDisplayTime,&head))&&
           (head.locationFlags&3)==3) {
            static_assert(sizeof(Transport::Pose)==sizeof(XrPosef));
            memcpy(&tracking.head,&head.pose,sizeof(head.pose));
            for(int eye=0;eye<2;eye++) {
                memcpy(&tracking.eyes[eye].pose,&views[eye].pose,sizeof(XrPosef));
                tracking.eyes[eye].left=views[eye].fov.angleLeft;tracking.eyes[eye].right=views[eye].fov.angleRight;
                tracking.eyes[eye].up=views[eye].fov.angleUp;tracking.eyes[eye].down=views[eye].fov.angleDown;
            }
            tracking.valid=1;
        }
        controller.Sample(session,space,fs.predictedDisplayTime,state==XR_SESSION_STATE_FOCUSED,tracking);
        Transport::WriteTracking(trackingChannel,tracking);
        if(frameReadyEvent)SetEvent(frameReadyEvent);
        XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
        if(!Good(xrBeginFrame(session,&bi),"xrBeginFrame")){failed=true;return;}
        XrCompositionLayerQuad quads[2]{{XR_TYPE_COMPOSITION_LAYER_QUAD},{XR_TYPE_COMPOSITION_LAYER_QUAD}};
        XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        XrCompositionLayerProjectionView projectionViews[2]{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
        const XrCompositionLayerBaseHeader* layers[2]{}; uint32_t layerCount=0;
        if(fs.shouldRender && EnsureSwapchain(d,h)) {
            uint32_t index=0; XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            if(Good(xrAcquireSwapchainImage(swapchain,&ai,&index),"xrAcquireSwapchainImage")) {
                XrSwapchainImageWaitInfo iw{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; iw.timeout=XR_INFINITE_DURATION;
                XrResult wait=xrWaitSwapchainImage(swapchain,&iw);
                if(wait==XR_SUCCESS && index<images.size()) {
                    // The two GPU copies read one native texture at ONE Present boundary.
                    // They target the same acquired image and are released together.
                    for(UINT eye=0;eye<2;eye++) {
                        UINT srcEye=swap?1-eye:eye;
                        D3D11_BOX box{0,srcEye*h,0,d.Width,(srcEye+1)*h,1};
                        context->CopySubresourceRegion(images[index].texture,eye,0,0,0,pair,0,&box);
                    }
                    context->Flush();
                    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                    if(Good(xrReleaseSwapchainImage(swapchain,&ri),"xrReleaseSwapchainImage")) {
                        bool tracked=renderInfo.mode==1 && renderInfo.eyeMask==3 && renderInfo.tracking.valid;
                        if(tracked) {
                            // Place the next interaction screen in front of the
                            // user, rather than at the startup menu's old anchor.
                            anchorValid=false;
                            FrozenReset(renderInfo.tracking,fs.predictedDisplayTime,recenter);
                            for(UINT eye=0;eye<2;eye++) {
                                auto& v=projectionViews[eye];auto& e=renderInfo.tracking.eyes[eye];
                                memcpy(&v.pose,&e.pose,sizeof(v.pose));v.fov={e.left,e.right,e.up,e.down};
                                if(frozenShift)v.pose=Shift(v.pose);
                                v.subImage.swapchain=swapchain;v.subImage.imageArrayIndex=eye;
                                v.subImage.imageRect={{0,0},{int32_t(d.Width),int32_t(h)}};
                            }
                            projection.space=space;projection.viewCount=2;projection.views=projectionViews;
                            layers[layerCount++]=reinterpret_cast<XrCompositionLayerBaseHeader*>(&projection);
                        }
                        // The virtual screen handles its own reset through anchorValid.
                        if(!tracked){resetPending=false;frozenShift=false;frozenId=0;}
                        if(!tracked && (!anchorValid || recenter)) {
                            XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
                            if(Good(xrLocateSpace(headSpace,space,fs.predictedDisplayTime,&loc),"xrLocateSpace") &&
                               (loc.locationFlags&(XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))==
                               (XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
                                anchor=loc.pose;
                                auto q=anchor.orientation;
                                if(levelScreen) {
                                    // Local patch: keep only the heading, so the screen stands
                                    // upright and level 2 m straight ahead at head height,
                                    // however the head was tilted when it was placed. Same
                                    // treatment as LevelReference() in EngineCamera.cpp.
                                    float fx=-2.f*(q.x*q.z+q.w*q.y),fz=-(1.f-2.f*(q.x*q.x+q.y*q.y)); // forward (-Z)
                                    float yaw;
                                    if(std::hypot(fx,fz)<.001f) { // looking straight up or down: use the head's right side
                                        float rx=1.f-2.f*(q.y*q.y+q.z*q.z),rz=2.f*(q.x*q.z-q.w*q.y);
                                        yaw=std::atan2(-rz,rx);
                                    } else yaw=std::atan2(-fx,-fz);
                                    anchor.orientation={0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};
                                    anchor.position.x-=2.f*std::sin(yaw);
                                    anchor.position.z-=2.f*std::cos(yaw);
                                } else {
                                    anchor.position.x-=4.f*(q.x*q.z+q.w*q.y);
                                    anchor.position.y-=4.f*(q.y*q.z-q.w*q.x);
                                    anchor.position.z-=2.f*(1.f-2.f*(q.x*q.x+q.y*q.y));
                                }
                                anchorValid=true;
                            }
                        }
                        if(!tracked && renderInfo.mode==0 && anchorValid)for(UINT eye=0;eye<2;eye++) {
                            auto& q=quads[eye]; q.space=space;
                            q.eyeVisibility=eye?XR_EYE_VISIBILITY_RIGHT:XR_EYE_VISIBILITY_LEFT;
                            q.subImage.swapchain=swapchain; q.subImage.imageArrayIndex=eye;
                            q.subImage.imageRect={{0,0},{int32_t(d.Width),int32_t(h)}};
                            // The native screen camera retains its 16:9 logical
                            // aspect even when rendering to tall headset textures.
                            q.pose=anchor; q.size={3.2f,1.8f};
                            layers[layerCount++]=reinterpret_cast<XrCompositionLayerBaseHeader*>(&q);
                        }
                    } else failed=true;
                } else {Good(wait,"xrWaitSwapchainImage"); failed=true;}
            } else failed=true;
        }
        XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO}; ei.displayTime=fs.predictedDisplayTime;
        ei.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE; ei.layerCount=layerCount; ei.layers=layers;
        if(Good(xrEndFrame(session,&ei),"xrEndFrame")) {
            if(layerCount) {
                pairs++;
                static uint32_t lastMode=99;
                if(pairs==1 || capture || lastMode!=renderInfo.mode)Log("submittedPairs=%llu sourceFrame=%llu leftFrame=%llu rightFrame=%llu mode=%s pose=%llu eyeMask=%u",pairs,sourceFrame,sourceFrame,sourceFrame,renderInfo.mode?"native-tracked-projection":"native-stereo-screen",renderInfo.tracking.id,renderInfo.eyeMask);
                lastMode=renderInfo.mode;
                auto now=GetTickCount64();
                if(!rateTick){rateTick=now;ratePairs=pairs;}
                else if(capture && now>rateTick){Log("Measured submission rate=%.2f pairs/s eye=%ux%u displayRefresh=%.3f Hz",1000.*double(pairs-ratePairs)/double(now-rateTick),width,height,display.refreshHz);rateTick=now;ratePairs=pairs;}
            }
        } else failed=true;
        if(historyArmed && !history.Record(device.Get(),context.Get(),pair,{sourceFrame,GetTickCount64(),uint64_t(fs.predictedDisplayTime),layerCount,uint32_t(fs.shouldRender),uint32_t(swap),renderInfo})) {
            Log("Rolling history initialization/copy failed; recording disabled");historyArmed=false;history.Reset();
        }
    }
};
Bridge bridge;
bool f8Down=false,f9Down=false;
uint64_t externalFrame=0;
}
bool ValidatePair(const D3D11_TEXTURE2D_DESC& d,UINT h) {
    return h>0 && h<=D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION/2 && d.Width>0 &&
           d.Width<=D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION && d.Height==2*h &&
           d.ArraySize==1 && d.MipLevels==1 && d.SampleDesc.Count==1 &&
           d.Format!=DXGI_FORMAT_UNKNOWN;
}
void Present(IDXGISwapChain* chain,ID3D11Texture2D* pair,UINT h,bool swap) {
    std::lock_guard lock(mutex);
    if(!pair)return;
    D3D11_TEXTURE2D_DESC d{}; pair->GetDesc(&d);
    if(!ValidatePair(d,h))return;
    ComPtr<ID3D11Device> dev; pair->GetDevice(&dev);
    if(bridge.device && bridge.device.Get()!=dev.Get())bridge.Reset();
    if(bridge.owner && chain!=bridge.owner)return;
    bridge.sourceFrame=externalFrame?externalFrame:bridge.sourceFrame+1;
    externalFrame=0;
    bool f8=(GetAsyncKeyState(VK_F8)&0x8000)!=0, f9=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
    bool capture=f8&&!f8Down,recenter=f9&&!f9Down; f8Down=f8; f9Down=f9;
    if(!bridge.instance) {
        if(GetTickCount64()<bridge.nextRetry)return;
        if(!bridge.Init(dev.Get())) {bridge.Reset();bridge.nextRetry=GetTickCount64()+5000;return;}
        bridge.owner=chain;
    }
    if(!bridge.Poll()) {bridge.Reset();bridge.nextRetry=GetTickCount64()+5000;return;}
    bridge.Frame(pair,h,swap,capture,recenter);
    if(bridge.failed) {bridge.Reset();bridge.failed=false;bridge.nextRetry=GetTickCount64()+5000;}
}
void Shutdown() {std::lock_guard lock(mutex);bridge.Reset();}
uint64_t SubmittedPairs() {std::lock_guard lock(mutex);return bridge.pairs;}
void SetSourceFrame(uint64_t frame) {std::lock_guard lock(mutex);externalFrame=frame;}
void SetTrackingChannel(Transport::Header* shared) {
    std::lock_guard lock(mutex);trackingChannel=shared;
    if(frameReadyEvent)CloseHandle(frameReadyEvent);frameReadyEvent=nullptr;
    if(shared){wchar_t name[96];Transport::FrameEventName(name,shared->pid);frameReadyEvent=OpenEventW(EVENT_MODIFY_STATE,FALSE,name);}
}
void SetRenderInfo(const Transport::RenderInfo& info) {std::lock_guard lock(mutex);renderInfo=info;}
bool QueryDisplaySettings(HeadsetDisplay::Settings& settings) {
    std::lock_guard lock(mutex);Bridge query;
    bool ok=query.Init(nullptr);
    if(ok)settings=query.display;
    query.Reset();return ok && HeadsetDisplay::Valid(settings);
}
}
