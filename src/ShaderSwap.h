#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward decl — LumaSettingsCB::Manager is defined in LumaSettingsCB.h.
// ShaderSwap binds it at b13 before substituting a replacement shader, so the
// replacement (which reads LumaSettings.GameSettings.* for intensity
// multipliers etc.) sees correct values.
namespace LumaSettingsCB { class Manager; }

// Native Luma-shader swap for DXHRVR.
//
// Luma fixes DXHR by replacing the game's shaders with hand-authored HLSL
// (Bloom, DoF, ColorGrading, XeGTAO, SMAA, etc.), keyed in ReShade's shader
// hash space. ReShade delivers those replacements as an addon; that path is
// unavailable under DXHRVR because ReShade's D3D11 device wrapping collides
// with this mod's own hooks, and ReShade is stereo-blind anyway.
//
// ShaderSwap sidesteps ReShade entirely. It loads a hash -> compiled-blob
// table built from Luma's DXHR shader sources, and offers to substitute the
// replacement shader for the engine's bound shader at draw time. The hash is
// ReShade's compute_crc32 over the DXBC bytecode (see ShaderHash.h), so it
// keys directly into Luma's filename hashes (0x6B0219A1, etc).
//
// Phase 1 (this file): table loading + match instrumentation (dry run).
// Substitution + injected passes (XeGTAO/SMAA/ModulateLighting) land in
// later phases, after hash matching is confirmed against the live game.

class ShaderSwap {
public:
    enum class Stage : unsigned char { Pixel, Compute, Vertex, Unknown };

    struct Entry {
        uint32_t hash{};            // ReShade shader_hash (e.g. 0x6B0219A1)
        std::string name;           // human label, e.g. "MLAAMask1Gen"
        std::string profile;        // "ps_5_0", "cs_5_0", ...
        std::filesystem::path cso;  // precompiled blob (may be absent in dev)
        Stage stage{Stage::Unknown};
    };

    // Load the hash table and resolve .cso paths relative to the mod folder.
    // `modRoot` is the directory containing DeusExHRVRHost.exe / the proxy
    // DLLs (i.e. the game folder). Table lives at
    // "<modRoot>/DeusExHRVR/shaders/dxhr/table.csv". Missing table is not an
    // error: the swap simply stays empty and every lookup returns false.
    void Load(const std::filesystem::path& modRoot);

    // Note a bound engine shader from RenderStateHook. `engineShader` is the
    // engine's shader object pointer (the value DXHRVR already reads at
    // state+0x198 for PS / state+0x19c for VS). Computes the ReShade hash and
    // records a match if the table knows it. Phase 1: logs only, no
    // substitution. Returns the hash (0 if unreadable).
    //
    // Hot path: RenderStateHook is called per draw on the render thread. The
    // DXBC hash is cached by engine-shader pointer (stable per object, like
    // EffectShader's cache), so the expensive ReadProcessMemory + CRC32 runs
    // at most once per unique shader object, not once per draw.
    uint32_t NoteBound(uint32_t frame, Stage stage, uintptr_t engineShader);
    void CaptureDraw(uint32_t frame, uint32_t hash, unsigned eye);
    void RestoreDrawOverrides();

    // True if `hash` has a table entry for the given stage.
    bool IsKnown(Stage stage, uint32_t hash) const;

    // Lazily compile (or load) the replacement PS for `hash`. Returns nullptr
    // if no entry exists or compilation failed. Cached per (stage,hash).
    ID3D11PixelShader* GetReplacementPS(uint32_t hash, ID3D11Device* device);
    ID3D11ComputeShader* GetReplacementCS(uint32_t hash, ID3D11Device* device);

    // Set the LumaSettings cbuffer manager. When non-null, TrySubstitutePS
    // binds it at b13 before PSSetShader so replacement shaders see correct
    // LumaSettings values.
    void SetLumaSettingsCB(LumaSettingsCB::Manager* m) { lumaSettingsCB = m; }

    // Phase 4: overlap dedup with DXHRVR's existing per-eye corrections.
    // When true, TrySubstitutePS skips substitution for shaders that DXHRVR
    // already corrects stereo-correctly (projected light/shadow via F3, etc.).
    // The `dxhrvrCorrected` flag is computed in RenderStateHook via
    // EffectShader::UsesCentreViewMatrix and passed to TrySubstitutePS.
    void SetDedupWithDXHRVR(bool on) { dedupWithDXHRVR = on; }
    bool DedupWithDXHRVR() const { return dedupWithDXHRVR; }

