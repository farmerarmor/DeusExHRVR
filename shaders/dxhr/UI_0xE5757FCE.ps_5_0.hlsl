cbuffer DrawableBuffer : register(b1)
{
  float4 FogColor : packoffset(c0);
  float4 DebugColor : packoffset(c1);
  float MaterialOpacity : packoffset(c2);
  float AlphaThreshold : packoffset(c3);
}

cbuffer InstanceBuffer : register(b5)
{
  float4 InstanceParams[8] : packoffset(c0);
}

SamplerState p_default_Material_051164A4424935531_Param_sampler_s : register(s0);
Texture2D<float4> p_default_Material_051164A4424935531_Param_texture : register(t0);

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.x = p_default_Material_051164A4424935531_Param_texture.Sample(p_default_Material_051164A4424935531_Param_sampler_s, v2.xy).w;
  r0.x = v1.w * r0.x;
  r0.x = r0.x * InstanceParams[5].w + InstanceParams[6].w;
  o0.w = MaterialOpacity * r0.x;
  o0.xyz = v1.xyz;

  // Luma:
  o0.rgb = max(o0.rgb, 0.0);
  o0.w = saturate(o0.w);
}