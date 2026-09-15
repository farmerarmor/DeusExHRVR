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

SamplerState p_default_Material_03ADF8249642911_BackBufferTexture_sampler_s : register(s0);
SamplerState p_default_Material_080BF2A44178963_Param_sampler_s : register(s1);
Texture2D<float4> p_default_Material_03ADF8249642911_BackBufferTexture_texture : register(t0);
Texture2D<float4> p_default_Material_080BF2A44178963_Param_texture : register(t1);

void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float2 uv = v0.xy * ScreenExtents.zw + ScreenExtents.xy;
  float2 offset = p_default_Material_080BF2A44178963_Param_texture.Sample(p_default_Material_080BF2A44178963_Param_sampler_s, uv).xy;
  offset = offset * 2.0 - 1.0;
  float distortionAmount = MaterialParams[0].x;
  float2 distortedUV = uv + offset * distortionAmount;

  // Skip the distortion if the alpha is too low (it probably means we are at the edges of the screen or something)
  float threshold = p_default_Material_080BF2A44178963_Param_texture.Sample(p_default_Material_080BF2A44178963_Param_sampler_s, distortedUV).z;
  distortedUV = (threshold >= 0.001) ? distortedUV : uv;

  o0.xyzw = p_default_Material_03ADF8249642911_BackBufferTexture_texture.Sample(p_default_Material_03ADF8249642911_BackBufferTexture_sampler_s, distortedUV).xyzw;
}