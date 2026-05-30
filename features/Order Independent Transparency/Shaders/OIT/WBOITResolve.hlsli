#ifndef __WBOIT_RESOLVE__
#define __WBOIT_RESOLVE__

#define OIT_RESOLVE_FUNC WBOITResolve

Texture2D<float4> TexWBOITAccum : register(t0);
Texture2D<float2> TexWBOITRevealage : register(t1);
Texture2D<float4> TexWBOITFrontAccum : register(t2);

void WBOITResolve(uint2 pixelAddr, out float4 ocolor, out float4 wcolor)
{
	float4 accumAll = TexWBOITAccum[pixelAddr];
	float4 accumFront = TexWBOITFrontAccum[pixelAddr];
	float2 revealage = saturate(TexWBOITRevealage[pixelAddr]);

	float allAlpha = 1.0 - revealage.x;
	float frontAlpha = 1.0 - revealage.y;
	float3 allColor = accumAll.a > 1e-5 ? accumAll.rgb / accumAll.a : 0.0.xxx;
	float3 frontColor = accumFront.a > 1e-5 ? accumFront.rgb / accumFront.a : 0.0.xxx;

	wcolor = float4(allColor * allAlpha, revealage.x);
	ocolor = float4(frontColor * frontAlpha, revealage.y);
}

#endif
