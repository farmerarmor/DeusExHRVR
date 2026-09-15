#ifndef SRC_COLOR_GRADING_LUT_HLSL
#define SRC_COLOR_GRADING_LUT_HLSL

#include "Common.hlsl"
#include "Oklab.hlsl"
#include "DarktableUCS.hlsl"
#include "JzAzBz.hlsl"

//TODOFT: try basic extrapolation mode where we simply compress 0.5 to INF input to 0.5 to 1, do LUT and then decompress range again

// Make sure to define these to your value, or set it to 0, so it retrieves the size from the LUT (in some functions).
// The default is based on the common value for some old games but can be changed without consequences.
#ifndef LUT_SIZE
#define LUT_SIZE 16u
#endif
#ifndef LUT_MAX
#define LUT_MAX (LUT_SIZE - 1u)
#endif
#ifndef LUT_3D
#define LUT_3D 0
#endif
// 0 None
// 1 Neutral LUT
// 2 Neutral LUT + bypass extrapolation
#ifndef FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE
#define FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE 0
#endif
#ifndef TEST_LUT_EXTRAPOLATION
#define TEST_LUT_EXTRAPOLATION 0
#endif

#if LUT_3D
#define LUT_TEXTURE_TYPE Texture3D
#else
#define LUT_TEXTURE_TYPE Texture2D
#endif

// NOTE: it's possible to add more of these, like PQ or Log3.
// Note that these match "GAMMA_CORRECTION_TYPE" and "VANILLA_ENCODING_TYPE" for now.
#define LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB 0
#define LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2 1
#define LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB_WITH_GAMMA_2_2_BY_CHANNEL_CORRECTION_LUMINANCE 2
#define LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB_WITH_GAMMA_2_2_BY_LUMINANCE_CORRECTION_LUMINANCE 3
#define LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB_WITH_GAMMA_2_2_BY_LUMINANCE_CORRECTION_LUMINANCE_AND_GAMMA_2_2_BY_CHANNEL_CORRECTION_CHROMA 4
#define DEFAULT_LUT_EXTRAPOLATION_TRANSFER_FUNCTION LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2

// TODO: pick one
#if 0 // Oklab
#define LINEAR_TO_UCS(x, colorSpace) Oklab::rgb_to_oklab(x, colorSpace)
#define UCS_TO_LINEAR(x, colorSpace) Oklab::oklab_to_rgb(x, colorSpace)
#elif 1 // JzAzBz
#define LINEAR_TO_UCS(x, colorSpace) JzAzBz::rgbToJzazbz(x, colorSpace)
#define UCS_TO_LINEAR(x, colorSpace) JzAzBz::jzazbzToRgb(x, colorSpace)
#elif 1 // Darktable UCS // TODO: add color space support!!!
#define LINEAR_TO_UCS(x, colorSpace) DarktableUcs::RGBToUCSLUV(x)
#define UCS_TO_LINEAR(x, colorSpace) DarktableUcs::UCSLUVToRGB(x)
#elif 1 // Hellwig/Fairchild
#define LINEAR_TO_UCS(x, colorSpace) HellwigFairchild::rgb_to_ucs(x)
#define UCS_TO_LINEAR(x, colorSpace) HellwigFairchild::ucs_to_rgb(x)
#endif

#if LUT_3D
uint4
#else
uint3
#endif
ConditionalConvert3DTo2DLUTCoordinates(uint3 Coordinates3D, uint3 lutSize = LUT_SIZE)
{
#if LUT_3D
  return uint4(Coordinates3D, 0);
#else
  return uint3(Coordinates3D.x + (Coordinates3D.z * lutSize.y), Coordinates3D.y, 0); // Horizontal unwrapping
#endif
}

// WIP (rename if ever...)
#ifndef HIGH_QUALITY_ENCODING_TYPE
#define HIGH_QUALITY_ENCODING_TYPE 1
#endif

//TODOFT: try basic extrapolation mode where we simply compress 0.5 to INF input to 0.5 to 1, do LUT and then decompress range again
//TODO: invert direction of extrapolation below 0?
//TODO: if the input color has 0 chroma (1 1 1, or 2 2 2, etc), don't extrapolate on the grey axis and just scale the original LUT white color?
//TODOFT5: use Log instead of PQ? It's actually not making much difference
//TODOFT5: Do extrapolation in another color space? Does that even make sense? Maybe in linear it'd actually be best to avoid hue shifts.
float3 Linear_to_PQ2(float3 LinearColor, int clampType = GCT_DEFAULT)
{
#if HIGH_QUALITY_ENCODING_TYPE == 0
	return LinearColor;
#elif HIGH_QUALITY_ENCODING_TYPE == 1
	return Linear_to_PQ(LinearColor, clampType);
#else // HIGH_QUALITY_ENCODING_TYPE >= 2
	return linearToLog(LinearColor, clampType);
#endif
}
float3 PQ_to_Linear2(float3 ST2084Color, int clampType = GCT_DEFAULT)
{
#if HIGH_QUALITY_ENCODING_TYPE == 0
	return ST2084Color;
#elif HIGH_QUALITY_ENCODING_TYPE == 1
	return PQ_to_Linear(ST2084Color, clampType);
#else // HIGH_QUALITY_ENCODING_TYPE >= 2
	return logToLinear(ST2084Color, clampType);
#endif
}

// 0 None
// 1 Reduce saturation and increase brightness until luminance is >= 0 (~gamut mapping)
// 2 Clip negative colors (makes luminance >= 0)
// 3 Snap to black
void FixColorGradingLUTNegativeLuminance(inout float3 col, uint type = 1, uint colorSpace = CS_DEFAULT)
{
  if (type <= 0) { return; }

  float luminance = GetLuminance(col.xyz, colorSpace);
  if (luminance < -FLT_MIN)
  {
    if (type == 1)
    {
      // Make the color more "SDR" (less saturated, and thus less beyond Rec.709) until the luminance is not negative anymore (negative luminance means the color was beyond Rec.709 to begin with, unless all components were negative).
      // This is preferrable to simply clipping all negative colors or snapping to black, because it keeps some HDR colors, even if overall it's still "black", luminance wise.
      // This should work even in case "positiveLuminance" was <= 0, as it will simply make the color black.
      float3 positiveColor = max(col.xyz, 0.0);
      float3 negativeColor = min(col.xyz, 0.0);
      float positiveLuminance = GetLuminance(positiveColor, colorSpace);
      float negativeLuminance = GetLuminance(negativeColor, colorSpace);
#pragma warning( disable : 4008 )
      float negativePositiveLuminanceRatio = positiveLuminance / -negativeLuminance;
#pragma warning( default : 4008 )
      negativeColor.xyz *= negativePositiveLuminanceRatio;
      col.xyz = positiveColor + negativeColor;
      
#if 0 // Check again for extra safety (not needed until proven otherwise)
      luminance = GetLuminance(col.xyz, colorSpace);
      if (luminance < 0.0)
      {
        col.xyz = max(col.xyz, 0.0);
      }
#endif
    }
    else if (type == 2)
    {
      // This can break gradients as it snaps colors to brighter ones (it depends on how the displays clips HDR10 or scRGB invalid colors)
      col.xyz = max(col.xyz, 0.0);
    }
    else //if (type >= 3)
    {
      col.xyz = 0.0;
    }
  }
}

// Restores the source color hue (and optionally brightness) through Oklab (this works on colors beyond SDR in brightness and gamut too).
// The strength sweet spot for a strong hue restoration seems to be 0.75, while for chrominance, going up to 1 is ok.
float3 RestoreHueAndChrominance(float3 targetColor, float3 sourceColor, float hueStrength = 0.75, float chrominanceStrength = 1.0, float minChrominanceChange = 0.0, float maxChrominanceChange = FLT_MAX, float lightnessStrength = 0.0, uint colorSpace = CS_DEFAULT)
{
  if (colorSpace == CS_AP1)
    return float3(1, 0, 1); // Unsupported (return purple)
	if (hueStrength == 0.0 && chrominanceStrength == 0.0 && lightnessStrength == 0.0) // Static optimization (useful if the param is const)
		return targetColor;

  // Invalid or black colors fail oklab conversions or ab blending so early out
  if (GetLuminance(targetColor, colorSpace) <= FLT_MIN)
    return targetColor; // Optionally we could blend the target towards the source, or towards black, but there's no need until proven otherwise

	const float3 sourceUcsLab = LINEAR_TO_UCS(sourceColor, colorSpace);
	float3 targetUcsLab = LINEAR_TO_UCS(targetColor, colorSpace);
   
  targetUcsLab.x = lerp(targetUcsLab.x, sourceUcsLab.x, lightnessStrength);
  
	float currentChrominance = length(targetUcsLab.yz);

  if (hueStrength != 0.0)
  {
    // First correct both hue and chrominance at the same time (oklab a and b determine both, they are the color xy coordinates basically).
    // As long as we don't restore the hue to a 100% (which should be avoided?), this will always work perfectly even if the source color is pure white (or black, any "hueless" and "chromaless" color).
    // This method also works on white source colors because the center of the oklab ab diagram is a "white hue", thus we'd simply blend towards white (but never flipping beyond it (e.g. from positive to negative coordinates)),
    // and then restore the original chrominance later (white still conserving the original hue direction, so likely spitting out the same color as the original, or one very close to it).
    const float chrominancePre = currentChrominance;
    targetUcsLab.yz = lerp(targetUcsLab.yz, sourceUcsLab.yz, hueStrength);
    const float chrominancePost = length(targetUcsLab.yz);
    // Then restore chrominance to the original one
    float chrominanceRatio = safeDivision(chrominancePre, chrominancePost, 1);
    targetUcsLab.yz *= chrominanceRatio;
    //currentChrominance = chrominancePre; // Redundant
  }

  if (chrominanceStrength != 0.0)
  {
    const float sourceChrominance = length(sourceUcsLab.yz);
    // Scale original chroma vector from 1.0 to ratio of target to new chroma
    // Note that this might either reduce or increase the chroma.
    float targetChrominanceRatio = safeDivision(sourceChrominance, currentChrominance, 1);
    // Optional safe boundaries (0.333x to 2x is a decent range)
    targetChrominanceRatio = clamp(targetChrominanceRatio, minChrominanceChange, maxChrominanceChange);
    targetUcsLab.yz *= lerp(1.0, targetChrominanceRatio, chrominanceStrength);
  }

	return UCS_TO_LINEAR(targetUcsLab, colorSpace);
}

// Not 100% hue conservering but better than just max(color, 0.f), this maps the color on the closest humanly visible xy location on th CIE graph.
// This doesn't break gradients. The color luminance is not considered, so invalid luminances still get gamut mapped through the same math.
// Supports either BT.2020 or BT.709 (sRGB/scRGB) clamping (input and output need to be in the same color space). Hardcoded for D65 white point.
// Mostly from Lilium.
float3 SimpleGamutClip(float3 Color, bool BT2020, bool ClampToSDRRange = false)
{
	const bool3 isNegative = Color < 0.f;
	const bool allArePositive = !any(isNegative);
	const bool allAreNegative = all(isNegative);

	if (allArePositive)
	{
	}
	// Clip to black as the hue of an all negative color is invalid
	else if (allAreNegative)
	{
		return 0.f;
	}
	else
	{
		float3 XYZ = mul(BT2020 ? BT2020_To_XYZ : BT709_To_XYZ, Color);
		float3 xyY = XYZToxyY(XYZ);
		float m = GetM(xyY.xy, D65xy);
		const float2 Rxy = BT2020 ? R2020xy : R709xy;
		const float2 Gxy = BT2020 ? G2020xy : G709xy;
		const float2 Bxy = BT2020 ? B2020xy : B709xy;

		float2 gamutClippedXY;
		// we can determine on which side we need to do the intercept based on where the negative number/s is/are
		// the intercept needs to happen on the opposite side of where the primary of the smallest negative number is
		// with 2 negative numbers the smaller one determines the side to check
		if (all(isNegative.rg))
		{
			if (Color.r <= Color.g)
				gamutClippedXY = LineIntercept(m, Gxy, Bxy); // GB
			else
				gamutClippedXY = LineIntercept(m, Bxy, Rxy); // BR
		}
		else if (all(isNegative.rb))
		{
			if (Color.r <= Color.b)
				gamutClippedXY = LineIntercept(m, Gxy, Bxy); // GB
			else
				gamutClippedXY = LineIntercept(m, Rxy, Gxy); // RG
		}
		else if (all(isNegative.gb))
		{
			if (Color.g <= Color.b)
				gamutClippedXY = LineIntercept(m, Bxy, Rxy); // BR
			else
				gamutClippedXY = LineIntercept(m, Rxy, Gxy); // RG
		}
		else if (isNegative.r)
			gamutClippedXY = LineIntercept(m, Gxy, Bxy); // GB
		else if (isNegative.g)
			gamutClippedXY = LineIntercept(m, Bxy, Rxy); // BR
		else //if (isNegative.b)
			gamutClippedXY = LineIntercept(m, Rxy, Gxy); // RG

		float3 gamutClippedXYZ = xyYToXYZ(float3(gamutClippedXY, xyY.z)); // Maintains the old luminance
		Color = mul(BT2020 ? XYZ_To_BT2020 : XYZ_To_BT709, gamutClippedXYZ);
	}
	// Reduce brightness instead of reducing saturation
	if (ClampToSDRRange)
	{
		const float maxChannel = max(1.f, max(Color.r, max(Color.g, Color.b)));
		Color /= maxChannel;
	}
	return Color;
}

// This expands the LUT min/max range to avoid raised blacks and clipped highlights, while still being able to keep the shadow color tint,
// it helps a lot with HDR/OLED for LUTs that were made on displays that didn't have deep blacks.
float3 NormalizeLUT(float3 vOriginalGamma, float3 vBlackGamma, float3 vMidGrayGamma, float3 vWhiteGamma, float3 vNeutralGamma)
{
	const float3 vAddedGamma = max(vBlackGamma, 0.f);
	const float3 vRemovedGamma = 1.f - min(1.f, vWhiteGamma);

	const float fMidGrayAverage = (vMidGrayGamma.r + vMidGrayGamma.g + vMidGrayGamma.b) / 3.f;

   // Remove from 0 to mid gray
	const float fShadowLength = fMidGrayAverage;
	const float fShadowStop = max(vNeutralGamma.r, max(vNeutralGamma.g, vNeutralGamma.b));
	const float3 vFloorRemove = vAddedGamma * max(0, fShadowLength - fShadowStop) / (fShadowLength != 0.f ? fShadowLength : 1.f);

   // Add back from mid gray to 1
	const float fHighlightsLength = 1.f - fMidGrayAverage;
	const float fHighlightsStop = 1.f - min(vNeutralGamma.r, min(vNeutralGamma.g, vNeutralGamma.b));
	const float3 vCeilingAdd = vRemovedGamma * max(0, fHighlightsLength - fHighlightsStop) / (fHighlightsLength != 0.f ? fHighlightsLength : 1.f);

	const float3 vUnclampedGamma = max(min(vOriginalGamma, 0.f), vOriginalGamma - vFloorRemove) + vCeilingAdd;
	return vUnclampedGamma;
}

// Takes any original color (before some post process is applied to it) and re-applies the same transformation the post process had applied to it on a different (but similar) color.
// The images are expected to have roughly the same mid gray.
// It can be used for example to apply any SDR LUT or SDR color correction on an HDR color.
// The "BT.2020" flag means it will convert to that from "BT.709"
float3 RestorePostProcess(float3 nonPostProcessedTargetColor, float3 nonPostProcessedSourceColor, float3 postProcessedSourceColor, float hueRestoration = 0.0, bool BT2020 = true)
{
  static const float MaxShadowsColor = pow(1.f / 3.f, DefaultGamma); // The lower this value, the more "accurate" is the restoration (math wise), but also more error prone (e.g. division by zero). If the color range is wider than the original one, the higher this value is, the further it will extend, due to working by offset near black (and thus generating negative rgb values).

	// Optionally convert to BT.2020 to allow more saturated shadow to be generated (BT.709 will reach the edges of the gamut and clip on shadow).
  // We could do this in AP0 gamut but that'd probably generated too many unsupported colors.
  // This should actually distort colors less.
	if (BT2020)
	{
		nonPostProcessedTargetColor = BT709_To_BT2020(nonPostProcessedTargetColor);
		nonPostProcessedSourceColor = BT709_To_BT2020(nonPostProcessedSourceColor);
		postProcessedSourceColor = BT709_To_BT2020(postProcessedSourceColor);
	}

	const float3 postProcessColorRatio = safeDivision(postProcessedSourceColor, nonPostProcessedSourceColor, 1);
	const float3 postProcessColorOffset = postProcessedSourceColor - nonPostProcessedSourceColor;
	const float3 postProcessedRatioColor = nonPostProcessedTargetColor * postProcessColorRatio;
	const float3 postProcessedOffsetColor = nonPostProcessedTargetColor + postProcessColorOffset;
	// Near black, we prefer using the "offset" (sum) pp restoration method, as otherwise any raised black would not work,
	// for example if any zero was shifted to a more raised color, "postProcessColorRatio" would not be able to replicate that shift due to a division by zero.
	float3 newPostProcessedColor = lerp(postProcessedOffsetColor, postProcessedRatioColor, max(saturate(abs(nonPostProcessedTargetColor / MaxShadowsColor)), saturate(abs(nonPostProcessedSourceColor / MaxShadowsColor))));

	// Force keep the original post processed color hue.
  // This often ends up shifting the hue too much, either looking too desaturated or too saturated, mostly because in SDR highlights are all burned to white by LUTs, and by the Vanilla SDR tonemappers.
	if (hueRestoration > 0)
	{
		newPostProcessedColor = RestoreHueAndChrominance(newPostProcessedColor, postProcessedSourceColor, hueRestoration, 0.0, 0.0, FLT_MAX, 0.0, BT2020 ? CS_BT2020 : CS_DEFAULT);
	}
  
	if (BT2020)
	{
		newPostProcessedColor = BT2020_To_BT709(newPostProcessedColor);
	}

	return newPostProcessedColor;
}

