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

SamplerState p_default_Material_0B464C7419133480_Param_sampler_s : register(s0);
Texture2D<float4> p_default_Material_0B464C7419133480_Param_texture : register(t0); // Scene

// This shader has two hashes because one is for the original game and one for the original bloom in the DC
void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xy = v0.xy * ScreenExtents.zw + ScreenExtents.xy;
  bool forceSDR = ShouldForceSDR(r0.xy);
  r0.xyzw = p_default_Material_0B464C7419133480_Param_texture.Sample(p_default_Material_0B464C7419133480_Param_sampler_s, r0.xy).xyzw;
  r0.xyz = IsNaN_Strict(r0.xyz) ? 0.0 : r0.xyz; // Luma: fix NaNs
#if ENABLE_LUMA // Luma: Don't go beyond 5 times the SDR range (in gamma space). Some emissive objects had a brightness almost as high as the max float and would explode through bloom
  //r0.xyz = min(r0.xyz, 5.0); // Not needed until proven again, bloom isn't crazy high anymore (it does get bright and saturated). Also not sure we should clamp by channel.
  if (forceSDR)
    r0 = saturate(r0);
#else
  r0.xyz = saturate(r0.xyz);
#endif
  r0.w = saturate(MaterialParams[0].x * r0.w);
  o0.xyz = r0.w * r0.xyz;
  o0.xyz = IsNaN_Strict(o0.xyz) ? 0.0 : o0.xyz; // Luma: fix NaNs again (they were usually in the alpha channel)
  o0.w = MaterialOpacity;
}