#include "LumaPasses.h"
#include "ShaderHash.h"
#include "DrawStateStack.h"
#include "LumaSettingsCB.h"
#include <cstdio>
#include <cstdarg>
#include <fstream>

// ReShade shader hashes for DXHR's injected-pass trigger draws. These match
// the ShaderHashesList values Luma populates in main.cpp (lines 1578-1587),
// resolved at runtime against the bound PS via ShaderHash::Crc32 over DXBC.
namespace {
// pixel_shader_hashes_SSAOGeneration — DC and OG (Luma line 1583)
constexpr uint32_t kHash_SSAOGeneration[] = { 0xD44718C4u, 0x7A054979u };
// compute_shader_hashes_SSAODenoise (Luma line 1584)
constexpr uint32_t kHash_SSAODenoiseCS[] = { 0x54A9A847u, 0x8A03353Cu };
// pixel_shader_hashes_Copy — used to copy SSAO out (Luma line 1585)
constexpr uint32_t kHash_Copy[] = { 0xB8813A2Fu };
// pixel_shader_hashes_MLAA_mask (Luma line 1579)
constexpr uint32_t kHash_MLAAMask[] = { 0x6B0219A1u, 0x1DA1E46Eu };
// pixel_shader_hashes_SupportedAA — MLAA composition + FXAA High (Luma line 1580)
constexpr uint32_t kHash_SupportedAA[] = { 0x51BBB596u, 0xFF6E347Au };
// pixel_shader_hashes_BloomComposition (Luma line 1582)
constexpr uint32_t kHash_BloomComposition[] = { 0x29E509CFu, 0x24314FFAu, 0xAAB155FFu };
// pixel_shader_hashes_Lighting (Luma line 1587) — some from OG, some from DC
constexpr uint32_t kHash_Lighting[] = {
    0x944C549Du, 0x4C48AF67u, 0xD6937DB8u, 0x0B16DD34u, 0x2175B8F6u, 0x00C1331Bu,
    0x5EF35A1Eu, 0xC7F2C455u, 0xEBE2567Fu, 0x0AB7755Cu, 0x7E526193u, 0xC7B58EF0u
};
// pixel_shader_hashes_UI (Luma line 1586) — excluded from ModulateLighting's
// material-draw detection.
constexpr uint32_t kHash_UI[] = {
    0xE5757FCEu, 0xD07AC030u, 0xB8813A2Fu, 0x3773AC30u, 0x9CB44B83u, 0x6BAF4A32u
};

constexpr unsigned XE_GTAO_DEPTH_MIP_LEVELS = 5;
constexpr unsigned XE_GTAO_NUMTHREADS_X = 8;
constexpr unsigned XE_GTAO_NUMTHREADS_Y = 8;

bool HashInList(uint32_t h, const uint32_t* arr, size_t n) {
    for (size_t i = 0; i < n; ++i) if (arr[i] == h) return true;
    return false;
}
#define HASH_IN_LIST(h, arr) HashInList((h), (arr), sizeof(arr)/sizeof((arr)[0]))
} // namespace

void LumaPasses::OnFrameStart() {
    RestoreDrawOverrides();
    for(auto& input:eyes){input.pending=false;input.lightingDone=false;input.lighting.Reset();}
    // Reset per-frame scheduling flags (mirrors Luma resetting game_device_data
    // at frame boundary). The engine renders both eyes within one frame, so
    // these flags span both eyes; per-eye correctness comes from the hook
    // firing per draw, not from per-eye flags.
    hasFoundLightingBuffer = false;
    lightingBufferRtv.Reset();
    hasDrawnSSAO = false;
    hasDrawnXeGTAO = false;
    hasDrawnMainPostProcessing = false;
    hasModulatedLighting = false;
    // swapchainRtv is intentionally NOT reset here — Luma caches it across
    // frames (only re-captured when the swapchain changes). Keep it.
}