    // Add a ReShade CRC32 hash to the manual skip-list. Substitution is
    // skipped for these hashes regardless of the dxhrvrCorrected flag.
    // Used for e.g. SSAO generation when XeGTAO is enabled (XeGTAO overwrites
    // the result, so Luma's replacement is wasted work).
    void AddSkipHash(uint32_t hash) { skipHashes.insert(hash); }

    // Capture the D3D11 device + its immediate context for substitution.
    // Called once from NativeTransport::Producer::Init (the point where the
    // engine's device is first available). Render-thread only, like the rest
    // of the swap, so the cached pointers need no synchronization.
    void SetDevice(ID3D11Device* dev);

    // Master switch for actual substitution. Off by default (Phase 1 was
    // instrumentation only); turned on by [Luma] SubstituteShaders=1 or the
    // F12 live toggle. When off, NoteBound still logs matches but no
    // PSSetShader override is issued.
    bool SubstitutionEnabled() const { return substituteEnabled; }
    void SetSubstitutionEnabled(bool on);

    // Substitute the currently-bound pixel shader with Luma's replacement for
    // `hash`, if one exists and substitution is enabled. Called from
    // RenderStateHook AFTER originalRenderState (the engine has just bound
    // its shader), so the override is active for the imminent draw. Returns
    // true if a substitution was issued. No-op (returns false) if
    // substitution is off, the hash isn't in the table, the .cso is missing,
    // or the device/context isn't cached yet.
    //
    // `dxhrvrCorrected`: when true + dedupWithDXHRVR is on, the substitution
    // is skipped — DXHRVR's per-eye correction (F3/F4/F7) already handles
    // this draw stereo-correctly, and Luma's mono replacement might conflict.
    bool TrySubstitutePS(uint32_t hash, bool dxhrvrCorrected = false);

    // True if Load() found a table and at least one entry.
    bool Active() const;

    // Counts for the camera log line.
    struct Stats { uint64_t notes{}; uint64_t matches{}; uint64_t uniqueMatches{}; uint64_t substitutions{}; };
    // Named GetStats (not Stats) to avoid a MSVC name-lookup collision between
    // the nested Stats type and a member function of the same name.
    Stats GetStats() const;

private:
    mutable std::mutex m;
    bool loaded{false};
    std::filesystem::path shaderDir; // .../DeusExHRVR/shaders/dxhr
    // hash -> entry, per stage. Immutable after Load() returns, so the hot
    // NoteBound path reads them without the mutex.
    std::unordered_map<uint32_t, Entry> pixel;
    std::unordered_map<uint32_t, Entry> compute;
    std::unordered_map<uint32_t, Entry> vertex;
    // compiled shader cache (hash -> live shader). Phase 2 cold path; guarded.
    std::unordered_map<uint32_t, Microsoft::WRL::ComPtr<ID3D11PixelShader>> psCache;
    std::unordered_map<uint32_t, Microsoft::WRL::ComPtr<ID3D11ComputeShader>> csCache;
    // Hot-path DXBC hash cache: engine shader object pointer -> ReShade hash.
    // Render-thread only (RenderStateHook detour), so no mutex, mirroring
    // EffectShader's cache. An entry of 0 means "already tried, unreadable".
    std::unordered_map<uintptr_t, uint32_t> hashCache;
    // instrumentation (render-thread only)
    uint64_t noteCount{};
    uint64_t matchCount{};
    uint64_t substitutionCount{};
    std::unordered_set<uint32_t> matchedHashes;
    std::unordered_set<uint32_t> substitutedHashes; // first-substitution logging
    // substitution state (render-thread only; SetDevice/SetSubstitutionEnabled
    // are called from the render thread too)
    bool substituteEnabled{false};
    bool drawOverridden{};
    Microsoft::WRL::ComPtr<ID3D11PixelShader> savedShader;
    Microsoft::WRL::ComPtr<ID3D11Buffer> savedSettings;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    // Optional LumaSettings cbuffer manager (set via SetLumaSettingsCB).
    LumaSettingsCB::Manager* lumaSettingsCB{};
    // Phase 4: dedup state
    bool dedupWithDXHRVR{true};
    std::unordered_set<uint32_t> skipHashes; // manual skip-list (CRC32 hashes)
    std::unordered_set<uint32_t> skippedHashes; // first-skip logging

    void Log(const char* fmt, ...) const;
    static Stage ParseStage(const std::string& profile);
    static std::string HashHex(uint32_t h);
};
