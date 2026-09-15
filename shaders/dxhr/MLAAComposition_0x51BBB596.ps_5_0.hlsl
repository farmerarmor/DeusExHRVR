#include "Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/Reinhard.hlsl"

Texture2D<float4> g_txInitialImage : register(t0);
Texture2D<uint2> g_txCount : register(t2);

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


uint spvBitfieldUExtract(uint Base, uint Offset, uint Count)
{
    uint Mask = Count == 32 ? 0xffffffff : ((1 << Count) - 1);
    return (Base >> Offset) & Mask;
}

uint2 spvBitfieldUExtract(uint2 Base, uint Offset, uint Count)
{
    uint Mask = Count == 32 ? 0xffffffff : ((1 << Count) - 1);
    return (Base >> Offset) & Mask;
}

uint3 spvBitfieldUExtract(uint3 Base, uint Offset, uint Count)
{
    uint Mask = Count == 32 ? 0xffffffff : ((1 << Count) - 1);
    return (Base >> Offset) & Mask;
}

uint4 spvBitfieldUExtract(uint4 Base, uint Offset, uint Count)
{
    uint Mask = Count == 32 ? 0xffffffff : ((1 << Count) - 1);
    return (Base >> Offset) & Mask;
}

int spvBitfieldSExtract(int Base, int Offset, int Count)
{
    int Mask = Count == 32 ? -1 : ((1 << Count) - 1);
    int Masked = (Base >> Offset) & Mask;
    int ExtendShift = (32 - Count) & 31;
    return (Masked << ExtendShift) >> ExtendShift;
}

int2 spvBitfieldSExtract(int2 Base, int Offset, int Count)
{
    int Mask = Count == 32 ? -1 : ((1 << Count) - 1);
    int2 Masked = (Base >> Offset) & Mask;
    int ExtendShift = (32 - Count) & 31;
    return (Masked << ExtendShift) >> ExtendShift;
}

int3 spvBitfieldSExtract(int3 Base, int Offset, int Count)
{
    int Mask = Count == 32 ? -1 : ((1 << Count) - 1);
    int3 Masked = (Base >> Offset) & Mask;
    int ExtendShift = (32 - Count) & 31;
    return (Masked << ExtendShift) >> ExtendShift;
}

int4 spvBitfieldSExtract(int4 Base, int Offset, int Count)
{
    int Mask = Count == 32 ? -1 : ((1 << Count) - 1);
    int4 Masked = (Base >> Offset) & Mask;
    int ExtendShift = (32 - Count) & 31;
    return (Masked << ExtendShift) >> ExtendShift;
}

int cvt_f32_i32(float v)
{
    return isnan(v) ? 0 : ((v < (-2147483648.0f)) ? int(0x80000000) : ((v > 2147483520.0f) ? 2147483647 : int(v)));
}

float GetLuminance_Custom(float3 a)
{
#if 0
  return dot(a, float3(0.3, 0.59, 0.11));
#else // Luma: fixed random luminance coeffs and it being calculated in gamma space
  return linear_to_gamma1(GetLuminance(gamma_to_linear(a, GCT_POSITIVE)));
#endif
}

