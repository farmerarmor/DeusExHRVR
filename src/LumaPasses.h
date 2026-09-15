#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Per-eye injected render passes ported from Luma's ReShade-addon scheduling.
//
// Phase 2 swapped shaders in place (drop-in replacements). Phase 3 adds passes
// Luma *injects* — whole new draw/dispatch calls that didn't exist in the
// engine's pipeline. In Luma these run as draw-time overrides in
// OnDrawOrDispatch; here they run from RenderStateHook at the same logical
// moments, identified by the bound pixel shader's hash.
//
// Per-eye is automatic: the engine renders each eye's draws separately into
// the double-height native stereo buffer, calling RenderStateHook per draw.
// state[0x5ea] is the current eye (0/1). Injected passes read whatever RT/
// depth the engine currently has bound (via OMGetRenderTargets, same as Luma),
// so they operate on the current eye's slice naturally.
//
// Three passes, in scheduling order:
//   1. XeGTAO (ambient occlusion): 4 compute dispatches, triggered when the
//      engine's SSAO-generation draw fires. Replaces the game's SSAO with
//      Intel XeGTAO. Reads depth + view-space normals; writes AO term into
//      the alpha channel of the engine's normal RT (same slot the game uses).
//   2. ModulateLighting: a custom pixel-shader pass on the lighting buffer,
//      run once when materials rendering begins (the first draw on the
//      swapchain RT after SSAO). Adjusts lighting color before materials
//      composite against it.
//   3. SMAA: replaces the game's MLAA. Runs when the engine's MLAA-mask draw
//      fires. Linearizes the scene (CS), then runs SMAA edge-detect + blend
//      weight + neighborhood-blend draws onto the swapchain RT.
//
// Each pass is independently toggleable ([Luma] XeGTAOEnable / SMAAEnable /
// ModulateLightingEnable). All default off until tested.

class LumaPasses {
public:
    // Compile (or load from .cso) the injected-pass shaders. Called once after
    // ShaderSwap::SetDevice, when the D3D11 device is first available. `csoDir`
    // is shaders/dxhr/compiled/ (where tools/compile_shaders.ps1 writes).
    // Caches the device + immediate context for later Run* calls (render-thread
    // only, like ShaderSwap). Returns false if every pass failed to load.
    bool Load(const std::filesystem::path& csoDir, ID3D11Device* device);

    // Master enable for each pass. When off, the trigger draws are ignored
    // (not even logged past the first sighting).
    void SetXeGTAOEnabled(bool on) { xegtaoEnabled = on; }
    void SetSMAAEnabled(bool on) { smaaEnabled = on; }
    void SetModulateLightingEnabled(bool on) { modulateEnabled = on; }
    bool XeGTAOEnabled() const { return xegtaoEnabled; }
    bool SMAAEnabled() const { return smaaEnabled; }
    bool ModulateLightingEnabled() const { return modulateEnabled; }

    // Trigger evaluation, called from RenderStateHook with the bound PS hash.
    // Returns true if a pass ran. Uses the cached device/context from Load.
    // `eye` is state[0x5ea]?0:1 for logging.
    bool OnDraw(uint32_t frame, uint32_t hash, unsigned eye);

    bool Loaded() const { return loaded; }

    // Counts for the camera log.
    struct Stats {
        uint64_t xegtaoRuns{};
        uint64_t smaaRuns{};
        uint64_t modulateRuns{};
    };
    Stats GetStats() const { return stats; }

    // Per-frame reset: call at the start of each engine frame (from OnPresent)
    // to clear the one-shot-per-frame guards and the GameDeviceData-style
    // scheduling flags, exactly like Luma resets its game_device_data on
    // frame boundary. The engine renders both eyes within one frame, so the
    // flags cover both eyes; per-eye correctness comes from the hook firing
    // separately per eye's draws, not from per-eye flags here.
    void OnFrameStart();

private:
    bool loaded{false};
    bool xegtaoEnabled{false};
    bool smaaEnabled{false};
    bool modulateEnabled{false};
    // Cached engine device + immediate context (render-thread only).
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;

    // --- Per-frame scheduling state, mirroring Luma's GameDeviceData ---
    // has_found_lighting_buffer: set when the Lighting PS first runs and we
    //   capture the bound RTV into lighting_buffer_rtv (Luma line 575-581).
    bool hasFoundLightingBuffer{false};
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> lightingBufferRtv;
    // has_drawn_ssao: set when the SSAOGeneration PS runs (Luma line 683).
    bool hasDrawnSSAO{false};
    // has_drawn_xegtao: set when our XeGTAO chain completes for this frame.
    bool hasDrawnXeGTAO{false};
    // has_drawn_main_post_processing: set when BloomComposition runs (Luma
    //   line 673) — gates ModulateLighting to run before post starts.
    bool hasDrawnMainPostProcessing{false};
    // has_modulated_lighting: set when ModulateLighting runs (Luma line 858).
    bool hasModulatedLighting{false};
    // Cached swapchain RTV (Luma caches it on the first SupportedAA draw,
    //   line 594-595). Used to detect "first material draw on swapchain".
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> swapchainRtv;
    bool hasSwapchainRtv{false};
    // Per-frame re-entrancy guards for the trigger draws (so SMAA's MLAA-mask
    //   trigger fires once per frame even though the draw may repeat).
    uint64_t lastFrameXeGTAO{};
    uint64_t lastFrameModulate{};
    uint64_t lastFrameSMAA{};

