#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
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

private:
    bool loaded{false};
    bool xegtaoEnabled{false};
    bool smaaEnabled{false};
    bool modulateEnabled{false};
    // Cached engine device + immediate context (render-thread only).
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;

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
};