// This fixes the luminance of linear lerps not being perceptual (but it leaves the original hue, which are a bit randomly shifted if you look at it perceptually)
float4 PerceptualLerp(float4 colorA, float4 colorB, float alpha, float gamma = DefaultGamma)
{
	float luminanceA = GetLuminance(colorA.rgb);
	float luminanceB = GetLuminance(colorB.rgb);
   // We use gamma 2.2 or so just because it's standard, though it's likely not the best here
	float gammaLuminanceA = pow(max(luminanceA, 0.f), 1.0 / gamma); // Linear->Gamma
	float gammaLuminanceB = pow(max(luminanceB, 0.f), 1.0 / gamma); // Linear->Gamma
	float targetLuminance = pow(lerp(gammaLuminanceA, gammaLuminanceB, alpha), gamma); // Blend in gamma space

	float4 colorLerped = lerp(colorA, colorB, alpha);
	float sourceLuminance = GetLuminance(colorLerped.rgb);
	// Restore the target luminance without hue shifts
	if (sourceLuminance != 0.f)
	{
		colorLerped.rgb *= max(targetLuminance / sourceLuminance, 0.f); // If any of the two luminance is negative, clip to black
	}
	return colorLerped;
}

//TODOFT: move these to generic places
// Encode.
// Set "mirrored" to true in case the input can have negative values,
// otherwise we run the non mirrored version that is more optimized but might have worse or broken results.
float3 ColorGradingLUTTransferFunctionIn(float3 col, uint transferFunction, bool mirrored = true)
{
  if (transferFunction == LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB)
  {
    return linear_to_sRGB_gamma(col, mirrored ? GCT_MIRROR : GCT_NONE);
  }
  else if (transferFunction == LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2)
  {
    return linear_to_gamma(col, mirrored ? GCT_MIRROR : GCT_NONE);
  }
  // Note that this isn't completely mirrored with "ColorGradingLUTTransferFunctionOut" and thus is "lossy"
  else if (transferFunction == LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB_WITH_GAMMA_2_2_BY_CHANNEL_CORRECTION_LUMINANCE)
  {
    float3 gammaCorrectedColor = gamma_sRGB_to_linear(linear_to_gamma(col, mirrored ? GCT_MIRROR : GCT_NONE), mirrored ? GCT_MIRROR : GCT_NONE);
    return linear_to_sRGB_gamma(RestoreLuminance(col, gammaCorrectedColor), mirrored ? GCT_MIRROR : GCT_NONE);
  }
  else if (transferFunction == LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB_WITH_GAMMA_2_2_BY_LUMINANCE_CORRECTION_LUMINANCE)
  {
    return float3(0.5, 0, 0.5); // Unsupported (return purple)
  }
  else //if (transferFunction == LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB_WITH_GAMMA_2_2_BY_LUMINANCE_CORRECTION_LUMINANCE_AND_GAMMA_2_2_BY_CHANNEL_CORRECTION_CHROMA)
  {
    return float3(0.5, 0, 0.5); // Unsupported (return purple)
  }
}
// Decode.
float3 ColorGradingLUTTransferFunctionOut(float3 col, uint transferFunction, bool mirrored = true)
{
	float3 rawColor = gamma_sRGB_to_linear(col, mirrored ? GCT_MIRROR : GCT_NONE); // sRGB color (not necessarily "raw")
	float3 colorGammaCorrectedByChannel = gamma_to_linear(col, mirrored ? GCT_MIRROR : GCT_NONE); // 2.2 color (not necessarily "gamma corrected")
	float luminanceGammaCorrected = gamma_to_linear(linear_to_sRGB_gamma(GetLuminance(rawColor), GCT_POSITIVE).x, GCT_POSITIVE).x;
	float3 colorGammaCorrectedByLuminance = RestoreLuminance(rawColor, luminanceGammaCorrected);
  if (transferFunction == LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB)
  {
    return rawColor;
  }
  else if (transferFunction == LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2)
  {
    return colorGammaCorrectedByChannel;
  }
  else if (transferFunction == LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB_WITH_GAMMA_2_2_BY_CHANNEL_CORRECTION_LUMINANCE)
  {
    return RestoreLuminance(rawColor, colorGammaCorrectedByChannel);
  }
  else if (transferFunction == LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB_WITH_GAMMA_2_2_BY_LUMINANCE_CORRECTION_LUMINANCE)
  {
    return colorGammaCorrectedByLuminance;
  }
  else //if (transferFunction == LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB_WITH_GAMMA_2_2_BY_LUMINANCE_CORRECTION_LUMINANCE_AND_GAMMA_2_2_BY_CHANNEL_CORRECTION_CHROMA)
  {
    return RestoreHueAndChrominance(colorGammaCorrectedByLuminance, colorGammaCorrectedByChannel, 0.0, 1.0);
  }
  return 0; // Possibly avoids warnings
}

// Use the LUT input transfer function within 0-1 and the LUT output transfer function beyond 0-1 (e.g. sRGB to gamma 2.2),
// this is because LUTs are baked with a gamma mismatch, but for extrapolation, we might only want to replicate the gamma mismatch within the 0-1 range.
float3 ColorGradingLUTTransferFunctionInCorrected(float3 col, uint transferFunctionIn, uint transferFunctionOut)
{
  if (transferFunctionIn != transferFunctionOut)
  {
    float3 reEncodedColor = ColorGradingLUTTransferFunctionIn(col, transferFunctionOut, true);
    float3 colorInExcess = reEncodedColor - saturate(reEncodedColor);
    return ColorGradingLUTTransferFunctionIn(saturate(col), transferFunctionIn, false) + colorInExcess;
  }
  return ColorGradingLUTTransferFunctionIn(col, transferFunctionIn, true);
}

// This perfectly mirrors "ColorGradingLUTTransferFunctionInCorrected()" (e.g. running this after that results in the original color).
float3 ColorGradingLUTTransferFunctionInCorrectedInverted(float3 col, uint transferFunctionIn, uint transferFunctionOut)
{
  if (transferFunctionIn != transferFunctionOut)
  {
    float3 reEncodedColor = ColorGradingLUTTransferFunctionOut(col, transferFunctionOut, true);
    float3 colorInExcess = reEncodedColor - saturate(reEncodedColor);
    return ColorGradingLUTTransferFunctionOut(saturate(col), transferFunctionIn, false) + colorInExcess;
  }
  return ColorGradingLUTTransferFunctionOut(col, transferFunctionIn, true);
}

// Use the LUT output transfer function within 0-1 and the LUT input transfer function beyond 0-1 (e.g. gamma 2.2 to sRGB),
// this is because LUTs are baked with a gamma mismatch, but we only want to replicate the gamma mismatch within the 0-1 range.
float3 ColorGradingLUTTransferFunctionOutCorrected(float3 col, uint transferFunctionIn, uint transferFunctionOut)
{
  if (transferFunctionIn != transferFunctionOut)
  {
    float3 reEncodedColor = ColorGradingLUTTransferFunctionOut(col, transferFunctionIn, true);
    float3 colorInExcess = reEncodedColor - saturate(reEncodedColor);
    return ColorGradingLUTTransferFunctionOut(saturate(col), transferFunctionOut, false) + colorInExcess;
  }
  return ColorGradingLUTTransferFunctionOut(col, transferFunctionOut, true);
}

// Optimized merged version of "ColorGradingLUTTransferFunctionInCorrected" and "ColorGradingLUTTransferFunctionOutCorrected".
// If "linearTolinear" is true, we assume linear in and out. Otherwise, we assume the input was encoded with "transferFunctionIn" and encode the output with "transferFunctionOut".
void ColorGradingLUTTransferFunctionInOutCorrected(inout float3 col, uint transferFunctionIn, uint transferFunctionOut, bool linearTolinear)
{
    if (transferFunctionIn != transferFunctionOut)
    {
      if (linearTolinear)
      {
        // E.g. decoding sRGB gamma with gamma 2.2 crushes blacks (which is what we want).
  #if 1 // Equivalent branches (this is the most optimized and most accurate)
        float3 colInExcess = col - saturate(col);
        col = ColorGradingLUTTransferFunctionOut(ColorGradingLUTTransferFunctionIn(saturate(col), transferFunctionIn, false), transferFunctionOut, false);
        col += colInExcess;
  #elif 1
        col = ColorGradingLUTTransferFunctionOutCorrected(ColorGradingLUTTransferFunctionIn(col, transferFunctionIn, true), transferFunctionIn, transferFunctionOut);
  #else
        col = ColorGradingLUTTransferFunctionOut(ColorGradingLUTTransferFunctionInCorrected(col, transferFunctionIn, transferFunctionOut), transferFunctionOut, true);
  #endif
      }
      else
      {
        // E.g. encoding "linear sRGB" with gamma 2.2 raises blacks (which is the opposite of what we want), so we do the opposite (encode "linear 2.2" with sRGB gamma).
  #if 1 // Equivalent branches (this is the most optimized and most accurate)
        float3 colInExcess = col - saturate(col);
        col = ColorGradingLUTTransferFunctionIn(ColorGradingLUTTransferFunctionOut(saturate(col), transferFunctionOut, false), transferFunctionIn, false);
        col += colInExcess;
  #elif 1
        col = ColorGradingLUTTransferFunctionIn(ColorGradingLUTTransferFunctionOutCorrected(col, transferFunctionIn, transferFunctionOut), transferFunctionIn, true);
  #else
        col = ColorGradingLUTTransferFunctionInCorrected(ColorGradingLUTTransferFunctionOut(col, transferFunctionOut, true), transferFunctionIn, transferFunctionOut);
  #endif
      }
    }
}

// Corrects transfer function encoded LUT coordinates to return more accurate LUT colors for LUTs that have an encoded input but linear output (or all other combinations).
// Assuming a 2 pixels 1D LUT, the first point would map 0 to 0 and the second point 1 to 1.
// Now, if the LUT was meant to be sampled in gamma 2.2 space (as it should, otherwise the point distribution would all be focused on highlights),
// and we sampled 0.5 from the LUT, ideally we'd get 0.5 in output too, but if the LUT output was linear space (e.g. UNORM_SRGB, or FLOAT textures),
// we'd then expect the result to be roughly 0.218 (0.5^2.2, similar to mid grey), but instead we'd get 0.5, which isn't correct, as it's much brighter than expected.
// This contributes to creating banding and generally broken greadients (incontiguous steps).
// This formula skewes the input coordinates in a way that forces the output to return 0.218, without any downsides.
// For LUTs that use the same encoding and decoding formula, then the contiguity error from interpolated mid points isn't really problematic, unless the encoding curve suddenly changed between two LUT points (which usually isn't the case), hence a basic blend is pretty accurate already.
// Note that in cases the LUT inverted colors (e.g. mapping 0 to 1 and 1 to 0) this code actually makes the output less accurate, however, it's an acceptable consequence, which almost never arises and can be avoided in other ways (by branching out flipped LUTs).
// For cases where the LUT is also very strong in intensity, this also doesn't help as much as weaker luts, however, it will still skew the output towards more accurate results.
// 
// It can also be used with tetrahedral interpolation.
// This expects input coordinates within the 0-1 range (prior to acknowledging the half texel offset of LUTs, which should be applied after, just before actually sampling the LUT) (it should not be used to find the extrapolated (out of range) coordinates, but only on valid LUT coordinates).
float3 AdjustLUTCoordinatesForLinearLUT(const float3 clampedLUTCoordinatesGammaSpace, bool highQuality = true, uint lutTransferFunctionIn = DEFAULT_LUT_EXTRAPOLATION_TRANSFER_FUNCTION, bool lutInputLinear = false, bool lutOutputLinear = false, const float3 lutSize = LUT_SIZE, bool specifyLinearSpaceLUTCoordinates = false, float3 clampedLUTCoordinatesLinearSpace = 0)
{
	if (!specifyLinearSpaceLUTCoordinates)
	{
    clampedLUTCoordinatesLinearSpace = ColorGradingLUTTransferFunctionOut(clampedLUTCoordinatesGammaSpace, lutTransferFunctionIn, false);
	}
  if (lutInputLinear)
  {
#if FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE > 0
    if (highQuality && !lutOutputLinear)
    {
      // The "!lutOutputLinear" case would need coordinate adjustments to sample properly, but "linear in gamma out" LUTs don't really exist as they make no sense so we don't account for that case
    }
#endif
    return clampedLUTCoordinatesLinearSpace;
  }
	if (!lutOutputLinear || !highQuality)
	{
		return clampedLUTCoordinatesGammaSpace;
	}
	//if (!lutInputLinear && lutOutputLinear)
#if FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE > 0 // This case will skip LUT sampling so we shouldn't correct the input coordinates
  // Low quality version with no linear input correction
  return clampedLUTCoordinatesGammaSpace;
#else
//TODOFT4: when this branch runs, there's some speckles on the shotgun numerical decal in some scenes (e.g. when under a light, in front of the place where I tested AF on a decal a lot) (with DLSS they turn into black dots in the albedo view). Would this happen without "dev" settings enabled!? Probably not!
  float3 lutMax = lutSize - 1.0;
  float3 previousLUTCoordinatesGammaSpace = floor(clampedLUTCoordinatesGammaSpace * lutMax) / lutMax;
  float3 nextLUTCoordinatesGammaSpace = ceil(clampedLUTCoordinatesGammaSpace * lutMax) / lutMax;
  float3 previousLUTCoordinatesLinearSpace = ColorGradingLUTTransferFunctionOut(previousLUTCoordinatesGammaSpace, lutTransferFunctionIn, false);
  float3 nextLUTCoordinatesLinearSpace = ColorGradingLUTTransferFunctionOut(nextLUTCoordinatesGammaSpace, lutTransferFunctionIn, false);
  // Every step size is different as it depends on where we are within the transfer function range.
  const float3 stepSize = nextLUTCoordinatesLinearSpace - previousLUTCoordinatesLinearSpace;
  // If "stepSize" is zero (due to the LUT pixel coords being exactly an integer), whether alpha is zero or one won't matter as "previousLUTCoordinatesGammaSpace" and "nextLUTCoordinatesGammaSpace" will be identical.
  const float3 blendAlpha = safeDivision(clampedLUTCoordinatesLinearSpace - previousLUTCoordinatesLinearSpace, stepSize, 1);
  return lerp(previousLUTCoordinatesGammaSpace, nextLUTCoordinatesGammaSpace, blendAlpha);
#endif
}

