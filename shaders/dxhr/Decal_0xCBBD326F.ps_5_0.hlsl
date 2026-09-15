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

SamplerState p_default_Setup_1D4E78E47636589_Texture_sampler_s : register(s0);
SamplerState p_default_Normal_1D4E6FE47421562_Texture_sampler_s : register(s1);
SamplerState p_default_Material_17D7DA242013551_BackBufferTexture_sampler_s : register(s2);
SamplerState p_default_Material_1168F8E4100967_Texture_sampler_s : register(s3);
Texture2D<float4> p_default_Setup_1D4E78E47636589_Texture_texture : register(t0);
Texture2D<float4> p_default_Normal_1D4E6FE47421562_Texture_texture : register(t1);
Texture2D<float4> p_default_Material_17D7DA242013551_BackBufferTexture_texture : register(t2);
Texture2D<float4> p_default_Material_1168F8E4100967_Texture_texture : register(t3);

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD2,
  float4 v3 : TEXCOORD3,
  float4 v4 : TEXCOORD4,
  float4 v5 : TEXCOORD5,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  r0.x = p_default_Setup_1D4E78E47636589_Texture_texture.Sample(p_default_Setup_1D4E78E47636589_Texture_sampler_s, v1.xy).y;
  r0.x = MaterialParams[0].z * r0.x;
  r0.y = dot(v3.xyz, v3.xyz);
  r0.y = rsqrt(r0.y);
  r0.yzw = v3.xyz * r0.y;
  r1.xyz = CameraPosition.xyz + -v5.xyz;
  r1.w = dot(r1.xyz, r1.xyz);
  r1.w = rsqrt(r1.w);
  r1.xyz = r1.w * r1.xyz;
  r2.x = dot(r0.yzw, r1.xyz);
  r0.y = dot(v4.xyz, v4.xyz);
  r0.y = rsqrt(r0.y);
  r0.yzw = v4.xyz * r0.y;
  r2.y = dot(r0.yzw, r1.xyz);
  r0.xy = r0.x * r2.xy + v1.xy;
  r2.xyz = p_default_Normal_1D4E6FE47421562_Texture_texture.Sample(p_default_Normal_1D4E6FE47421562_Texture_sampler_s, r0.xy).xyw;
  r0.xyzw = p_default_Material_1168F8E4100967_Texture_texture.Sample(p_default_Material_1168F8E4100967_Texture_sampler_s, r0.xy).xyzw;
  r2.y = r2.y * r2.z;
  r2.xy = r2.xy * 2.0 - 1.0;
  r3.xyz = v4.xyz * r2.y;
  r3.xyz = r2.x * v3.xyz + r3.xyz;
  r1.w = dot(r2.xy, r2.xy);
  r1.w = 1 + -r1.w;
  r1.w = max(0, r1.w);
  r1.w = sqrt(r1.w);
  r2.xyz = r1.w * v2.xyz + r3.xyz;
  r1.w = dot(r2.xyz, r2.xyz);
  r1.w = rsqrt(r1.w);
  r2.xyz = r1.w * r2.xyz;
  r1.x = dot(r2.xyz, r1.xyz);
#if 1 // Luma: fixed nans
  r1.x = pow(abs(r1.x), MaterialParams[0].y) * sign(r1.x);
#else
  r1.x = pow(r1.x, MaterialParams[0].y);
#endif
  float2 screenUV = v0.xy * ScreenExtents.zw + ScreenExtents.xy;
  float3 sceneColor = p_default_Material_17D7DA242013551_BackBufferTexture_texture.Sample(p_default_Material_17D7DA242013551_BackBufferTexture_sampler_s, screenUV).xyz;
  r1.y = GetLuminance(sceneColor); // Luma: fixed BT.601 luminance
  r1.x = r1.y * r1.x;
  r1.x = MaterialParams[0].x * r1.x;
  r1.xzw = r1.x * r0.xyz;
  r0.xyz = r1.y * r0.xyz + r1.xzw;
  o0.w = MaterialOpacity * r0.w;
  r1.xyz = FogColor.xyz + -r0.xyz;
  r0.w = saturate(v5.w);
  r0.w = GlobalParams[2].x * r0.w;
  o0.xyz = r0.w * r1.xyz + r0.xyz;
}