void LumaPasses::Log(const char* fmt, ...) const {
    char line[512];
    va_list a; va_start(a, fmt);
    vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, a);
    va_end(a);
    FILE* f{};
    if (!fopen_s(&f, "DeusExHRVR-lumapasses.log", "a")) {
        fprintf(f, "[%llu] %s\n", GetTickCount64(), line);
        fclose(f);
    }
}

bool LumaPasses::LoadBlob(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    if (sz <= 0) return false;
    f.seekg(0);
    out.resize(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(out.data()), out.size());
    return f.gcount() == static_cast<std::streamsize>(out.size());
}

Microsoft::WRL::ComPtr<ID3D11ComputeShader> LumaPasses::LoadCS(
    const std::filesystem::path& csoDir, ID3D11Device* device, const wchar_t* stem) {
    auto path = csoDir / (std::wstring(stem) + L".cso");
    std::vector<uint8_t> blob;
    if (!LoadBlob(path, blob)) {
        FILE* f{}; if (!fopen_s(&f, "DeusExHRVR-lumapasses.log", "a")) {
            fprintf(f, "[%llu] LoadCS: missing %S\n", GetTickCount64(), path.wstring().c_str()); fclose(f);
        }
        return nullptr;
    }
    ID3D11ComputeShader* cs{};
    if (FAILED(device->CreateComputeShader(blob.data(), blob.size(), nullptr, &cs))) return nullptr;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> result; result.Attach(cs); return result;
}

Microsoft::WRL::ComPtr<ID3D11PixelShader> LumaPasses::LoadPS(
    const std::filesystem::path& csoDir, ID3D11Device* device, const wchar_t* stem) {
    auto path = csoDir / (std::wstring(stem) + L".cso");
    std::vector<uint8_t> blob;
    if (!LoadBlob(path, blob)) return nullptr;
    ID3D11PixelShader* ps{};
    if (FAILED(device->CreatePixelShader(blob.data(), blob.size(), nullptr, &ps))) return nullptr;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> result; result.Attach(ps); return result;
}

Microsoft::WRL::ComPtr<ID3D11VertexShader> LumaPasses::LoadVS(
    const std::filesystem::path& csoDir, ID3D11Device* device, const wchar_t* stem) {
    auto path = csoDir / (std::wstring(stem) + L".cso");
    std::vector<uint8_t> blob;
    if (!LoadBlob(path, blob)) return nullptr;
    ID3D11VertexShader* vs{};
    if (FAILED(device->CreateVertexShader(blob.data(), blob.size(), nullptr, &vs))) return nullptr;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> result; result.Attach(vs); return result;
}

bool LumaPasses::Load(const std::filesystem::path& csoDir, ID3D11Device* dev) {
    if (!dev) return false;
    device = dev;
    dev->GetImmediateContext(&context);
    // XeGTAO: 4 CS variants. The .cso filenames must match what the offline
    // compile step produces for each entry point + macro combination. The
    // compile script (Phase 3g) emits:
    //   Luma_DXHR_XeGTAO_prefilter_depths16x16_cs.cso
    //   Luma_DXHR_XeGTAO_main_pass_cs.cso
    //   Luma_DXHR_XeGTAO_denoise_pass_cs_XE_GTAO_FINAL_APPLY_0.cso
    //   Luma_DXHR_XeGTAO_denoise_pass_cs_XE_GTAO_FINAL_APPLY_1.cso
    // XeGTAO withdrawn after headset testing; do not load its shaders.

    csSMAALinearize   = LoadCS(csoDir, dev, L"Luma_SMAA_Linearize");
    psModulateLighting = LoadPS(csoDir, dev, L"Luma_ModulateLighting");
    // Copy VS: Luma's fullscreen-triangle vertex shader. Required for
    // DrawCustomPixelShader (ModulateLighting and SMAA draws).
    vsCopy = LoadVS(csoDir, dev, L"Luma_Copy_VS");

    // Point sampler (Luma's device_data.sampler_state_point). Used by
    // DrawCustomPixelShader and XeGTAO's depth taps.
    D3D11_SAMPLER_DESC smpDesc{}; smpDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    smpDesc.AddressU = smpDesc.AddressV = smpDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    smpDesc.MaxAnisotropy = 1; smpDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    smpDesc.MinLOD = 0; smpDesc.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&smpDesc, samplerPoint.GetAddressOf());

    // SMAA draw-pass shaders (Phase 3e). Optional for now; SMAA stays disabled
    // if these are missing.
    // TODO(Phase 3e): load vs/ps SMAA edge/blending/neighborhood shaders.

    bool xegtaoOk = csXeGTAOPrefilter && csXeGTAOMain && csXeGTAODenoise1 && csXeGTAODenoise2;
    bool smaaCsOk = csSMAALinearize != nullptr;
    bool modulateOk = psModulateLighting != nullptr && vsCopy != nullptr;

    loaded = xegtaoOk || smaaCsOk || modulateOk;
    Log("Load: xegtao=%d smaa_cs=%d modulate=%d copyvs=%d (loaded=%d)",
        int(xegtaoOk), int(smaaCsOk), int(modulateOk), int(vsCopy != nullptr), int(loaded));
    return loaded;
}