// Color grading/charts tex lookup. Called "TexColorChart2D()" in vanilla CryEngine code.
float3 SampleLUT(LUT_TEXTURE_TYPE lut, SamplerState samplerState, float3 color, uint3 lutSize = LUT_SIZE, bool tetrahedralInterpolation = false, bool debugLutInputLinear = false, bool debugLutOutputLinear = false, uint debugLutTransferFunctionIn = DEFAULT_LUT_EXTRAPOLATION_TRANSFER_FUNCTION)
{
#if FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE > 0
  // Do not saturate() "color" on purpose
  if (debugLutInputLinear == debugLutOutputLinear)
  {
    return color;
  }
  return debugLutOutputLinear ? ColorGradingLUTTransferFunctionOut(color, debugLutTransferFunctionIn) : ColorGradingLUTTransferFunctionIn(color, debugLutTransferFunctionIn);
#endif // FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE > 0

	const uint3 chartDimUint = lutSize;
	const float3 chartDim	= (float3)chartDimUint;
	const float3 chartDimSqr	= chartDim * chartDim;
	const float3 chartMax	= chartDim - 1.0;
	const uint3 chartMaxUint = chartDimUint - 1u;
  
  if (!tetrahedralInterpolation)
  {
#if LUT_3D
    const float3 scale = chartMax / chartDim;
    const float3 bias = 0.5 / chartDim;
    
    float3 lookup = saturate(color) * scale + bias;
    
    return lut.Sample(samplerState, lookup).rgb;
#else // !LUT_3D
    const float3 scale = chartMax / chartDim;
    const float3 bias = float3(0.5, 0.5, 0.0) / chartDim;

    float3 lookup = saturate(color) * scale + bias;
    
    // convert input color into 2d color chart lookup address
    float slice = lookup.z * chartDim.y;
    float sliceFrac = frac(slice);	
    float sliceIdx = slice - sliceFrac;
    
    lookup.x = (lookup.x + sliceIdx) / chartDim.y;
    
    // lookup adjacent slices
    float3 col0 = lut.Sample(samplerState, lookup.xy).rgb;
    lookup.x += 1.0 / chartDim.y; // Horizontal unwrapping
    float3 col1 = lut.Sample(samplerState, lookup.xy).rgb;

    // linearly blend between slices
    return lerp(col0, col1, sliceFrac); // LUMA FT: changed to be a lerp (easier to read)
#endif // LUT_3D
  }
  else // LUMA FT: added tetrahedral LUT interpolation (from Lilium) (note that this ignores the texture sampler) // TODO: fix, it doesn't work in 3D LUTs
  {
    // We need to clip the input coordinates as LUT texture samples below are not clamped.
    const float3 coords = saturate(color) * chartMax; // Pixel coords 

    // floorCoords are on [0,chartMaxUint]
    uint3 floorBaseCoords = coords;
    uint3 floorNextCoords = min(floorBaseCoords + 1u, chartMaxUint);
    
    // baseInd and nextInd are on [0,1]
    uint3 baseInd = floorBaseCoords;
    uint3 nextInd = floorNextCoords;

    // indV2 and indV3 are on [0,chartMaxUint]
    uint3 indV2;
    uint3 indV3;

    // fract is on [0,1]
    float3 fract = frac(coords);

    const float3 v1 = lut.Load(ConditionalConvert3DTo2DLUTCoordinates(baseInd, chartDimUint)).rgb;
    const float3 v4 = lut.Load(ConditionalConvert3DTo2DLUTCoordinates(nextInd, chartDimUint)).rgb;

    float f1, f2, f3, f4;

    [flatten]
    if (fract.r >= fract.g)
    {
      [flatten]
      if (fract.g >= fract.b)  // R > G > B
      {
        indV2 = uint3(1u, 0u, 0u);
        indV3 = uint3(1u, 1u, 0u);

        f1 = 1u - fract.r;
        f4 = fract.b;

        f2 = fract.r - fract.g;
        f3 = fract.g - fract.b;
      }
      else [flatten] if (fract.r >= fract.b)  // R > B > G
      {
        indV2 = uint3(1u, 0u, 0u);
        indV3 = uint3(1u, 0u, 1u);

        f1 = 1u - fract.r;
        f4 = fract.g;

        f2 = fract.r - fract.b;
        f3 = fract.b - fract.g;
      }
      else  // B > R > G
      {
        indV2 = uint3(0u, 0u, 1u);
        indV3 = uint3(1u, 0u, 1u);

        f1 = 1u - fract.b;
        f4 = fract.g;

        f2 = fract.b - fract.r;
        f3 = fract.r - fract.g;
      }
    }
    else
    {
      [flatten]
      if (fract.g <= fract.b)  // B > G > R
      {
        indV2 = uint3(0u, 0u, 1u);
        indV3 = uint3(0u, 1u, 1u);

        f1 = 1u - fract.b;
        f4 = fract.r;

        f2 = fract.b - fract.g;
        f3 = fract.g - fract.r;
      }
      else [flatten] if (fract.r >= fract.b)  // G > R > B
      {
        indV2 = uint3(0u, 1u, 0u);
        indV3 = uint3(1u, 1u, 0u);

        f1 = 1u - fract.g;
        f4 = fract.b;

        f2 = fract.g - fract.r;
        f3 = fract.r - fract.b;
      }
      else  // G > B > R
      {
        indV2 = uint3(0u, 1u, 0u);
        indV3 = uint3(0u, 1u, 1u);

        f1 = 1u - fract.g;
        f4 = fract.r;

        f2 = fract.g - fract.b;
        f3 = fract.b - fract.r;
      }
    }

    indV2 = min(floorBaseCoords + indV2, chartMaxUint);
    indV3 = min(floorBaseCoords + indV3, chartMaxUint);

    const float3 v2 = lut.Load(ConditionalConvert3DTo2DLUTCoordinates(indV2, chartDimUint)).rgb;
    const float3 v3 = lut.Load(ConditionalConvert3DTo2DLUTCoordinates(indV3, chartDimUint)).rgb;

    return (f1 * v1) + (f2 * v2) + (f3 * v3) + (f4 * v4);
  }
}

struct LUTExtrapolationData
{
  // The "HDR" color before or after tonemapping to the display capabilities (preferably before, to have more consistent results), it needs to be in the same range as the vanilla color (0.18 as mid gray), with values beyond 1 being out of vanila range (e.g. HDR as opposed to SDR).
  // In other words, this is the LUT input coordinate (once converted the LUT input transfer function).
  // Note that this can be in any color space (e.g. sRGB, scRGB, Rec.709, Rec.2020, ...), it's agnostic from that.
  float3 inputColor;
  
  // The vanilla color the game would have fed as LUT input (so usually after tonemapping, and SDR), it should roughly be in the 0-1 range (you can optionally manually saturate() this to make sure of that).
  // This is optional and only used if "vanillaLUTRestorationAmount" is > 0.
  float3 vanillaInputColor;
};

struct LUTExtrapolationSettings
{
  // Set to 0 to find it automatically
#if LUT_3D
  uint3 lutSize;
#else // We currently expect unwrapped LUTs to have the same size on every axis in their original 3D form
  uint lutSize;
#endif
  // Is the input color we pass in linear or encoded with a transfer function?
  // If false, the color is expectred to the in the "transferFunctionIn" space.
  bool inputLinear;
  // Does the LUT take linear or transfer function encoded input coordinates/colors?
  bool lutInputLinear;
  // Does the LUT output linear or transfer function encoded colors?
  bool lutOutputLinear;
  // Do we expect this function to output linear or transfer function encoded colors?
  bool outputLinear;
  // What transfer function the LUT used for its input coordinates, if it wasn't linear ("lutInputLinear" false)?
  // Note that this might still be used even if the LUT is linear in input, because the extrapolation logic needs to happen in perceptual space.
  uint transferFunctionIn;
  // What transfer function the LUT used for its output colors, if it wasn't linear ("lutOutputLinear" false)?
  // Note that if this is different from "transferFunctionIn", it doesn't mean that the LUT also directly applies a gamma mismatch within its colors (e.g. for an input of 0.1 it would could still return 0.1),
  // but that the LUT output color was intended to be visualized on a display that used this transfer function.
  // Leave this equal to "transferFunctionIn" if you want to completely ignore any possible transfer function mismatch correction (in case "lutInputLinear" and "lutOutputLinear" were true).
  // If this is different from "transferFunctionIn", then the code will apply a transfer function correction, even if the input or output are linear.
  // Many games use the sRGB transfer function for LUT input, but then they theoretically output gamma 2.2 (as they were developed on and for gamma 2.2 displays),
  // thus their gamma needs to be corrected for that, whether "outputLinear" was true not (set this to "LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2" to do the classic SDR gamma correction).
  // The transfer function correction only applies in the LUT range (0-1) and is ignored for colors out of range,
  // given that the transfer function mismatch in out of range values can go wild, and also because in the vanilla version the would have been clipped anyway
  // (this behaviour assumes both input and output were in the 0-1 range, which might not be true depending on the LUT transfer functions, but it's true in the ones we support).
  uint transferFunctionOut;
  // TODO: add LUT color space param
  // 0 Basic sampling
  // 1 Linear corrected sampling (if "lutOutputLinear" is false this is equal to "0", but if true, the LUT input coordinates need to be adjusted with the inverse of the transfer function, otherwise even a neutral LUT would shift colors that didn't fall precisely on a LUT texel)
  // 2 Linear corrected sampling + tetrahedral interpolation (it won't necessarily look better, especially with LUTs close to neutral)
  uint samplingQuality;
  
  // Enable or disable LUT extrapolation.
  // Use "neutralLUTRestorationAmount" to control the extrapolation intensity
  // (it wouldn't make sense to only partially extrapolate without scaling back the color intensity, otherwise LUT extrapolation would have an output range smaller than its input range).
  bool enableExtrapolation;
  // 0 Low (likely results in major hue shifts) (2 fixed samples per pixel)
  // 1 High (no major hue shifts) (1 fixed sample + 3 optional samples per pixel)
  // 2 Extreme (no major hue shifts, more accurately preserves the rate of change towards the edges of the original LUT (see "extrapolationQuality"), though it's often unnecessary) (1 fixed sample + 6 optional samples per pixel)
  uint extrapolationQuality;
  // LUT extrapolation works by taking more centered samples starting from the "clipped" LUT coordinates (in case the native ones were out of range).
  // This determines how much we go backwards towards the LUT center.
  // The value is supposed to be > 0 and <= 1, with 1 mapping to 50% centering (we shouldn't do any more than that or the extrapolation would not be accurate).
  // The smaller this value, the more "accurate" extrapolation will be, respecting more lawfully the way the LUT changed around its edges (as long as it ends up mapped beyond the center of the first and second texels).
  // The higher the value, the "smoother" the extrapolation will be, with gradients possibly looking nicer.
  float backwardsAmount;
  // When the extrapolated rgb flip in "hue", as in, the biggest rgb channel value at the edge of the LUT is exceeded by another channel value that was accelerating faster towards the edge of the LUT.
  // This is only really needed if the extrapolation is done per channel and not through perceptual color representations.
  // Enable this if you see weird hue shifts in highlights, especially useful with strong LUTs that shift colors a lot.
  // Note that this might desaturate highlights a lot, and removes wider color gamut!
  bool clipExtrapolationToWhite;
  // LUT extrapolation can generate invalid colors (colors with a negative luminance) if the input color had values below 0,
  // this fixes them in the best possible way without altering their hue wherever possible.
  bool fixExtrapolationInvalidColors;
  // What white level does the LUT have for its input coordinates (e.g. what's the expected brightness of an input color of 1 1 1?).
  // This value doesn't directly scale the brightness of the output but affects the logic of some internal math (e.g. tonemapping and transfer functions).
  // Ideally it would be set to the same brightness the developers of the LUTs had their screen set to, some good values for SDR LUTs are 80, 100 or 203.
  // Given that this is used as a scaler for PQ, using the Rec.709 white level of 100 nits is a good start, as that maps to ~50% of the PQ range.
  float whiteLevelNits;
  
  // If our input color was too high (and thus out of range, (e.g. beyond 0-1)), we can temporarily tonemap it to avoid the LUT extrapolation math going wild (e.g. too saturated, or hue shifted, or generating too strong highlights),
  // this is especially useful in the following conditions:
  //  -With LUTs that change colors a lot in brightness, especially towards the edges
  //  -When using lower "extrapolationQuality" modes
  //  -When feeding in an untonemapped input color (with values that can possibly go very high)
  // This should not be used in the following conditions:
  //  -With LUTs that change colors a lot in hue and saturation (it might still work)
  //  -With LUTs that at "clipped" (LUTs that reach their peak per axis values before its latest texel)
  //  -With LUTs that invert colors (the tonemapping logic isn't compatible with it increasingly higher input colors mapping to increasingly lower output colors)
  // This is relative to the "whiteLevelNits" and needs to be greater than it.
  // Tonemapping is disabled if this is <= 0.
  float inputTonemapToPeakWhiteNits;
  // Basically an inverse LUT intensity setting.
  // How much we blend back towards the "neutral" LUT color (the unclamped source color (e.g. HDR)).
  // This has the same limitations of "inputTonemapToPeakWhiteNits" and should be used and not used in the same cases.
  // It's generally not suggested to use it as basically it undoes the LUT extrapolation, but if you have LUTs not far from being neutral,
  // you might set this to a smallish value and get better results (e.g. better hues).
  float neutralLUTRestorationAmount;
  // How much we blend back towards the vanilla LUT color (or hue/chrominance).
  // It can be used to restore some of the vanilla hues or chrominance on bright (or not bright) colors (they would likely have desaturated on highlights).
  // This adds one sample per pixel.
  float vanillaLUTRestorationAmount;
  uint vanillaLUTRestorationType;
  // How much we blend back towards the "clipped" LUT color.
  // This is different from the vanilla color, as it's sourced from the new (e.g. HDR) input color, but clipped the the LUT input coordinates range (0-1).
  // It can be used to hide some of the weird hues generated from too aggressive extrapolation (e.g. for overly bright input colors, or for the lower "extrapolationQuality" modes).
  float clampedLUTRestorationAmount;
};

LUTExtrapolationData DefaultLUTExtrapolationData()
{
  LUTExtrapolationData data;
  data.vanillaInputColor = 0;
  return data;
}

LUTExtrapolationSettings DefaultLUTExtrapolationSettings()
{
  LUTExtrapolationSettings settings;
  settings.lutSize = LUT_SIZE;
  settings.inputLinear = true;
  settings.lutInputLinear = false;
  settings.lutOutputLinear = false;
  settings.outputLinear = true;
  settings.transferFunctionIn = DEFAULT_LUT_EXTRAPOLATION_TRANSFER_FUNCTION;
  settings.transferFunctionOut = DEFAULT_LUT_EXTRAPOLATION_TRANSFER_FUNCTION;
  settings.samplingQuality = 1;
  settings.neutralLUTRestorationAmount = 0;
  settings.vanillaLUTRestorationAmount = 0;
  settings.vanillaLUTRestorationType = 0;
  settings.enableExtrapolation = true;
  settings.extrapolationQuality = 1;
  settings.backwardsAmount = 0.5;
  settings.clipExtrapolationToWhite = false;
  settings.whiteLevelNits = Rec709_WhiteLevelNits;
  settings.inputTonemapToPeakWhiteNits = 0;
  settings.clampedLUTRestorationAmount = 0;
  settings.fixExtrapolationInvalidColors = true;
  return settings;
}

float3 SampleLUT(LUT_TEXTURE_TYPE lut, SamplerState samplerState, float3 encodedCoordinates, LUTExtrapolationSettings settings, bool forceOutputLinear = false, bool specifyLinearColor = false, float3 linearCoordinates = 0)
{
  const bool highQualityLUTCoordinateAdjustments = settings.samplingQuality >= 1;
  const bool tetrahedralInterpolation = settings.samplingQuality >= 2;
  
  float3 sampleCoordinates = AdjustLUTCoordinatesForLinearLUT(encodedCoordinates, highQualityLUTCoordinateAdjustments, settings.transferFunctionIn, settings.lutInputLinear, settings.lutOutputLinear, settings.lutSize, specifyLinearColor, linearCoordinates);
  float3 color = SampleLUT(lut, samplerState, sampleCoordinates, settings.lutSize, tetrahedralInterpolation, settings.lutInputLinear, settings.lutOutputLinear, settings.transferFunctionIn);
  // We appply the transfer function even beyond 0-1 as if the color comes from a linear LUT, it shouldn't already have any kind of gamma correction applied to it (gamma correction runs later).
  if (!settings.lutOutputLinear && forceOutputLinear)
  {
		color = ColorGradingLUTTransferFunctionOut(color, settings.transferFunctionIn, true); // Doing a return directly here causes warning 4000
  }
  return color;
}

//TODOFT: store the acceleration around the lut's last texel in the alpha channel?
//TODOFT: lower lut extrapolation intensity on brighter colors?

