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

SamplerState p_default_Material_1C7F38A4131815_Param_sampler_s : register(s0);
SamplerState p_default_Material_228BB2E414810090_Param_sampler_s : register(s1);
Texture2D<float4> p_default_Material_1C7F38A4131815_Param_texture : register(t0);
Texture2D<float4> p_default_Material_228BB2E414810090_Param_texture : register(t1);

void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xy = v0.xy * ScreenExtents.zw + ScreenExtents.xy;
  r0.z = p_default_Material_228BB2E414810090_Param_texture.SampleLevel(p_default_Material_228BB2E414810090_Param_sampler_s, r0.xy, 0).x;
  o0.xyz = p_default_Material_1C7F38A4131815_Param_texture.Sample(p_default_Material_1C7F38A4131815_Param_sampler_s, r0.xy).xyz;
  r0.x = r0.z * DepthToW.x + DepthToW.y;
  r0.x = max(9.99999997e-007, r0.x);
  r0.x = 1 / r0.x;
  r0.xy = -InstanceParams[0].zy + r0.xx;
  r0.zw = InstanceParams[0].wx + -InstanceParams[0].zy;
  r0.xy = r0.xy / r0.zw;
  r0.x = saturate(max(r0.x, r0.y));
  r0.x = saturate(InstanceParams[1].x * r0.x);
  o0.w = MaterialOpacity * r0.x;
}