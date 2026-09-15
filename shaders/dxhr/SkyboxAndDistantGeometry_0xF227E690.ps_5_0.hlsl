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

SamplerState p_default_Material_0B390A243201500_0851E8642221812_Texture_sampler_s : register(s0);
Texture2D<float4> p_default_Material_0B390A243201500_0851E8642221812_Texture_texture : register(t0);

// This can actually draw any geometry
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD5,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.x = saturate(v2.w);
  r0.x = GlobalParams[2].x * r0.x;
  r0.yz = MaterialParams[1].xy * v1.xy;
  r1.xyzw = p_default_Material_0B390A243201500_0851E8642221812_Texture_texture.Sample(p_default_Material_0B390A243201500_0851E8642221812_Texture_sampler_s, r0.yz).xyzw;
  r0.yzw = MaterialParams[0].xyz * r1.xyz;
  o0.w = MaterialParams[0].w * r1.w;
  r1.xyz = MaterialParams[1].zzz * r0.yzw;
  r0.yzw = -MaterialParams[1].zzz * r0.yzw + FogColor.xyz;
  o0.xyz = r0.xxx * r0.yzw + r1.xyz;
  
#if 1 // Luma
  uint width, height, mipCount;
  p_default_Material_0B390A243201500_0851E8642221812_Texture_texture.GetDimensions(0, width, height, mipCount);
  bool skyboxType1 = width == 1024 && height == 1024;
  bool skyboxType2 = width == 2048 && height == 2048 && mipCount == 12; // TODO: improve this test... Maybe make them optional too
  if (skyboxType1 || skyboxType2) // Skybox properties (of seemengly all levels)
  {
    // Skyboxes have black edges and only draw a circle in the middle
    float4 samples[5];
    if (skyboxType1)
    {
      samples[0] = p_default_Material_0B390A243201500_0851E8642221812_Texture_texture.Load(int3(0, 0, 0)).xyzw;
      samples[1] = p_default_Material_0B390A243201500_0851E8642221812_Texture_texture.Load(int3(width - 1, 0, 0)).xyzw;
      samples[2] = p_default_Material_0B390A243201500_0851E8642221812_Texture_texture.Load(int3(0, height - 1, 0)).xyzw;
      samples[3] = p_default_Material_0B390A243201500_0851E8642221812_Texture_texture.Load(int3(width - 1, height - 1, 0)).xyzw;
      samples[4] = p_default_Material_0B390A243201500_0851E8642221812_Texture_texture.Load(int3(width / 2, height / 2, 0)).xyzw;
    }

    // Heuristics. Slow and dumb but it works!
    float threshold = 2.5 / 255.0; // Needed due to texture compression messing up blacks
    float3 average = (samples[0].rgb + samples[1].rgb + samples[2].rgb + samples[3].rgb) / 4.0;
    if (skyboxType2 || (all(samples[0].rgb <= threshold) && all(samples[1].rgb <= threshold) && all(samples[2].rgb <= threshold) && all(samples[3].rgb <= threshold) && all(samples[4].rgb > average * 2.0)))
    {
      float2 sceneUV = v0.xy * ScreenExtents.zw + ScreenExtents.xy;
      bool forceSDR = ShouldForceSDR(sceneUV.xy);

      // Make sky more HDR (barely does anything if it's not day)
      if (LumaSettings.DisplayMode == 1 && !forceSDR)
      {
        float normalizationPoint = 0.05; // Found empyrically
        float fakeHDRIntensity = 0.333 * LumaSettings.GameSettings.HDRBoostIntensity;
        o0.xyz = gamma_to_linear(o0.xyz, GCT_MIRROR);
        o0.xyz = FakeHDR(o0.xyz, normalizationPoint, fakeHDRIntensity);
        o0.xyz = linear_to_gamma(o0.xyz, GCT_MIRROR);
      }
    
      // Aid banding in the sky. This doesn't do much!
      ApplyDithering(o0.xyz, sceneUV, true, 1.0, 6, LumaSettings.FrameIndex, true);
      
      // Luma: apply the inverse of the emissive intensity boost, to avoid it applying on the sky
      // TODO: Note that during the day, "564A532D" is seemengly used instead sometimes, other times "AD488406", other times with "7047B218", however that's just a sky ceiling mesh
      float emissiveScale = forceSDR ? 0.0 : LumaSettings.GameSettings.EmissiveIntensity;
      float emissiveMaxReduction = 0.333; // Empryically found (they still need boosted visibility)
      o0.rgb *= lerp(1.0, max(sqr(sqr(saturate(1.0 - o0.a))), 0.01), saturate(emissiveScale) * emissiveMaxReduction);

#if (DEVELOPMENT || TEST) && 1 // Draw purple to find false positives or false negatives
      o0.xyz = float3(1, 0, 1);
#endif
    }
  }
#endif
}