// LUT sample that allows to go beyond the 0-1 coordinates range through extrapolation.
// It finds the rate of change (acceleration) of the LUT color around the requested clamped coordinates, and guesses what color the sampling would have with the out of range coordinates.
// Extrapolating LUT by re-apply the rate of change has the benefit of consistency. If the LUT has the same color at (e.g.) uv 0.9 0.9 0.9 and 1.0 1.0 1.0, thus clipping to white (or black) earlier, the extrapolation will also stay clipped, preserving the artistic intention.
// Additionally, if the LUT had inverted colors or highly fluctuating colors or very hues shifted colors, extrapolation would work a lot better than a raw LUT out of range extraction with a luminance multiplier (or other similar simpler techniques).
// 
// This function allows the LUT to be in linear or transfer function encoded (e.g. gamma space) on input coordinates and output color separately.
// LUTs are expected to be of equal size on each axis (once unwrapped from 2D to 3D).
// LUT extrapolation works best on LUTs that are NOT "clipped" around their edges (e.g. if the 3 last texels on the red axis all map to 255 (in 8bit), LUT extrapolation would either end up also clipping (which was likely not intended in the vanilla LUT and would look bad in HDR), or extrapolating values after a clipped gradient, thus ending up with a gradient like 254 255 255 255 256)
float3 SampleLUTWithExtrapolation(LUT_TEXTURE_TYPE lut, SamplerState samplerState, LUTExtrapolationData data /*= DefaultLUTExtrapolationData()*/, LUTExtrapolationSettings settings /*= DefaultLUTExtrapolationSettings()*/)
{
	float3 lutMax3D;
	if (any(settings.lutSize == 0))
	{
		// LUT size in texels
		float lutWidth;
		float lutHeight;
#if LUT_3D
		float lutDepth;
		lut.GetDimensions(lutWidth, lutHeight, lutDepth);
		const float3 lutSize3D = float3(lutWidth, lutHeight, lutDepth);
#else
		lut.GetDimensions(lutWidth, lutHeight);
		lutWidth = sqrt(lutWidth); // 2D LUTs usually extend horizontally
		const float3 lutSize3D = float3(lutWidth, lutWidth, lutHeight); // Unwrapped size, usually all the same for 2D LUTs
#endif
		settings.lutSize = lutHeight;
		lutMax3D = lutSize3D - 1.0;
	}
	else
	{
		lutMax3D = settings.lutSize - 1u;
	}
	// The uv distance between the center of one texel and the next one (this is before applying the uv bias and scaling later on, that's done when sampling)
	float3 lutTexelRange = 1.0 / settings.lutSize;

  // Theoretically these input colors match the output of a "neutral" LUT, so we call like that for clarity
	float3 neutralLUTColorLinear = data.inputColor;
	float3 neutralLUTColorTransferFunctionEncoded = data.inputColor;
	float3 neutralVanillaColorLinear = data.vanillaInputColor;
	float3 neutralVanillaColorTransferFunctionEncoded = data.vanillaInputColor;

  // Here we need to pick an encoding for the 0-1 range, and one for the range beyond that.
  // For example, sRGB gamma doesn't really make sense beyond the 0-1 range (especially below 0), so it's not exactly compatible with scRGB colors (that go to negative values to represent colors beyond sRGB),
	// but either way, whether we use gamma 2.2 or sRGB encoding beyond the 0-1 range doesn't make that much difference, as neither of the two choices are "correct" or great,
	// using 2.2 might be a bit closer to human perception below 0 than sRGB, while sRGB might be closer to human perception beyond 1 than 2.2, so we can pick whatever is best for your case to increase the quality of extrapolation.
	// We still need to apply gamma correction on output anyway, this doesn't really influence that, it just makes parts of the extrapolation more perception friendly.
  // At the moment we simply use the LUT in transfer function for the whole range, as it's simple and tests shows it works fine.
	if (settings.inputLinear)
	{
		neutralLUTColorTransferFunctionEncoded = ColorGradingLUTTransferFunctionIn(neutralLUTColorLinear, settings.transferFunctionIn);
		neutralVanillaColorTransferFunctionEncoded = ColorGradingLUTTransferFunctionIn(neutralVanillaColorLinear, settings.transferFunctionIn);
	}
	else
	{
		neutralLUTColorLinear = ColorGradingLUTTransferFunctionOut(neutralLUTColorTransferFunctionEncoded, settings.transferFunctionIn);
		neutralVanillaColorLinear = ColorGradingLUTTransferFunctionOut(neutralVanillaColorTransferFunctionEncoded, settings.transferFunctionIn);
	}
	const float3 clampedNeutralLUTColorLinear = saturate(neutralLUTColorLinear);

  // Whether the LUT takes linear inputs or not, we encode the input coordinates with the specified input transfer function,
  // so we can later use the perceptual space UVs to run some extrapolation logic.
  // These LUT coordinates are in the 0-1 range (or beyond that), without acknowleding the lut size or lut max (like the half texel around each edge).
	// We purposely don't use "neutralLUTColorLinearTonemapped" here as we want the raw input color.
	const float3 unclampedUV = neutralLUTColorTransferFunctionEncoded;
	const float3 clampedUV = saturate(unclampedUV);
	const float distanceFromUnclampedToClampedUV = length(unclampedUV - clampedUV);
  // Some threshold is needed to avoid divisions by tiny numbers.
  // Ideally this check is enough to avoid black dots in output due to normalizing smallish vectors, if not, increase the threshold value (e.g. to FLT_EPSILON).
	const bool uvOutOfRange = distanceFromUnclampedToClampedUV > FLT_MIN;
  const bool doExtrapolation = settings.enableExtrapolation && uvOutOfRange;
  // The current working space of this function (all colors samples from LUTs need to be in this space, whether they natively already were or not).
  // All rgb colors within the extrapolation branch need to be in linear space (and so are the ones that will come out of it)
	bool lutOutputLinear = settings.lutOutputLinear || doExtrapolation;

  // Use "clampedUV" instead of "unclampedUV" as we don't know what kind of sampler was in use here (it's probably clamped)
	float3 clampedSample = SampleLUT(lut, samplerState, clampedUV, settings, lutOutputLinear, true, clampedNeutralLUTColorLinear);
  float3 outputSample = clampedSample;
  
	if (doExtrapolation)
	{
    float3 neutralLUTColorLinearTonemapped = neutralLUTColorLinear;
    float3 neutralLUTColorLinearTonemappedRestoreRatio = 1;
    // Tonemap colors beyond the 0-1 range (we don't touch colors within the 0-1 range), tonemapping will be inverted later
    if (settings.inputTonemapToPeakWhiteNits > 0)
    {
      const float maxExtrapolationColor = max((settings.inputTonemapToPeakWhiteNits / settings.whiteLevelNits) - 1.0, FLT_MIN);
      const float3 neutralLUTColorInExcessLinear = neutralLUTColorLinear - clampedNeutralLUTColorLinear;
      // Tonemap it with a basic Reinhard (we could do something better but it likely wouldn't improve the results much)
// We can either tonemap by channel or by max channel. Tonemapping by luminance here isn't a good idea because we are interested in reducing the range to a specific max channel value.
#if 1 // By max channel (hue conserving (at least in the color in excess of 0-1), but has inconsistent results depending on the luminance)
//TODOFT: this is causing incontiguous gradients!!! (it's neutralLUTColorLinearTonemappedRestoreRatio)
      float normalizedNeutralLUTColorInExcessLinear = max3(abs(neutralLUTColorInExcessLinear / maxExtrapolationColor));
      float normalizedNeutralLUTColorInExcessLinearTonemapped = normalizedNeutralLUTColorInExcessLinear / (normalizedNeutralLUTColorInExcessLinear + 1);
      float normalizedNeutralLUTColorInExcessLinearRestoreRatio = safeDivision(normalizedNeutralLUTColorInExcessLinearTonemapped, normalizedNeutralLUTColorInExcessLinear, 1);
      float3 neutralLUTColorInExcessLinearTonemapped = neutralLUTColorInExcessLinear * normalizedNeutralLUTColorInExcessLinearRestoreRatio;
      neutralLUTColorLinearTonemappedRestoreRatio = safeDivision(1.0, normalizedNeutralLUTColorInExcessLinearRestoreRatio, 1);
#else // By channel
      float3 normalizedNeutralLUTColorInExcessLinear = abs(neutralLUTColorInExcessLinear / maxExtrapolationColor);
      float3 neutralLUTColorInExcessLinearTonemapped = (normalizedNeutralLUTColorInExcessLinear / (normalizedNeutralLUTColorInExcessLinear + 1)) * maxExtrapolationColor * sign(neutralLUTColorInExcessLinear);
      neutralLUTColorLinearTonemappedRestoreRatio = safeDivision(neutralLUTColorInExcessLinear, neutralLUTColorInExcessLinearTonemapped, 1);
#endif
      neutralLUTColorLinearTonemapped = clampedNeutralLUTColorLinear + neutralLUTColorInExcessLinearTonemapped;
    }

    // While "centering" the UVs, we need to go backwards by a specific amount.
    // Going back 50% (e.g. from LUT coordinates 1 to 0.5, or 0 to 0.5) can be too much, so we should generally keep it lower than that.
    // Anything lower than 25% will be more accurate but prone to extrapolation looking more aggressive.
		float backwardsAmount = settings.backwardsAmount * 0.5;
// Extrapolation shouldn't run with a "backwards amount" smaller than half a texel, otherwise it will be almost like sampling the edge coordinates again.
// This is already explained in the settings description so we disabled the safety check.
#if 0
    if (backwardsAmount < lutTexelRange)
    {
      backwardsAmount = lutTexelRange;
    }
#endif

		const float PQNormalizationFactor = HDR10_MaxWhiteNits / settings.whiteLevelNits;

		const float3 clampedUV_PQ = Linear_to_PQ2(clampedNeutralLUTColorLinear / PQNormalizationFactor); // "clampedNeutralLUTColorLinear" is equal to "ColorGradingLUTTransferFunctionOut(clampedUV, settings.transferFunctionIn, false)"
		const float3 unclampedTonemappedUV_PQ = Linear_to_PQ2(neutralLUTColorLinearTonemapped / PQNormalizationFactor, GCT_MIRROR);
		const float3 clampedSample_PQ = Linear_to_PQ2(clampedSample / PQNormalizationFactor, GCT_MIRROR);
		const float3 clampedUV_UCS = DarktableUcs::RGBToUCSLUV(clampedNeutralLUTColorLinear);
		const float3 unclampedTonemappedUV_UCS = DarktableUcs::RGBToUCSLUV(neutralLUTColorLinearTonemapped);
		const float3 clampedSample_UCS = DarktableUcs::RGBToUCSLUV(clampedSample);
    
#pragma warning( disable : 4000 )
		float3 extrapolatedSample;

    // Here we do the actual extrapolation logic, which is relatively different depending on the quality mode.
    // LUT extrapolation lerping is best run in perceptual color space instead of linear space.
    // We settled for using PQ after long tests, here's a comparison of all of them: 
    // -PQ allows for a very wide range, it's relatively cheap, and simple to use.
    // -sRGB or gamma 2.2 falters in the range beyond 1, as they were made for SDR.
    // -Oklab/Oklch or Darktable UCS can work, but they seem to break on very bright colors, and are harder to control
    //  (it's hard to find the actual ratio of change for the extrapolation, they easily create invalid colors or broken gradients, and their hue is very hard to control).
    // -Linear just can't work for LUT extrapolation, because it would act very differently depending on the extrapolation direction (e.g. beyond 1 or below 0), given that it's not adjusted by perceptual
    //  (e.g.1 the extrapolation strength between -0.01 and 0.01 or 0.99 and 1.01 would be massively different, even if both of them have the same offset)
		//  (e.g.2 if the LUT sampling coordinates are 1.1, we'd want to extrapolate ~10% more color, but in linear space it would be a lot less than that, thus the peak brightness would be compressed a lot more than it should).
		if (settings.extrapolationQuality <= 0) //TODOFT: muke oklab and also fix this not extrapolating contiguously... depending on the backwards factor
		{
      // Take the direction between the clamped and unclamped coordinates, flip it, and use it to determine how much to go backwards by when taking the centered sample.
      // For example, if our "centeringNormal" is -1 -1 -1, we'd want to go backwards by our fixed amount, but multiplied by sqrt(3) (the lenght of a cube internal diagonal),
      // while of -1 -1 0, we'd only want to go back by sqrt(2) (the length of a side diagonal), etc etc. This helps keep the centering results consistent independently of their "angle".
		  const float3 centeringNormal = normalize(unclampedUV - clampedUV); // This should always be valid as "unclampedUV" and guaranteed to be different from "clampedUV".
      const float3 centeringNormalAbs = abs(centeringNormal);
      const float lutBackwardsDiagonalMultiplier = centeringNormalAbs.x + centeringNormalAbs.y + centeringNormalAbs.z; //TODOFT: this is unnecessary? it moves the vector in the same direction twice!??

			const float3 centeredUV = clampedUV - (centeringNormal * backwardsAmount * lutBackwardsDiagonalMultiplier);
			float3 centeredSample = SampleLUT(lut, samplerState, centeredUV, settings, lutOutputLinear);
			float3 centeredSample_PQ = Linear_to_PQ2(centeredSample / PQNormalizationFactor, GCT_MIRROR);
			float3 centeredUV_PQ = Linear_to_PQ2(ColorGradingLUTTransferFunctionOut(centeredUV, settings.transferFunctionIn, false) / PQNormalizationFactor);

			const float distanceFromUnclampedToClampedUV_PQ = length(unclampedTonemappedUV_PQ - clampedUV_PQ);
			const float distanceFromClampedToCenteredUV_PQ = length(clampedUV_PQ - centeredUV_PQ);
			const float extrapolationRatio = safeDivision(distanceFromUnclampedToClampedUV_PQ, distanceFromClampedToCenteredUV_PQ, 0);
			extrapolatedSample = PQ_to_Linear2(lerp(centeredSample_PQ, clampedSample_PQ, 1.0 + extrapolationRatio), GCT_MIRROR) * PQNormalizationFactor;

#if DEVELOPMENT && 0
      bool oklab = LumaSettings.DevSetting06 >= 0.5;
#else
      bool oklab = false;
#endif
      if (oklab) //TODOFT4: try oklab again? (update the starfield code ok extrapolation and oklab) And fix up oklab+PQ description above. Also try per channel (quality 1+) and try UCS.
      {
        // OKLAB/OKLCH (it doesn't really look good, it limits the saturation too much, and though it retains vanilla hues more accurately, it just doesn't look that good, and it breaks on high luminances)
        float3 unclampedUVOklch = LINEAR_TO_UCS(neutralLUTColorLinear, CS_DEFAULT);
        float3 clampedUVOklch = LINEAR_TO_UCS(clampedNeutralLUTColorLinear, CS_DEFAULT);
        float3 centeredUVOklch = LINEAR_TO_UCS(ColorGradingLUTTransferFunctionOut(centeredUV, settings.transferFunctionIn, false), CS_DEFAULT);
        
        const float3 distanceFromUnclampedToClampedOklch = unclampedUVOklch - clampedUVOklch;
        const float3 distanceFromClampedToCenteredOklch = clampedUVOklch - centeredUVOklch;
        const float3 extrapolationRatioOklch = safeDivision(distanceFromUnclampedToClampedOklch, distanceFromClampedToCenteredOklch, 0); //TODOFT: 0 or 1 on safe div? // This has borked uncontiguous values on x y and z, in oklab and oklch...
        const float distanceFromUnclampedToClampedOklch2 = length(unclampedUVOklch.yz - clampedUVOklch.yz);
        const float distanceFromClampedToCenteredOklch2 = length(clampedUVOklch.yz - centeredUVOklch.yz);
        const float extrapolationRatioOklch2 = safeDivision(distanceFromUnclampedToClampedOklch2, distanceFromClampedToCenteredOklch2, 0);

        float3 derivedLUTColor = LINEAR_TO_UCS(clampedSample, CS_DEFAULT);
        float3 derivedLUTCenteredColor = LINEAR_TO_UCS(centeredSample, CS_DEFAULT);
        float3 derivedLUTColorChangeOffset = derivedLUTColor - derivedLUTCenteredColor;
        // Reproject the centererd color change ratio onto the full range
#if 0
        //float3 extrapolatedDerivedLUTColorChangeOffset = derivedLUTColorChangeOffset * extrapolationRatioOklch;
        //float3 extrapolatedDerivedLUTColorChangeOffset = derivedLUTColorChangeOffset * abs(extrapolationRatioOklch);
        float3 extrapolatedDerivedLUTColorChangeOffset = derivedLUTColorChangeOffset * float3(abs(extrapolationRatioOklch.x), extrapolationRatioOklch2.xx);
        //float3 extrapolatedDerivedLUTColorChangeOffset = derivedLUTColorChangeOffset * float3(abs(extrapolationRatioOklch.x), extrapolationRatioOklch2.xx);
#elif 1
        float3 extrapolatedDerivedLUTColorChangeOffset = derivedLUTColorChangeOffset * extrapolationRatio;
#else
        float3 extrapolatedDerivedLUTColorChangeOffset = derivedLUTColorChangeOffset * float3(abs(distanceFromUnclampedToClampedOklch.x), length(distanceFromUnclampedToClampedOklch.yz).xx);
#endif
  #if LUT_EXTRAPOLATION_DESATURATE >= 1 // Not a desaturate here (simply conserve mode hue), but it achieves similar results
        // only recover hue at 50%? Given that otherwise we use the one clipped from "SDR" with the wrong rgb ratio (e.g. if we try to extrapolate 5 3 2, it will clip to 1 1 1, so there won't be any hue...)?
        //extrapolatedDerivedLUTColorChangeOffset.z *= 0.5;
        //extrapolatedDerivedLUTColorChangeOffset.y *= 1.0 / 3.0;
        extrapolatedDerivedLUTColorChangeOffset.yz *= 2.0 / 3.0;
  #endif
        //extrapolatedDerivedLUTColorChangeOffset.yz *= 2.5;
        //extrapolatedDerivedLUTColorChangeOffset.yz *= 0.2;
        //extrapolatedDerivedLUTColorChangeOffset.x *= 0.2;

        //float3 extrapolatedDerivedLUTColor = derivedLUTColor + float3(0, extrapolatedDerivedLUTColorChangeOffset.yz);
        float3 extrapolatedDerivedLUTColor = derivedLUTColor + extrapolatedDerivedLUTColorChangeOffset;
        //float3 extrapolatedDerivedLUTColor = lerp(derivedLUTCenteredColor, derivedLUTColor, 1.0 + extrapolationRatioOklch);
        // Avoid negative luminance. This can happen in case "derivedLUTColorChangeOffset" intensity/luminance was negative, even if we were at a bright/colorful LUT edge,
        // especially if the input color is extremely bright. We can't really fix the color from ending up as black though, unless we find a way to auto detect it.
        extrapolatedDerivedLUTColor.x = max(extrapolatedDerivedLUTColor.x, 0.f);

        derivedLUTColor = Oklab::oklab_to_oklch(derivedLUTColor);
        derivedLUTCenteredColor = Oklab::oklab_to_oklch(derivedLUTCenteredColor);
        extrapolatedDerivedLUTColor = Oklab::oklab_to_oklch(extrapolatedDerivedLUTColor);

#if DEVELOPMENT && 0
        // Avoid flipping ab direction, if we reached white, stay on white.
        // We only do it on colors that have some chroma and brightness.
        if (LumaSettings.DevSetting05 >= 0.5)
        {
          // do abs() before fmod() for safety
          if ((fmod(abs(extrapolatedDerivedLUTColor.z - derivedLUTColor.z), PI * 2.0) >= PI / 1.f)
              && extrapolatedDerivedLUTColor.x > 0.f && derivedLUTColor.x > 0.f
              && extrapolatedDerivedLUTColor.y > 0.f && derivedLUTColor.y > 0.f)
          {
            extrapolatedDerivedLUTColor.z = derivedLUTColor.z;
            extrapolatedDerivedLUTColor.y = 0.f;
          }
        }
#endif

        unclampedUVOklch = Oklab::oklab_to_oklch(unclampedUVOklch);
        clampedUVOklch = Oklab::oklab_to_oklch(clampedUVOklch);
        centeredUVOklch = Oklab::oklab_to_oklch(centeredUVOklch);
        //TODOFT3: oklch's H can't be subracted like that given that they are circular
        const float3 distanceFromUnclampedToClampedOklch3 = unclampedUVOklch - clampedUVOklch;
        const float3 distanceFromClampedToCenteredOklch3 = clampedUVOklch - centeredUVOklch;
        const float3 extrapolationRatioOklch3 = safeDivision(distanceFromUnclampedToClampedOklch3, distanceFromClampedToCenteredOklch3, 0);
        float3 derivedLUTColorChangeOffset3 = derivedLUTColor - derivedLUTCenteredColor;
        //float3 extrapolatedDerivedLUTColorChangeOffset3 = derivedLUTColorChangeOffset3 * abs(extrapolationRatioOklch3);
        //float3 extrapolatedDerivedLUTColorChangeOffset3 = derivedLUTColorChangeOffset3 * float3(abs(extrapolationRatioOklch3.x), extrapolationRatioOklch2.xx);
        float3 extrapolatedDerivedLUTColorChangeOffset3 = derivedLUTColorChangeOffset3 * extrapolationRatio;
        float3 derivedLUTColorExtrap3 = derivedLUTColor + extrapolatedDerivedLUTColorChangeOffset3;
#if 0
        extrapolatedDerivedLUTColor.y = derivedLUTColorExtrap3.y;
        //extrapolatedDerivedLUTColor.xy = derivedLUTColorExtrap3.xy;
#endif

        // Avoid negative chroma, as it would likely flip the hue. Theoretically this breaks the accuracy of some "LUTExtrapolationColorSpace" modes but the results would be visually bad without it.
        //extrapolatedDerivedLUTColor.y = max(extrapolatedDerivedLUTColor.y, 0.f);
        // Mirror hue (not sure this would be automatically done when converting back from oklch to sRGB)
        //extrapolatedDerivedLUTColor.z = abs(extrapolatedDerivedLUTColor.z); // looks worse, hue can go negative...
        //extrapolatedDerivedLUTColor.yz = max(extrapolatedDerivedLUTColor.yz, -3.f);
        //extrapolatedDerivedLUTColor.yz = min(extrapolatedDerivedLUTColor.yz, 3.f);

        // Keep other extrapolation luminance
        float extrapolatedSampleLuminance = GetLuminance(extrapolatedSample);
        
        // Shift luminance and chroma to the extrapolated values, keep the original LUT edge hue (we can't just apply the same hue change, hue isn't really scalable).
        // This has problems in case the LUT color was white, so basically the hue is picked at random.
        extrapolatedSample = Oklab::oklch_to_linear_srgb(extrapolatedDerivedLUTColor.xyz);
        //extrapolatedSample = Oklab::oklab_to_linear_srgb(extrapolatedDerivedLUTColor.xyz); // UCS_TO_LINEAR?
        //extrapolatedSample = Oklab::oklab_to_linear_srgb(float3(extrapolatedDerivedLUTColor.x, derivedLUTColor.yz)); // UCS_TO_LINEAR?
  #if 0 // Looks bad without this
        extrapolatedSample = Oklab::oklch_to_linear_srgb(float3(extrapolatedDerivedLUTColor.xy, derivedLUTColor.z));
  #endif
        //extrapolatedSample = UCS_TO_LINEAR(float3(extrapolatedDerivedLUTColor.x, derivedLUTColor.yz));
        //extrapolatedSample = UCS_TO_LINEAR(float3(derivedLUTColor.x, extrapolatedDerivedLUTColor.y, derivedLUTColor.z));
        //extrapolatedSample = UCS_TO_LINEAR(float3(derivedLUTColor.xy, extrapolatedDerivedLUTColor.z));
        //extrapolatedSample = abs(extrapolationRatioOklch);

#if 0
      float extrapolatedSampleLuminance2 = GetLuminance(extrapolatedSample);
			extrapolatedSample = extrapolatedSample * lerp(1.0, max(safeDivision(extrapolatedSampleLuminance, extrapolatedSampleLuminance2, 1), 0.0), 0.5); //50%
#endif
      }
		}
		else //if (settings.extrapolationQuality >= 1)
		{
      // We always run the UV centering logic in the vanilla transfer function space (e.g. sRGB), not PQ, as all these transfer functions are reliable enough within the 0-1 range.
			float3 centeredUV = clampedUV + (backwardsAmount * (clampedUV >= 0.5 ? -1 : 1));
			float3 centeredSamples[3] = { clampedSample, clampedSample, clampedSample };
			float3 centeredSamples_PQ[3] = { clampedSample_PQ, clampedSample_PQ, clampedSample_PQ };
		  const float3 clampedSample_UCS = DarktableUcs::RGBToUCSLUV(clampedSample);
			float3 centeredSamples_UCS[3] = { clampedSample_UCS, clampedSample_UCS, clampedSample_UCS };

#if 1
      const bool secondSampleLessCentered = backwardsAmount > (0.25 + FLT_EPSILON);
			const float backwardsAmount_2 = secondSampleLessCentered ? (backwardsAmount / 2) : (backwardsAmount * 2); // Go in the most sensible direction
			float3 centeredUV_2 = clampedUV + (backwardsAmount_2 * (clampedUV >= 0.5 ? -1 : 1));
#else // This might be more accurate, though it might be more aggressive, and fails to extrapolate properly in case the user set "backwardsAmount" was too close to "lutTexelRange", or if the LUT clipped to the max value before its edges.
      const bool secondSampleLessCentered = backwardsAmount > lutTexelRange;
			const float backwardsAmount_2 = secondSampleLessCentered ? lutTexelRange : (backwardsAmount * 2);
			float3 centeredUV_2 = clampedUV + (backwardsAmount_2 * (clampedUV >= 0.5 ? -1 : 1));
#endif
			float3 centeredSamples_2[3] = { clampedSample, clampedSample, clampedSample };
			float3 centeredSamples_PQ_2[3] = { clampedSample_PQ, clampedSample_PQ, clampedSample_PQ };

      // Swap them to avoid having to write more branches below,
      // the second (2) sample is always meant to be closer to the edges (less centered).
      if (settings.extrapolationQuality >= 2 && !secondSampleLessCentered)
      {
        float3 tempCenteredUV = centeredUV;
        centeredUV = centeredUV_2;
        centeredUV_2 = tempCenteredUV;
      }

      float3 centeredUV_PQ = Linear_to_PQ2(ColorGradingLUTTransferFunctionOut(centeredUV, settings.transferFunctionIn, false) / PQNormalizationFactor);
      float3 centeredUV_UCS = DarktableUcs::RGBToUCSLUV(ColorGradingLUTTransferFunctionOut(centeredUV, settings.transferFunctionIn, false));
      float3 centeredUVs_UCS[3] = { centeredUV_UCS, centeredUV_UCS, centeredUV_UCS };

      [unroll]
			for (uint i = 0; i < 3; i++)
			{
				if (unclampedUV[i] != clampedUV[i]) // Optional optimization to avoid taking samples that won't be used
				{
					float3 localCenteredUV = float3(i == 0 ? centeredUV.r : clampedUV.r, i == 1 ? centeredUV.g : clampedUV.g, i == 2 ? centeredUV.b : clampedUV.b);
					centeredSamples[i] = SampleLUT(lut, samplerState, localCenteredUV, settings, lutOutputLinear);
					centeredSamples_PQ[i] = Linear_to_PQ2(centeredSamples[i] / PQNormalizationFactor, GCT_MIRROR);
					centeredSamples_UCS[i] = DarktableUcs::RGBToUCSLUV(centeredSamples[i]);
					centeredUVs_UCS[i] = DarktableUcs::RGBToUCSLUV(ColorGradingLUTTransferFunctionOut(localCenteredUV, settings.transferFunctionIn, false));

          // The highest quality takes more samples and then "averages" them later
					if (settings.extrapolationQuality >= 2)
					{
						localCenteredUV = float3(i == 0 ? centeredUV_2.r : clampedUV.r, i == 1 ? centeredUV_2.g : clampedUV.g, i == 2 ? centeredUV_2.b : clampedUV.b);
						centeredSamples_2[i] = SampleLUT(lut, samplerState, localCenteredUV, settings, lutOutputLinear);
						centeredSamples_PQ_2[i] = Linear_to_PQ2(centeredSamples_2[i] / PQNormalizationFactor, GCT_MIRROR);
					}
				}
			}

#if 0 // OLD
      // Find the "velocity", or "rate of change" of the color.
      // This isn't simply an offset, it's an offset (the lut sample colors difference) normalized by another offset (the uv coordinates difference),
      // so it's basically the speed with which color changes at this point in the LUT.
			float3 rgbRatioSpeed = safeDivision(clampedSample_PQ - float3(centeredSamples_PQ[0][0], centeredSamples_PQ[1][1], centeredSamples_PQ[2][2]), clampedUV_PQ - centeredUV_PQ);
      float3 rgbRatioAcceleration = 0;
      // Extreme quality: use two extrapolation samples per channel
      // Note that it would be possibly to do the same thing with 3+ channels too, but further samples would have diminishing returns and not help at all in 99% of cases.
			if (settings.extrapolationQuality >= 2)
			{
				float3 centeredUV_PQ_2 = Linear_to_PQ2(ColorGradingLUTTransferFunctionOut(centeredUV_2, settings.transferFunctionIn, false) / PQNormalizationFactor);
#if 1
        // Find the acceleration of each color channel as the LUT coordinates move towards the (external) edge.
        // The second (2) sample is always more external, so it's "newer" if we consider time.
			  rgbRatioSpeed = safeDivision(float3(centeredSamples_PQ_2[0][0], centeredSamples_PQ_2[1][1], centeredSamples_PQ_2[2][2]) - float3(centeredSamples_PQ[0][0], centeredSamples_PQ[1][1], centeredSamples_PQ[2][2]), centeredUV_PQ_2 - centeredUV_PQ);
				float3 rgbRatioSpeed_2 = safeDivision(clampedSample_PQ - float3(centeredSamples_PQ_2[0][0], centeredSamples_PQ_2[1][1], centeredSamples_PQ_2[2][2]), clampedUV_PQ - centeredUV_PQ_2);
#if 1 // Theoretically the best version, though it's very aggressive //TODOFT4
        rgbRatioAcceleration = safeDivision(rgbRatioSpeed_2 - rgbRatioSpeed, abs(clampedUV_PQ - centeredUV_PQ) / 1.0);
        rgbRatioSpeed = rgbRatioSpeed_2; // Set the latest velocity we found as the final velocity (this is the velocity we'll start from at the edge of the LUT, before adding acceleration)
#elif 0
        // Make an approximate prediction of what the next speed will be, based on the previous two samples (this doesn't consider for how long we travelled at that speed)
        rgbRatioSpeed = rgbRatioSpeed_2 + (rgbRatioSpeed_2 - rgbRatioSpeed);
#elif 1
        // Find the average of the two speeds, hoping they were going in roughly the same direction (otherwise this might make extrapolation go towards an incorrect direction)
				rgbRatioSpeed = lerp(rgbRatioSpeed, rgbRatioSpeed_2, 0.5);
#endif
#else // Smoother fallback case that doesn't use acceleration
        // Find the mid point between the two centered samples we had, to smooth out any inconsistencies and have a result that is closer to what would be expected by the ratio of change around the LUT edges.
        float3 centeredSamples_PQAverage = lerp(float3(centeredSamples_PQ[0][0], centeredSamples_PQ[1][1], centeredSamples_PQ[2][2]), float3(centeredSamples_PQ_2[0][0], centeredSamples_PQ_2[1][1], centeredSamples_PQ_2[2][2]), 0.5);
        float3 centeredUV_PQAverage = lerp(centeredUV_PQ, centeredUV_PQ_2, 0.5);
				rgbRatioSpeed = safeDivision(clampedSample_PQ - centeredSamples_PQAverage, clampedUV_PQ - centeredUV_PQAverage);
#endif
			}
      
      // Find the actual extrapolation "time", we'll travel away from the LUT edge for this "duration"
			float3 extrapolationRatio = unclampedTonemappedUV_PQ - clampedUV_PQ;
      
      // Calculate the final extrapolation offset (a "distance") from "speed" and "time"
			float3 extrapolatedOffset = rgbRatioSpeed * extrapolationRatio;
      // Higher quality modes use "acceleration" as opposed to "speed" only
      if (settings.extrapolationQuality >= 2)
			{
        // We are using the basic "distance from acceleration" formula "(v*t) + (0.5*a*t*t)".
        extrapolatedOffset = (rgbRatioSpeed * extrapolationRatio) + (0.5 * rgbRatioAcceleration * extrapolationRatio * extrapolationRatio);
      }
#else //TODOFT4: new rgb method...
			float3 rgbRatioSpeeds[3];
      [unroll]
			for (uint i = 0; i < 3; i++)
			{
		    rgbRatioSpeeds[i] = safeDivision(clampedSample_PQ - centeredSamples_PQ[i], clampedUV_PQ[i] - centeredUV_PQ[i]);
      }
      float3 rgbRatioAccelerations[3] = { float3(0, 0, 0), float3(0, 0, 0), float3(0, 0, 0) };
      if (settings.extrapolationQuality >= 2)
      {
        float3 centeredUV_PQ_2 = Linear_to_PQ2(ColorGradingLUTTransferFunctionOut(centeredUV_2, settings.transferFunctionIn, false) / PQNormalizationFactor);
        [unroll]
			  for (uint i = 0; i < 3; i++)
			  {
		      float3 rgbRatioSpeed_2 = safeDivision(centeredSamples_PQ_2[i] - centeredSamples_PQ[i], centeredUV_PQ_2[i] - centeredUV_PQ[i]); // "Velocity" more towards the center
#if 0
          rgbRatioSpeeds[i] = safeDivision(clampedSample_PQ - centeredSamples_PQ_2[i], clampedUV_PQ[i] - centeredUV_PQ_2[i]); // "Velocity" more towards the edge

          if (LumaSettings.DevSetting05 <= 0.25) // Wrong
            rgbRatioAccelerations[i] = safeDivision(rgbRatioSpeeds[i] - rgbRatioSpeed_2, abs(clampedUV_PQ[i] - centeredUV_PQ[i]) / 1.0); //TODOFT: / 2? Abs()?
          else if (LumaSettings.DevSetting05 <= 0.5)
            rgbRatioAccelerations[i] = safeDivision(rgbRatioSpeeds[i] - rgbRatioSpeed_2, abs(clampedUV_PQ[i] - centeredUV_PQ[i]) / 2.0);
          else if (LumaSettings.DevSetting05 <= 0.75) // Looks best with proper ACC branch
            rgbRatioAccelerations[i] = safeDivision(rgbRatioSpeeds[i] - rgbRatioSpeed_2, (clampedUV_PQ[i] - centeredUV_PQ[i]) / 2.0);
          else // Looks best with bad ACC branch
            rgbRatioAccelerations[i] = safeDivision(rgbRatioSpeeds[i] - rgbRatioSpeed_2, clampedUV_PQ[i] - centeredUV_PQ[i]);
#elif 0
				  rgbRatioSpeeds[i] = lerp(rgbRatioSpeed_2, rgbRatioSpeeds[i], 0.5);
#else
				  rgbRatioSpeeds[i] = rgbRatioSpeeds[i] + (rgbRatioSpeeds[i] - rgbRatioSpeed_2);
#endif
        }
      }
      
			float3 extrapolationRatio = unclampedTonemappedUV_PQ - clampedUV_PQ;
#if 0 // Bad test!!!? Testing what?
      const float3 centeringNormal = normalize(unclampedUV - clampedUV);
      const float3 centeringNormalAbs = abs(centeringNormal);
      const float3 centeringVectorAbs = abs(unclampedUV - clampedUV); //NOTE: to be tonemapped?
      float extrapolationRatioLength = length(extrapolationRatio);
      //extrapolationRatio = centeringVectorAbs / (centeringVectorAbs.x + centeringVectorAbs.y + centeringVectorAbs.z);
      extrapolationRatio = centeringNormalAbs * (length(extrapolationRatio) / length(centeringNormalAbs)) * sign(unclampedUV - clampedUV);
#endif

			float3 extrapolatedOffset = (rgbRatioSpeeds[0] * extrapolationRatio[0]) + (rgbRatioSpeeds[1] * extrapolationRatio[1]) + (rgbRatioSpeeds[2] * extrapolationRatio[2]);
      //extrapolatedOffset *= extrapolationRatioLength;
      if (settings.extrapolationQuality >= 2)
			{
#if 1
        extrapolatedOffset =  (rgbRatioSpeeds[0] * extrapolationRatio[0]) + (0.5 * rgbRatioAccelerations[0] * extrapolationRatio[0] * extrapolationRatio[0])
                            + (rgbRatioSpeeds[1] * extrapolationRatio[1]) + (0.5 * rgbRatioAccelerations[1] * extrapolationRatio[1] * extrapolationRatio[1])
                            + (rgbRatioSpeeds[2] * extrapolationRatio[2]) + (0.5 * rgbRatioAccelerations[2] * extrapolationRatio[2] * extrapolationRatio[2]);
#else
        extrapolatedOffset =  (rgbRatioSpeeds[0] * extrapolationRatio[0]) + (rgbRatioAccelerations[0] * extrapolationRatio[0] * extrapolationRatio[0])
                            + (rgbRatioSpeeds[1] * extrapolationRatio[1]) + (rgbRatioAccelerations[1] * extrapolationRatio[1] * extrapolationRatio[0])
                            + (rgbRatioSpeeds[2] * extrapolationRatio[2]) + (rgbRatioAccelerations[2] * extrapolationRatio[2] * extrapolationRatio[0]);
#endif
      }
#endif

      //TODOFT: why is the LUT extrapolation debug preview running on top of the last LUT square?

      //return (extrapolatedOffset) * 5;

			extrapolatedSample = PQ_to_Linear2(clampedSample_PQ + extrapolatedOffset, GCT_MIRROR) * PQNormalizationFactor;
      
#if DEVELOPMENT && 0
      bool oklab = LumaSettings.DevSetting06 >= 0.5;
#else
      bool oklab = false;
#endif
      if (oklab)
      {
#define USE_LENGTH 1
#define USE_PQ 0

        [unroll]
        for (uint i = 0; i < 3; i++)
        {
          float3 numerator = clampedSample_UCS - centeredSamples_UCS[i];
#if USE_LENGTH
          float divisor = length(clampedUV_UCS.yz - centeredUVs_UCS[i].yz);
#else
          float divisor = (abs(clampedUV_UCS.y - centeredUVs_UCS[i].y) + abs(clampedUV_UCS.z - centeredUVs_UCS[i].z)) * 0.5;
#endif
          rgbRatioSpeeds[i] = safeDivision(numerator, divisor); // This doesn't even need safe div
#if USE_PQ
          rgbRatioSpeeds[i] = safeDivision(numerator, abs(clampedUV_PQ[i] - centeredUV_PQ[i]));
#endif
        }
        
#if USE_LENGTH
        float extrapolationRatioUCS = length(unclampedTonemappedUV_UCS.yz - clampedUV_UCS.yz);
#else
        float extrapolationRatioUCS = (abs(unclampedTonemappedUV_UCS.y - clampedUV_UCS.y) + abs(unclampedTonemappedUV_UCS.z - clampedUV_UCS.z)) * 0.5;
#endif
#if USE_PQ
        extrapolationRatioUCS = length(unclampedTonemappedUV_PQ - clampedUV_PQ);
#endif

        extrapolationRatio = abs(extrapolationRatio); // This one is worse (more broken gradients), I can't explain why (what about with the last changes!???)
#if DEVELOPMENT && 0
        if (LumaSettings.DevSetting05 > 0.5) // Seems to look better even if it makes little sense
        {
          //float3 unclampedUV_PQ = Linear_to_PQ2(neutralLUTColorLinear / PQNormalizationFactor, GCT_MIRROR);
          //const float3 centeringNormal = normalize(unclampedUV_PQ - clampedUV_PQ);
          const float3 centeringVectorAbs = abs(unclampedUV - clampedUV);
          extrapolationRatio = centeringVectorAbs;
          //return (extrapolationRatio - centeringVectorAbs) * 100;
        }
#endif

        extrapolationRatio /= extrapolationRatio.x + extrapolationRatio.y + extrapolationRatio.z;
        //extrapolationRatio = normalize(extrapolationRatio);

        extrapolatedOffset = (rgbRatioSpeeds[0] * extrapolationRatio[0]) + (rgbRatioSpeeds[1] * extrapolationRatio[1]) + (rgbRatioSpeeds[2] * extrapolationRatio[2]);
        //extrapolatedOffset *= extrapolationRatioUCS * (1.0 - LumaSettings.DevSetting05);
        extrapolatedOffset *= extrapolationRatioUCS;
        //extrapolatedOffset = clamp(extrapolatedOffset, -3, 3);
        // We exclusively extrapolate the color (hue and chroma) in UCS; we can't extrapolate the luminance for two major reasons:
        // - LStar, the brightness component of UCS is not directly perceptual (e.g. doubling its value doesn't match double perceived brightness), in fact, it's very far from it, its whole range is 0 to ~2.1, with 2.1 representing infinite brightness, so we can't do velocity operations with it, without massive math
        // - We find the color change on each channel (axis) before then extrapolating the color change in the target direction. To do so, we need to find the color velocity ratio on each axis. The "luminance" might not
        //   be relevant at all, because if the green channel was turned into red by the LUT, the luminance would be completely different and not comparable. Also we compare the ratio between the "backwards"/"centered" samples and the target UV one, but they are in completely different directions, so neither the luminance or their chroma/hue can be compared, if not for a generic chroma length test.
        const float3 extrapolatedSample_PQ = extrapolatedSample;
        extrapolatedSample = DarktableUcs::UCSLUVToRGB(float3(clampedSample_UCS.x, clampedSample_UCS.yz + extrapolatedOffset.yz));
        extrapolatedSample = RestoreLuminance(extrapolatedSample, extrapolatedSample_PQ); //TODOFT: this creates some broken gradients?
      }
		}
#pragma warning( default : 4000 )

    if (settings.clipExtrapolationToWhite)
    {
//TODOFT: as we approach white and beyond (roughly the greyscale, direction of white, but beyond 1 1 1), we should not extrapolate as much, at least if the OG LUT mapped white to white, otherwise we'd hue shift white to other colors and fail to properly desaturate.
//Maybe add an "SDR" color param for this (as in, base it on the vanilla TM sampled LUT instead of the clipped HDR sampled LUT).
//Maybe add this as a percentage instead of a toggle.
#if 1
      float3 extrapolatedClampedSample = RestoreLuminance(clampedSample, extrapolatedSample, true);
      float3 whiteClippedExtrapolatedSample = extrapolatedSample;

      uint maxIndexExtrapolated = GetMaxIndex(extrapolatedSample);
      uint midIndexExtrapolated = GetMidIndex(extrapolatedSample);
      uint minIndexExtrapolated = GetMinIndex(extrapolatedSample);

      // - Clamp the new max channel to the original max channel
      // - Clamp the new mid channel to the new max channel
      // - Clamp the new min channel to the new mid channel
      SetIndexValue(whiteClippedExtrapolatedSample, maxIndexExtrapolated, min(max3(extrapolatedClampedSample), whiteClippedExtrapolatedSample[maxIndexExtrapolated]));
      SetIndexValue(whiteClippedExtrapolatedSample, midIndexExtrapolated, min(whiteClippedExtrapolatedSample[maxIndexExtrapolated], whiteClippedExtrapolatedSample[midIndexExtrapolated]));
      SetIndexValue(whiteClippedExtrapolatedSample, minIndexExtrapolated, min(whiteClippedExtrapolatedSample[midIndexExtrapolated], whiteClippedExtrapolatedSample[minIndexExtrapolated]));
      // - Clamp the new min channel to the original min channel
      // - Clamp the new mid channel to the new min channel
      // - Clamp the new max channel to the new mid channel
      SetIndexValue(whiteClippedExtrapolatedSample, minIndexExtrapolated, max(min3(extrapolatedClampedSample), whiteClippedExtrapolatedSample[minIndexExtrapolated]));
      SetIndexValue(whiteClippedExtrapolatedSample, midIndexExtrapolated, max(whiteClippedExtrapolatedSample[minIndexExtrapolated], whiteClippedExtrapolatedSample[midIndexExtrapolated]));
      SetIndexValue(whiteClippedExtrapolatedSample, maxIndexExtrapolated, max(whiteClippedExtrapolatedSample[midIndexExtrapolated], whiteClippedExtrapolatedSample[maxIndexExtrapolated]));

#if 0 // Restore the original chrominance, assuming there was any (it wasn't pure white). Disabled as it creates visible steps in the image.
      float extrapolatedChrominance = GetChrominance(extrapolatedSample);
      whiteClippedExtrapolatedSample = SetChrominance(whiteClippedExtrapolatedSample, extrapolatedChrominance);
#endif

      // Restore the original extrapolated luminance, just because it's likely to be more accurate and holds more detail/nuance
      extrapolatedSample = RestoreLuminance(whiteClippedExtrapolatedSample, extrapolatedSample, true);
#else // This is bad, ultimately each channel needs to work individually and if we prevent hue from flipping, we end up generating broke gradients. There's possibly a lighter version of this idea that might work and help prevent hue shifts in strong highlights, but let's see... // TODO: this version is broken. Delete it?
      // Prevent hue rgb values flipping when extrapolating too much (like if green grows bigger than blue, we want to stop at white, e.g. from 0.9 0.9 1 to 1 1 1 shouldn't then go to 1.1 1.1 1 when we further extrapolate).
      bool rMax = clampedSample.r > clampedSample.g && clampedSample.r > clampedSample.b;
      bool gMax = clampedSample.g > clampedSample.r && clampedSample.g > clampedSample.b;
      bool bMax = clampedSample.b > clampedSample.r && clampedSample.b > clampedSample.g;
      if (rMax || gMax || bMax)
      {
        float3 preHueClampingExtrapolatedSample = extrapolatedSample;
        
        // Clamp to the max color
        int maxChannel = (int)rMax * 0 + (int)gMax * 1 + (int)bMax * 2;
        extrapolatedSample[0] = min(extrapolatedSample[0], max(extrapolatedSample[maxChannel], clampedSample[maxChannel]));
        extrapolatedSample[1] = min(extrapolatedSample[1], max(extrapolatedSample[maxChannel], clampedSample[maxChannel]));
        extrapolatedSample[2] = min(extrapolatedSample[2], max(extrapolatedSample[maxChannel], clampedSample[maxChannel]));
        
        // Clamp to the second max color
        bool rMid = (clampedSample.r > clampedSample.g && clampedSample.r < clampedSample.b) || (clampedSample.r < clampedSample.g && clampedSample.r > clampedSample.b);
        bool gMid = (clampedSample.g > clampedSample.r && clampedSample.g < clampedSample.b) || (clampedSample.g < clampedSample.r && clampedSample.g > clampedSample.b);
        bool bMid = (clampedSample.b > clampedSample.r && clampedSample.b < clampedSample.g) || (clampedSample.b < clampedSample.r && clampedSample.b > clampedSample.g);
        if (rMid || gMid || bMid)
        {
          int minChannel = (int)rMid * 0 + (int)gMid * 1 + (int)bMid * 2;
          if (0 != minChannel && 0 != maxChannel)
            extrapolatedSample[0] = min(extrapolatedSample[0], max(extrapolatedSample[minChannel], clampedSample[minChannel]));
          if (1 != minChannel && 1 != maxChannel)
            extrapolatedSample[1] = min(extrapolatedSample[1], max(extrapolatedSample[minChannel], clampedSample[minChannel]));
          if (2 != minChannel && 2 != maxChannel)
            extrapolatedSample[2] = min(extrapolatedSample[2], max(extrapolatedSample[minChannel], clampedSample[minChannel]));
        }

        // Restore the pre-clamp brightness
        extrapolatedSample = RestoreLuminance(extrapolatedSample, preHueClampingExtrapolatedSample);
      }
#endif
    }

    // Apply the inverse of the original tonemap ratio on the new out of range values (this time they are not necessary out the values beyond 0-1, but the values beyond the clamped/vanilla sample).
    // We don't directly apply the inverse tonemapper formula here as that would make no sense.
		if (settings.inputTonemapToPeakWhiteNits > 0) // Optional optimization in case "inputTonemapToPeakWhiteNits" was static (or not...)
		{
#if 1 //TODOFT: fix text and polish code
      // Try to (partially) consider the new ratio for colors beyond 1, comparing the pre and post LUT (extrapolation) values.
      // For example, if after LUT extrapolation red has been massively compressed, we wouldn't want to apply the inverse of the original tonemapper up to a 100%, or red might go too bright again.
      // Given that we might be extrapolating on the direction of one channel only (as in, the only UV that was beyond 0-1 was the red channel), but that the extrapolation from a single channel direction
      // can actually change all 3 color channels, we can't adjust the tonemapping restoration by channel, and we are forced to do it by length.
      // Given this is about ratios and perception, it might arguably be better done in PQ space, but given the original tonemapper above was done in linear, for the sake of simplicity we also do this in linear.
#if 1 // 1D path (length) for per max channel tonemapper
			//float extrapolationRatio = safeDivision(length(Linear_to_PQ2(extrapolatedSample / PQNormalizationFactor, GCT_MIRROR) - clampedSample_PQ), length(unclampedTonemappedUV_PQ - saturate(unclampedTonemappedUV_PQ)), 0);
			float extrapolationRatio = safeDivision(length(extrapolatedSample - clampedSample), length(neutralLUTColorLinearTonemapped - saturate(neutralLUTColorLinearTonemapped)), 0);
#else // Per channel path for per channel tonemapper
			float3 extrapolationRatio = safeDivision(extrapolatedSample - clampedSample, neutralLUTColorLinearTonemapped - saturate(neutralLUTColorLinearTonemapped), 0); // This is the broken one
#endif
#if 0
      // To avoid too crazy results, we limit the min/max influence the extrapolation can have on the tonemap restoration (at 1, it won't have any influence). The higher the value, the more accurate and tolerant the results (theoretically, in reality they might cause outlier values).
      static const float maxExtrapolationInfluence = 2.5; // Note: expose parameter if needed
			extrapolatedSample = clampedSample + ((extrapolatedSample - clampedSample) * lerp(1, neutralLUTColorLinearTonemappedRestoreRatio, clamp(extrapolationRatio, 1.0 / maxExtrapolationInfluence, maxExtrapolationInfluence)));
#else
			//extrapolatedSample = clampedSample + ((extrapolatedSample - clampedSample) * lerp(1, neutralLUTColorLinearTonemappedRestoreRatio, abs(extrapolationRatio)));
			extrapolatedSample = clampedSample + ((extrapolatedSample - clampedSample) * lerp(1, neutralLUTColorLinearTonemappedRestoreRatio, max(extrapolationRatio, 0)));
#endif
#else // Simpler and faster implementation that doesn't account for the LUT extrapolation ratio of change when applying the inverse of the original tonemap ratio.
			extrapolatedSample = clampedSample + ((extrapolatedSample - clampedSample) * neutralLUTColorLinearTonemappedRestoreRatio);
#endif
		}

    // See the setting description for more information
		if (settings.clampedLUTRestorationAmount > 0)
		{
#if 1
      // Restore the extrapolated sample luminance onto the clamped sample, so we keep the clamped hue and saturation while maintaining the extrapolated luminance.
      float3 extrapolatedClampedSample = RestoreLuminance(clampedSample, extrapolatedSample, true);
#else // Disabled as this can have random results
      float3 unclampedUV_PQ = Linear_to_PQ2(neutralLUTColorLinear / PQNormalizationFactor, GCT_MIRROR); // "neutralLUTColorLinear" is equal to "ColorGradingLUTTransferFunctionOut(unclampedUV, settings.transferFunctionIn, true)"
			float3 extrapolationRatio = unclampedUV_PQ - clampedUV_PQ;
      // Restore the original unclamped color offset on the clamped sample in PQ space (so it's more perceptually accurate).
      // Note that this will cause hue shifts and possibly very random results, it only works on neutral LUTs.
      // This code is not far from "neutralLUTRestorationAmount".
      // Near black we opt for a sum as opposed to a multiplication, to avoid failing to restore the ratio when the source number is zero.
			float3 extrapolatedClampedSample = PQ_to_Linear2(lerp(clampedSample_PQ + extrapolationRatio, clampedSample_PQ * (1.0 + extrapolationRatio), saturate(abs(clampedSample_PQ))), GCT_MIRROR) * PQNormalizationFactor;
#endif
			extrapolatedSample = lerp(extrapolatedSample, extrapolatedClampedSample, settings.clampedLUTRestorationAmount);
		}

		// We can optionally leave or fix negative luminances colors here in case they were generated by the extrapolation, everything works by channel in most games (e.g. Prey), not much is done by luminance, so this isn't needed until proven otherwise.
    // Do done that this might slightly change the "on screen" hue that would have been clipped to >= 0 channels, but in most cases it'd be better anyway
		if (settings.fixExtrapolationInvalidColors)
		{
			FixColorGradingLUTNegativeLuminance(extrapolatedSample);
		}

		outputSample = extrapolatedSample;
#if FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE == 2
		outputSample = neutralLUTColorLinear;
#endif // FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE == 2
#if TEST_LUT_EXTRAPOLATION
		outputSample = 0;
#endif // TEST_LUT_EXTRAPOLATION
	}

  // See the setting description for more information
  // We purposely do this one before "vanillaLUTRestorationAmount", otherwise we'd undo its effects.
  if (settings.neutralLUTRestorationAmount > 0)
	{
    if (!lutOutputLinear)
    {
			outputSample = ColorGradingLUTTransferFunctionOut(outputSample, settings.transferFunctionIn, true);
      lutOutputLinear = true;
    }
    outputSample = lerp(outputSample, neutralLUTColorLinear, settings.neutralLUTRestorationAmount);
  }
  
  // See the setting description for more information
	if (settings.vanillaLUTRestorationAmount > 0)
	{
    // Note that if the vanilla game had UNORM8 LUTs but for our mod they were modified to be FLOAT16, then maybe we'd want to saturate() "vanillaSample", but it's not really needed until proved otherwise
		float3 vanillaSample = SampleLUT(lut, samplerState, saturate(neutralVanillaColorTransferFunctionEncoded), settings, true, true, saturate(neutralVanillaColorLinear));
    if (!lutOutputLinear)
    {
			outputSample = ColorGradingLUTTransferFunctionOut(outputSample, settings.transferFunctionIn, true);
      lutOutputLinear = true;
    }
    if (settings.vanillaLUTRestorationType == 0) // Advanced hue restoration
    {
      outputSample = RestoreHueAndChrominance(outputSample, vanillaSample, settings.vanillaLUTRestorationAmount * 0.25, settings.vanillaLUTRestorationAmount, 0.0); // Restore chrominance instead of hue, it should better preserve highlights desaturation //TODOFT5: try it!!! And just expose them as separate params, for now we defaulted to a decent looking value. 0.25 for BS2 and 0 for Mafia III
      //outputSample = RestoreHueAndChrominance(outputSample, vanillaSample, settings.vanillaLUTRestorationAmount, 0.0, 0.0);
    }
    else
    {
      // Restoration by luminance
		  float3 extrapolatedVanillaSample = RestoreLuminance(vanillaSample, outputSample);
		  outputSample = lerp(outputSample, extrapolatedVanillaSample, settings.vanillaLUTRestorationAmount);
    }
	}

  // If the input and output transfer functions are different, this will perform a transfer function correction (e.g. the typical SDR gamma mismatch: game encoded with gamma sRGB and was decode with gamma 2.2).
  // The best place to do "gamma correction" after LUT sampling and after extrapolation.
  // Most LUTs don't have enough precision (samples) near black to withstand baking in correction.
	// LUT extrapolation is also more correct when run in sRGB gamma, as that's the LUT "native" gamma, correction should still be computed later, only in the 0-1 range.
	// Encoding (gammification): sRGB (from 2.2) crushes blacks, 2.2 (from sRGB) raises blacks.
	// Decoding (linearization): sRGB (from 2.2) raises blacks, 2.2 (from sRGB) crushes blacks.
	if (!lutOutputLinear && settings.outputLinear)
	{
		outputSample.xyz = ColorGradingLUTTransferFunctionOutCorrected(outputSample.xyz, settings.transferFunctionIn, settings.transferFunctionOut);
	}
	else if (lutOutputLinear && !settings.outputLinear)
	{
		if (settings.transferFunctionIn != settings.transferFunctionOut)
		{
		  outputSample.xyz = ColorGradingLUTTransferFunctionIn(outputSample.xyz, settings.transferFunctionIn, true);
      ColorGradingLUTTransferFunctionInOutCorrected(outputSample.xyz, settings.transferFunctionIn, settings.transferFunctionOut, false);
		}
    else
    {
		  outputSample.xyz = ColorGradingLUTTransferFunctionIn(outputSample.xyz, settings.transferFunctionOut, true);
    }
	}
	else if (lutOutputLinear && settings.outputLinear)
	{
    ColorGradingLUTTransferFunctionInOutCorrected(outputSample.xyz, settings.transferFunctionIn, settings.transferFunctionOut, true);
	}
	else if (!lutOutputLinear && !settings.outputLinear)
	{
    ColorGradingLUTTransferFunctionInOutCorrected(outputSample.xyz, settings.transferFunctionIn, settings.transferFunctionOut, false);
	}
	return outputSample;
}

