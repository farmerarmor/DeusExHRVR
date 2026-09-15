#include "Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/Reinhard.hlsl"

cbuffer SceneBuffer : register(b2)
{
  row_major float4x4 View : packoffset(c0);
  row_major float4x4 ScreenMatrix : packoffset(c4);
  float2 DepthExportScale : packoffset(c8);
  float2 FogScaleOffset : packoffset(c9);
  float3 CameraPosition : packoffset(c10);
  float3 CameraDirection : packoffset(c11);
  float3 DepthFactors : packoffset(c12);
  float2 ShadowDepthBias : packoffset(c13);
  float4 SubframeViewport : packoffset(c14);
  row_major float3x4 DepthToWorld : packoffset(c15);
  float4 DepthToView : packoffset(c18);
  float4 OneOverDepthToView : packoffset(c19);
  float4 DepthToW : packoffset(c20);
  float4 ClipPlane : packoffset(c21);
  float2 ViewportDepthScaleOffset : packoffset(c22);
  float2 ColorDOFDepthScaleOffset : packoffset(c23);
  float2 TimeVector : packoffset(c24);
  float3 HeightFogParams : packoffset(c25);
  float3 GlobalAmbient : packoffset(c26);
  float4 GlobalParams[16] : packoffset(c27);
  float DX3_SSAOScale : packoffset(c43);
  float4 ScreenExtents : packoffset(c44);
  float2 ScreenResolution : packoffset(c45);
  float4 PSSMToMap1Lin : packoffset(c46);
  float4 PSSMToMap1Const : packoffset(c47);
  float4 PSSMToMap2Lin : packoffset(c48);
  float4 PSSMToMap2Const : packoffset(c49);
  float4 PSSMToMap3Lin : packoffset(c50);
  float4 PSSMToMap3Const : packoffset(c51);
  float4 PSSMDistances : packoffset(c52);
  row_major float4x4 WorldToPSSM0 : packoffset(c53);
  float StereoOffset : packoffset(c25.w);
}

SamplerState ScreenSampler_s : register(s0);
Texture2D<float4> ScreenTexture : register(t0);

