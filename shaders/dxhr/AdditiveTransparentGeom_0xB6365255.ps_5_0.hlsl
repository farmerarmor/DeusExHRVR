#include "Includes/Common.hlsl"

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

SamplerState p_default_Material_0A2F31A42976409_05142B6415252097_DepthBufferTexture_sampler_s : register(s0);
SamplerState p_default_Material_05C33064469382_Texture_sampler_s : register(s1);
Texture2D<float4> p_default_Material_0A2F31A42976409_05142B6415252097_DepthBufferTexture_texture : register(t0);
Texture2D<float4> p_default_Material_05C33064469382_Texture_texture : register(t1);

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD2,
  float4 v3 : TEXCOORD5,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xyz = CameraPosition.xyz + -v3.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.www * r0.xyz;
  r0.w = dot(v2.xyz, v2.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = v2.xyz * r0.www;
  r0.x = dot(r0.xyz, r1.xyz);
  r0.x = log2(r0.x);
  r0.x = MaterialParams[2].y * r0.x;
  r0.x = exp2(r0.x);
  r0.x = saturate(MaterialParams[1].y * r0.x);
  r0.yzw = p_default_Material_05C33064469382_Texture_texture.Sample(p_default_Material_05C33064469382_Texture_sampler_s, v1.xy).xyz;
  r0.xyz = r0.yzw * r0.xxx;
  r0.xyz = MaterialParams[0].xyz * r0.xyz;
  r0.xyz = MaterialParams[2].xxx * r0.xyz;
  r1.xy = v0.xy * ScreenExtents.zw + ScreenExtents.xy;
  bool forceSDR = ShouldForceSDR(r1.xy);
  r0.w = p_default_Material_0A2F31A42976409_05142B6415252097_DepthBufferTexture_texture.Sample(p_default_Material_0A2F31A42976409_05142B6415252097_DepthBufferTexture_sampler_s, r1.xy).x;
  r0.w = r0.w * DepthToW.x + DepthToW.y;
  r0.w = max(9.99999997e-007, r0.w);
  r0.w = 1 / r0.w;
  r1.xyz = -CameraPosition.xyz + v3.xyz;
  r1.x = dot(r1.xyz, CameraDirection.xyz);
  r0.w = -r1.x + r0.w;
  r1.x = -MaterialParams[1].z + r1.x;
  r0.w = saturate(r0.w / MaterialParams[0].w);
  r0.xyz = r0.www * r0.xyz;
  r0.w = MaterialParams[1].w + -MaterialParams[1].z;
  r0.w = saturate(r1.x / r0.w);
  o0.xyz = r0.www * r0.xyz;
  o0.w = MaterialOpacity;

  // Luma: boost up additive lights
  float emissiveScale = forceSDR ? 0.0 : LumaSettings.GameSettings.EmissiveIntensity;
  // Base it on how strong the light was
  o0.rgb /= lerp(1.0, max(sqr(sqr(saturate(1.0 - average(o0.rgb)))), 0.01), saturate(emissiveScale)); 
}