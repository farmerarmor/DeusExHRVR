#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstring>

// LumaSettings constant buffer — faithful port of Luma's cb_luma_global_settings.
//
// The cbuffer is declared in shaders/Includes/Settings.hlsl as:
//   cbuffer LumaSettings : register(b13) { ... LumaGameSettings GameSettings; }
// and the DXHR-specific LumaGameSettings is in shaders/dxhr/Includes/GameCBuffers.hlsl.
//
// This header mirrors that exact layout in C++ so we can populate it and bind
// it at b13 before Luma replacement shaders (Phase 2) and injected passes
// (Phase 3) run. Without this, ModulateLighting multiplies by white (no-op)
// and the replacement shaders (bloom, color grading, etc.) read garbage for
// their intensity multipliers.
//
// The buffer is D3D11_USAGE_DYNAMIC + CPU_ACCESS_WRITE, updated via
// Map(DISCARD) + memcpy + Unmap (same as Luma's SetLumaConstantBuffers).
// Bound to PS at slot 13 (LUMA_SETTINGS_CB_INDEX) before each pass.

namespace LumaSettingsCB {

// Match Luma's DisplayModeType (Settings.hlsl): 0=SDR(80nits), 1=HDR, 2=SDR-on-HDR(203nits)
enum class DisplayMode : uint32_t { SDR = 0, HDR = 1, SDRonHDR = 2 };

#pragma pack(push, 4)
// DXHR-specific game settings (GameCBuffers.hlsl struct LumaGameSettings).
// Field order and types match the HLSL exactly; padding is explicit.
struct GameSettings {
    float InvOutputResX;             // float2 InvOutputRes
    float InvOutputResY;
    float BloomIntensity;            // Neutral/Vanilla at 1
    float FogIntensity;              // Neutral/Vanilla at 1
    float ColorGradingIntensity;     // Neutral/Vanilla at 1
    float DesaturationIntensity;     // Neutral/Vanilla at 1
    float AmbientLightingIntensity;  // Neutral/Vanilla at 1
    float EmissiveIntensity;         // Neutral/Vanilla at 0
    float HDRBoostIntensity;         // Neutral/Vanilla at 0
    uint32_t HasColorGradingPass;    // Whether the "gold filter" is enabled
    float Padding1X;                 // float2 Padding1 (align to 16 bytes)
    float Padding1Y;
    float AmbientLightColor[4];      // float4, Neutral/Vanilla at 1 1 1 1
    float LightingColor[4];          // float4, Neutral/Vanilla at 1 1 1 1
};
#pragma pack(pop)

// The full LumaSettings cbuffer (Settings.hlsl), global part + GameSettings.
// Must be 16-byte aligned and a multiple of 16 bytes (DX cbuffer requirement).
// alignas(16) on the struct ensures the struct itself starts 16-byte aligned;
// members pack naturally (all float/uint = 4 bytes each, no gaps).
struct alignas(16) LumaSettings {
    float SwapchainSizeX;        // float2 SwapchainSize
    float SwapchainSizeY;
    float SwapchainInvSizeX;     // float2 SwapchainInvSize
    float SwapchainInvSizeY;
    float RenderSizeX;           // float2 RenderSize
    float RenderSizeY;
    float RenderInvSizeX;        // float2 RenderInvSize
    float RenderInvSizeY;
    uint32_t DisplayMode;        // DisplayModeType
    float PeakWhiteNits;
    float GamePaperWhiteNits;
    float UIPaperWhiteNits;
    uint32_t SRType;
    uint32_t FrameIndex;
    float Padding1X;             // float2 Padding1 (non-dev mode)
    float Padding1Y;
    GameSettings GameSettings;   // game-specific tail (80 bytes)
};
static_assert(sizeof(LumaSettings) % 16 == 0, "LumaSettings must be 16-byte aligned for DX cbuffers");
static_assert(sizeof(LumaSettings) >= 16, "DX cbuffer minimum size is 16 bytes");

// The register slot Luma's shaders expect LumaSettings at (Settings.hlsl:
// #define LUMA_SETTINGS_CB_INDEX b13). DXHR's engine may or may not use this
// slot; Luma's DEVELOPMENT safety check would flag a conflict.
constexpr uint32_t kSlot = 13;

// Manager: creates the dynamic cbuffer, updates it, binds it.
class Manager {
public:
    Manager() { SetDefaults(); }
    // Create the cbuffer. Call once after the device is available.
    void Init(ID3D11Device* dev) {
        if (buffer || !dev) return;
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(LumaSettings);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        dev->CreateBuffer(&desc, nullptr, &buffer);
    }

