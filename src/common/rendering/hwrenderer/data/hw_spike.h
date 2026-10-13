/*
** hw_spike.h
**
** Ticket 05 spike (throwaway, never merged): shared data for the GPU-driven
** cluster cull + indirect draw prototype.
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

#include <stdint.h>
#include "tarray.h"

// Spike modes (r_spike):
//  0 classic (only the GPU scene span is timed)
//  1 mesh-only, two-phase Hi-Z cull
//  2 mesh-only, frustum-only cull
//  3 mesh-only, no cull (whole mesh, one indirect command)
enum
{
	SPIKE_OFF = 0,
	SPIKE_HIZ = 1,
	SPIKE_FRUSTUM = 2,
	SPIKE_NOCULL = 3,
};

// GL world coordinates (x, height, y), like the classic vertex buffer.
struct SpikeVertex
{
	float x, y, z;
	uint32_t surf;
};

struct SpikeCluster
{
	float bmin[3];
	uint32_t firstIndex;
	float bmax[3];
	uint32_t indexCount;
};

struct SpikeMeshData
{
	TArray<SpikeVertex> Vertices;
	TArray<uint32_t> Indices;
	TArray<SpikeCluster> Clusters;
	int Budget = 0;
};

struct SpikeClusterUpdate
{
	uint32_t index;
	SpikeCluster cluster;
};

struct SpikeView
{
	float mvp[16];		// projection * view (GL clip space; the Vulkan z fix is applied in the shaders)
	int mode;
};

// Per-frame stats for r_perflog. GPU values are from the previous frame.
struct SpikeStats
{
	int mode = 0;
	int clusters = 0;
	int drawn1 = 0, drawn2 = 0;		// clusters drawn in phase 1 / phase 2
	int frustum = 0;				// clusters passing the frustum (phase 2 / frustum-only)
	int64_t tris = 0;				// triangles drawn
	double gCull1 = 0, gDraw1 = 0, gHiz = 0, gCull2 = 0, gDraw2 = 0;	// GPU ms per phase
	double gScene = 0;				// GPU ms from scene start to scene end
	int movPlanes = 0, movDirty = 0;	// moving planes this frame, dirty clusters uploaded
	double movCPU = 0;				// CPU ms: z-range refresh + upload staging
};

extern SpikeStats spikestats;
