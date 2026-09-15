#include "LumaPasses.h"
#include "ShaderHash.h"
#include <cstdio>
#include <cstdarg>
#include <fstream>

// ReShade shader hashes for DXHR's injected-pass trigger draws. These are the
// same values Luma keys on in main.cpp (forced_shader_names / ShaderHashesList).
// Sourced from Luma's DXHR shader filenames; resolved at runtime against the
// bound PS via ShaderHash::Crc32 over DXBC.
//
//   SSAOGeneration  — 0xD44718C4 (GenerateAmbientOcclusion_0xD44718C4)
//   SSAODenoise CS  — 0x61A9E9CD / 0x74C22F6D (DoF_Step2/3 are compute, but
//                     the SSAO denoise CS hash isn't in the shader filenames;
//                     Luma matches it via runtime hash, we do the same)
//   MLAA mask       — 0x6B0219A1 (MLAAMask1Gen)
//   Copy (SSAO out) — 0x4068DABF / 0xB8813A2F
//
// Phase 3a: we trigger XeGTAO on the Copy draw (like Luma) where the normal RT
// is bound, and SMAA on the MLAA-mask draw. ModulateLighting triggers on the
// first post-SSAO material draw on the swapchain RT — detected by RT-resource
// comparison, same as Luma. For Phase 3's first cut we use the hash triggers
// that are unambiguous; ModulateLighting's swapchain-RT detection lands in 3f.
namespace {
constexpr uint32_t kHash_SSAOGeneration = 0xD44718C4u;
constexpr uint32_t kHash_Copy1          = 0x4068DABFu;
constexpr uint32_t kHash_Copy2          = 0xB8813A2Fu;
constexpr uint32_t kHash_MLAAMask1      = 0x6B0219A1u;

constexpr unsigned XE_GTAO_DEPTH_MIP_LEVELS = 5;
constexpr unsigned XE_GTAO_NUMTHREADS_X = 8;
constexpr unsigned XE_GTAO_NUMTHREADS_Y = 8;
} // namespace

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

    // SMAA draw-pass shaders (Phase 3e). Optional for now; SMAA stays disabled
    // if these are missing.
    // TODO(Phase 3e): load vs/ps SMAA edge/blending/neighborhood shaders.

    bool xegtaoOk = csXeGTAOPrefilter && csXeGTAOMain && csXeGTAODenoise1 && csXeGTAODenoise2;
    bool smaaCsOk = csSMAALinearize != nullptr;
    bool modulateOk = psModulateLighting != nullptr;

    loaded = xegtaoOk || smaaCsOk || modulateOk;
    Log("Load: xegtao=%d smaa_cs=%d modulate=%d (loaded=%d)",
        int(xegtaoOk), int(smaaCsOk), int(modulateOk), int(loaded));
    return loaded;
}

bool LumaPasses::OnDraw(uint32_t frame, uint32_t hash, unsigned eye) {
    if (!loaded || !device || !context) return false;

    // XeGTAO: trigger on the SSAO "Copy" draw where the normal RT (with alpha
    // slot for AO) is bound, like Luma. We skip the engine's own SSAO gen +
    // denoise draws (hashes kHash_SSAOGeneration and the CS denoise) by letting
    // them run but not acting on them — the engine writes its SSAO, then we
    // overwrite the AO term on the Copy draw. (Phase 3d refinement: skip the
    // engine's SSAO draws entirely once we confirm the hashes.)
    if (xegtaoEnabled && (hash == kHash_Copy1 || hash == kHash_Copy2) &&
        frame != lastFrameXeGTAO && csXeGTAOPrefilter) {
        // Grab the currently-bound RTV (the normal RT) + DSV (depth) + the
        // SSAO scene constant buffer the engine just bound to PS cb2.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
        context->OMGetRenderTargets(1, rtv.GetAddressOf(), dsv.GetAddressOf());
        if (!rtv || !dsv) { Log("XeGTAO trigger: no RTV/DSV bound, skipping"); return false; }

        // Build a depth SRV from the DSV's resource. The engine's depth is a
        // TYPELESS or DEPTH24_STENCIL8 texture; we create an SRV with the
        // corresponding shader-resource format.
        Microsoft::WRL::ComPtr<ID3D11Resource> depthRes;
        dsv->GetResource(depthRes.GetAddressOf());
        if (!depthRes) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        // Query the texture desc to pick the right format.
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
            lastFrameXeGTAO = frame;
            ++stats.xegtaoRuns;
            return true;
        }
    }

    // SMAA: trigger on MLAA-mask draw (Phase 3e — full impl lands then).
    // For now, just count the trigger so we can confirm hash matching.
    if (smaaEnabled && hash == kHash_MLAAMask1 && frame != lastFrameSMAA) {
        lastFrameSMAA = frame;
        Log("SMAA trigger (MLAA mask) frame=%u eye=%u — Phase 3e stub", frame, eye);
        // Phase 3e: RunSMAA(...) here.
    }

    // ModulateLighting: Phase 3f — triggers on first post-SSAO swapchain draw.
    // Stub for now.
    if (modulateEnabled && frame != lastFrameModulate && psModulateLighting) {
        // Phase 3f: detect swapchain RT + run RunModulateLighting.
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
    ID3D11ShaderResourceView* srvsMain[] = { srvPrefilter.Get(), nullptr }; // tex0=prefiltered depth; tex1=normals (TODO: bind normal SRV)
    // TODO(Phase 3d): the engine's view-space normal is in `normalRes`. Build
    // an SRV for it and bind to t1. For the first cut we pass nullptr; XeGTAO
    // falls back to generated normals if XE_GTAO_GENERATE_NORMALS is defined.
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

bool LumaPasses::RunModulateLighting(uint32_t, unsigned, ID3D11RenderTargetView*) {
    // Phase 3f: bind psModulateLighting, draw a fullscreen triangle onto the
    // lighting RT, restore engine state.
    return false;
}
