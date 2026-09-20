#pragma once
#include <cstdint>
#include "SharedPair.h"
namespace EngineCamera {
Transport::RenderInfo OnPresent(uint64_t frame, bool capture);
void SetChannel(Transport::Header* header);
bool SnapTurnView(float& yaw);
// Local patch: bit mask of active virtual-screen reasons (1 interaction, 2 menu, 4 video, 8 game over, 16 scope,
// 32 in-game menu, 64 e-reader/news reader: buttons only, the display stays tracked;
// snap turn still works over a reader. 128 a wheel screen pausing gameplay: keep
// [Buttons], and the display is left as it is).
unsigned CurrentScreenReasons();
}