// Only the compute slots touched below are saved. Restoration also runs on failure.
namespace {
struct AoState {
    ID3D11DeviceContext* ctx;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv[2];
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav[5];
    Microsoft::WRL::ComPtr<ID3D11Buffer> cb;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rt[8];
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;
    AoState(ID3D11DeviceContext* c):ctx(c) {
        ctx->CSGetShader(&shader,nullptr,nullptr);
        ctx->CSGetShaderResources(0,2,&srv[0]);
        ctx->CSGetUnorderedAccessViews(0,5,&uav[0]);
        ctx->CSGetConstantBuffers(0,1,&cb);
        ctx->CSGetSamplers(0,1,&sampler);
        ctx->OMGetRenderTargets(8,&rt[0],&depth);
    }
    ~AoState() {
        ID3D11UnorderedAccessView* noUav[5]{};
        ID3D11ShaderResourceView* noSrv[2]{};
        ctx->CSSetUnorderedAccessViews(0,5,noUav,nullptr);
        ctx->CSSetShaderResources(0,2,noSrv);
        ID3D11RenderTargetView* rtRaw[8]; UINT count=0;
        for(UINT i=0;i<8;++i) { rtRaw[i]=rt[i].Get();if(rtRaw[i])count=i+1; }
        ctx->OMSetRenderTargets(count,rtRaw,depth.Get());
        ID3D11UnorderedAccessView* uRaw[5];for(UINT i=0;i<5;++i)uRaw[i]=uav[i].Get();
        ctx->CSSetUnorderedAccessViews(0,5,uRaw,nullptr);
        ID3D11ShaderResourceView* sRaw[]={srv[0].Get(),srv[1].Get()};
        ctx->CSSetShaderResources(0,2,sRaw);
        ID3D11Buffer* b=cb.Get();ctx->CSSetConstantBuffers(0,1,&b);
        ID3D11SamplerState* s=sampler.Get();ctx->CSSetSamplers(0,1,&s);
        ctx->CSSetShader(shader.Get(),nullptr,0);
    }
};
}

void LumaPasses::RestoreDrawOverrides() {
    if(!sourceOverridden || !context)return;
    context->PSSetShader(savedCopyShader.Get(),nullptr,0);
    savedCopyShader.Reset();sourceOverridden=false;
}