    // Injected-pass shaders. Held as raw ComPtrs because they're built once
    // and bound directly (no per-hash lookup, unlike ShaderSwap).
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> csXeGTAOPrefilter;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> csXeGTAOMain;
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> csXeGTAODenoise1; // XE_GTAO_FINAL_APPLY=0
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> csXeGTAODenoise2; // XE_GTAO_FINAL_APPLY=1
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> csSMAALinearize;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psModulateLighting;
    // SMAA draw passes (3 PS + matching VS). Phase 3e.
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vsSMAAEdgeDetection;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psSMAAEdgeDetection;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vsSMAABlendingWeight;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psSMAABlendingWeight;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vsSMAANeighborhoodBlending;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> psSMAANeighborhoodBlending;
    // Copy VS: Luma's fullscreen-triangle vertex shader (SV_VertexID-based).
    // Used by DrawCustomPixelShader for ModulateLighting (and later SMAA).
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vsCopy;
    // Cached point sampler for injected passes (Luma's device_data.sampler_state_point).
    Microsoft::WRL::ComPtr<ID3D11SamplerState> samplerPoint;
    // Per-pass cached SRV/RTV/texture for DrawCustomPixelShaderPass (mirrors
    // Luma's CustomPixelShaderPassData). Re-created when the target RTV
    // resource changes; reused across frames for the same target.
    struct CustomPassData {
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> originalRtv;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture2d;     // SRV copy of the RT
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;  // SRV on texture2d
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;    // RTV on texture2d (used if original isn't an RTV)
        UINT width{};
        UINT height{};
    };
    CustomPassData modulatePassData;

    // Per-frame re-entrancy guard: each injected pass runs at most once per
    // engine frame per eye. Reset in OnPresent. The engine fires the trigger
    // draw many times during a frame's post-processing; we only want the first.
    uint64_t lastFrameXeGTAO{};
    uint64_t lastFrameModulate{};
    uint64_t lastFrameSMAA{};

    Stats stats;

    void Log(const char* fmt, ...) const;
    static Microsoft::WRL::ComPtr<ID3D11ComputeShader> LoadCS(
        const std::filesystem::path& csoDir, ID3D11Device* device,
        const wchar_t* stem);
    static Microsoft::WRL::ComPtr<ID3D11PixelShader> LoadPS(
        const std::filesystem::path& csoDir, ID3D11Device* device,
        const wchar_t* stem);
    static Microsoft::WRL::ComPtr<ID3D11VertexShader> LoadVS(
        const std::filesystem::path& csoDir, ID3D11Device* device,
        const wchar_t* stem);
    static bool LoadBlob(const std::filesystem::path& path, std::vector<uint8_t>& out);

    // XeGTAO: run the 4-pass chain. `depthSrv` is the engine's bound depth;
    // `normalRtv` is the RT the SSAO-generation draw targets (we write AO into
    // its alpha). Returns true if all 4 dispatches issued.
    bool RunXeGTAO(uint32_t frame, unsigned eye,
                   ID3D11ShaderResourceView* depthSrv,
                   ID3D11RenderTargetView* normalRtv,
                   ID3D11Buffer* sceneCB);

    // SMAA: linearize + 3 draw passes onto the swapchain RT. Phase 3e.
    bool RunSMAA(uint32_t frame, unsigned eye,
                 ID3D11ShaderResourceView* sceneSrv,
                 ID3D11RenderTargetView* swapchainRtv);

    // ModulateLighting: one custom PS pass on the lighting buffer RT. Phase 3f.
    bool RunModulateLighting(uint32_t frame, unsigned eye,
                             ID3D11RenderTargetView* lightingRtv);

    // Faithful port of Luma's DrawCustomPixelShader (Source/Core/utils/draw.hpp).
    // Binds a fullscreen TRIANGLESTRIP draw (Copy VS + given PS), sources from
    // `sourceSrv` at t0, renders into `targetRtv`, sets a full viewport, no
    // scissor, null IA input layout, null rasterizer state, null DSV, then
    // Draw(4, 0). The caller is responsible for caching/restoring surrounding
    // state via DrawStateStack.
    void DrawCustomPixelShader(ID3D11DeviceContext* ctx,
                               ID3D11DepthStencilState* dss, ID3D11BlendState* blend,
                               ID3D11SamplerState* sampler,
                               ID3D11VertexShader* vs, ID3D11PixelShader* ps,
                               ID3D11ShaderResourceView* sourceSrv,
                               ID3D11RenderTargetView* targetRtv,
                               UINT width, UINT height, bool alpha = true);

    // Faithful port of Luma's DrawCustomPixelShaderPass. Copies the resource
    // behind `rtv` into a temp SRV texture (so the same texture can be both
    // SRV and RTV), then calls DrawCustomPixelShader with that SRV + the
    // original RTV + the Copy VS + `ps`. Caches the temp texture per target
    // (re-created only when the target resource changes). `data` holds the
    // cache. This is the pattern Luma uses for ModulateLighting.
    void DrawCustomPixelShaderPass(ID3D11RenderTargetView* rtv,
                                   ID3D11PixelShader* ps, CustomPassData& data);
};
