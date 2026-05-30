#ifndef __WBOIT_RESOLVE__
#define __WBOIT_RESOLVE__

#define OIT_RESOLVE_FUNC WBOITResolve

Texture2D<float4> TexWBOITFrontAccumalation : register(t0);
Texture2D<float4> TexWBOITAccumalation : register(t1);
Texture2D<float2> TexWBOITRevealage : register(t2);

void WBOITResolve(uint2 pixelAddr, out float4 ocolor, out float4 wcolor)
{
	float4 accumFront = TexWBOITFrontAccumalation[pixelAddr];
	float4 accumAll = TexWBOITAccumalation[pixelAddr];
	float2 revealage = saturate(TexWBOITRevealage[pixelAddr]);
	accumAll += 0.0001.xxxx;
	accumFront += 0.0001.xxxx;

	float allAlpha = 1.0 - revealage.x;
	float frontAlpha = 1.0 - revealage.y;
	float3 allColor = accumAll.rgb / accumAll.a;
	float3 frontColor = accumFront.rgb / accumFront.a;

	wcolor = float4(allColor * allAlpha, revealage.x);
	ocolor = float4(frontColor * frontAlpha, revealage.y);
}

#endif
