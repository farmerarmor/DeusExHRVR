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

cbuffer InstanceBuffer : register(b5)
{
  float4 InstanceParams[8] : packoffset(c0);
}

SamplerState p_default_Material_0821DF245161732_DepthBufferTexture_sampler_s : register(s0);
Texture2D<float4> p_default_Material_0821DF245161732_DepthBufferTexture_texture : register(t0);

void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xy = v0.xy * ScreenExtents.zw + ScreenExtents.xy;
  r0.z = p_default_Material_0821DF245161732_DepthBufferTexture_texture.Sample(p_default_Material_0821DF245161732_DepthBufferTexture_sampler_s, r0.xy).x;
  r0.xy = r0.xy * DepthToView.xy + DepthToView.zw;
  r0.z = r0.z * DepthToW.x + DepthToW.y;
  r0.z = max(1.52600005e-005, r0.z);
  r1.z = 1 / r0.z;
  r1.xy = r1.zz * r0.xy;
  r0.xyz = InstanceParams[1].xyz * r1.yyy;
  r0.xyz = r1.xxx * InstanceParams[0].xyz + r0.xyz;
  r0.xyz = r1.zzz * InstanceParams[2].xyz + r0.xyz;
  r2.xyz = InstanceParams[3].xyz + r0.xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.www * r0.xyz;
  r0.w = dot(r0.xyz, r2.xyz);
  r1.w = dot(r0.xyz, -InstanceParams[3].xyz);
  r0.xyz = r1.www * r0.xyz + InstanceParams[3].xyz;
  r0.x = dot(r0.xyz, r0.xyz);
  r0.x = sqrt(r0.x);
  r0.x = min(1, r0.x);
  r0.y = -r0.x * r0.x + 1;
  r0.x = 1 + -r0.x;
  r0.y = sqrt(r0.y);
  r0.z = min(r0.y, r0.w);
  r0.y = min(r0.y, r1.w);
  r0.y = saturate(r0.y + r0.z);
  r0.x = r0.y * r0.x;
  r2.x = StereoOffset;
  r2.yz = float2(-0,-0);
  r0.yzw = r2.xyz + r1.xyz;
  r1.xyz = InstanceParams[4].xyz + r2.xyz;
  r1.w = dot(r0.yzw, r0.yzw);
  r2.x = rsqrt(r1.w);
  r1.w = sqrt(r1.w);
  r2.xyz = r2.xxx * r0.yzw;
  r1.x = dot(r2.xyz, r1.xyz);
  r1.x = saturate(r1.x / r1.w);
  r2.yz = r1.xx * r0.zw;
  r2.x = r1.x * r0.y + -StereoOffset;
  r0.yzw = -InstanceParams[4].xyz + r2.xyz;
  r0.y = dot(r0.yzw, r0.yzw);
  r0.y = r0.y * InstanceParams[6].x + InstanceParams[6].y;
  r0.y = 1 / r0.y;
  r0.yzw = InstanceParams[5].xyz * r0.yyy;
  o0.xyz = r0.yzw * r0.xxx;
  o0.w = MaterialOpacity;
  
  // Luma: fix negative values creating outlines
  o0 = max(o0, 0.0);
}