bool LumaPasses::OnDraw(uint32_t frame, uint32_t hash, unsigned eye) {
    if(!loaded || !context || eye>1)return false;
    auto& input=eyes[eye];
    if(modulateEnabled && !input.lightingDone) {
        if(HASH_IN_LIST(hash,kHash_Lighting)) {
            if(!input.lighting)context->OMGetRenderTargets(1,&input.lighting,nullptr);
        } else if(input.lighting) {
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> source;
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
            Microsoft::WRL::ComPtr<ID3D11Resource> lightingResource,sourceResource,targetResource;
            // Captured DC material draws consume the lighting texture at t3.
            context->PSGetShaderResources(3,1,&source);
            input.lighting->GetResource(&lightingResource);
            if(source)source->GetResource(&sourceResource);
            context->OMGetRenderTargets(1,&target,nullptr);
            if(target)target->GetResource(&targetResource);
            if(targetResource && targetResource.Get()!=lightingResource.Get() &&
               sourceResource && sourceResource.Get()==lightingResource.Get()) {
                input.lightingDone=RunModulateLighting(frame,eye,input.lighting.Get());
                if(input.lightingDone)++stats.modulateRuns;
            }
        }
    }
    if(!xegtaoEnabled)return false;
    if(HASH_IN_LIST(hash,kHash_SSAOGeneration)) {
        input.pending=false; input.depth.Reset();input.source.Reset();
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
        Microsoft::WRL::ComPtr<ID3D11Buffer> scene;
        context->OMGetRenderTargets(1,&target,nullptr);
        context->PSGetShaderResources(0,1,&input.depth);
        context->PSGetConstantBuffers(2,1,&scene);
        if(!target || !input.depth || !scene)return false;
        D3D11_BUFFER_DESC desc{};scene->GetDesc(&desc);
        if(desc.ByteWidth!=912)return false;
        if(!input.scene) {
            desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;desc.MiscFlags=0;
            if(FAILED(device->CreateBuffer(&desc,nullptr,&input.scene)))return false;
        }
        context->CopyResource(input.scene.Get(),scene.Get());
        target->GetResource(&input.source);input.pending=true;
    } else if(input.pending && HASH_IN_LIST(hash,kHash_Copy)) {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> source;
        Microsoft::WRL::ComPtr<ID3D11Resource> sourceResource;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
        context->PSGetShaderResources(0,1,&source);
        if(!source)return false;
        source->GetResource(&sourceResource);
        if(sourceResource.Get()!=input.source.Get())return false;
        input.pending=false;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthTarget;
        context->OMGetRenderTargets(1,&target,&depthTarget);
        if(depthTarget)return false;
        if(!target || !RunXeGTAO(frame,eye,input.depth.Get(),target.Get(),input.scene.Get()))return false;
        // RunXeGTAO already copied the complete corrected normals/AO texture
        // back. Suppress color output from the matching native fullscreen copy.
        // Restore before the next engine state update (PS bindings are cached).
        context->PSGetShader(&savedCopyShader,nullptr,nullptr);sourceOverridden=true;
        context->PSSetShader(nullptr,nullptr,0);
        ++stats.xegtaoRuns;
        if(stats.xegtaoRuns<=4)Log("XeGTAO native copy frame=%u eye=%u %ux%u",frame,eye,aoWidth,aoHeight);
        return true;
    }
    return false;
}

bool LumaPasses::EnsureAoResources(UINT width, UINT height) {
    if(aoWidth==width && aoHeight==height)return true;
    aoWidth=aoHeight=0;
    aoMain={};aoDenoised={};aoOutput={};depthPyramid={};
    for(auto& mip:depthMips)mip.Reset();
    auto create=[&](ComputeTarget& target,DXGI_FORMAT format,UINT mips) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width=width;d.Height=height;d.MipLevels=mips;d.ArraySize=1;
        d.Format=format;d.SampleDesc.Count=1;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        if(FAILED(device->CreateTexture2D(&d,nullptr,&target.texture)))return false;
        if(FAILED(device->CreateShaderResourceView(target.texture.Get(),nullptr,&target.srv)))return false;
        D3D11_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=format;u.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;
        return SUCCEEDED(device->CreateUnorderedAccessView(target.texture.Get(),&u,&target.uav));
    };
    if(!create(depthPyramid,DXGI_FORMAT_R32_FLOAT,5) ||
       !create(aoMain,DXGI_FORMAT_R8G8_UNORM,1) ||
       !create(aoDenoised,DXGI_FORMAT_R8G8_UNORM,1) ||
       !create(aoOutput,DXGI_FORMAT_R8G8B8A8_UNORM,1))return false;
    for(UINT i=0;i<5;++i) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC u{};
        u.Format=DXGI_FORMAT_R32_FLOAT;u.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;u.Texture2D.MipSlice=i;
        if(FAILED(device->CreateUnorderedAccessView(depthPyramid.texture.Get(),&u,&depthMips[i])))return false;
    }
    aoWidth=width;aoHeight=height;
    Log("XeGTAO resources %ux%u (reused between eyes and frames)",width,height);
    return true;
}

