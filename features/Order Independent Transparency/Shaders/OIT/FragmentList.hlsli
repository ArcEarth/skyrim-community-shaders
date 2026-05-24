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

#include "OIT/OITCommon.hlsli"

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

Texture2D<uint> gFragmentListFirstNodeAddressSRV : register(t1);
StructuredBuffer<FragmentListNode> gFragmentListNodesSRV : register(t2);

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
#if OIT_WRITE_DEPTH
	// return -depth;
	return !(flags & OIT_FLAGS_DEPTH_WRITE) ? depth : -depth;
#else
	return depth;
#endif
	//return asfloat((asuint(depth) & 0xFFFFFFF0UL) | flags);
}

void FL_UnpackDepthAndFlags(in float packedDepthCovg, out float depth, out uint flags)
{
	depth = abs(packedDepthCovg);
	flags = packedDepthCovg < 0 ? OIT_FLAGS_DEPTH_WRITE : 0;
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

	uint maxNodes, stride;
	gFragmentListNodesUAV.GetDimensions(maxNodes, stride);

	return newNodeAddress1D <= maxNodes; //SharedData::orderIndependentTransparencySettings.MaxListNodes;
}

// Insert a new node at the head of the list
void FL_InsertNode(in int2 screenAddress, in uint newNodeAddress, in FragmentListNode newNode)
{
	uint oldNodeAddress;
	InterlockedExchange(gFragmentListFirstNodeAddressUAV[screenAddress], newNodeAddress, oldNodeAddress);

	newNode.next = oldNodeAddress;
	gFragmentListNodesUAV[newNodeAddress] = newNode;
}

bool OIT_CaptureImpl(in int2 screenAddress, in float4 color, in float depth, uint flags)
{
	uint packed = FL_PackColor(color);
	// discard fully transparent pixels
	if (packed == 0x00000000UL)
		return true;

	uint newNodeAddress;
	if (FL_AllocNode(newNodeAddress))
	{
		FragmentListNode node;
		node.color = packed;
		node.depth = FL_PackDepthAndFlags(depth, flags);
		FL_InsertNode(screenAddress, newNodeAddress, node);
		return true;
	}
	return false; // return original color if we failed to allocate a new node
}

FragmentListNode FL_GetNode(uint nodeAddress)
{
	return gFragmentListNodesSRV[nodeAddress];
}

#endif // H_FRAGMENT_LIST