#define cmp

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9;
  r0.xyz = ScreenTexture.SampleLevel(ScreenSampler_s, v2.xy, 0, int2(0, -1)).xyz;
  r1.xyz = ScreenTexture.SampleLevel(ScreenSampler_s, v2.xy, 0, int2(-1, 0)).xyz;
  r2.xyz = ScreenTexture.SampleLevel(ScreenSampler_s, v2.xy, 0, int2(0, 0)).xyz;
  r3.xyz = ScreenTexture.SampleLevel(ScreenSampler_s, v2.xy, 0, int2(1, 0)).xyz;
  r4.xyz = ScreenTexture.SampleLevel(ScreenSampler_s, v2.xy, 0, int2(0, 1)).xyz;
  // TODO: fix RG improper luminance calculations
  r0.w = r0.y * 1.9632107 + r0.x;
  r1.w = r1.y * 1.9632107 + r1.x;
  r2.w = r2.y * 1.9632107 + r2.x;
  r3.w = r3.y * 1.9632107 + r3.x;
  r4.w = r4.y * 1.9632107 + r4.x;
  r5.x = min(r1.w, r0.w);
  r5.y = min(r4.w, r3.w);
  r5.x = min(r5.y, r5.x);
  r5.x = min(r5.x, r2.w);
  r5.y = max(r1.w, r0.w);
  r5.z = max(r4.w, r3.w);
  r5.y = max(r5.z, r5.y);
  r5.y = max(r5.y, r2.w);
  r5.x = r5.y + -r5.x;
  r5.y = 0.125 * r5.y;
  r5.y = max(0.0416666679, r5.y);
  r5.y = cmp(r5.x >= r5.y);
  if (r5.y != 0) {
    r0.xyz = r1.xyz + r0.xyz;
    r0.xyz = r0.xyz + r2.xyz;
    r0.xyz = r0.xyz + r3.xyz;
    r0.xyz = r0.xyz + r4.xyz;
    r1.x = r1.w + r0.w;
    r1.x = r1.x + r3.w;
    r1.x = r1.x + r4.w;
    r1.x = r1.x * 0.25 + -r2.w;
    r1.x = abs(r1.x) / r5.x;
    r1.x = -0.25 + r1.x;
    r1.x = max(0, r1.x);
    r1.x = 1.33333337 * r1.x;
    r1.x = min(0.75, r1.x);
    r3.xyz = ScreenTexture.SampleLevel(ScreenSampler_s, v2.xy, 0, int2(-1, -1)).xyz;
    r4.xyz = ScreenTexture.SampleLevel(ScreenSampler_s, v2.xy, 0, int2(1, -1)).xyz;
    r5.xyz = ScreenTexture.SampleLevel(ScreenSampler_s, v2.xy, 0, int2(-1, 1)).xyz;
    r6.xyz = ScreenTexture.SampleLevel(ScreenSampler_s, v2.xy, 0, int2(1, 1)).xyz;
    r7.xyz = r4.xyz + r3.xyz;
    r7.xyz = r7.xyz + r5.xyz;
    r7.xyz = r7.xyz + r6.xyz;
    r0.xyz = r7.xyz + r0.xyz;
    r1.y = r3.y * 1.9632107 + r3.x;
    r1.z = r4.y * 1.9632107 + r4.x;
    r3.x = r5.y * 1.9632107 + r5.x;
    r3.y = r6.y * 1.9632107 + r6.x;
    r3.z = -0.5 * r0.w;
    r3.z = r1.y * 0.25 + r3.z;
    r3.z = r1.z * 0.25 + r3.z;
    r4.x = -0.5 * r1.w;
    r4.y = r1.w * 0.5 + -r2.w;
    r4.z = -0.5 * r3.w;
    r4.y = r3.w * 0.5 + r4.y;
    r3.z = abs(r4.y) + abs(r3.z);
    r4.y = -0.5 * r4.w;
    r4.y = r3.x * 0.25 + r4.y;
    r4.y = r3.y * 0.25 + r4.y;
    r3.z = abs(r4.y) + r3.z;
    r1.y = r1.y * 0.25 + r4.x;
    r1.y = r3.x * 0.25 + r1.y;
    r3.x = r0.w * 0.5 + -r2.w;
    r3.x = r4.w * 0.5 + r3.x;
    r1.y = abs(r3.x) + abs(r1.y);
    r1.z = r1.z * 0.25 + r4.z;
    r1.z = r3.y * 0.25 + r1.z;
    r1.y = abs(r1.z) + r1.y;
    r1.y = cmp(r1.y >= r3.z);
    r1.z = r1.y ? -ScreenExtents.w : -ScreenExtents.z;
    r0.w = r1.y ? r0.w : r1.w;
    r1.w = r1.y ? r4.w : r3.w;
    r3.x = r0.w + -r2.w;
    r3.y = r1.w + -r2.w;
    r0.w = r0.w + r2.w;
    r0.w = 0.5 * r0.w;
    r1.w = r1.w + r2.w;
    r1.w = 0.5 * r1.w;
    r3.z = cmp(abs(r3.x) >= abs(r3.y));
    r0.w = r3.z ? r0.w : r1.w;
    r1.w = max(abs(r3.y), abs(r3.x));
    r1.z = r3.z ? r1.z : -r1.z;
    r3.x = 0.5 * r1.z;
    r3.y = r1.y ? 0 : r3.x;
    r4.x = v2.x + r3.y;
    r3.x = asfloat(asint(r1.y) & asint(r3.x));
    r4.y = v2.y + r3.x;
    r1.w = 0.25 * r1.w;
    r3.yz = float2(0,0);
    r3.xw = ScreenExtents.zw;
    r3.xy = r1.yy ? r3.xy : r3.zw;
    r3.zw = -r3.xy + r4.xy;
    r4.xy = r3.xy + r4.xy;
    r5.xyz = float3(0,0,0);
    r4.zw = r3.zw;
    r6.xy = r4.xy;
    r5.w = r0.w;
    r6.z = r0.w;
    r6.w = 0;
    int4 r7i;
    r7i.x = 0;
    while (true) {
      if (r7i.x >= 16) break;
      r7.yz = ScreenTexture.SampleLevel(ScreenSampler_s, r4.zw, 0).xy;
      r7.y = r7.z * 1.9632107 + r7.y;
      r7.zw = r4.zw + -r3.xy;
      r8.xy = ScreenTexture.SampleLevel(ScreenSampler_s, r7.zw, 0).xy;
      r8.x = r8.y * 1.9632107 + r8.x;
      r8.yz = ScreenTexture.SampleLevel(ScreenSampler_s, r6.xy, 0).xy;
      r8.y = r8.z * 1.9632107 + r8.y;
      r8.zw = r6.xy + r3.xy;
      r9.xy = ScreenTexture.SampleLevel(ScreenSampler_s, r8.zw, 0).xy;
      r9.x = r9.y * 1.9632107 + r9.x;
      r9.y = r7.y + -r0.w;
      r9.y = cmp(abs(r9.y) >= r1.w);
      r9.y = asfloat(asint(r5.x) | asint(r9.y));
      r9.z = r8.y + -r0.w;
      r9.z = cmp(abs(r9.z) >= r1.w);
      r9.z = asfloat(asint(r5.y) | asint(r9.z));
      r9.w = r8.x + -r0.w;
      r9.w = cmp(abs(r9.w) >= r1.w);
      r5.z = asfloat(asint(r5.z) | asint(r9.w));
      r9.w = r9.x + -r0.w;
      r9.w = cmp(abs(r9.w) >= r1.w);
      r6.w = asfloat(asint(r6.w) | asint(r9.w));
      r9.w = asfloat(~asint(r9.y));
      r9.w = asfloat(asint(r9.w) & asint(r5.z));
      r5.w = r9.w ? r8.x : r7.y;
      r7.y = asfloat(~asint(r9.z));
      r7.y = asfloat(asint(r6.w) & asint(r7.y));
      r6.z = r7.y ? r9.x : r8.y;
      r7.zw = r9.ww ? r7.zw : r4.zw;
      r8.xy = r7.yy ? r8.zw : r6.xy;
      r5.x = asfloat(asint(r9.y) | asint(r5.z));
      r5.y = asfloat(asint(r9.z) | asint(r6.w));
      r7.y = asfloat(asint(r5.x) & asint(r5.y));
      if (r7.y != 0) {
        r4.zw = r7.zw;
        r6.xy = r8.xy;
        break;
      }
      r8.zw = -r3.xy * float2(2,2) + r7.zw;
      r4.zw = r5.xx ? r7.zw : r8.zw;
      r7.yz = r3.xy * float2(2,2) + r8.xy;
      r6.xy = r5.yy ? r8.xy : r7.yz;
      r7i.x += 2;
    }
    r3.xy = v2.xy + -r4.zw;
    r1.w = r1.y ? r3.x : r3.y;
    r3.xy = -v2.xy + r6.xy;
    r3.x = r1.y ? r3.x : r3.y;
    r3.y = cmp(r1.w < r3.x);
    r3.y = r3.y ? r5.w : r6.z;
    r2.w = -r0.w + r2.w;
    r2.w = cmp(r2.w < 0);
    r0.w = r3.y + -r0.w;
    r0.w = cmp(r0.w < 0);
    r0.w = cmp(asint(r2.w) == asint(r0.w)); // It was compared by int in the compile but it shouldn't really matter
    r0.w = r0.w ? 0 : r1.z;
    r1.z = r3.x + r1.w;
    r1.w = min(r3.x, r1.w);
    r1.z = -1 / r1.z;
    r1.z = r1.w * r1.z + 0.5;
    r0.w = r1.z * r0.w;
    r1.z = r1.y ? 0 : 1;
    r3.x = r1.z * r0.w + v2.x;
    r1.y = asfloat(asint(r1.y) & 0x3f800000); // x ? 1.0 : 0.0
    r3.y = r1.y * r0.w + v2.y;
    r1.yzw = ScreenTexture.SampleLevel(ScreenSampler_s, r3.xy, 0).xyz;
    r0.xyz = r0.xyz * float3(0.111111112,0.111111112,0.111111112) + -r1.yzw;
    r2.xyz = r1.xxx * r0.xyz + r1.yzw;
  }
  o0.xyz = r2.xyz;
  o0.w = 1;
  
  float2 uv = v0.xy * ScreenExtents.zw + ScreenExtents.xy;
  bool forceSDR = ShouldForceSDR(uv);
  if (!LumaSettings.GameSettings.HasColorGradingPass && !forceSDR) // Luma
  {
    o0.rgb = gamma_to_linear(o0.rgb, GCT_MIRROR);
    
    const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
    bool tonemapPerChannel = LumaSettings.DisplayMode != 1;
#if ENABLE_HIGHLIGHTS_DESATURATION_TYPE == 1 || ENABLE_HIGHLIGHTS_DESATURATION_TYPE >= 3
    tonemapPerChannel = true;
#endif
    if (LumaSettings.DisplayMode == 1)
    {
      DICESettings settings = DefaultDICESettings(tonemapPerChannel ? DICE_TYPE_BY_CHANNEL_PQ : DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
      o0.rgb = DICETonemap(o0.rgb * paperWhite, peakWhite, settings) / paperWhite;
    }
    else
    {
      if (tonemapPerChannel)
      {
        o0.rgb = Reinhard::ReinhardRange(o0.rgb, MidGray, -1.0, peakWhite / paperWhite, false);
      }
      else
      {
        o0.rgb = RestoreLuminance(o0.rgb, Reinhard::ReinhardRange(GetLuminance(o0.rgb), MidGray, -1.0, peakWhite / paperWhite, false).x, true);
        o0.rgb = CorrectOutOfRangeColor(o0.rgb, true, true, 0.5, peakWhite / paperWhite);
      }
    }
  
#if UI_DRAW_TYPE == 2
    ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true);
    o0.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
    ColorGradingLUTTransferFunctionInOutCorrected(o0.rgb, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, true);
#endif // UI_DRAW_TYPE == 2

    o0.rgb = linear_to_gamma(o0.rgb, GCT_MIRROR);
  }
}