// Returns the lower or higher non clipped edge of the LUT (as 0-1 float, without acknowledging the LUT half texel offset, that should be applied later only when sampling).
// if "backwards" is false, it will find the last texel that is 0 (or well, the texel before the first one that is > 0), given that before there the LUT would be "clipping", as it would have been UNORM and unable to represent values lower than 0.
// if "backwards" is true, it will find the earliest texel that is 1 (or well, the texel before the first one that is > 0), given that from there on the LUT would be "clipping", as it would have been UNORM and unable to represent values higher than 1.
// Once we have this, we can do some form of LUT extrapolation, to allow outputs that would have clipped to 1 before the end of the LUT to keep growing beyond 1, and outputs that would have clipped to 0 after the start of the LUT to keep lowering below 0.
// "maxRange" is the max amount we crawl the LUT of, with 1 being all of it. 0.5 is a good start but if you are doing this per pixel and know the game doesn't really clip LUTs, setting to 0.0375 or so might be a good start. If set to 1 and the LUT had been flattened, min and max could end up being the same, or maybe even flipped? So be careful!
// The texture is expected to be a 2D texture even it's 1 in height.
// Note that if just 1 texel was clipped at the edge of each side, some quadratic interpolation advanced sampling could fit it too, but if it's any more, not really.
// Note that this could end up generating negative (and possibly invalid) colors (expanded gamut) after remapping, if you don't want that, clip the output to >= 0 (but then doing this wouldn't help nearly as much).
float3 Find1DLUTClippingEdge(Texture2D<float4> lut, const uint lutSize, float maxRange /*= 0.5*/, const uint lutRedY /*= 0*/, const uint lutGreenY /*= 0*/, const uint lutBlueY /*= 0*/, const bool backwards)
{
  const uint lutMax = lutSize - 1;

  const uint3 baseLutEdge = backwards ? lutMax : 0;
  uint3 lutEdge = baseLutEdge; // If we never found any (e.g. LUT is all white or black), act as if the LUT had no clipping at all, we don't really have an alternative (this might be a LUT that flips colors)
  bool3 foundEdges = false;

  int searchStart = backwards ? lutMax : 0;
  int searchEnd = backwards ? 0 : lutMax;
  searchEnd += int(lutMax * (1.0 - maxRange) + 0.5) * (backwards ? 1 : -1);
  int searchStep = backwards ? -1 : 1;
  int i = -int(lutMax); // Random default to avoid false positives below
  [loop]
  for (i = searchStart; backwards ? (i >= searchEnd) : (i <= searchEnd); i += searchStep)
  {
    float3 output;
    // Avoid doing 3 samples if they'd all be the same
    if (lutRedY == lutGreenY && lutRedY == lutBlueY)
    {
      output = lut.Load(int3(i, lutRedY, 0)).rgb;
    }
    // This design kinda makes no sense, but some games use it
    else
    {
      output.r = lut.Load(int3(i, lutRedY, 0)).r;
      output.g = lut.Load(int3(i, lutGreenY, 0)).g;
      output.b = lut.Load(int3(i, lutBlueY, 0)).b;
    }
    [unroll]
    for (int k = 0; k <= 2; k++)
    {
      // First crossing on each channel.
      // Here we assume the LUT couldn't output values less than 0, given 1D LUTs were only really used by older games, and had UNORM LUTs.
      // In case the LUT inverted colors, this should detect it immediately and end at the first iteration.
      // Note: we can optionally add some thresholds, even if we assume LUTs to properly sample to the exact float values of 0 and 1.
      // Note: if both the first and second texels were >0 (or <1) while also having the same value, this function doesn't help detect that (it couldn't).
      float threshold = FLT_EPSILON;
      if (!foundEdges[k] && (backwards ? (output[k] < (1.0 - threshold)) : (output[k] > (0.0 + threshold))))
      {
        foundEdges[k] = true;
        lutEdge[k] = backwards ? min(i + 1, lutMax) : max(i - 1, 0);
      }
    }

    if (all(foundEdges))
    {
      break;
    }
  }

  // If we reached the end of our scan without founding the edge, just move the edges to whatever edge we last scanned, as it's the closest result we could get to the actual valid one.
  // This is arguable, as it might be the LUT was flattened to a single color etc. It should ideally be optional.
  i -= searchStep;
  bool wasLast = i == searchEnd;
  if (wasLast)
  {
    lutEdge = foundEdges ? lutEdge : (backwards ? min(i, lutMax) : max(i, 0));
  }

  return float3(lutEdge) / float(lutMax);
}

