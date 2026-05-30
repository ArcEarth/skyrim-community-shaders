#ifndef __WBOIT_WEIGHT__
#define __WBOIT_WEIGHT__

float WBOITComputeWeight(float alpha, float depth)
{
	float depthWeight = max(0.01, 3000.0 * pow(1.0 - depth, 3.0));
	float a = max(0.01, alpha);
	return depthWeight * (alpha > 0.0 ? a : 0.2);
}

#endif
