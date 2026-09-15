#include "../Includes/Common.hlsl"

#if 0
cbuffer cbData : register(b0)
{
  float WidthMinusOne : packoffset(c0.y);
  float HeightMinusOne : packoffset(c0.z);
}
#else
cbuffer cb0_buf : register(b0)
{
    uint cb0_m0 : packoffset(c0);
    float2 cb0_m1 : packoffset(c0.y);
    uint cb0_m2 : packoffset(c0.w);
};
#endif

Texture2D<float4> g_txInitialImage : register(t0);

int cvt_f32_i32(float v)
{
    return isnan(v) ? 0 : ((v < (-2147483648.0f)) ? int(0x80000000) : ((v > 2147483520.0f) ? 2147483647 : int(v)));
}

uint main(
  float4 gl_FragCoord : SV_Position0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0) : SV_Target0
{
    gl_FragCoord.w = 1.0 / gl_FragCoord.w; // Might be a leftover from SPIRV
    uint outValue;

    int _69 = cvt_f32_i32(gl_FragCoord.x);
    int _70 = cvt_f32_i32(gl_FragCoord.y);
    int _79 = cvt_f32_i32(cb0_m1.x);
    int _80 = cvt_f32_i32(cb0_m1.y);
    uint _87 = uint(clamp(_70, 0, _80));
    uint _96 = uint(clamp(_69, 0, _79));
    float3 color1 = float3(g_txInitialImage.Load(int3(uint2(_96, _87), 0u)).xyz);
    float3 color2 = float3(g_txInitialImage.Load(int3(uint2(_96, uint(clamp(_70 - 1, 0, _80))), 0u)).xyz);
    float3 color3 = float3(g_txInitialImage.Load(int3(uint2(uint(clamp(_69 + 1, 0, _79)), _87), 0u)).xyz);
    float lum1 = dot(color1, float3(0.3, 0.59, 0.11));
    float lum2 = dot(color2, float3(0.3, 0.59, 0.11));
    float lum3 = dot(color3, float3(0.3, 0.59, 0.11));
#if 1 // Luma: fixed random luminance coeffs and it being calculated in gamma space
    lum1 = linear_to_gamma1(GetLuminance(gamma_to_linear(color1.xyz, GCT_POSITIVE)));
    lum2 = linear_to_gamma1(GetLuminance(gamma_to_linear(color2.xyz, GCT_POSITIVE)));
    lum3 = linear_to_gamma1(GetLuminance(gamma_to_linear(color3.xyz, GCT_POSITIVE)));
#endif
    bool _117 = abs(lum1 - lum2) > 0.083333335816860198974609375f;
    outValue = (abs(lum1 - lum3) > 0.083333335816860198974609375f) ? (_117 ? 3u : 2u) : uint(_117);
    return outValue;
}