// See "Find1DLUTClippingEdge"
void Find1DLUTClippingEdges(Texture2D<float4> lut, const uint lutSize, float maxRange /*= 0.5*/, const uint lutRedY /*= 0*/, const uint lutGreenY /*= 0*/, const uint lutBlueY /*= 0*/, out float3 lutMinInput /*= 0*/, out float3 lutMaxInput /*= 1*/)
{
  lutMinInput = Find1DLUTClippingEdge(lut, lutSize, maxRange, lutRedY, lutGreenY, lutBlueY, false);
  lutMaxInput = Find1DLUTClippingEdge(lut, lutSize, maxRange, lutRedY, lutGreenY, lutBlueY, true);
}

// Due to their nature, any LUT that wasn't purely neutral, would have quantized multiple texels to the same value, unless they were more than 8bit or so in their 0-1 output range.
// For example, a LUT could have had an 8 bit gradient like 140 141 141 142; Any sample that fell between the two 141 texels would result in a broken gradient (a gradient suddenly turning flag, and then recovering to its previous angle soon after).
// This formula smooths the results by making each texel the average of the two ones next to it, so the gradient above would turn into something like 140 140.5 (141) 141.5 142. With "(141)"" being the middle value between the two central texels, not an actual texel.
// If more than two texels in a row quantized, this won't be able to smooth that, but that's generally pretty rare so it's fine (we could expand the search radius but it's not really necessary).
// This isn't conceptually far from the tetrahedral interpolation that 2D and 3D LUTs use, however quantization doesn't cause as many issues with them, because the output is always a blend of 8 samples (cube edges).
float4 Sample1DLUTWithSmoothing(Texture2D<float4> lut, const float lutMax, float3 color, const uint lutRedY = 0, const uint lutGreenY = 0, const uint lutBlueY = 0, const uint lutRedChannel = 0, const uint lutGreenChannel = 1, const uint lutBlueChannel = 2, const uint lutAlphaChannel = 3, const bool singleChannelInput = false)
{
  color = saturate(color); // There's nothing to smooth beyond the edges, and texture loads might fail, so clamp!

  int lutMaxI = int(lutMax + 0.5);
  int3 inputTexelCenter = (color * lutMax) + 0.5; // Unused, but here for clarity
  // Floor and take the sample before and the two after
  int3 inputTexelFloored = color * lutMax;
  int3 inputTexelN1 = max(inputTexelFloored - 1, 0); // Not sure the clamps are necessary, it could be Load() already clamps pixels to the valid range, but let's do it anyway
  int3 inputTexelP1 = min(inputTexelFloored + 1, lutMaxI);
  int3 inputTexelP2 = min(inputTexelFloored + 2, lutMaxI);
  
  // The progress between sample 1 and 2
  float4 alpha = float4(frac(color * lutMax), 0.5);
  
  float4 smoothingIntensity = 1.0; // Expose if necessary (as a single float)

  // Disable smoothing around edges, we'd average the edge value with the texel after it (the more central one), which mean 0/black would get raised and 1/white would get lowered.
  smoothingIntensity.rgb = (inputTexelFloored <= 0 || inputTexelFloored >= (lutMaxI - 1)) ? 0.0 : smoothingIntensity.rgb;

  float4 colors[4];

  // See "Sample1DLUTWithExtrapolation" for explanation of these branches.
  if (singleChannelInput)
  {
    float4 tempColor;
    
    tempColor = lut.Load(int3(inputTexelN1.r, lutRedY, 0));
    colors[0].r = tempColor[lutRedChannel];
    colors[0].g = tempColor[lutGreenChannel];
    colors[0].b = tempColor[lutBlueChannel];
    colors[0].a = tempColor[lutAlphaChannel];

    tempColor = lut.Load(int3(inputTexelFloored.r, lutRedY, 0));
    colors[1].r = tempColor[lutRedChannel];
    colors[1].g = tempColor[lutGreenChannel];
    colors[1].b = tempColor[lutBlueChannel];
    colors[1].a = tempColor[lutAlphaChannel];

    tempColor = lut.Load(int3(inputTexelP1.r, lutRedY, 0));
    colors[2].r = tempColor[lutRedChannel];
    colors[2].g = tempColor[lutGreenChannel];
    colors[2].b = tempColor[lutBlueChannel];
    colors[2].a = tempColor[lutAlphaChannel];

    tempColor = lut.Load(int3(inputTexelP2.r, lutRedY, 0));
    colors[3].r = tempColor[lutRedChannel];
    colors[3].g = tempColor[lutGreenChannel];
    colors[3].b = tempColor[lutBlueChannel];
    colors[3].a = tempColor[lutAlphaChannel];

    smoothingIntensity.a = smoothingIntensity.r;
    alpha.a = alpha.r;
  }
  else
  {
    colors[0].r = lut.Load(int3(inputTexelN1.r, lutRedY, 0))[lutRedChannel];
    colors[0].g = lut.Load(int3(inputTexelN1.g, lutGreenY, 0))[lutGreenChannel];
    colors[0].b = lut.Load(int3(inputTexelN1.b, lutBlueY, 0))[lutBlueChannel];
    colors[0].a = 1.0;

    colors[1].r = lut.Load(int3(inputTexelFloored.r, lutRedY, 0))[lutRedChannel];
    colors[1].g = lut.Load(int3(inputTexelFloored.g, lutGreenY, 0))[lutGreenChannel];
    colors[1].b = lut.Load(int3(inputTexelFloored.b, lutBlueY, 0))[lutBlueChannel];
    colors[1].a = 1.0;

    colors[2].r = lut.Load(int3(inputTexelP1.r, lutRedY, 0))[lutRedChannel];
    colors[2].g = lut.Load(int3(inputTexelP1.g, lutGreenY, 0))[lutGreenChannel];
    colors[2].b = lut.Load(int3(inputTexelP1.b, lutBlueY, 0))[lutBlueChannel];
    colors[2].a = 1.0;

    colors[3].r = lut.Load(int3(inputTexelP2.r, lutRedY, 0))[lutRedChannel];
    colors[3].g = lut.Load(int3(inputTexelP2.g, lutGreenY, 0))[lutGreenChannel];
    colors[3].b = lut.Load(int3(inputTexelP2.b, lutBlueY, 0))[lutBlueChannel];
    colors[3].a = 1.0;
  }

#if 1
  // Set our two sampled texel colors to be a blend of their immediate neighbors.
  float4 smoothedColor1 = lerp(colors[0], colors[2], 0.5);
  float4 smoothedColor2 = lerp(colors[1], colors[3], 0.5);
  // Black back to the raw sample value if the intensity is lower.
  smoothedColor1 = lerp(colors[1], smoothedColor1, smoothingIntensity);
  smoothedColor2 = lerp(colors[2], smoothedColor2, smoothingIntensity);

  // Linear interpolation
  float4 finalSmoothedColor = lerp(smoothedColor1, smoothedColor2, alpha);
  return finalSmoothedColor;
#else // Cubic interpolation is probably be better for this, even if LUTs generally always grow in value as they progress, so linear interpolation is okish already // TODO: disabled as this doesn't seem to work as nicely? The math seems right though, maybe it's simply too "conservative" and leaves bad gradients in
  // Linear fallback
  float4 linearColor = lerp(colors[1], colors[2], alpha);

  // Catmull–Rom cubic
  float4 t = alpha;
  float4 t2 = t * t;
  float4 t3 = t2 * t;

  float4 cubicSmoothedColor =
    0.5 * (
      (2.0 * colors[1]) +
      (-colors[0] + colors[2]) * t +
      (2.0 * colors[0] - 5.0 * colors[1] + 4.0 * colors[2] - colors[3]) * t2 +
      (-colors[0] + 3.0 * colors[1] - 3.0 * colors[2] + colors[3]) * t3
    );

  float4 finalSmoothedColor = lerp(linearColor, cubicSmoothedColor, smoothingIntensity);
  return finalSmoothedColor;
#endif
}

