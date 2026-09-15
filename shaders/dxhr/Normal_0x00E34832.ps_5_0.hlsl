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

SamplerState p_default_Normal_050F2CA413890394_05A108244333199_Texture_sampler_s : register(s0);
Texture2D<float4> p_default_Normal_050F2CA413890394_05A108244333199_Texture_texture : register(t0);

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD2,
  float3 v3 : TEXCOORD3,
  float3 v4 : TEXCOORD4,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xy = MaterialParams[0].xy * v1.xy;
  r0.xyz = p_default_Normal_050F2CA413890394_05A108244333199_Texture_texture.Sample(p_default_Normal_050F2CA413890394_05A108244333199_Texture_sampler_s, r0.xy).xyw;
  r0.y = r0.y * r0.z;
  r0.xy = r0.xy * float2(2,2) + float2(-1,-1);
  r0.zw = saturate(MaterialParams[0].ww + -abs(r0.xy));
  r1.x = 1 / MaterialParams[0].w;
  r0.zw = -r0.zw * r1.xx + float2(1,1);
  r0.zw = r0.zw * r0.xy;
  r0.x = dot(r0.xy, r0.xy);
  r0.x = 1 + -r0.x;
  r0.x = max(0, r0.x);
  r0.x = sqrt(r0.x);
  r0.yz = MaterialParams[0].zz * r0.zw;
  r1.xyz = v4.xyz * r0.zzz;
  r0.yzw = r0.yyy * v3.xyz + r1.xyz;
  r0.xyz = r0.xxx * v2.xyz + r0.yzw;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w); // This causes the nans, however saturating it would break the output
  r0.xyz = r0.w * r0.xyz;
  r1.xyz = GlobalParams[12].xyz * r0.yyy;
  r0.xyw = r0.xxx * GlobalParams[11].xyz + r1.xyz;
  r0.xyz = r0.zzz * GlobalParams[13].xyz + r0.xyw;
  o0.xyz = r0.xyz * float3(0.5,0.5,0.5) + float3(0.5,0.5,0.5);
  o0.w = MaterialOpacity;

  // Luma: fixed NaNs
  o0 = saturate(o0);
}