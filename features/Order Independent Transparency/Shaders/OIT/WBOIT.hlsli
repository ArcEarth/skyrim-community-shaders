#ifndef __WBOIT__
#define __WBOIT__

#include "OIT/OITCommon.hlsli"
#include "OIT/WBOITWeight.hlsli"
#include "Common/SharedData.hlsli"

Texture2D<float> OITWaterDepthTexture : register(t120);

struct WBOITResult
{
	float4 accumAll;
	float4 revealage;
	float4 accumFront;
};

float4 WBOITQuantizePrototypeColor(float4 color)
{
	return floor(saturate(color) * 255.0 + 0.5) / 255.0;
}

WBOITResult WBOITCapture(float4 color, int2 screenAddress, float depth, uint flags)
{
	WBOITResult result;
	result.accumAll = 0.0.xxxx;
	result.revealage = 1.0.xxxx;
	result.accumFront = 0.0.xxxx;

	uint blend = flags & OIT_FLAGS_BLEND_MODS;

#if !OIT_CAPTURE_IGNORE_ALPHA_THRESHOULD
	color.w = max(0.f, color.w - SharedData::orderIndependentTransparencySettings.AlphaThreshold) / (1.f - SharedData::orderIndependentTransparencySettings.AlphaThreshold);
#endif

	if (blend == OIT_FLAGS_MULTIPLICATIVE_A)
	{
		color = float4(0.0, 0.0, 0.0, (1.0 - color.w) * color.a);
	}
	else if (blend == OIT_FLAGS_ADDITIVE)
	{
		color = float4(color.xyz * color.w, 0.0);
	}
	else if (blend == OIT_FLAGS_MULTIPLICATIVE)
	{
		color = float4(0.0, 0.0, 0.0, 1.0 - color.a);
	}
	else
	{
		color = float4(color.xyz * color.w, color.w);
	}

	color = saturate(color);

	float a = color.w == 0.f ? 0.01 * sqrt(sqrt(length(color.xyz / sqrt(3)))) : color.w;
	color.w = a;
	// There is a OM blend bug that clamps the accumalated color/alpha to 1.0
	// Thus scale it down to prevent overflow, constant weight cancels out in the end
	// Find a proper weight function seems hard
	// The weight provided by the paper for projected depth does not work well here
	// Particularly bad for the vanilla rain particles in screen space
	float weight = 0.05 * WBOITComputeWeight(a, depth);

	float waterDepth = OITWaterDepthTexture[screenAddress];
	bool frontOfWater = depth <= waterDepth;

	result.accumAll = color * weight;
	result.accumFront = frontOfWater ? result.accumAll : 0.0.xxxx;
	result.revealage = float4(1.0 - a, frontOfWater ? 1.0 - a : 1.0, 0, 0);
	return result;
}

#endif