bool LumaPasses::RunXeGTAO(uint32_t, unsigned,
    ID3D11ShaderResourceView* depthSrv,ID3D11RenderTargetView* normalRtv,ID3D11Buffer* sceneCB) {
    if(!csXeGTAOPrefilter || !csXeGTAOMain || !csXeGTAODenoise1 || !csXeGTAODenoise2 || !samplerPoint)return false;
    Microsoft::WRL::ComPtr<ID3D11Resource> resource,depthResource;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> normal,depth;
    normalRtv->GetResource(&resource);depthSrv->GetResource(&depthResource);
    if(FAILED(resource.As(&normal)) || FAILED(depthResource.As(&depth)))return false;
    D3D11_TEXTURE2D_DESC d{},dd{};normal->GetDesc(&d);depth->GetDesc(&dd);
    D3D11_VIEWPORT vp{};UINT n=1;context->RSGetViewports(&n,&vp);
    // Captured DC path uses a single full-sized texture for each eye, not an
    // atlas. Reject other layouts rather than sampling across eye boundaries.
    if(n!=1 || vp.TopLeftX || vp.TopLeftY || vp.Width!=d.Width || vp.Height!=d.Height ||
       d.Width!=dd.Width || d.Height!=dd.Height || d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM ||
       d.ArraySize!=1 || d.MipLevels!=1 || d.SampleDesc.Count!=1 ||
       dd.ArraySize!=1 || dd.SampleDesc.Count!=1 || d.Width<16 || d.Height<16)return false;
    if(!EnsureAoResources(d.Width,d.Height))return false;
    AoState saved(context.Get());
    context->OMSetRenderTargets(0,nullptr,nullptr);
    context->CopyResource(aoOutput.texture.Get(),normal.Get());
    ID3D11Buffer* cb=sceneCB;context->CSSetConstantBuffers(0,1,&cb);
    ID3D11SamplerState* sampler=samplerPoint.Get();context->CSSetSamplers(0,1,&sampler);
    ID3D11ShaderResourceView* noSrv[2]{};ID3D11UnorderedAccessView* noUav[5]{};
    auto unbind=[&](){context->CSSetUnorderedAccessViews(0,5,noUav,nullptr);context->CSSetShaderResources(0,2,noSrv);};
    unbind();
    ID3D11UnorderedAccessView* mips[5];for(UINT i=0;i<5;++i)mips[i]=depthMips[i].Get();
    context->CSSetUnorderedAccessViews(0,5,mips,nullptr);
    context->CSSetShaderResources(0,1,&depthSrv);
    context->CSSetShader(csXeGTAOPrefilter.Get(),nullptr,0);
    context->Dispatch((d.Width+15)/16,(d.Height+15)/16,1);
    unbind();
    ID3D11UnorderedAccessView* u=aoMain.uav.Get();context->CSSetUnorderedAccessViews(0,1,&u,nullptr);
    ID3D11ShaderResourceView* inputs[]={depthPyramid.srv.Get(),aoOutput.srv.Get()};
    context->CSSetShaderResources(0,2,inputs);context->CSSetShader(csXeGTAOMain.Get(),nullptr,0);
    context->Dispatch((d.Width+7)/8,(d.Height+7)/8,1);
    unbind();
    u=aoDenoised.uav.Get();context->CSSetUnorderedAccessViews(0,1,&u,nullptr);
    ID3D11ShaderResourceView* s=aoMain.srv.Get();context->CSSetShaderResources(0,1,&s);
    context->CSSetShader(csXeGTAODenoise1.Get(),nullptr,0);context->Dispatch((d.Width+15)/16,(d.Height+7)/8,1);
    unbind();
    u=aoOutput.uav.Get();context->CSSetUnorderedAccessViews(0,1,&u,nullptr);
    s=aoDenoised.srv.Get();context->CSSetShaderResources(0,1,&s);
    context->CSSetShader(csXeGTAODenoise2.Get(),nullptr,0);context->Dispatch((d.Width+15)/16,(d.Height+7)/8,1);
    unbind();
    context->CopyResource(normal.Get(),aoOutput.texture.Get());
    return true;
}