// Sample that allows to go beyond the 0-1 coordinates range of a 1D horizontal LUT through extrapolation.
// It finds the rate of change (acceleration) of the LUT color around the requested clamped coordinates, and guesses what color the sampling would have with the out of range coordinates.
// Extrapolating LUT by re-apply the rate of change has the benefit of consistency. If the LUT has the same color at (e.g.) uv 0.9 0.9 and 1.0 1.0, thus clipping to white or black, the extrapolation might also stay clipped (use "Find1DLUTClippingEdges" to handle that).
// Additionally, if the LUT had inverted colors or highly fluctuating colors, extrapolation would work a lot better than a raw LUT out of range extraction with a luminance multiplier.
//
// The performance impact is low as this only does one extra sample per pixel (unless smoothing is on).
// For best results, this should be paired with "Find1DLUTClippingEdges", run around the extrapolation, though by default it's not because it's expensive to run that per pixel, and it's not needed in most games.
// This function does not acknowledge the LUT transfer function nor any specific LUT properties (as long as the input and output encoding are the same, it should be ok, extrapolation generally works better in a perceptual encoding though).
// Note that this function might return "invalid colors", they could have negative values etc etc, so make sure to clamp them after if you need to.
// This version is for a 2D float4 texture with a single gradient per channel.
float4 Sample1DLUTWithExtrapolation(Texture2D<float4> lut, SamplerState linearSampler, float3 unclampedColor, const uint lutRedY = 0, const uint lutGreenY = 0, const uint lutBlueY = 0, const uint lutRedChannel = 0, const uint lutGreenChannel = 1, const uint lutBlueChannel = 2, const uint lutAlphaChannel = 3, const bool singleChannelInput = false, const bool enableExtrapolation = true, const bool enableSmoothing = true, float backwardsAmount = 0.5)
{
  // LUT size in texels
  float lutWidth;
  float lutHeight;
  lut.GetDimensions(lutWidth, lutHeight); // TODO: optionally pass this in
  const float2 lutSize = float2(lutWidth, lutHeight);
  const float2 lutMax = lutSize - 1.0;
  const float2 uvScale = lutMax / lutSize;        // Also "1-(1/lutSize)"
  const float2 uvOffset = 1.0 / (2.0 * lutSize);  // Also "(1/lutSize)/2" or "(0.5/lutSize)"
  // The uv distance between the center of one texel and the next one
  const float2 lutTexelRange = (lutMax.y == 0.0) ? 0.5 : (1.0 / lutMax);

  const float3 clampedColor = saturate(unclampedColor);
  const float3 distanceFromUnclampedToClamped = abs(unclampedColor - clampedColor); // Note: abs() here is probably not necessary as it cancels itself out with "distanceFromClampedToCentered"
  const bool3 uvOutOfRange = distanceFromUnclampedToClamped > FLT_MIN; // Some threshold is needed to avoid divisions by tiny numbers

  // y (uv.y)
  const float3 v = (lutMax.y == 0.0) ? 0.5 : (float3(lutRedY, lutGreenY, lutBlueY) / lutMax.y);

  float4 clampedSample;
  if (enableSmoothing)
  {
    clampedSample = Sample1DLUTWithSmoothing(lut, lutMax.x, unclampedColor, lutRedY, lutGreenY, lutBlueY, lutRedChannel, lutGreenChannel, lutBlueChannel, lutAlphaChannel, singleChannelInput);
  }
  else
  {
    // If the input was "float" as opposed to "float3" ("singleChannelInput"), only do one sample and swizzle the channels as requested.
    if (singleChannelInput) // We assume "lutRedY", "lutGreenY" and "lutBlueY" are all equal, as they should in this case
    {
      float4 tempClampedSample;
      tempClampedSample = lut.Sample(linearSampler, (float2(clampedColor.r, v.r) * uvScale) + uvOffset);
      clampedSample.r = tempClampedSample[lutRedChannel];
      clampedSample.g = tempClampedSample[lutGreenChannel];
      clampedSample.b = tempClampedSample[lutBlueChannel];
      clampedSample.a = tempClampedSample[lutAlphaChannel];
    }
    else
    {
      // Use "clampedColor" instead of "unclampedColor" as we don't know what kind of sampler was in use here.
      // Some games have one stripe for all channels, simply sampling the sample stripe with a different r/g/b color, meaning the LUT could only ever change contrast, or fade to white/black etc.
      // Other games sample the same stripe for all color channels, but read a different channel for each output.
      // Yet other games sample a different stripe for each channel, and potentially also a different chanel for each output.
      // This code supports all variations.
      clampedSample.r = lut.Sample(linearSampler, (float2(clampedColor.r, v.r) * uvScale) + uvOffset)[lutRedChannel];
      clampedSample.g = lut.Sample(linearSampler, (float2(clampedColor.g, v.g) * uvScale) + uvOffset)[lutGreenChannel];
      clampedSample.b = lut.Sample(linearSampler, (float2(clampedColor.b, v.b) * uvScale) + uvOffset)[lutBlueChannel];
      clampedSample.a = 1.0; // Default alpha to 1 (a neutral value), it's likely the calling code won't read it, as it'd make no sense in this case
    }
  }

  float4 finalColor = clampedSample;

  if (enableExtrapolation && any(uvOutOfRange))
  {
    // "backwardsAmount" is the distance to travel back of, it's best if it is ~0.5 to avoid the hue shifts that are often at the edges of LUTs, and also prevents the edges from being clamped.
    if (backwardsAmount <= 0.0)
      backwardsAmount = lutTexelRange.x * 2.0; // Travel back by 2 texels, 1 single texel offset can often end up having the same color, especially around the edges, due to the quantization to 8 bit

    float3 centeredColor = unclampedColor;
    // Don't go backwards more than the center, or it'd get messy (we shouldn't even be here if that would have happened)
    centeredColor = clampedColor >= 0.5 ? max(clampedColor - backwardsAmount, 0.5) : min(clampedColor + backwardsAmount, 0.5);

    float4 centeredSample;
    // No need to use "Sample1DLUTWithSmoothing" here, it's barely make any difference, and possibly would be less accurate
    // TODO: try a higher quality mode that considers two backwards samples. Though taking two samples in two different backwards points and blending them also looks worse in "Hollow Knight: Silksong" (does it really?).
    if (singleChannelInput)
    {
      float4 tempCenteredSample;
      tempCenteredSample = lut.Sample(linearSampler, (float2(centeredColor.r, v.r) * uvScale) + uvOffset);
      centeredSample.r = tempCenteredSample[lutRedChannel];
      centeredSample.g = tempCenteredSample[lutGreenChannel];
      centeredSample.b = tempCenteredSample[lutBlueChannel];
      centeredSample.a = tempCenteredSample[lutAlphaChannel];
    }
    else
    {
      centeredSample.r = lut.Sample(linearSampler, (float2(centeredColor.r, v.r) * uvScale) + uvOffset)[lutRedChannel];
      centeredSample.g = lut.Sample(linearSampler, (float2(centeredColor.g, v.g) * uvScale) + uvOffset)[lutGreenChannel];
      centeredSample.b = lut.Sample(linearSampler, (float2(centeredColor.b, v.b) * uvScale) + uvOffset)[lutBlueChannel];
      centeredSample.a = 1.0;
    }
    const float3 distanceFromClampedToCentered = abs(clampedColor - centeredColor);
    const float3 extrapolationRatio = (!uvOutOfRange || distanceFromClampedToCentered == 0.0) ? 0.0 : (distanceFromUnclampedToClamped / distanceFromClampedToCentered);
    const float4 extrapolationRatio4 = singleChannelInput ? extrapolationRatio.r : float4(extrapolationRatio.rgb, 0.0);
    const float4 extrapolatedColor = lerp(centeredSample, clampedSample, 1.0 + extrapolationRatio4); // Extrapolate individually on every each channel (it couldn't really be otherwise, 1D LUTs don't have any cross pollution between channels)
    finalColor = extrapolatedColor;
  }

  return finalColor;
}

