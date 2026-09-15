#include "ShaderSwap.h"
#include "ShaderHash.h"
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <sstream>

void ShaderSwap::Log(const char* fmt, ...) const {
    char line[512];
    va_list a; va_start(a, fmt);
    vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, a);
    va_end(a);
    FILE* f{};
    if (!fopen_s(&f, "DeusExHRVR-shaderswap.log", "a")) {
        fprintf(f, "[%llu] %s\n", GetTickCount64(), line);
        fclose(f);
    }
}

ShaderSwap::Stage ShaderSwap::ParseStage(const std::string& profile) {
    if (profile.rfind("ps_", 0) == 0) return Stage::Pixel;
    if (profile.rfind("cs_", 0) == 0) return Stage::Compute;
    if (profile.rfind("vs_", 0) == 0) return Stage::Vertex;
    return Stage::Unknown;
}

std::string ShaderSwap::HashHex(uint32_t h) {
    char buf[12];
    sprintf_s(buf, "0x%08X", h);
    return buf;
}

void ShaderSwap::Load(const std::filesystem::path& modRoot) {
    std::lock_guard lock(m);
    pixel.clear(); compute.clear(); vertex.clear();
    psCache.clear(); csCache.clear();
    matchedHashes.clear();
    noteCount = matchCount = 0;
    loaded = false;

    // The installer drops the shader table beside the companion, under
    // <game>/DeusExHRVR/shaders/dxhr/, mirroring where DeusExHRVRHost.exe
    // already lives. This keeps Luma's bundled assets out of the game root.
    shaderDir = modRoot / L"DeusExHRVR" / L"shaders" / L"dxhr";
    auto table = shaderDir / L"table.csv";
    if (!std::filesystem::exists(table)) {
        Log("No shader table at %S (swap inactive)", table.wstring().c_str());
        return;
    }
    std::ifstream in(table);
    if (!in) { Log("Failed to open table %S", table.wstring().c_str()); return; }

    std::string line;
    unsigned lineNo = 0, added = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        // # comment / blank
        size_t h = line.find_first_not_of(" \t\r\n");
        if (h == std::string::npos || line[h] == '#') continue;
        // columns: hash,name,profile[,cso]
        std::stringstream ss(line);
        std::string hashStr, name, profile, csoRel;
        if (!std::getline(ss, hashStr, ',') || !std::getline(ss, name, ',') ||
            !std::getline(ss, profile, ',')) {
            Log("table.csv:%u: skipped malformed row", lineNo);
            continue;
        }
        std::getline(ss, csoRel, ',');
        // hash is hex, optional 0x prefix
        uint32_t hash = 0;
        try { hash = static_cast<uint32_t>(std::stoul(hashStr, nullptr, 16)); }
        catch (...) { Log("table.csv:%u: bad hash '%s'", lineNo, hashStr.c_str()); continue; }
        if (!hash) continue;
        Entry e; e.hash = hash; e.name = name; e.profile = profile;
        e.stage = ParseStage(profile);
        if (!csoRel.empty()) {
            // cso paths are ASCII filenames (compiled/<name>.cso), relative to
            // shaderDir. u8path is deprecated in C++20; plain path handles it.
            e.cso = shaderDir / std::filesystem::path(csoRel);
        }
        switch (e.stage) {
            case Stage::Pixel:   pixel[hash] = e; break;
            case Stage::Compute: compute[hash] = e; break;
            case Stage::Vertex:  vertex[hash] = e; break;
            default: Log("table.csv:%u: unknown profile '%s' for %s", lineNo, profile.c_str(), name.c_str()); continue;
        }
        ++added;
    }
    loaded = added > 0;
    Log("Loaded %u swap entries (ps=%zu cs=%zu vs=%zu) from %S",
        added, pixel.size(), compute.size(), vertex.size(),
        table.wstring().c_str());
}

