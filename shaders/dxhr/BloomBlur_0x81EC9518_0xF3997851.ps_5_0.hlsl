#include "Includes/Common.hlsl"

#ifndef ENABLE_LUMA
#define ENABLE_LUMA 1
#endif

cbuffer DrawableBuffer : register(b1)
{
  float4 FogColor : packoffset(c0);
  float4 DebugColor : packoffset(c1);
  float MaterialOpacity : packoffset(c2);
  float AlphaThreshold : packoffset(c3);
}

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

cbuffer MaterialBuffer : register(b3)
{
  float4 MaterialParams[32] : packoffset(c0);
}

SamplerState p_default_Material_0C38D4A418992488_Param_sampler_s : register(s0);
Texture2D<float4> p_default_Material_0C38D4A418992488_Param_texture : register(t0);

#ifndef ENABLE_IMPROVED_BLOOM
#define ENABLE_IMPROVED_BLOOM 1
#endif

#ifndef BLOOM_UPGRADE_TYPE
#if ENABLE_IMPROVED_BLOOM == 1
#define BLOOM_UPGRADE_TYPE 2
#else
#define BLOOM_UPGRADE_TYPE 0
#endif
#endif

// This shader has two hashes because one is for the original game and one for the original bloom in the DC
void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float2 centralUV = v0.xy * ScreenExtents.zw + ScreenExtents.xy;
  float scale = 1.0;
  uint iterations = 1.0;
#if BLOOM_UPGRADE_TYPE != 0 // Luma: scale bloom based on 720p/1080p resolution, given that it was very faint at high resolutions (this will make it closer to the Xbox 360 days, where bloom was very spread (as in, radius))
  float2 size;
  p_default_Material_0C38D4A418992488_Param_texture.GetDimensions(size.x, size.y);
  float2 originalSize = size;
  size *= 4.0; // At this point we are at 0.25x scale (rounded down to int)

  scale = size.y / DevelopmentVerticalResolution;
#if BLOOM_UPGRADE_TYPE == 1
  iterations = max(scale + 0.5, 1);
#if 0 // Reduce the radius as we don't fully generate mips properly, we have an approximation of them
  scale *= 0.75;
#endif
#elif BLOOM_UPGRADE_TYPE == 2
  iterations = 4; // Mips downscale
#endif
#endif
  const float3 centralBloom = p_default_Material_0C38D4A418992488_Param_texture.Sample(p_default_Material_0C38D4A418992488_Param_sampler_s, centralUV).xyz;

  float4 r0,r1;
  r0.z = 0;
  r1.xy = MaterialParams[0].xy * ScreenExtents.zw;
  r0.xy = MaterialParams[0].zz * r1.xy;
  r0.w = -r0.x;
  float2 uv1 = centralUV + r0.wz;
  float2 uv2 = centralUV + r0.xz;
  float2 uv3 = r0.zy * float2(1,-1) + centralUV;
  float2 uv4 = centralUV + r0.zy;
  float2 uv5 = -r1.xy * MaterialParams[0].zz + centralUV;
  float2 uv6 = r1.xy * MaterialParams[0].zz + centralUV;
  float2 uv7 = r0.xy * float2(1,-1) + centralUV;
  float2 uv8 = r0.xy * float2(-1,1) + centralUV;

  float3 bloomSum = 0.0;
  float localScale = scale;
  // Luma: added local mips generation for wider radius bloom (hacky, but mostly works)
#if BLOOM_UPGRADE_TYPE == 1
  [loop]
#else
  [unroll]
#endif
  for (uint i = 0; i < iterations; i++)
  {
    localScale = scale * ((i + 1) / iterations);
    float2 offset = 0.0;
#if BLOOM_UPGRADE_TYPE == 2 // Do live mips
    offset = 0.5 / originalSize;
    offset *= min(scale, 2.0); // Not 100% sure why this is needed, but it looks the same as the original shader otherwise. We cannot easily scale bloom beyond 2x in a single pass, so 8k will have to wait.
    if (i == 0) { offset = float2(offset.x, offset.y); }
    if (i == 1) { offset = float2(offset.x, -offset.y); }
    if (i == 2) { offset = float2(-offset.x, offset.y); }
    if (i == 3) { offset = float2(-offset.x, -offset.y); }
    localScale = 1.0;
#endif
    float3 bloom1 = p_default_Material_0C38D4A418992488_Param_texture.Sample(p_default_Material_0C38D4A418992488_Param_sampler_s, lerp(centralUV, uv1, localScale) + offset).xyz;
    float3 bloom2 = p_default_Material_0C38D4A418992488_Param_texture.Sample(p_default_Material_0C38D4A418992488_Param_sampler_s, lerp(centralUV, uv2, localScale) + offset).xyz;
    float3 bloom3 = p_default_Material_0C38D4A418992488_Param_texture.Sample(p_default_Material_0C38D4A418992488_Param_sampler_s, lerp(centralUV, uv3, localScale) + offset).xyz;
    float3 bloom4 = p_default_Material_0C38D4A418992488_Param_texture.Sample(p_default_Material_0C38D4A418992488_Param_sampler_s, lerp(centralUV, uv4, localScale) + offset).xyz;
    float3 bloom5 = p_default_Material_0C38D4A418992488_Param_texture.Sample(p_default_Material_0C38D4A418992488_Param_sampler_s, lerp(centralUV, uv5, localScale) + offset).xyz;
    float3 bloom6 = p_default_Material_0C38D4A418992488_Param_texture.Sample(p_default_Material_0C38D4A418992488_Param_sampler_s, lerp(centralUV, uv6, localScale) + offset).xyz;
    float3 bloom7 = p_default_Material_0C38D4A418992488_Param_texture.Sample(p_default_Material_0C38D4A418992488_Param_sampler_s, lerp(centralUV, uv7, localScale) + offset).xyz;
    float3 bloom8 = p_default_Material_0C38D4A418992488_Param_texture.Sample(p_default_Material_0C38D4A418992488_Param_sampler_s, lerp(centralUV, uv8, localScale) + offset).xyz;
    bloomSum += bloom1 + bloom2 + bloom3 + bloom4 + bloom5 + bloom6 + bloom7 + bloom8 + centralBloom;
  }
  o0.xyz = (bloomSum.xyz / 9.0) / iterations;
  o0.w = MaterialOpacity; // Unused
}