#include "LumaPasses.h"
#include "ShaderHash.h"
#include "DrawStateStack.h"
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
    return Microsoft::WRL::ComPtr<ID3D11ComputeShader>(cs);
}

Microsoft::WRL::ComPtr<ID3D11PixelShader> LumaPasses::LoadPS(
    const std::filesystem::path& csoDir, ID3D11Device* device, const wchar_t* stem) {
    auto path = csoDir / (std::wstring(stem) + L".cso");
    std::vector<uint8_t> blob;
    if (!LoadBlob(path, blob)) return nullptr;
    ID3D11PixelShader* ps{};
    if (FAILED(device->CreatePixelShader(blob.data(), blob.size(), nullptr, &ps))) return nullptr;
    return Microsoft::WRL::ComPtr<ID3D11PixelShader>(ps);
}

Microsoft::WRL::ComPtr<ID3D11VertexShader> LumaPasses::LoadVS(
    const std::filesystem::path& csoDir, ID3D11Device* device, const wchar_t* stem) {
    auto path = csoDir / (std::wstring(stem) + L".cso");
    std::vector<uint8_t> blob;
    if (!LoadBlob(path, blob)) return nullptr;
    ID3D11VertexShader* vs{};
    if (FAILED(device->CreateVertexShader(blob.data(), blob.size(), nullptr, &vs))) return nullptr;
    return Microsoft::WRL::ComPtr<ID3D11VertexShader>(vs);
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
    csXeGTAOPrefilter = LoadCS(csoDir, dev, L"Luma_DXHR_XeGTAO_prefilter_depths16x16_cs");
    csXeGTAOMain      = LoadCS(csoDir, dev, L"Luma_DXHR_XeGTAO_main_pass_cs");
    csXeGTAODenoise1  = LoadCS(csoDir, dev, L"Luma_DXHR_XeGTAO_denoise_pass_cs_XE_GTAO_FINAL_APPLY_0");
    csXeGTAODenoise2  = LoadCS(csoDir, dev, L"Luma_DXHR_XeGTAO_denoise_pass_cs_XE_GTAO_FINAL_APPLY_1");

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
    smpDesc.MinLOD = smpDesc.MaxLOD = 0;
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

bool LumaPasses::OnDraw(uint32_t frame, uint32_t hash, unsigned eye) {
    if (!loaded || !device || !context) return false;

    // Scheduling follows Luma's OnDrawOrDispatch (main.cpp lines 555-895) in
    // the same if/else-if order. Luma returns Skip (don't draw) or Replaced
    // (custom draw instead) for some triggers; in DXHRVR's hook we can't
    // suppress the engine's draw (it runs after originalRenderState), so for
    // "Skip" cases we let the engine draw proceed (harmless — XeGTAO/Modulate
    // overwrite the result) and for "Replaced" cases we run our pass on top.

    // (1) Lighting-buffer capture (Luma line 575-581): the first time a
    // Lighting PS runs, grab the bound RTV — that's the lighting buffer we'll
    // modulate later. Per-frame (reset in OnFrameStart).
    if (modulateEnabled && !hasFoundLightingBuffer && HASH_IN_LIST(hash, kHash_Lighting)) {
        context->OMGetRenderTargets(1, lightingBufferRtv.GetAddressOf(), nullptr);
        hasFoundLightingBuffer = true;
        Log("Lighting buffer captured frame=%u eye=%u rtv=%p", frame, eye, lightingBufferRtv.Get());
    }
    // (2) MLAA mask (Luma line 582-588): if SMAA enabled, Luma returns Skip so
    // the engine doesn't draw its MLAA mask. We can't skip; we let it draw and
    // SMAA overwrites later. (Phase 3e will run SMAA here.)
    else if (smaaEnabled && HASH_IN_LIST(hash, kHash_MLAAMask) && frame != lastFrameSMAA) {
        lastFrameSMAA = frame;
        Log("SMAA trigger (MLAA mask) frame=%u eye=%u — Phase 3e stub", frame, eye);
        // Phase 3e: RunSMAA(...) here.
    }
    // (3) SupportedAA (Luma line 590-651): cache the swapchain RTV. Luma runs
    // SMAA here if enabled. We cache the RTV for later material-draw detection.
    else if (HASH_IN_LIST(hash, kHash_SupportedAA)) {
        if (!hasSwapchainRtv) {
            swapchainRtv.Reset();
            context->OMGetRenderTargets(1, swapchainRtv.GetAddressOf(), nullptr);
            hasSwapchainRtv = swapchainRtv != nullptr;
            if (hasSwapchainRtv)
                Log("Swapchain RTV cached frame=%u eye=%u rtv=%p", frame, eye, swapchainRtv.Get());
        }
        // Phase 3e: if smaaEnabled, RunSMAA here (SupportedAA draw is the AA
        // pass that draws on the swapchain).
    }
    // (4) ColorGrading (Luma line 654-669): marks the gold filter drawn. No
    // injected action in our port (the ColorGrading shader swap covers it).
    // (5) BloomComposition (Luma line 671-678): mark main post-processing done;
    //   bind depth SRV to t3 for fog. We just set the flag.
    else if (HASH_IN_LIST(hash, kHash_BloomComposition)) {
        hasDrawnMainPostProcessing = true;
    }
    // (6) SSAOGeneration (Luma line 680-690): mark SSAO drawn. If XeGTAO
    //   enabled, Luma captures cb_ssao_scene_buffer and returns Skip. We set
    //   the flag; the cb capture happens at the Copy draw (next).
    else if (HASH_IN_LIST(hash, kHash_SSAOGeneration)) {
        hasDrawnSSAO = true;
    }
    // (7) SSAODenoise CS (Luma line 691-697): Luma returns Skip if XeGTAO.
    //   Nothing to do here (engine's denoise is harmless; we overwrite).
    // (8) Copy (Luma line 698-826): if XeGTAO enabled && !has_drawn_xegtao &&
    //   has_drawn_ssao → run the XeGTAO 4-pass chain, mark has_drawn_xegtao.
    else if (xegtaoEnabled && HASH_IN_LIST(hash, kHash_Copy) &&
             !hasDrawnXeGTAO && hasDrawnSSAO && csXeGTAOPrefilter) {
        // Grab the currently-bound RTV (the normal RT with alpha AO slot) +
        // DSV (depth) + the SSAO scene constant buffer from PS cb2.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
        context->OMGetRenderTargets(1, rtv.GetAddressOf(), dsv.GetAddressOf());
        if (!rtv || !dsv) { Log("XeGTAO trigger: no RTV/DSV bound, skipping"); return false; }

        // Build a depth SRV from the DSV's resource. The engine's depth is a
        // TYPELESS or DEPTH24_STENCIL8 texture; create an SRV with the matching
        // shader-resource format.
        Microsoft::WRL::ComPtr<ID3D11Resource> depthRes;
        dsv->GetResource(depthRes.GetAddressOf());
        if (!depthRes) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTex;
        if (FAILED(depthRes.As(&depthTex))) return false;
        D3D11_TEXTURE2D_DESC td{}; depthTex->GetDesc(&td);
        srvDesc.Format = (td.Format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
                          td.Format == DXGI_FORMAT_R24G8_TYPELESS ||
                          td.Format == DXGI_FORMAT_R24_UNORM_X8_TYPELESS)
                         ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS
                         : (td.Format == DXGI_FORMAT_D32_FLOAT ||
                            td.Format == DXGI_FORMAT_R32_TYPELESS)
                           ? DXGI_FORMAT_R32_FLOAT : td.Format;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthSrv;
        if (FAILED(device->CreateShaderResourceView(depthRes.Get(), &srvDesc, depthSrv.GetAddressOf()))) {
            Log("XeGTAO: failed to create depth SRV (fmt=%u)", td.Format);
            return false;
        }

        // PSGetConstantBuffers(2) — Luma captures cb_ssao_scene_buffer here.
        Microsoft::WRL::ComPtr<ID3D11Buffer> sceneCB;
        context->PSGetConstantBuffers(2, 1, sceneCB.GetAddressOf());

        if (RunXeGTAO(frame, eye, depthSrv.Get(), rtv.Get(), sceneCB.Get())) {
            hasDrawnXeGTAO = true;
            lastFrameXeGTAO = frame;
            ++stats.xegtaoRuns;
            return true;
        }
    }
    // (9) Materials (Luma line 830-878): the first material draw on the
    //   swapchain RT, after SSAO, not UI. If we have a captured lighting buffer
    //   and haven't modulated yet this frame → run ModulateLighting.
    else if (modulateEnabled && !hasModulatedLighting && !hasDrawnMainPostProcessing &&
             (!hasDrawnSSAO || hasDrawnSSAO) && !HASH_IN_LIST(hash, kHash_UI) &&
             lightingBufferRtv && psModulateLighting && vsCopy) {
        // Detect "draw on the swapchain RT": compare the currently-bound RTV's
        // resource to the cached swapchain RTV's resource (Luma line 832-845).
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
        context->OMGetRenderTargets(1, rtv.GetAddressOf(), nullptr);
        if (rtv && swapchainRtv) {
            Microsoft::WRL::ComPtr<ID3D11Resource> r1, r2;
            swapchainRtv->GetResource(r1.GetAddressOf());
            rtv->GetResource(r2.GetAddressOf());
            if (r2 && r2.Get() == r1.Get()) {
                if (RunModulateLighting(frame, eye, lightingBufferRtv.Get())) {
                    hasModulatedLighting = true;
                    lastFrameModulate = frame;
                    ++stats.modulateRuns;
                    return true;
                }
            }
        }
    }

    return false;
}

bool LumaPasses::RunXeGTAO(uint32_t frame, unsigned eye,
                           ID3D11ShaderResourceView* depthSrv,
                           ID3D11RenderTargetView* normalRtv,
                           ID3D11Buffer* sceneCB) {
    if (!csXeGTAOPrefilter || !csXeGTAOMain || !csXeGTAODenoise1 || !csXeGTAODenoise2)
        return false;

    // Resolve output dimensions from the normal RT (the engine's SSAO output
    // size = per-eye resolution).
    Microsoft::WRL::ComPtr<ID3D11Resource> normalRes;
    normalRtv->GetResource(normalRes.GetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11Texture2D> normalTex;
    if (FAILED(normalRes.As(&normalTex))) return false;
    D3D11_TEXTURE2D_DESC normalDesc{}; normalTex->GetDesc(&normalDesc);
    const UINT W = normalDesc.Width, H = normalDesc.Height;

    // --- Pass 1: Prefilter depths. 5-mip R32_FLOAT depth pyramid. ---
    D3D11_TEXTURE2D_DESC depthPydDesc{};
    depthPydDesc.Width = W; depthPydDesc.Height = H;
    depthPydDesc.MipLevels = XE_GTAO_DEPTH_MIP_LEVELS;
    depthPydDesc.ArraySize = 1;
    depthPydDesc.Format = DXGI_FORMAT_R32_FLOAT;
    depthPydDesc.SampleDesc.Count = 1;
    depthPydDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthPyd;
    if (FAILED(device->CreateTexture2D(&depthPydDesc, nullptr, depthPyd.GetAddressOf()))) {
        Log("XeGTAO: CreateTexture2D(depthPyd) failed"); return false;
    }
    std::array<Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>, XE_GTAO_DEPTH_MIP_LEVELS> uavPrefilter;
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = depthPydDesc.Format;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    for (unsigned i = 0; i < XE_GTAO_DEPTH_MIP_LEVELS; ++i) {
        uavDesc.Texture2D.MipSlice = i;
        if (FAILED(device->CreateUnorderedAccessView(depthPyd.Get(), &uavDesc, uavPrefilter[i].GetAddressOf()))) {
            Log("XeGTAO: CreateUAV(depthPyd[%u]) failed", i); return false;
        }
    }
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srvPrefilter;
    if (FAILED(device->CreateShaderResourceView(depthPyd.Get(), nullptr, srvPrefilter.GetAddressOf()))) {
        Log("XeGTAO: CreateSRV(depthPyd) failed"); return false;
    }

    // Point sampler for depth taps (XeGTAO uses point sampling).
    D3D11_SAMPLER_DESC smpDesc{}; smpDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    smpDesc.AddressU = smpDesc.AddressV = smpDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    smpDesc.MaxAnisotropy = 1; smpDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    smpDesc.MinLOD = smpDesc.MaxLOD = 0;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> smpPoint;
    device->CreateSamplerState(&smpDesc, smpPoint.GetAddressOf());

    // Bind + dispatch prefilter. tex0=depth(t0), smp=s0, u0..u4=mip pyramid.
    std::array<ID3D11UnorderedAccessView*, XE_GTAO_DEPTH_MIP_LEVELS> uavs{};
    for (unsigned i = 0; i < XE_GTAO_DEPTH_MIP_LEVELS; ++i) uavs[i] = uavPrefilter[i].Get();
    context->CSSetUnorderedAccessViews(0, XE_GTAO_DEPTH_MIP_LEVELS, uavs.data(), nullptr);
    context->CSSetShader(csXeGTAOPrefilter.Get(), nullptr, 0);
    context->CSSetShaderResources(0, 1, &depthSrv);
    ID3D11Buffer* cb = sceneCB; context->CSSetConstantBuffers(0, 1, &cb);
    ID3D11SamplerState* smp = smpPoint.Get(); context->CSSetSamplers(0, 1, &smp);
    context->Dispatch((W + 16 - 1) / 16, (H + 16 - 1) / 16, 1);

    // Unbind prefilter UAVs.
    std::array<ID3D11UnorderedAccessView*, XE_GTAO_DEPTH_MIP_LEVELS> nullUavs{};
    context->CSSetUnorderedAccessViews(0, XE_GTAO_DEPTH_MIP_LEVELS, nullUavs.data(), nullptr);

    // --- Pass 2: Main AO pass. Output = R8G8_UNORM (AO term + edges). ---
    D3D11_TEXTURE2D_DESC mainDesc = normalDesc;
    mainDesc.Format = DXGI_FORMAT_R8G8_UNORM;
    mainDesc.MipLevels = 1;
    mainDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> mainTex;
    if (FAILED(device->CreateTexture2D(&mainDesc, nullptr, mainTex.GetAddressOf()))) return false;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uavMain;
    device->CreateUnorderedAccessView(mainTex.Get(), nullptr, uavMain.GetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srvMain;
    device->CreateShaderResourceView(mainTex.Get(), nullptr, srvMain.GetAddressOf());

    // Unbind the normal RTV (XeGTAO writes only to UAVs); restore after.
    context->OMSetRenderTargets(0, nullptr, nullptr);
    ID3D11UnorderedAccessView* uavMainArr[] = { uavMain.Get() };
    context->CSSetUnorderedAccessViews(0, 1, uavMainArr, nullptr);
    context->CSSetShader(csXeGTAOMain.Get(), nullptr, 0);
    // Build an SRV for the normal RT texture (tex1 = view-space normals).
    // XeGTAO's main_pass_cs samples tex1 and decodes normals (xyz * 2 - 1).
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srvNormal;
    device->CreateShaderResourceView(normalRes.Get(), nullptr, srvNormal.GetAddressOf());
    ID3D11ShaderResourceView* srvsMain[] = { srvPrefilter.Get(), srvNormal.Get() };
    context->CSSetShaderResources(0, 2, srvsMain);
    context->Dispatch((W + XE_GTAO_NUMTHREADS_X - 1) / XE_GTAO_NUMTHREADS_X,
                      (H + XE_GTAO_NUMTHREADS_Y - 1) / XE_GTAO_NUMTHREADS_Y, 1);

    // --- Pass 3: Denoise pass 1 (XE_GTAO_FINAL_APPLY=0). Output float4. ---
    D3D11_TEXTURE2D_DESC dn1Desc = mainDesc;
    dn1Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // final_output is float4 in pass 1
    Microsoft::WRL::ComPtr<ID3D11Texture2D> dn1Tex;
    device->CreateTexture2D(&dn1Desc, nullptr, dn1Tex.GetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uavDn1;
    device->CreateUnorderedAccessView(dn1Tex.Get(), nullptr, uavDn1.GetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srvDn1;
    device->CreateShaderResourceView(dn1Tex.Get(), nullptr, srvDn1.GetAddressOf());

    ID3D11UnorderedAccessView* uavDn1Arr[] = { uavDn1.Get() };
    context->CSSetUnorderedAccessViews(0, 1, uavDn1Arr, nullptr);
    context->CSSetShader(csXeGTAODenoise1.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srvsDn1[] = { srvMain.Get() };
    context->CSSetShaderResources(0, 1, srvsDn1);
    // Denoise runs 2x horizontal threads: dispatch (W/2, H).
    context->Dispatch((W + (XE_GTAO_NUMTHREADS_X * 2) - 1) / (XE_GTAO_NUMTHREADS_X * 2),
                      (H + XE_GTAO_NUMTHREADS_Y - 1) / XE_GTAO_NUMTHREADS_Y, 1);

    // --- Pass 4: Denoise pass 2 (XE_GTAO_FINAL_APPLY=1). Output unorm float2
    // back into the normal RT's alpha (the engine's AO slot). ---
    // Build a UAV on the normal RT texture (normalRes) to write final AO.
    D3D11_UNORDERED_ACCESS_VIEW_DESC normalUavDesc{};
    normalUavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // interpret as RGBA to write RG
    normalUavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uavNormal;
    if (FAILED(device->CreateUnorderedAccessView(normalRes.Get(), &normalUavDesc, uavNormal.GetAddressOf()))) {
        Log("XeGTAO: CreateUAV(normalRT) failed — AO not written back");
    } else {
        ID3D11UnorderedAccessView* uavNormalArr[] = { uavNormal.Get() };
        context->CSSetUnorderedAccessViews(0, 1, uavNormalArr, nullptr);
        context->CSSetShader(csXeGTAODenoise2.Get(), nullptr, 0);
        ID3D11ShaderResourceView* srvsDn2[] = { srvDn1.Get() };
        context->CSSetShaderResources(0, 1, srvsDn2);
        context->Dispatch((W + (XE_GTAO_NUMTHREADS_X * 2) - 1) / (XE_GTAO_NUMTHREADS_X * 2),
                          (H + XE_GTAO_NUMTHREADS_Y - 1) / XE_GTAO_NUMTHREADS_Y, 1);
    }

    // Cleanup: unbind all CS resources we bound.
    ID3D11UnorderedAccessView* nullUav[] = { nullptr };
    context->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
    ID3D11ShaderResourceView* nullSrv[2] = { nullptr, nullptr };
    context->CSSetShaderResources(0, 2, nullSrv);
    ID3D11Buffer* nullCb[] = { nullptr };
    context->CSSetConstantBuffers(0, 1, nullCb);
    ID3D11SamplerState* nullSmp[] = { nullptr };
    context->CSSetSamplers(0, 1, nullSmp);
    // Restore the engine's RTV we unbound at pass 2.
    context->OMSetRenderTargets(1, &normalRtv, nullptr);

    Log("XeGTAO frame=%u eye=%u %ux%u OK", frame, eye, W, H);
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
    state.Cache(context.Get(), D3D11_1_UAV_SLOT_COUNT);
    Microsoft::WRL::ComPtr<ID3D11HullShader> hs;
    context->HSGetShader(hs.GetAddressOf(), nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);

    DrawCustomPixelShaderPass(lightingRtv, psModulateLighting.Get(), modulatePassData);

    context->HSSetShader(hs.Get(), nullptr, 0);
    state.Restore(context.Get());
    Log("ModulateLighting frame=%u eye=%u OK", frame, eye);
    return true;
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