bool LumaPasses::RunSMAA(uint32_t, unsigned,
                         ID3D11ShaderResourceView*, ID3D11RenderTargetView*) {
    // Phase 3e: linearize CS dispatch + 3 draw passes (edge detect, blend
    // weight, neighborhood blending). Needs SMAA area/tex2Dlook textures too.
    return false;
}

bool LumaPasses::RunModulateLighting(uint32_t frame, unsigned eye,
                                     ID3D11RenderTargetView* lightingRtv) {
    if (!psModulateLighting || !vsCopy || !lightingRtv) return false;
    // Bind LumaSettings at b13 (PS stage) — ModulateLighting reads
    // LumaSettings.GameSettings.LightingColor.
    // Faithful port of Luma line 850-862:
    //   DrawStateStack<FullGraphics> to cache/restore all pipeline state
    //   (because setting the lighting RTV may unbind the same resource bound
    //   as SRV elsewhere).
    //   HSGetShader + HSSetShader(nullptr) to disable hull shaders during the
    //   pass (the game uses hull shaders; a fullscreen triangle draw with an
    //   active HS would malfunction).
    //   DrawCustomPixelShaderPass(lighting_rtv, ModulateLighting PS, data).
    //   Restore HS.
    //   DrawStateStack.Restore().
    DrawStateStack state;
    state.Cache(context.Get(), D3D11_PS_CS_UAV_REGISTER_COUNT);
    if (lumaSettingsCB) lumaSettingsCB->Bind(context.Get());
    Microsoft::WRL::ComPtr<ID3D11HullShader> hs;
    Microsoft::WRL::ComPtr<ID3D11DomainShader> ds;
    Microsoft::WRL::ComPtr<ID3D11GeometryShader> gs;
    context->HSGetShader(hs.GetAddressOf(), nullptr, 0);
    context->DSGetShader(ds.GetAddressOf(), nullptr, 0);
    context->GSGetShader(gs.GetAddressOf(), nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);

    DrawCustomPixelShaderPass(lightingRtv, psModulateLighting.Get(), modulatePassData);

    context->HSSetShader(hs.Get(), nullptr, 0);
    context->DSSetShader(ds.Get(), nullptr, 0);
    context->GSSetShader(gs.Get(), nullptr, 0);
    state.Restore(context.Get());
    const bool ok=modulatePassData.srv!=nullptr;
    if(ok && stats.modulateRuns<4)Log("ModulateLighting frame=%u eye=%u", frame, eye);
    return ok;
}

