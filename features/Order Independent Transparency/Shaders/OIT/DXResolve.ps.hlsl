#include "OIT/DXAOITResolve.hlsli"
#include "Upscaling/UpscaleVS.hlsl"
#define PS_INPUT VS_OUTPUT

struct PS_OUT
{
	float4 Color : SV_Target0;
	float4 Alpha : SV_Target1;
};

PS_OUT main(PS_INPUT input)
{
	PS_OUT psout;
	float2 address = uint2(input.Position.xy);
	float4 color;
	float4 wcolor;
	
	OIT_RESOLVE_FUNC(address, color, wcolor);

	color.w = 1.0 - color.w;
	wcolor.w = 1.0 - wcolor.w;
	psout.Color = color;
	psout.Alpha = wcolor;
	return psout;
}
