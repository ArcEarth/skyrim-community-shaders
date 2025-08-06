/////////////////////////////////////////////////////////////////////////////////////////////
// Copyright 2017 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
/////////////////////////////////////////////////////////////////////////////////////////////

#ifndef H_FRAGMENT_LIST
#define H_FRAGMENT_LIST

// #include "../../../../package/Shaders/Common/FrameBuffer.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/Permutation.hlsli"

//////////////////////////////////////////////
// Structs
//////////////////////////////////////////////

struct FragmentListNode
{
	uint next;
	float depth;
	uint color;
};

//////////////////////////////////////////////
// Resources
//////////////////////////////////////////////

RWTexture2D<uint> gFragmentListFirstNodeAddressUAV : register(u3);
RWStructuredBuffer<FragmentListNode> gFragmentListNodesUAV : register(u4);

Texture2D<uint> gFragmentListFirstNodeAddressSRV : register(t0);
StructuredBuffer<FragmentListNode> gFragmentListNodesSRV : register(t1);

//////////////////////////////////////////////
// Helper Functions
//////////////////////////////////////////////

int2 FL_GetDimensions()
{
	int2 dim;
	gFragmentListFirstNodeAddressSRV.GetDimensions(dim.x, dim.y);

	return dim;
}

// flags have 4 bits
float FL_PackDepthAndFlags(in float depth, in uint flags)
{
	return depth;
	//return asfloat((asuint(depth) & 0xFFFFFFF0UL) | flags);
}

void FL_UnpackDepthAndFlags(in float packedDepthCovg, out float depth, out uint flags)
{
	depth = packedDepthCovg;
	flags = 0;
	//uint uiPackedDepthCovg = asuint(packedDepthCovg);
	//depth = asfloat(uiPackedDepthCovg & 0xFFFFFFF0UL);
	//flags = uiPackedDepthCovg & 0xFUL;
}

float4 FL_UnpackColor(uint packedInput)
{
	float4 unpackedOutput;
	uint4 p = uint4((packedInput & 0xFFUL),
					(packedInput >> 8UL) & 0xFFUL,
					(packedInput >> 16UL) & 0xFFUL,
					(packedInput >> 24UL));

	unpackedOutput = ((float4) p) / 255;
	return unpackedOutput;
}

uint FL_PackColor(float4 unpackedInput)
{
	uint4 u = (uint4) (saturate(unpackedInput) * 255 + 0.5);
	uint packedOutput = (u.w << 24UL) | (u.z << 16UL) | (u.y << 8UL) | u.x;
	return packedOutput;
}


uint FL_GetFirstNodeOffset(int2 screenAddress)
{
	return gFragmentListFirstNodeAddressSRV[screenAddress];
}

bool FL_AllocNode(out uint newNodeAddress1D)
{
	// alloc a new node
	newNodeAddress1D = gFragmentListNodesUAV.IncrementCounter();

	//uint maxNodes, stride;	
	//gFragmentListNodesUAV.GetDimensions(maxNodes, stride);

	return newNodeAddress1D <= SharedData::orderIndependentTransparencySettings.MaxListNodes;
}

// Insert a new node at the head of the list
void FL_InsertNode(in int2 screenAddress, in uint newNodeAddress, in FragmentListNode newNode)
{
	uint oldNodeAddress;
	InterlockedExchange(gFragmentListFirstNodeAddressUAV[screenAddress], newNodeAddress, oldNodeAddress);

	newNode.next = oldNodeAddress;
	gFragmentListNodesUAV[newNodeAddress] = newNode;
}

float4 OIT_Capture(in int2 screenAddress, in float4 color, in float depth)
{
	// discard pixels that are too far
	if (depth > SharedData::orderIndependentTransparencySettings.DepthThreshold)
		return color;

	uint flags = ((Permutation::ExtraFeatureDescriptor >> 10) & 0x3);

	// uniform branching to skip OIT for multiplicative blend when not supported
	[branch]
	if ((flags & 0x2) && (SharedData::orderIndependentTransparencySettings.Flags == 0))
		return color;

	float4 zero = (flags & 0x2) ? float4(1, 1, 1, 1) : float4(0, 0, 0, 0);
	
	float4 incolor = color;
	[flatten]
	if (flags & 0x1) // additive blend
		color = float4(color.xyz * color.w, 0.f);
	else if (flags & 0x2) // multiplicative blend, fully supported needs per channel alpha
		color = float4(0, 0, 0, 1 - color.x);
	else // regular alpha blend
		color = float4(color.xyz * color.w, color.w);
	
	int packed = FL_PackColor(color);
	// discard fully transparent pixels
	if (packed == 0x00000000 || all(color <= SharedData::orderIndependentTransparencySettings.AlphaThreshold.xxxx))
		return zero;

	uint newNodeAddress;
	if (FL_AllocNode(newNodeAddress))
	{
		FragmentListNode node;
		node.color = packed;
		node.depth = FL_PackDepthAndFlags(depth, flags);
		FL_InsertNode(screenAddress, newNodeAddress, node);
		return zero;
	}
	return incolor; // return original color if we failed to allocate a new node
}

FragmentListNode FL_GetNode(uint nodeAddress)
{
	return gFragmentListNodesSRV[nodeAddress];
}

#endif // H_FRAGMENT_LIST