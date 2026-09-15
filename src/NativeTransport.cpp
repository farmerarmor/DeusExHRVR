#include "NativeBridge.h"
#include "SharedPair.h"
#include "PairCapture.h"
#include "EngineCamera.h"
#include "GamepadBridge.h"
#include <wrl/client.h>
#include <mutex>
#include <cstdio>
#include <cstdarg>
#include <d3d11sdklayers.h>
using Microsoft::WRL::ComPtr;
namespace NativeBridge {
namespace {
std::mutex guard;
void Log(const char* fmt,...) {
    char line[512];va_list a;va_start(a,fmt);vsnprintf_s(line,sizeof(line),_TRUNCATE,fmt,a);va_end(a);
    FILE* f{};if(!fopen_s(&f,"DeusExHRVR-transport.log","a")){fprintf(f,"[%llu] %s\n",GetTickCount64(),line);fclose(f);}
}
struct Producer {
    HANDLE mapping{},process{},frameEvent{};Transport::Header* header{};
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> shared;ComPtr<IDXGIKeyedMutex> keyed;
    UINT width{},height{};DXGI_FORMAT format{};uint64_t frame{},sent{},retry{};
    IDXGISwapChain* owner{};bool f8=false;
    uint64_t rateTick{},rateFrame{},rateSent{};
    void ClearTexture(){keyed.Reset();shared.Reset();device.Reset();context.Reset();width=height=0;}
    void Reset(){
        ClearTexture();GamepadBridge::SetChannel(nullptr);EngineCamera::SetChannel(nullptr);if(header)UnmapViewOfFile(header);if(mapping)CloseHandle(mapping);if(process)CloseHandle(process);
        if(frameEvent)CloseHandle(frameEvent);frameEvent=nullptr;
        header=nullptr;mapping=process=nullptr;owner=nullptr;
    }
    bool Init(ID3D11Device* dev,const D3D11_TEXTURE2D_DESC& d,UINT h) {
        if(!mapping) {
            wchar_t name[96];Transport::Name(name,GetCurrentProcessId());
            mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Transport::Header),name);
            if(!mapping)return false;
            if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(mapping);mapping=nullptr;return false;}
            header=static_cast<Transport::Header*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Transport::Header)));
            if(!header)return false;
            header->magic=Transport::Magic;header->version=Transport::Version;header->pid=GetCurrentProcessId();
            Transport::FrameEventName(name,GetCurrentProcessId());frameEvent=CreateEventW(nullptr,FALSE,FALSE,name);
            EngineCamera::SetChannel(header);
            GamepadBridge::SetChannel(header);
        }
        ClearTexture();device=dev;dev->GetImmediateContext(&context);
        auto sd=d;sd.Usage=D3D11_USAGE_DEFAULT;sd.CPUAccessFlags=0;
        sd.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        sd.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        HRESULT hr=dev->CreateTexture2D(&sd,nullptr,&shared);
        if(FAILED(hr)){
            Log("Shared texture failed hr=%08lx W=%u H=%u mip=%u array=%u fmt=%u samples=%u quality=%u usage=%u bind=%u cpu=%u misc=%u feature=%x",hr,sd.Width,sd.Height,sd.MipLevels,sd.ArraySize,sd.Format,sd.SampleDesc.Count,sd.SampleDesc.Quality,sd.Usage,sd.BindFlags,sd.CPUAccessFlags,sd.MiscFlags,dev->GetFeatureLevel());
            ComPtr<ID3D11InfoQueue> info;
            if(SUCCEEDED(dev->QueryInterface(IID_PPV_ARGS(&info)))) {
                UINT64 n=info->GetNumStoredMessagesAllowedByRetrievalFilter();
                for(UINT64 i=n>12?n-12:0;i<n;i++) {
                    SIZE_T len=0;info->GetMessage(i,nullptr,&len);std::vector<unsigned char> data(len);
                    auto msg=reinterpret_cast<D3D11_MESSAGE*>(data.data());
                    if(SUCCEEDED(info->GetMessage(i,msg,&len)))Log("D3D11 diagnostic %d: %s",msg->ID,msg->pDescription);
                }
                info->ClearStoredMessages();
            }
            return false;
        }
        ComPtr<IDXGIResource> resource;ComPtr<IDXGIDevice> dxdev;ComPtr<IDXGIAdapter> adapter;
        HANDLE handle{};DXGI_ADAPTER_DESC ad{};
        if(FAILED(shared.As(&keyed))||FAILED(shared.As(&resource))||FAILED(resource->GetSharedHandle(&handle))||
           FAILED(dev->QueryInterface(IID_PPV_ARGS(&dxdev)))||FAILED(dxdev->GetAdapter(&adapter))||FAILED(adapter->GetDesc(&ad)))return false;
        header->width=d.Width;header->height=h;header->format=d.Format;
        header->adapter=ad.AdapterLuid;header->sharedHandle=uint64_t(reinterpret_cast<uintptr_t>(handle));
        MemoryBarrier();InterlockedIncrement(&header->generation);
        width=d.Width;height=h;format=d.Format;
        Log("Native shared pair %ux%u (each eye %ux%u), generation=%ld",d.Width,d.Height,d.Width,h,header->generation);
        // Luma port: the engine's D3D11 device is now known. Forward it to
        // ShaderSwap so it can build replacement shaders and issue
        // PSSetShader overrides. Producer::Init and RenderStateHook both run
        // on the render thread, so the cached pointers stay race-free.
        EngineCamera::SetShaderSwapDevice(dev);
        return true;
    }
    bool Launch() {
        if(process)return true;
        wchar_t module[MAX_PATH]{};HMODULE self{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&Present),&self);
        GetModuleFileNameW(self,module,MAX_PATH);wchar_t* slash=wcsrchr(module,L'\\');if(!slash)return false;
        slash[1]=0;std::wstring dir=module,exe=dir+L"DeusExHRVR\\DeusExHRVRHost.exe";
        wchar_t cmd[1024];swprintf_s(cmd,L"\"%s\" --game-pid %lu",exe.c_str(),GetCurrentProcessId());
        STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
        if(!CreateProcessW(exe.c_str(),cmd,nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,dir.c_str(),&si,&pi)) {
            Log("Companion launch failed error=%lu",GetLastError());return false;
        }
        CloseHandle(pi.hThread);process=pi.hProcess;Log("64-bit OpenXR companion started pid=%lu",pi.dwProcessId);return true;
    }
};
Producer p;
}
void Present(IDXGISwapChain* chain,ID3D11Texture2D* pair,UINT h,bool swap) {
    std::lock_guard lock(guard);if(!pair||h==0)return;
    D3D11_TEXTURE2D_DESC d{};pair->GetDesc(&d);
    if(d.Height!=h*2||d.ArraySize!=1||d.MipLevels!=1||d.SampleDesc.Count!=1)return;
    if(p.owner&&p.owner!=chain)return;
    ++p.frame;
    bool captureKey=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
    bool capture=captureKey&&!p.f8;
    GamepadBridge::OnPresent(capture);
    auto rendered=EngineCamera::OnPresent(p.frame,capture);
    if(p.process&&WaitForSingleObject(p.process,0)==WAIT_OBJECT_0){p.Reset();p.retry=GetTickCount64()+5000;}
    if(GetTickCount64()<p.retry)return;
    ComPtr<ID3D11Device> dev;pair->GetDevice(&dev);
    if(!p.shared||p.device.Get()!=dev.Get()||d.Width!=p.width||h!=p.height||d.Format!=p.format) {
        if(!p.Init(dev.Get(),d,h)){p.Reset();p.retry=GetTickCount64()+5000;return;}
        p.owner=chain;
    }
    bool key=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
    // CPU readback is opt-in; automatic captures stall at headset resolution.
    if(key&&!p.f8) {
        Log("Capture native frame=%llu success=%d",p.frame,int(CaptureNativePair(p.device.Get(),p.context.Get(),pair,h,p.frame)));
    }
    p.f8=key;
    if(!p.Launch()){p.Reset();p.retry=GetTickCount64()+5000;return;}
    HRESULT hr=p.keyed->AcquireSync(0,100);
    if(hr==WAIT_TIMEOUT)return; // Drop the WHOLE pair if companion has not consumed it.
    if(hr!=S_OK){Log("Producer keyed mutex failed=%08lx",hr);p.Reset();p.retry=GetTickCount64()+5000;return;}
    p.context->CopyResource(p.shared.Get(),pair);p.context->Flush();
    p.header->frameId=p.frame;p.header->swapEyes=swap;
    p.header->rendered=rendered;
    MemoryBarrier();hr=p.keyed->ReleaseSync(1);
    if(FAILED(hr)){Log("Producer ReleaseSync failed=%08lx",hr);p.Reset();return;}
    ++p.sent;if(p.sent==1||capture)Log("sentPairs=%llu nativePresent=%llu",p.sent,p.frame);
    // One game pair per OpenXR frame request. A bounded wait leaves the game
    // responsive if the headset is removed, the session stops, or the host exits.
    Transport::Tracking tracking{};
    if(p.frameEvent && Transport::ReadTracking(p.header,tracking) && tracking.tick && GetTickCount64()-tracking.tick<250) {
        HANDLE waits[]={p.frameEvent,p.process};WaitForMultipleObjects(2,waits,FALSE,100);
    }
    auto now=GetTickCount64();
    if(!p.rateTick){p.rateTick=now;p.rateFrame=p.frame;p.rateSent=p.sent;}
    else if(capture && now>p.rateTick){
        double seconds=double(now-p.rateTick)/1000.;
        Log("Measured native=%.2f pairs/s delivered=%.2f pairs/s eye=%ux%u",double(p.frame-p.rateFrame)/seconds,double(p.sent-p.rateSent)/seconds,p.width,p.height);
        p.rateTick=now;p.rateFrame=p.frame;p.rateSent=p.sent;
    }
}
void Shutdown(){std::lock_guard lock(guard);p.Reset();}
uint64_t SubmittedPairs(){std::lock_guard lock(guard);return p.sent;}
}