float4 main(float4 gl_FragCoord : SV_Position) : SV_Target0
{
    gl_FragCoord.w = 1.0 / gl_FragCoord.w; // Might be a leftover from SPIRV
    float4 outColor;

    int _79 = cvt_f32_i32(gl_FragCoord.x);
    int _80 = cvt_f32_i32(gl_FragCoord.y);
    uint _82 = uint(_79);
    uint _83 = uint(_80);
    uint2 _85 = uint2(_82, _83);
    float4 _86 = g_txInitialImage.Load(int3(_85, 0u));
    float _87 = _86.x;
    float _88 = _86.y;
    float _89 = _86.z;
    float _90 = _86.w;
    uint2 _92 = g_txCount.Load(int3(_85, 0u));
    uint _94 = _92.x;
    uint _95 = _92.y;
    uint _97 = uint(_80 + 1);
    uint2 _98 = uint2(_82, _97);
    uint2 _99 = g_txCount.Load(int3(_98, 0u));
    uint _100 = _99.x;
    uint _102 = uint(_79 - 1);
    uint2 _103 = uint2(_102, _83);
    uint2 _104 = g_txCount.Load(int3(_103, 0u));
    uint _105 = _104.y;
    float _221;
    float _222;
    float _223;
    float _224;
    if ((_94 & 136u) != 0u)
    {
        uint _111 = spvBitfieldUExtract(_94, 4u, 3u);
        uint _112 = _94 & 7u;
        float4 _118 = g_txInitialImage.Load(int3(uint2(_82, uint(_80 - 1)), 0u));
        float _124 = _118.x - _87;
        float _125 = _118.y - _88;
        float _126 = _118.z - _89;
        bool _132 = (_94 & 8u) != 0u;
        bool _134 = (_94 & 128u) != 0u;
        uint _135 = _134 ? _111 : 8u;
        float _139 = float(int(((_132 ? _112 : 8u) + _135) + 1u));
        float _140 = _139 * 0.5f;
        int _142 = _134 ? int(_111) : 8;
        float _143 = float(_142);
        bool _166 = abs(GetLuminance_Custom(float3(g_txInitialImage.Load(int3(uint2(uint(_79 - _142), _83), 0u)).xyz)) - GetLuminance_Custom(float3(g_txInitialImage.Load(int3(uint2(uint(int(4294967295u - _135) + _79), _83), 0u)).xyz))) > 0.083333335816860198974609375f;
        int _169 = (_132 ? int(_112) : 8) + _79;
        bool _189 = abs(GetLuminance_Custom(float3(g_txInitialImage.Load(int3(uint2(uint(_169), _83), 0u)).xyz)) - GetLuminance_Custom(float3(g_txInitialImage.Load(int3(uint2(uint(_169 + 1), _83), 0u)).xyz))) > 0.083333335816860198974609375f;
        bool _200 = ((((!_166) && _189) && (_140 <= _143)) || ((_166 && (!_189)) && (_140 >= _143))) || (_166 && _189);
        float _201 = 1.0f / _139;
        float _202 = _139 - _143;
        float _209 = (abs(mad(_201, _202, -0.5f)) + abs(mad(_201, _202 - 1.0f, -0.5f))) * 0.5f;
        bool _216 = (_111 + _112) != 0u;
        _221 = _216 ? _90 : mad(_118.w - _90, 0.125f, _90);
        _222 = _216 ? (_200 ? mad(_209, _126, _89) : _89) : mad(_126, 0.125f, _89);
        _223 = _216 ? (_200 ? mad(_209, _125, _88) : _88) : mad(_125, 0.125f, _88);
        _224 = _216 ? (_200 ? mad(_124, _209, _87) : _87) : mad(_124, 0.125f, _87);
    }
    else
    {
        _221 = _90;
        _222 = _89;
        _223 = _88;
        _224 = _87;
    }
    float _337;
    float _338;
    float _339;
    float _340;
    if ((_100 & 136u) != 0u)
    {
        uint _229 = spvBitfieldUExtract(_100, 4u, 3u);
        uint _230 = _100 & 7u;
        float4 _233 = g_txInitialImage.Load(int3(_98, 0u));
        float _239 = _233.x - _224;
        float _240 = _233.y - _223;
        float _241 = _233.z - _222;
        bool _247 = (_100 & 8u) != 0u;
        bool _249 = (_100 & 128u) != 0u;
        uint _250 = _249 ? _229 : 8u;
        float _254 = float(int(((_247 ? _230 : 8u) + _250) + 1u));
        float _255 = _254 * 0.5f;
        int _257 = _249 ? int(_229) : 8;
        float _258 = float(_257);
        bool _281 = abs(GetLuminance_Custom(float3(g_txInitialImage.Load(int3(uint2(uint(_79 - _257), _97), 0u)).xyz)) - GetLuminance_Custom(float3(g_txInitialImage.Load(int3(uint2(uint(_79 + int(4294967295u - _250)), _97), 0u)).xyz))) > 0.083333335816860198974609375f;
        int _284 = _79 + (_247 ? int(_230) : 8);
        bool _304 = abs(GetLuminance_Custom(float3(g_txInitialImage.Load(int3(uint2(uint(_284), _97), 0u)).xyz)) - GetLuminance_Custom(float3(g_txInitialImage.Load(int3(uint2(uint(_284 + 1), _97), 0u)).xyz))) > 0.083333335816860198974609375f;
        bool _316 = (!(_281 || _304)) || (((_281 && (!_304)) && (_255 <= _258)) || ((_304 && (!_281)) && (_255 >= _258)));
        float _317 = 1.0f / _254;
        float _318 = _254 - _258;
        float _325 = (abs(mad(_317, _318, -0.5f)) + abs(mad(_317, _318 - 1.0f, -0.5f))) * 0.5f;
        bool _332 = (_230 + _229) != 0u;
        _337 = _332 ? _221 : mad(_233.w - _221, 0.125f, _221);
        _338 = _332 ? (_316 ? mad(_325, _241, _222) : _222) : mad(_241, 0.125f, _222);
        _339 = _332 ? (_316 ? mad(_325, _240, _223) : _223) : mad(_240, 0.125f, _223);
        _340 = _332 ? (_316 ? mad(_239, _325, _224) : _224) : mad(_239, 0.125f, _224);
    }
    else
    {
        _337 = _221;
        _338 = _222;
        _339 = _223;
        _340 = _224;
    }
    float _454;
    float _455;
    float _456;
    float _457;
    if ((_95 & 136u) != 0u)
    {
        uint _344 = spvBitfieldUExtract(_95, 4u, 3u);
        uint _345 = _95 & 7u;
        float4 _351 = g_txInitialImage.Load(int3(uint2(uint(_79 + 1), _83), 0u));
        float _357 = _351.x - _340;
        float _358 = _351.y - _339;
        float _359 = _351.z - _338;
        bool _365 = (_95 & 128u) != 0u;
        bool _366 = (_95 & 8u) != 0u;
        uint _367 = _365 ? _344 : 8u;
        float _372 = float(int((_367 + (_366 ? _345 : 8u)) + 1u));
        float _373 = _372 * 0.5f;
        int _375 = _365 ? int(_344) : 8;
        float _376 = float(_375);
        bool _399 = abs(GetLuminance_Custom(float3(g_txInitialImage.Load(int3(uint2(_82, uint(_375 + _80)), 0u)).xyz)) - GetLuminance_Custom(float3(g_txInitialImage.Load(int3(uint2(_82, uint(int(_367 + 1u) + _80)), 0u)).xyz))) > 0.083333335816860198974609375f;
        int _402 = _80 - (_366 ? int(_345) : 8);
        bool _422 = abs(GetLuminance_Custom(float3(g_txInitialImage.Load(int3(uint2(_82, uint(_402)), 0u)).xyz)) - GetLuminance_Custom(float3(g_txInitialImage.Load(int3(uint2(_82, uint(_402 - 1)), 0u)).xyz))) > 0.083333335816860198974609375f;
        bool _433 = (_399 && _422) || (((_373 <= _376) && (_422 && (!_399))) || ((_399 && (!_422)) && (_373 >= _376)));
        float _434 = 1.0f / _372;
        float _435 = _372 - _376;
        float _442 = (abs(mad(_434, _435, -0.5f)) + abs(mad(_434, _435 - 1.0f, -0.5f))) * 0.5f;
        bool _449 = (_345 + _344) != 0u;
        _454 = _449 ? _337 : mad(_351.w - _337, 0.125f, _337);
        _455 = _449 ? (_433 ? mad(_442, _359, _338) : _338) : mad(_359, 0.125f, _338);
        _456 = _449 ? (_433 ? mad(_442, _358, _339) : _339) : mad(_358, 0.125f, _339);
        _457 = _449 ? (_433 ? mad(_357, _442, _340) : _340) : mad(_357, 0.125f, _340);
    }
    else
    {
        _454 = _337;
        _455 = _338;
        _456 = _339;
        _457 = _340;
    }
    if ((_105 & 136u) != 0u)
    {
        uint _463 = spvBitfieldUExtract(_105, 4u, 3u);
        uint _464 = _105 & 7u;
        float4 _467 = g_txInitialImage.Load(int3(_103, 0u));
        float _473 = _467.x - _457;
        float _474 = _467.y - _456;
        float _475 = _467.z - _455;
        bool _481 = (_105 & 8u) != 0u;
        bool _483 = (_105 & 128u) != 0u;
        uint _484 = _483 ? _463 : 8u;
        float _488 = float(int(((_481 ? _464 : 8u) + _484) + 1u));
        float _489 = _488 * 0.5f;
        int _491 = _483 ? int(_463) : 8;
        float _492 = float(_491);
        bool _515 = abs(GetLuminance_Custom(float3(g_txInitialImage.Load(int3(uint2(_102, uint(_80 + _491)), 0u)).xyz)) - GetLuminance_Custom(float3(g_txInitialImage.Load(int3(uint2(_102, uint(_80 + int(_484 + 1u))), 0u)).xyz))) > 0.083333335816860198974609375f;
        int _518 = _80 - (_481 ? int(_464) : 8);
        bool _538 = abs(GetLuminance_Custom(float3(g_txInitialImage.Load(int3(uint2(_102, uint(_518)), 0u)).xyz)) - GetLuminance_Custom(float3(g_txInitialImage.Load(int3(uint2(_102, uint(_518 - 1)), 0u)).xyz))) > 0.083333335816860198974609375f;
        bool _550 = (!(_515 || _538)) || ((((!_515) && _538) && (_489 >= _492)) || ((_515 && (!_538)) && (_489 <= _492)));
        float _551 = 1.0f / _488;
        float _552 = _488 - _492;
        float _559 = (abs(mad(_551, _552, -0.5f)) + abs(mad(_551, _552 - 1.0f, -0.5f))) * 0.5f;
        bool _566 = (_464 + _463) != 0u;
        outColor.x = _566 ? (_550 ? mad(_473, _559, _457) : _457) : mad(_473, 0.125f, _457);
        outColor.y = _566 ? (_550 ? mad(_474, _559, _456) : _456) : mad(_474, 0.125f, _456);
        outColor.z = _566 ? (_550 ? mad(_475, _559, _455) : _455) : mad(_475, 0.125f, _455);
        outColor.w = _566 ? _454 : mad(_467.w - _454, 0.125f, _454);
    }
    else
    {
        outColor.x = _457;
        outColor.y = _456;
        outColor.z = _455;
        outColor.w = _454;
    }
    
  float2 uv = gl_FragCoord.xy * ScreenExtents.zw + ScreenExtents.xy;
  bool forceSDR = ShouldForceSDR(uv);
  if (!LumaSettings.GameSettings.HasColorGradingPass && !forceSDR) // Luma
  {
    outColor.rgb = gamma_to_linear(outColor.rgb, GCT_MIRROR);
    
    const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
    bool tonemapPerChannel = LumaSettings.DisplayMode != 1;
#if ENABLE_HIGHLIGHTS_DESATURATION_TYPE == 1 || ENABLE_HIGHLIGHTS_DESATURATION_TYPE >= 3
    tonemapPerChannel = true;
#endif
    if (LumaSettings.DisplayMode == 1)
    {
      DICESettings settings = DefaultDICESettings(tonemapPerChannel ? DICE_TYPE_BY_CHANNEL_PQ : DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
      outColor.rgb = DICETonemap(outColor.rgb * paperWhite, peakWhite, settings) / paperWhite;
    }
    else
    {
      if (tonemapPerChannel)
      {
        outColor.rgb = Reinhard::ReinhardRange(outColor.rgb, MidGray, -1.0, peakWhite / paperWhite, false);
      }
      else
      {
        outColor.rgb = RestoreLuminance(outColor.rgb, Reinhard::ReinhardRange(GetLuminance(outColor.rgb), MidGray, -1.0, peakWhite / paperWhite, false).x, true);
        outColor.rgb = CorrectOutOfRangeColor(outColor.rgb, true, true, 0.5, peakWhite / paperWhite);
      }
    }
  
#if UI_DRAW_TYPE == 2
    ColorGradingLUTTransferFunctionInOutCorrected(outColor.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true);
    outColor.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
    ColorGradingLUTTransferFunctionInOutCorrected(outColor.rgb, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, true);
#endif // UI_DRAW_TYPE == 2

    outColor.rgb = linear_to_gamma(outColor.rgb, GCT_MIRROR);
  }
  
  return outColor;
}