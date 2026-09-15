#include "Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

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

SamplerState p_default_Material_051E652418262650_Texture_sampler_s : register(s0);
SamplerState p_default_Material_0BB3C5A410399412_Texture_sampler_s : register(s1);
Texture2D<float4> p_default_Material_051E652418262650_Texture_texture : register(t0);
Texture2D<float4> p_default_Material_0BB3C5A410399412_Texture_texture : register(t1);

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD5,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.x = MaterialParams[2].y * TimeVector.x;
  r0.x = sin(r0.x);
  r0.x = 1 + -abs(r0.x);
  r0.x = -r0.x * MaterialParams[2].x + 1;
  r0.yz = v1.xy / MaterialParams[1].zw;
  r0.yz = TimeVector.xx * MaterialParams[1].xy + r0.yz;
  r0.yzw = p_default_Material_0BB3C5A410399412_Texture_texture.Sample(p_default_Material_0BB3C5A410399412_Texture_sampler_s, r0.yz).xyz;
  r0.xyz = r0.yzw * r0.xxx;
  r1.xy = MaterialParams[0].xy * TimeVector.xx;
  r1.xy = v1.xy * MaterialParams[0].zw + r1.xy;
  r1.xyz = p_default_Material_051E652418262650_Texture_texture.Sample(p_default_Material_051E652418262650_Texture_sampler_s, r1.xy).xyz;
  r1.xyz = float3(1,1,1) + -r1.xyz;
  r1.xyz = -r1.xyz * MaterialParams[2].www + float3(1,1,1);
  r2.xyz = r1.xyz * r0.xyz;
  r0.xyz = -r0.xyz * r1.xyz + FogColor.xyz;
  r0.w = saturate(v2.w);
  r0.w = GlobalParams[2].x * r0.w;
  o0.xyz = r0.www * r0.xyz + r2.xyz;
  o0.w = MaterialOpacity;

  // Luma: fix it up
  o0.xyz = gamma_to_linear(o0.xyz, GCT_MIRROR);
	FixColorGradingLUTNegativeLuminance(o0.xyz);
  o0.xyz = linear_to_gamma(o0.xyz, GCT_MIRROR);
  o0.xyz = max(o0.xyz, 0);
  //o0.xyz = min(o0.xyz, 5.0); // Don't go beyond 5 times the SDR range (in gamma space). These emissive objects had a brightness almost as high as the max float
  o0.w = saturate(o0.w);
}