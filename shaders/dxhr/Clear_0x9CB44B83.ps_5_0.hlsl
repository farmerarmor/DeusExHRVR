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

// This seems to draw black bars (maybe not exclusively)
void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  o0.w = InstanceParams[4].w * MaterialOpacity;
  o0.xyz = InstanceParams[4].xyz;

  // Luma:
  o0.rgb = max(o0.rgb, 0.0);
  o0.w = saturate(o0.w);
}