// Note that this function expects "LUT_SIZE" to be divisible by 2. If your LUT is (e.g.) 15x instead of 16x, move some math to be floating point and round to the closest pixel.
// "PixelPosition" is expected to be centered around texles center, so the first pixel would be 0.5 0.5, not 0 0.
// This partially mirrors "ShouldSkipPostProcess()".
float3 DrawLUTTexture(LUT_TEXTURE_TYPE lut, SamplerState samplerState, float2 PixelPosition, inout bool DrawnLUT, bool inLinear = false, bool outLinear = false)
{
	const uint LUTMinPixel = 0; // Extra offset from the top left
	uint LUTMaxPixel = LUT_MAX; // Bottom (right) limit
	uint LUTSizeMultiplier = 1;
  uint PixelScale = DRAW_LUT_TEXTURE_SCALE;
#if ENABLE_LUT_EXTRAPOLATION
	LUTSizeMultiplier = 2; // This will end up multiplying the number of shown cube slices as well
	// Shift the LUT coordinates generation to account for 50% of extra area beyond 1 and 50% below 0,
	// so "LUTPixelPosition3D" would represent the LUT from -0.5 to 1.5 before being normalized.
	// The bottom and top 25% squares (cube sections) will be completely outside of the valid cube range and be completely extrapolated,
	// while for the middle 50% squares, only their outer half would be extrapolated.
	LUTMaxPixel += LUT_SIZE * (LUTSizeMultiplier - 1);
	PixelScale = round(pow(PixelScale, 1.f / LUTSizeMultiplier));
#endif // ENABLE_LUT_EXTRAPOLATION

	const uint LUTPixelSideSize = LUT_SIZE * LUTSizeMultiplier; // LUT pixel size (one dimension) on screen (with extrapolated pixels too)
	float2 LUTPixelPosition2DFloat = (PixelPosition / (float)PixelScale) - 0.5; // We remove the half texel offsets even if in float (this is fine to do after scaling)!
	const uint2 LUTPixelPosition2D = round(LUTPixelPosition2DFloat); // Round to account for any scaling and snap to the closest texel
	const uint3 LUTPixelPosition3D = uint3(LUTPixelPosition2D.x % LUTPixelSideSize, LUTPixelPosition2D.y, LUTPixelPosition2D.x / LUTPixelSideSize); // Yes the modulo and division by "LUTPixelSideSize" are correct
	if (!any(LUTPixelPosition3D < LUTMinPixel) && !any(LUTPixelPosition3D > LUTMaxPixel))
	{
    // Note that the LUT sampling function will still use bilinear sampling, we are just manually centering the LUT coordinates to match the center of texels.
    // Note that turning this on might slightly change the edges of each LUT slice, as it uses rounding on the pixel coordinates.
		static const bool NearestNeighbor = false;

		DrawnLUT = true;

		// The color the neutral LUT would have, in sRGB gamma space (with no half texel offsets)
    float3 LUTCoordinates;

    if (NearestNeighbor)
    {
      LUTCoordinates = LUTPixelPosition3D / float(LUTMaxPixel);
    }
    else
    {
		  float3 LUTPixelPosition3DFloat = float3(fmod(LUTPixelPosition2DFloat.x, LUTPixelSideSize), LUTPixelPosition2DFloat.y, (uint)(LUTPixelPosition2DFloat.x / LUTPixelSideSize)); // The Z dimension needs to be "quantized" as we don't have enough detail in the source uv for it (a limitation of the 2D<->3D mapping logic)
      LUTCoordinates = LUTPixelPosition3DFloat / float(LUTMaxPixel);
    }
    LUTCoordinates *= LUTSizeMultiplier;
    LUTCoordinates -= (LUTSizeMultiplier - 1.f) / 2.f;
#if ENABLE_LUT_EXTRAPOLATION && TEST_LUT_EXTRAPOLATION
    if (any(LUTCoordinates < -FLT_MIN) || any(LUTCoordinates > 1.f + FLT_EPSILON))
    {
		  return 0;
    }
#endif // ENABLE_LUT_EXTRAPOLATION && TEST_LUT_EXTRAPOLATION

    LUTExtrapolationData extrapolationData = DefaultLUTExtrapolationData();
    extrapolationData.inputColor = LUTCoordinates.rgb;
    extrapolationData.vanillaInputColor = LUTCoordinates.rgb;

    LUTExtrapolationSettings extrapolationSettings = DefaultLUTExtrapolationSettings();
    extrapolationSettings.enableExtrapolation = bool(ENABLE_LUT_EXTRAPOLATION);
    extrapolationSettings.extrapolationQuality = LUT_EXTRAPOLATION_QUALITY;
#if DEVELOPMENT && 1 // These match the settings defined in "HDRFinalScenePS" (in case you wanted to preview them)
    //extrapolationSettings.inputTonemapToPeakWhiteNits = 1000.0;
    extrapolationSettings.inputTonemapToPeakWhiteNits = 10000 * LumaSettings.DevSetting01;
    //extrapolationSettings.clampedLUTRestorationAmount = 1.0 / 4.0;
    //extrapolationSettings.vanillaLUTRestorationAmount = 1.0 / 3.0;
    
    extrapolationSettings.extrapolationQuality = LumaSettings.DevSetting03 * 2.99; //TODOFT
    extrapolationSettings.backwardsAmount = LumaSettings.DevSetting04;
    //if (extrapolationSettings.extrapolationQuality >= 2) extrapolationSettings.backwardsAmount = 2.0 / 3.0;
#endif
    extrapolationSettings.inputLinear = false;
    extrapolationSettings.lutInputLinear = inLinear;
    extrapolationSettings.lutOutputLinear = outLinear;
    extrapolationSettings.outputLinear = bool(POST_PROCESS_SPACE_TYPE == 1);
    extrapolationSettings.transferFunctionIn = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB;
// We might not want gamma correction on the debug LUT, gamma correction comes after extrapolation and isn't directly a part of the LUT, so it shouldn't affect its "raw" visualization
#if 1
    extrapolationSettings.transferFunctionOut = (bool(POST_PROCESS_SPACE_TYPE == 1) && GAMMA_CORRECTION_TYPE == 1) ? LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2 : extrapolationSettings.transferFunctionIn;
#else
    extrapolationSettings.transferFunctionOut = extrapolationSettings.transferFunctionIn;
#endif
    extrapolationSettings.samplingQuality = (HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS || ENABLE_LUT_TETRAHEDRAL_INTERPOLATION) ? (ENABLE_LUT_TETRAHEDRAL_INTERPOLATION ? 2 : 1) : 0;

		const float3 LUTColor = SampleLUTWithExtrapolation(lut, samplerState, extrapolationData, extrapolationSettings);
    return LUTColor;
	}
	return 0;
}

#endif // SRC_COLOR_GRADING_LUT_HLSL