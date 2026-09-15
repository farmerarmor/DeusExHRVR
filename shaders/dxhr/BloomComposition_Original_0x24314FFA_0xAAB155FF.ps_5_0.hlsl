#include "Includes/Common.hlsl"

#ifndef ENABLE_FAKE_HDR
#define ENABLE_FAKE_HDR 1
#endif

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
}
cbuffer MaterialBuffer : register(b3)
{
  float4 MaterialParams[32] : packoffset(c0);
}

// Only set in the DE version of the game
cbuffer InstanceBuffer : register(b5)
{
  float4 InstanceParams[8] : packoffset(c0);
}

SamplerState p_default_Material_0B33AFF46643651_Param_sampler_s : register(s0);
SamplerState p_default_Material_0B33AF346638807_Param_sampler_s : register(s1);
Texture2D<float4> p_default_Material_0B33AFF46643651_Param_texture : register(t0); // Bloomed Scene
Texture2D<float4> p_default_Material_0B33AF346638807_Param_texture : register(t1); // Scene
Texture2D<float4> depth_texture : register(t3); // Depth

// This shader has two hashes because one is for the original game and one for the original bloom in the DC
void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xy = v0.xy * ScreenExtents.zw + ScreenExtents.xy;

  float4 sceneColor = p_default_Material_0B33AF346638807_Param_texture.Sample(p_default_Material_0B33AF346638807_Param_sampler_s, r0.xy);
  sceneColor = IsNaN_Strict(sceneColor) ? 0.0 : sceneColor; // Luma: fix NaNs in bloom output
#if 0
  sceneColor.xyz = isinf(sceneColor.xyz) ? 0.0 : sceneColor.xyz;
#endif
#if 0 // Luma: Don't go beyond 5 times the SDR range (in gamma space). Some emissive objects had a brightness almost as high as the max float and would explode through bloom // Disabled as for some reason this breaks if there were nans
  sceneColor.xyz = min(sceneColor.xyz, 5.0); // Bloom was already clamped before
#endif

  bool forceSDR = ShouldForceSDR(r0.xy);

#if ENABLE_FAKE_HDR // The game doesn't have many bright highlights, the dynamic range is relatively low, this helps alleviate that. Do it before bloom to avoid bloom going crazy too
  if (LumaSettings.DisplayMode == 1 && !forceSDR)
  {
    float normalizationPoint = 0.05; // Found empyrically
    float fakeHDRIntensity = 0.1 * LumaSettings.GameSettings.HDRBoostIntensity;
    sceneColor.xyz = gamma_to_linear(sceneColor.xyz, GCT_MIRROR);
    sceneColor.xyz = FakeHDR(sceneColor.xyz, normalizationPoint, fakeHDRIntensity);
    sceneColor.xyz = linear_to_gamma(sceneColor.xyz, GCT_MIRROR);
  }
#endif // ENABLE_FAKE_HDR

  float4 bloomedColor;
  bloomedColor = p_default_Material_0B33AFF46643651_Param_texture.Sample(p_default_Material_0B33AFF46643651_Param_sampler_s, r0.xy);
  //bloomedColor = IsNaN_Strict(bloomedColor) ? 0.0 : bloomedColor; // Shouldn't be needed
  
  // Extra safety to guarantee the vanilla look
  if (forceSDR)
  {
    bloomedColor = saturate(bloomedColor);
    sceneColor = saturate(sceneColor);
  }
  
  float emissiveScale = forceSDR ? 0.0 : LumaSettings.GameSettings.EmissiveIntensity;
#if ENABLE_LUMA
  sceneColor.xyz /= lerp(1.0, max(sqr(sqr(saturate(1.0 - sceneColor.w))), 0.01), saturate(emissiveScale)); // Luma: scale scene color (before fogging it, and before bloom, as we already have bloom controls)
#endif

  float4 foggedColor = sceneColor;
#if ENABLE_LUMA // Add back missing fog from the original bloom
  if (LumaSettings.GameSettings.FogIntensity > 0.0) // Avoid trying to add the fog in the original version of the game (non DE) as the cbuffers for it aren't there
  {
    // Note: this is using an anisotropic sampler, which makes no sense for any post process (all pp does in this game), but it's probably cheap enough
    r0.z = depth_texture.Sample(p_default_Material_0B33AF346638807_Param_sampler_s, r0.xy).x;
    r0.z = r0.z * DepthToW.x + DepthToW.y;
    r0.z = max(9.99999997e-007, r0.z);
    r0.z = 1 / r0.z;
    r0.z = -InstanceParams[3].x + r0.z;
    r0.w = InstanceParams[3].y - InstanceParams[3].x;
    r0.z = saturate(r0.z / r0.w);
    r0.w = -InstanceParams[3].z * r0.z + 1;
    r0.z = InstanceParams[3].z * r0.z;

    r0.z *= LumaSettings.GameSettings.FogIntensity; // Luma: scale the fog effect that was in the DC version of the game too

    r0.x = linear_to_gamma1(saturate(GetLuminance(gamma_to_linear(bloomedColor.rgb, GCT_MIRROR)))) * r0.w + r0.z;
    r0.x *= r0.z;
    r0.xyz = r0.x * (InstanceParams[5].xyz * r0.x - sceneColor.xyz);
    foggedColor.xyz = r0.xyz + sceneColor.xyz; // This shouldn't ever result in negative colors
  }
#endif

  bloomedColor *= forceSDR ? 1.f : LumaSettings.GameSettings.BloomIntensity; // Luma: scale bloom

#if 1
  o0.xyz = bloomedColor.xyz * MaterialParams[0].x + foggedColor.xyz;
#else // The version of the original bloom in the DC edition. It seems like this would draw bloom ignoring fog, so it's probably not good for our case (it indeed seems to be very broken)
  o0.xyz = lerp(foggedColor.xyz, bloomedColor.xyz, MaterialParams[0].x * saturate(bloomedColor.xyz)); // Luma: added saturate on alpha factor
#endif
  o0.w = MaterialOpacity;
  
  // Luma
  o0 = IsNaN_Strict(o0) ? 0.0 : o0;
  //o0.rgb = isinf(o0.rgb) ? 1.0 : o0.rgb;
}