uint32_t ShaderSwap::NoteBound(uint32_t frame, Stage stage, uintptr_t engineShader) {
    if (!loaded || !engineShader) return 0;
    // Hot path: render-thread only. hashCache mirrors EffectShader's pattern.
    // An engine shader object is stable for its lifetime, so the DXBC read +
    // CRC32 runs at most once per unique object.
    uint32_t hash = 0;
    if (auto it = hashCache.find(engineShader); it != hashCache.end()) {
        hash = it->second;
    } else {
        hash = ShaderHash::ForEngineShader(engineShader);
        hashCache[engineShader] = hash; // 0 means "tried, unreadable"
    }
    if (!hash) return 0;
    ++noteCount;
    const Entry* e = nullptr;
    switch (stage) {
        case Stage::Pixel:   { auto it = pixel.find(hash);   if (it != pixel.end())   e = &it->second; break; }
        case Stage::Compute: { auto it = compute.find(hash); if (it != compute.end()) e = &it->second; break; }
        case Stage::Vertex:  { auto it = vertex.find(hash);  if (it != vertex.end())  e = &it->second; break; }
        default: break;
    }
    if (!e) return hash; // hash resolved but not in the swap table
    ++matchCount;
    bool first = matchedHashes.insert(hash).second;
    // First sighting of a given hash is logged verbosely (name + hash +
    // frame); repeats are folded so the log stays readable during gameplay.
    if (first) {
        Log("MATCH %s hash=%s frame=%u stage=%d cso=%S",
            e->name.c_str(), HashHex(hash).c_str(), frame,
            int(stage), e->cso.wstring().c_str());
    }
    return hash;
}

bool ShaderSwap::IsKnown(Stage stage, uint32_t hash) const {
    // Table maps are immutable after Load(); render-thread reads need no lock.
    switch (stage) {
        case Stage::Pixel:   return pixel.count(hash) > 0;
        case Stage::Compute: return compute.count(hash) > 0;
        case Stage::Vertex:  return vertex.count(hash) > 0;
        default: return false;
    }
}

namespace {
// Read a compiled shader blob (.cso) from disk into memory.
bool LoadBlob(const std::filesystem::path& path, std::vector<uint8_t>& out) {
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
} // namespace

ID3D11PixelShader* ShaderSwap::GetReplacementPS(uint32_t hash, ID3D11Device* device) {
    if (!device || !hash) return nullptr;
    {
        std::lock_guard lock(m);
        auto it = psCache.find(hash);
        if (it != psCache.end()) return it->second.Get();
    }
    Entry e;
    {
        std::lock_guard lock(m);
        auto it = pixel.find(hash);
        if (it == pixel.end()) return nullptr;
        e = it->second;
    }
    if (e.cso.empty()) { Log("GetReplacementPS %s: no cso path", HashHex(hash).c_str()); return nullptr; }
    std::vector<uint8_t> blob;
    if (!LoadBlob(e.cso, blob)) { Log("GetReplacementPS %s: failed to read %S", HashHex(hash).c_str(), e.cso.wstring().c_str()); return nullptr; }
    ID3D11PixelShader* ps{};
    HRESULT hr = device->CreatePixelShader(blob.data(), blob.size(), nullptr, &ps);
    if (FAILED(hr)) { Log("GetReplacementPS %s: CreatePixelShader hr=%08lx", HashHex(hash).c_str(), hr); return nullptr; }
    std::lock_guard lock(m);
    psCache[hash] = Microsoft::WRL::ComPtr<ID3D11PixelShader>(ps);
    Log("Compiled replacement PS %s (%s) -> %p", HashHex(hash).c_str(), e.name.c_str(), ps);
    return ps;
}

ID3D11ComputeShader* ShaderSwap::GetReplacementCS(uint32_t hash, ID3D11Device* device) {
    if (!device || !hash) return nullptr;
    {
        std::lock_guard lock(m);
        auto it = csCache.find(hash);
        if (it != csCache.end()) return it->second.Get();
    }
    Entry e;
    {
        std::lock_guard lock(m);
        auto it = compute.find(hash);
        if (it == compute.end()) return nullptr;
        e = it->second;
    }
    if (e.cso.empty()) { Log("GetReplacementCS %s: no cso path", HashHex(hash).c_str()); return nullptr; }
    std::vector<uint8_t> blob;
    if (!LoadBlob(e.cso, blob)) { Log("GetReplacementCS %s: failed to read %S", HashHex(hash).c_str(), e.cso.wstring().c_str()); return nullptr; }
    ID3D11ComputeShader* cs{};
    HRESULT hr = device->CreateComputeShader(blob.data(), blob.size(), nullptr, &cs);
    if (FAILED(hr)) { Log("GetReplacementCS %s: CreateComputeShader hr=%08lx", HashHex(hash).c_str(), hr); return nullptr; }
    std::lock_guard lock(m);
    csCache[hash] = Microsoft::WRL::ComPtr<ID3D11ComputeShader>(cs);
    Log("Compiled replacement CS %s (%s) -> %p", HashHex(hash).c_str(), e.name.c_str(), cs);
    return cs;
}

bool ShaderSwap::Active() const { return loaded; }

ShaderSwap::Stats ShaderSwap::GetStats() const {
    // Render-thread only (OnPresent capture path); no lock needed.
    return {noteCount, matchCount, matchedHashes.size()};
}
