/*
** ClusterAssignCpu.h
**
** UZDoom-only (plan M6.1): the light -> cluster assignment on the CPU, for backends without compute shaders
** (GL 3.3 .. 4.2). It implements exactly the algorithm of
** lightcluster.comp (ClusterAssignProgram.cpp) and produces the same buffer contents:
**
**   offsets[cluster] = index (in vec4 units) of the cluster's header in lights[], or -1 for an empty cluster
**   lights[offset]   = vec4(0, 4*nModulated, 4*(nModulated+nSubtractive), 4*(nModulated+nSubtractive+nAdditive))
**   lights[offset+1 ...] = the light records, 4 vec4 each: modulated lights, then subtractive, then additive
**
** which is the record layout of FLightBuffer::UploadLights / func_defaultlight.fp (ivec4(lights[i]) + i + 1 are the
** range limits). Inside a range lights are in ascending input order. A cluster keeps at most MaxPerCluster lights; when
** more touch it the ones nearest to the cluster box (squared distance, then input index) are kept. Clusters are
** placed one after the other by prefix sum in cluster order; a cluster that does not fit in the capacity gets -1.
**
** This file has no engine dependencies.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#pragma once

#include <cstdint>
#include <vector>
#include "OpenGL/Renderers/Legacy/World/Lights/ClusterGrid.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Lights
{

enum ClusterBucket
{
	ClusterBucketModulated = 0,		// FDynLightData::arrays[0]
	ClusterBucketSubtractive = 1,	// arrays[1]
	ClusterBucketAdditive = 2,		// arrays[2]
};

// One light, 16 floats in the layout of AddLightToList (hw_dynlightdata.cpp): [0..2] x, height, y; [3] radius; [4..6] r, g, b;
// [7] shadow index (negative = attenuated); [8..10] spot direction; [11] light type (1 = spot); [12] cos inner; [13] cos outer.
// In the assignment input [14] holds the ClusterBucket; in the output records [14] and [15] are 0.
struct ClusterLight
{
	float f[16];
};

struct ClusterAssignOptions
{
	int MaxPerCluster = 128;
	uint32_t CapacityVec4 = 0;
	// Identical light sets share one record (needed when lights[] is a uniform block of a few thousand vec4). The record
	// positions then differ from the compute shader's, the decoded per cluster content is the same.
	bool Dedupe = false;
};

struct ClusterAssignResult
{
	std::vector<int32_t> Offsets;
	std::vector<float> Lights;		// 4 floats per vec4
	uint32_t UsedVec4 = 0;
	uint32_t OverflowClusters = 0;	// clusters that had more than MaxPerCluster lights
	uint32_t DroppedClusters = 0;	// clusters that did not fit in CapacityVec4 (offset -1)
};

class ClusterAssignCpu
{
public:
	// A light in view space, the form the cluster test works on.
	struct ViewLight
	{
		float c[3];
		float r;
		bool spot;
		float axis[3];		// from the light along the cone, normalised
		float cosOuter;
		float sinOuter;
	};

	// view = column-major view matrix (the frame of lights[] -> eye space).
	static ViewLight Transform(const float* view, const ClusterLight& light);

	// Sphere (and for spots cone) against the padded cluster box {minx, miny, minz, maxx, maxy, maxz}.
	// dsq = squared distance from the light centre to the box (the key of the overflow selection).
	static bool Hits(const ViewLight& light, const float* box, float& dsq);

	static void Assign(const ClusterGrid& grid, const float* view, const ClusterLight* lights, int lightCount,
		const ClusterAssignOptions& options, ClusterAssignResult& result);
};

}
