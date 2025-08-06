#include "OIT/DXAOITResolve.hlsli"

// Main diffuse color render target, only output fragments in front of water surface
RWTexture2D<float4> RT_MAIN : register(u0);
// Alpha render target, include fragments behind water surface for refraction and reflection in next frame
RWTexture2D<float4> RT_ALPHA : register(u1);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	uint2 screenAddress = DTid.xy;
	float4 color;
	float4 wcolor;
	
	OIT_RESOLVE_FUNC(DTid.xy, color, wcolor);

	wcolor.w = 1.0 - wcolor.w;
	RT_MAIN[DTid.xy] = float4(RT_MAIN[DTid.xy].rgb * color.w + color.xyz, 1.f);
	RT_ALPHA[DTid.xy] = wcolor;
}
