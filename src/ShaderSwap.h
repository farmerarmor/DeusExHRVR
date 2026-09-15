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

    // True if `hash` has a table entry for the given stage.
    bool IsKnown(Stage stage, uint32_t hash) const;

    // Lazily compile (or load) the replacement PS for `hash`. Returns nullptr
    // if no entry exists or compilation failed. Cached per (stage,hash).
    // Phase 2 hook: not yet called from RenderStateHook.
    ID3D11PixelShader* GetReplacementPS(uint32_t hash, ID3D11Device* device);
    ID3D11ComputeShader* GetReplacementCS(uint32_t hash, ID3D11Device* device);

    // True if Load() found a table and at least one entry.
    bool Active() const;

    // Counts for the camera log line.
    struct Stats { uint64_t notes{}; uint64_t matches{}; uint64_t uniqueMatches{}; };
    Stats Stats() const;

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
    std::unordered_set<uint32_t> matchedHashes;

    void Log(const char* fmt, ...) const;
    static Stage ParseStage(const std::string& profile);
    static std::string HashHex(uint32_t h);
};