    // Set default values from Luma's DXHR main.cpp (lines 1589-1599).
    // These are Luma's "not vanilla like" defaults — they intentionally
    // differ from the game's original look.
    void SetDefaults() {
        memset(&settings, 0, sizeof(settings));
        settings.DisplayMode = static_cast<uint32_t>(DisplayMode::SDR);
        settings.PeakWhiteNits = 80.0f;   // SDR
        settings.GamePaperWhiteNits = 80.0f;
        settings.UIPaperWhiteNits = 80.0f;
        // GameSettings defaults (main.cpp 1589-1599):
        settings.GameSettings.BloomIntensity = 0.8f;
        settings.GameSettings.FogIntensity = 0.0f;
        settings.GameSettings.ColorGradingIntensity = 1.0f;
        settings.GameSettings.DesaturationIntensity = 0.333f;
        settings.GameSettings.AmbientLightingIntensity = 0.8f;
        settings.GameSettings.EmissiveIntensity = 0.667f;
        settings.GameSettings.HDRBoostIntensity = 1.0f;
        settings.GameSettings.HasColorGradingPass = 0; // set when gold filter detected
        settings.GameSettings.AmbientLightColor[0] = 1.0f;
        settings.GameSettings.AmbientLightColor[1] = 1.0f;
        settings.GameSettings.AmbientLightColor[2] = 1.0f;
        settings.GameSettings.AmbientLightColor[3] = 1.0f;
        settings.GameSettings.LightingColor[0] = 1.0f;
        settings.GameSettings.LightingColor[1] = 1.0f;
        settings.GameSettings.LightingColor[2] = 1.0f;
        settings.GameSettings.LightingColor[3] = 1.0f;
        dirty = true;
    }

    // Update InvOutputRes from the current output resolution (Luma main.cpp
    // line 528-529). Call when resolution changes or once per frame.
    void SetOutputResolution(float w, float h) {
        if (w > 0 && h > 0) {
            settings.GameSettings.InvOutputResX = 1.0f / w;
            settings.GameSettings.InvOutputResY = 1.0f / h;
            settings.RenderSizeX = w;
            settings.RenderSizeY = h;
            settings.RenderInvSizeX = 1.0f / w;
            settings.RenderInvSizeY = 1.0f / h;
            settings.SwapchainSizeX = w;
            settings.SwapchainSizeY = h;
            settings.SwapchainInvSizeX = 1.0f / w;
            settings.SwapchainInvSizeY = 1.0f / h;
            dirty = true;
        }
    }

    void SetFrameIndex(uint32_t frame) {
        if (settings.FrameIndex != frame) {
            settings.FrameIndex = frame;
            dirty = true;
        }
    }

    // Mark dirty so the next Bind() re-uploads.
    void MarkDirty() { dirty = true; }

    // Upload (if dirty) + bind to PS at slot kSlot. Call before Luma pixel
    // shaders run. Map(DISCARD) + memcpy + Unmap, matching Luma's pattern.
    void Bind(ID3D11DeviceContext* ctx) {
        if (!buffer || !ctx) return;
        UploadIfDirty(ctx);
        ID3D11Buffer* buf = buffer.Get();
        ctx->PSSetConstantBuffers(kSlot, 1, &buf);
    }

    // Same but for compute stage (XeGTAO etc.).
    void BindCS(ID3D11DeviceContext* ctx) {
        if (!buffer || !ctx) return;
        UploadIfDirty(ctx);
        ID3D11Buffer* buf = buffer.Get();
        ctx->CSSetConstantBuffers(kSlot, 1, &buf);
    }

    LumaSettings& Get() { return settings; }
    const LumaSettings& Get() const { return settings; }
    bool Initialized() const { return buffer != nullptr; }

private:
    void UploadIfDirty(ID3D11DeviceContext* ctx) {
        if (!dirty) return;
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(ctx->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, &settings, sizeof(settings));
            ctx->Unmap(buffer.Get(), 0);
            dirty = false;
        }
    }
    LumaSettings settings{};
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    bool dirty{true};
};

} // namespace LumaSettingsCB
