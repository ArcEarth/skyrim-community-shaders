#ifndef __WBOIT__
#define __WBOIT__

#include "OIT/OITCommon.hlsli"
#include "Common/SharedData.hlsli"

Texture2D<float> OITWaterDepthTexture : register(t120);

struct WBOITResult
{
	float4 accumAll;
	float4 revealage;
	float4 accumFront;
};

float WBOITComputeWeight(float alpha, float depth)
{
	float depthWeight = 1.0 - saturate(depth);
	float weight = pow(min(1.0, saturate(alpha) * 10.0) + 0.01, 3.0) * (1e2 * (depthWeight * depthWeight * depthWeight + 1e-2));
	return clamp(weight, 1e-2, 1e3);
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

	float3 premulColor = 0.0.xxx;
	float coverageAlpha = 0.0;
	float weightAlpha = 0.0;
	if (blend == OIT_FLAGS_MULTIPLICATIVE_A) {
		coverageAlpha = saturate((1.0 - color.w) * color.w);
		weightAlpha = max(coverageAlpha, 1.0 / 255.0);
	} else if (blend == OIT_FLAGS_ADDITIVE) {
		premulColor = color.xyz * color.w;
		coverageAlpha = saturate(color.w * 0.25);
		weightAlpha = max(color.w, 1.0 / 255.0);
	} else if (blend == OIT_FLAGS_MULTIPLICATIVE) {
		coverageAlpha = saturate(1.0 - color.w);
		weightAlpha = max(coverageAlpha, 1.0 / 255.0);
	} else {
		premulColor = color.xyz * color.w;
		coverageAlpha = saturate(color.w);
		weightAlpha = max(coverageAlpha, 1.0 / 255.0);
	}

	if (coverageAlpha <= 0.0 && dot(premulColor, 1.0.xxx) <= 0.0)
		return result;

	float weight = WBOITComputeWeight(weightAlpha, depth);
	float waterDepth = OITWaterDepthTexture[screenAddress];
	bool frontOfWater = waterDepth <= 0.0 || depth <= waterDepth;

	result.accumAll = float4(premulColor, weightAlpha) * weight;
	result.accumFront = frontOfWater ? result.accumAll : 0.0.xxxx;
	result.revealage = float4(1.0 - coverageAlpha, frontOfWater ? 1.0 - coverageAlpha : 1.0, 1.0, 1.0);
	return result;
}

#endif
