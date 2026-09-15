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

SamplerState p_default_Material_0C38D4A418992488_Param_sampler_s : register(s0);
Texture2D<float4> p_default_Material_0C38D4A418992488_Param_texture : register(t0);

// This does some kind of 3D shift effect, similar to chromatic aberration, on death
void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.x = InstanceParams[1].w * TimeVector.x;
  r0.x = trunc(r0.x);
  r0.yz = v0.xy * ScreenExtents.zw + ScreenExtents.xy;
  r0.w = r0.z + -r0.x;
  r0.x = r0.z + r0.x;
  r0.x = InstanceParams[0].x * r0.x;
  r0.x = cos(r0.x);
  r0.w = InstanceParams[0].z * r0.w;
  r0.w = cos(r0.w);
  r0.w = InstanceParams[0].w * r0.w;
  r0.x = r0.x * InstanceParams[0].y + r0.w;
  r1.xyz = InstanceParams[1].zyx * r0.xxx; // Horizontal shift
  r1.w = 0;
#if 1 // Fixed death overaly being a lot more shifted in UW
  float2 size;
  p_default_Material_0C38D4A418992488_Param_texture.GetDimensions(size.x, size.y);
  float sourceAspectRatio = 16.0 / 9.0; // What the vertex shader assumed (it seems to help at 4:3 too)
  float targetAspectRatio = size.x / size.y;
  r1.xyz *= sourceAspectRatio / targetAspectRatio;
#endif
  r0.xw = r1.zw + r0.yz;
  r0.x = p_default_Material_0C38D4A418992488_Param_texture.Sample(p_default_Material_0C38D4A418992488_Param_sampler_s, r0.xw).x;
  o0.x = r0.x;
  r0.xyzw = r1.yxww + r0.yyzz;
  r0.y = p_default_Material_0C38D4A418992488_Param_texture.Sample(p_default_Material_0C38D4A418992488_Param_sampler_s, r0.yz).z;
  o0.z = r0.y;
  r0.x = p_default_Material_0C38D4A418992488_Param_texture.Sample(p_default_Material_0C38D4A418992488_Param_sampler_s, r0.xw).y;
  o0.y = r0.x;
  o0.w = MaterialOpacity;
}