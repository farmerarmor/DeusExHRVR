#ifndef SRC_JZAZBZ_HLSL
#define SRC_JZAZBZ_HLSL

#include "Color.hlsl"

// -----------------------------------------------------------------------------
// Jzazbz
// 
// Reference:
// Muhammad Safdar, Guihua Cui, Youn Jin Kim, and Ming Ronnier Luo,
// "Perceptually uniform color space for image signals including high dynamic
// range and wide gamut," Opt. Express 25, 15131-15151 (2017)
// -----------------------------------------------------------------------------
namespace JzAzBz
{
    #define JZAZBZ_EXPONENT_SCALE_FACTOR 1.7f // Scale factor for exponent

    // Input: linear rgb with a paper white of ~100 nits (SDR neutral) (pre scale it if you want)
    // Jz Luminance-like
    // Az Red–Green opponent axis
    // Bz Blue–Yellow opponent axis
    float3 rgbToJzazbz(float3 rgb, uint colorSpace = CS_DEFAULT)
    {
        // The matrix below is for BT.2020 input.
        // The transforms should fold with the ones below when compiled.
        if (colorSpace == CS_BT709)
        {
            rgb = BT709_To_BT2020(rgb);
        }
        else if (colorSpace == CS_AP1)
        {
            rgb = float3(1, 0, 1); // Not done
        }

        float3 lms;
        lms.x = rgb[0] * 0.530004f + rgb[1] * 0.355704f + rgb[2] * 0.086090f;
        lms.y = rgb[0] * 0.289388f + rgb[1] * 0.525395f + rgb[2] * 0.157481f;
        lms.z = rgb[0] * 0.091098f + rgb[1] * 0.147588f + rgb[2] * 0.734234f;

        float3 lmsPQ = Linear_to_PQ(lms / (HDR10_MaxWhiteNits / Rec709_WhiteLevelNits), GCT_MIRROR, JZAZBZ_EXPONENT_SCALE_FACTOR); // Negative input values seem to work ok to prevent weird colors from breaking or clamping

        float iz = 0.5f * lmsPQ.x + 0.5f * lmsPQ.y;

        float3 jab;
        jab.x = (0.44f * iz) / (1.0f - 0.56f * iz) - 1.6295499532821566e-11f; // TODO: why is this added to be removed below? It's a tiny number
        jab.y = 3.524000f * lmsPQ.x - 4.066708f * lmsPQ.y + 0.542708f * lmsPQ.z;
        jab.z = 0.199076f * lmsPQ.x + 1.096799f * lmsPQ.y - 1.295875f * lmsPQ.z;
        return jab;
    }

    // Output: linear rgb
    float3 jzazbzToRgb(float3 jab, uint colorSpace = CS_DEFAULT)
    {
        float jz = jab[0] + 1.6295499532821566e-11f;
        float iz = jz / (0.44f + 0.56f * jz);
        float a  = jab[1];
        float b  = jab[2];

        float3 lms;
        lms.x = iz + a * 1.386050432715393e-1f + b * 5.804731615611869e-2f;
        lms.y = iz + a * -1.386050432715393e-1f + b * -5.804731615611869e-2f;
        lms.z = iz + a * -9.601924202631895e-2f + b * -8.118918960560390e-1f;

        float3 lmsLin = PQ_to_Linear(lms, GCT_MIRROR, JZAZBZ_EXPONENT_SCALE_FACTOR) * (HDR10_MaxWhiteNits / Rec709_WhiteLevelNits);

        float3 rgb;
        rgb.x = lmsLin.x * 2.990669f + lmsLin.y * -2.049742f + lmsLin.z * 0.088977f;
        rgb.y = lmsLin.x * -1.634525f + lmsLin.y * 3.145627f + lmsLin.z * -0.483037f;
        rgb.z = lmsLin.x * -0.042505f + lmsLin.y * -0.377983f + lmsLin.z * 1.448019f;
        if (colorSpace == CS_BT709)
        {
            rgb = BT2020_To_BT709(rgb);
        }
        return rgb;
    }
}

#endif // SRC_JZAZBZ_HLSL