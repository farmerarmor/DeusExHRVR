#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list> // MSVC: range-based-for over braced-init-lists
#include <vector>

// ReShade-compatible shader bytecode hashing.
//
// Luma's DXHR shader filenames encode a 32-bit hash in hex (e.g.
// "MLAAMask1Gen_0x6B0219A1.ps_5_0.hlsl"). That hash is the value ReShade's
// addon API reports as `shader_hash` for a bound pipeline, which ReShade
// computes with its standard `compute_crc32` over the shader bytecode.
//
// Luma resolves the filename back to a number with std::stoul(hex, 16)
// (Shader::Hash_StrToNum), so "6B0219A1" -> 0x6B0219A1.
//
// This header re-implements that exact CRC32 so DXHRVR can identify the
// currently-bound engine shader against Luma's replacement table using the
// same key space, without ReShade present.
//
// DXHRVR's existing EffectShader already reads the DXBC blob out of engine
// memory (via ReadProcessMemory on the engine's shader object) and FNV-1a
// hashes it. ShaderHash offers the same blob-reading primitive plus the
// ReShade CRC32, so a bound shader can be identified in both key spaces from
// one read.

namespace ShaderHash {

// ReShade compute_crc32: standard CRC32 (reflected, poly 0xEDB88320,
// init 0xFFFFFFFF, final XOR 0xFFFFFFFF). Identical to zlib crc32().
inline uint32_t Crc32(const uint8_t* data, size_t size) {
    uint32_t hash = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        for (unsigned j = 0; j < 8; ++j)
            hash = (hash >> 1) ^ (0xEDB88320u & (0u - (hash & 1u)));
    }
    return ~hash;
}

// DXBC container magic ("DXBC" little-endian) and the size field offset.
// DXBC header layout: u32 magic, u8 checksum[16], u32 version, ...,
//                      u32 total_size at offset 24.
inline constexpr uint32_t kDxbcMagic = 0x43425844u; // 'D','X','B','C'
inline constexpr unsigned kDxbcSizeOffset = 24;

// Read `n` bytes from `address` in the current process. Returns false on any
// bounds failure. Mirrors EffectShader/EngineShaderTrace's ReadProcessMemory
// pattern so shader blobs owned by the engine are read safely.
inline bool Read(uintptr_t address, void* out, size_t n) {
    SIZE_T got{};
    return address &&
           ReadProcessMemory(GetCurrentProcess(),
                             reinterpret_cast<void*>(address), out, n, &got) &&
           got == n;
}

// Locate the DXBC bytecode blob that an engine shader object points to.
//
// The engine's shader object stores (among other things) pointers into its
// own shader-cache heap; the DXBC blob lives at one of those pointers, at an
// offset of 0 or 16 from the pointer base (the same two-offset scan that
// EffectShader and EngineShaderTrace use). Returns the blob address and its
// byte length, or 0 if no valid DXBC was found.
inline uintptr_t FindDxbc(uintptr_t shaderObject, size_t* outSize) {
    if (outSize) *outSize = 0;
    if (!shaderObject) return 0;
    std::array<uint32_t, 20> fields{};
    if (!Read(shaderObject, fields.data(), sizeof(fields))) return 0;
    for (auto pointer : fields) {
        for (unsigned offset : {0u, 16u}) {
            uintptr_t blob = uintptr_t(pointer) + offset;
            std::array<uint32_t, 8> header{};
            if (!Read(blob, header.data(), sizeof(header))) continue;
            if (header[0] != kDxbcMagic) continue;
            // header[6] is the DXBC total_size field (bytes 24..27).
            if (header[6] < 32 || header[6] > 2u * 1024u * 1024u) continue;
            if (outSize) *outSize = header[6];
            return blob;
        }
    }
    return 0;
}

// Read the full DXBC bytecode for an engine shader object into `out`.
// Returns true on success. Used when the caller needs the bytes (to hash, to
// patch, or to hand to D3DCompile/D3DCreatePixelShader).
inline bool ReadDxbc(uintptr_t shaderObject, std::vector<uint8_t>& out) {
    size_t size{};
    uintptr_t blob = FindDxbc(shaderObject, &size);
    if (!blob || !size) return false;
    out.resize(size);
    return Read(blob, out.data(), size);
}

// ReShade shader_hash for an engine shader object, or 0 if no DXBC was found.
// This is the value comparable to Luma's filename hashes (e.g. 0x6B0219A1).
inline uint32_t ForEngineShader(uintptr_t shaderObject) {
    std::vector<uint8_t> bytes;
    if (!ReadDxbc(shaderObject, bytes)) return 0;
    return Crc32(bytes.data(), bytes.size());
}

} // namespace ShaderHash
