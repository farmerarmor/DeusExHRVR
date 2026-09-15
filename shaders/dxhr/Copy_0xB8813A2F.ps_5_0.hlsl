SamplerState Sampler0_s : register(s0);
Texture2D<float4> InstanceTexture0 : register(t0);

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xyzw = InstanceTexture0.Sample(Sampler0_s, v2.xy).xyzw;
  o0.xyzw = v1.xyzw * r0.xyzw;

  // Luma:
  o0.rgb = max(o0.rgb, 0.0);
  o0.w = saturate(o0.w);
}