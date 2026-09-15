#include "Includes/Common.hlsl"

Texture2D<float4> sourceTexture : register(t0);

// This should already be set, with mostly valid values
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

// Do whatever we want with lighting here (even subtracting from it, and making negative colors)
float4 main(float4 pos : SV_Position0) : SV_Target0
{
  float4 color = sourceTexture.Load(int3(pos.xy, 0)); // Note: the alpha channel controls the amount of object UI overlay
  float3 lightingColor = LumaSettings.GameSettings.LightingColor.rgb;
  
  color.rgb = gamma_to_linear(color.rgb, GCT_MIRROR);
  lightingColor.rgb = gamma_to_linear(lightingColor.rgb, GCT_MIRROR);
  color.rgb = BT709_To_BT2020(color.rgb);
  lightingColor.rgb = BT709_To_BT2020(lightingColor.rgb);

  color.rgb *= lightingColor.rgb; // Do it in linear BT.2020 to generate colors beyond BT.709

  color.rgb = BT2020_To_BT709(color.rgb);
  color.rgb = linear_to_gamma(color.rgb, GCT_MIRROR);

	return color;
}