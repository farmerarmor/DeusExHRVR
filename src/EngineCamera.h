#pragma once
#include <cstdint>
#include "SharedPair.h"
struct ID3D11Device;
namespace EngineCamera {
Transport::RenderInfo OnPresent(uint64_t frame, bool capture);
void SetChannel(Transport::Header* header);
bool SnapTurnView(float& yaw);
// Local patch: the game's quickbar auto-hide setting (tilde). Returns the value
// found, setting it to 1 first if asked, or -1 if the game's layout doesn't match.
int QuickBarAutoHide(bool set);
bool ImmersiveScopeButton();
// Local patch: bit mask of active virtual-screen reasons (1 interaction, 2 menu, 4 video, 8 game over, 16 scope,
// 32 in-game menu, 64 e-reader/news reader: buttons only, the display stays tracked;
// snap turn still works over a reader).
unsigned CurrentScreenReasons();
// Luma port: forward the engine's D3D11 device to ShaderSwap so it can build
// replacement shaders and issue PSSetShader overrides. Called from
// NativeTransport::Producer::Init once the device is first available.
void SetShaderSwapDevice(ID3D11Device* device);
// Toggle live shader substitution (F12). Returns the new state.
bool ToggleShaderSubstitution();
}