void LumaPasses::DrawCustomPixelShader(ID3D11DeviceContext* ctx,
                                       ID3D11DepthStencilState* dss,
                                       ID3D11BlendState* blend,
                                       ID3D11SamplerState* sampler,
                                       ID3D11VertexShader* vs,
                                       ID3D11PixelShader* ps,
                                       ID3D11ShaderResourceView* sourceSrv,
                                       ID3D11RenderTargetView* targetRtv,
                                       UINT width, UINT height, bool alpha) {
    // Faithful port of Luma's DrawCustomPixelShader (draw.hpp line 893-934).
    // Sets a fullscreen TRIANGLESTRIP draw with the given VS/PS, sources the
    // source SRV at t0, renders into targetRtv, full viewport, no scissor,
    // null IA input layout, null rasterizer state, null DSV, then Draw(4, 0).
    constexpr FLOAT blendFactorAlpha[4] = { 1.f, 1.f, 1.f, 1.f };
    constexpr FLOAT blendFactor[4] = { 1.f, 1.f, 1.f, 0.f };
    ctx->OMSetBlendState(blend, alpha ? blendFactorAlpha : blendFactor, 0xFFFFFFFF);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->RSSetScissorRects(0, nullptr);
    D3D11_VIEWPORT viewport;
    viewport.TopLeftX = 0; viewport.TopLeftY = 0;
    viewport.Width = static_cast<FLOAT>(width); viewport.Height = static_cast<FLOAT>(height);
    viewport.MinDepth = 0; viewport.MaxDepth = 1;
    ctx->RSSetViewports(1, &viewport);
    ctx->PSSetShaderResources(0, 1, &sourceSrv);
    ctx->OMSetDepthStencilState(dss, 0);
    if (sampler) ctx->PSSetSamplers(0, 1, &sampler);
    ctx->OMSetRenderTargets(1, &targetRtv, nullptr);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->PSSetShader(ps, nullptr, 0);
    ctx->IASetInputLayout(nullptr);
    ctx->RSSetState(nullptr);
    ctx->Draw(4, 0);
}

void LumaPasses::DrawCustomPixelShaderPass(ID3D11RenderTargetView* rtv,
                                           ID3D11PixelShader* ps,
                                           CustomPassData& data) {
    // Faithful port of Luma's DrawCustomPixelShaderPass (draw.hpp line 1160-1227).
    // The pattern: the RT we want to modulate is also the SRV source, but D3D11
    // can't have the same texture bound as both RTV and SRV. So we clone the RT
    // into a temp SRV-only texture, copy the RT into it, then draw with the
    // temp as SRV and the original RTV as target. The temp is cached per target
    // (re-created only when the target resource changes).
    if (data.originalRtv.Get() != rtv) {
        data = CustomPassData(); // reset on target change
        if (!rtv) return;
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        rtv->GetResource(resource.GetAddressOf());
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex2d;
        if (FAILED(resource.As(&tex2d))) return;
        D3D11_TEXTURE2D_DESC texDesc{}; tex2d->GetDesc(&texDesc);
        // Clone with SRV bind flag (drop RTV bind on the clone to avoid the
        // simultaneous-bind conflict; the clone is SRV-only).
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->CreateTexture2D(&texDesc, nullptr, data.texture2d.GetAddressOf()))) return;
        data.originalRtv = rtv;
        // If the passed view is already an RTV, reuse it; else create one on
        // the clone. Luma reuses the original RTV when it is one (draw.hpp 1190).
        data.rtv = rtv;
        data.width = texDesc.Width;
        data.height = texDesc.Height;
        if (FAILED(device->CreateShaderResourceView(data.texture2d.Get(), nullptr, data.srv.GetAddressOf()))) {
            data = CustomPassData(); return;
        }
    }
    if (!data.rtv || !data.srv || !data.texture2d) return;
    // Copy the current RT contents into the temp SRV texture (so the PS reads
    // the pre-modulation lighting). Luma line 1223.
    Microsoft::WRL::ComPtr<ID3D11Resource> srcResource;
    data.rtv->GetResource(srcResource.GetAddressOf());
    context->CopySubresourceRegion(data.texture2d.Get(), 0, 0, 0, 0,
                                   srcResource.Get(), 0, nullptr);
    // Draw with Copy VS + the custom PS, sourcing from the temp SRV, into the
    // original RTV. Luma passes device_data.sampler_state_point as the sampler;
    // we pass our cached samplerPoint. Luma passes nullptr for DSS + blend
    // (using defaults); we do the same.
    DrawCustomPixelShader(context.Get(), nullptr, nullptr,
                          samplerPoint.Get(), vsCopy.Get(), ps,
                          data.srv.Get(), data.rtv.Get(), data.width, data.height);
}
