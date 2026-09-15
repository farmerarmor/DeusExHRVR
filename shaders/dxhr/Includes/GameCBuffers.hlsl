#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#endif

namespace CB
{
	struct LumaGameSettings
	{
		float2 InvOutputRes;
		float BloomIntensity; // Neutral/Vanilla at 1
		float FogIntensity; // Neutral/Vanilla at 1
		float ColorGradingIntensity; // Neutral/Vanilla at 1
		float DesaturationIntensity; // Neutral/Vanilla at 1
		float AmbientLightingIntensity; // Neutral/Vanilla at 1
		float EmissiveIntensity; // Neutral/Vanilla at 0
		float HDRBoostIntensity; // Neutral/Vanilla at 0
		uint HasColorGradingPass; // Whether the "gold filter" is enabled
		float2 Padding1; // Align to 16 bytes (somehow needed...)
		// Make these float4 for padding simplicity
		float4 AmbientLightColor; // Neutral/Vanilla at 1 1 1
		float4 LightingColor; // Neutral/Vanilla at 1 1 1
	};
	
	struct LumaGameData
	{
		float Dummy;
	};
}

#endif // LUMA_GAME_CB_STRUCTS
