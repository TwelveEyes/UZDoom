/*
** hw_clock.h
**
** Hardware render profiling info
**
**---------------------------------------------------------------------------
**
** Copyright 2007-2018 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Code written prior to 2026 is also licensed under:
**
** SPDX-License-Identifier: BSD-3-Clause
**
**---------------------------------------------------------------------------
**
*/

#ifndef __GL_CLOCK_H
#define __GL_CLOCK_H

#include "stats.h"
#include "m_fixed.h"

extern glcycle_t RenderWall,SetupWall,ClipWall;
extern glcycle_t RenderFlat,SetupFlat;
extern glcycle_t RenderSprite,SetupSprite;
extern glcycle_t All, Finish, PortalAll, Bsp;
extern glcycle_t ProcessAll, PostProcess;
extern glcycle_t RenderAll;
extern glcycle_t Dirty;
extern glcycle_t drawcalls, twoD, Flush3D;
extern glcycle_t MTWait, WTTotal;

extern int iter_dlightf, iter_dlight, draw_dlight, draw_dlightf;
extern int rendered_lines,rendered_flats,rendered_sprites,rendered_decals,render_vertexsplit,render_texsplit;
extern int rendered_portals;

extern int vertexcount, flatvertices, flatprimitives;

// Levelmesh GPU work of the latest completed draw (DFrameBuffer::
// GetLevelMeshStats), lagged by the frames in flight. Phase index 0 is the
// first (or only) cull + draw phase, 1 the Hi-Z phase.
struct LevelMeshPerfStats
{
	uint64_t serial = 0;		// completed levelmesh draws so far; 0 = none yet
	int cullMode = -1, drawMode = -1;	// LMCULL_* / LMDRAW_* of that draw
	unsigned clusters[2] = {}, triangles[2] = {};	// drawn per phase
	unsigned frustumCulled = 0, occluded = 0;	// clusters
	double cullMs[2] = {}, resolveMs[2] = {}, drawMs[2] = {}, hizMs = 0, totalMs = 0;	// GPU time
	unsigned portalViews = 0;	// portal views the levelmesh drew in that frame (ticket 15)
	double portalMs = 0;		// their GPU time (cull + resolve + draw, summed)
};
extern int rendered_levelmesh;	// levelmesh draws this frame

// Levelmesh dirty-state stream of this frame (LevelMesh_UpdateState), summed
// over its calls (one per rendered viewpoint).
struct LevelMeshStateStats
{
	unsigned candidates = 0;	// records compared (sectors, side tiers, plane xforms)
	unsigned sectors = 0;		// sector records uploaded
	unsigned sides = 0;			// side tier records uploaded
	unsigned xforms = 0;		// plane xform records uploaded
	unsigned clusters = 0;		// cluster z-ranges uploaded
	size_t bytes = 0;			// staging bytes uploaded
};
extern LevelMeshStateStats lmStateStats;
extern glcycle_t LevelMeshStateTime;

// Levelmesh actor gather of this frame (ticket 17, HWDrawInfo::
// GatherLevelMeshThings), summed over the views that gathered.
struct LevelMeshGatherStats
{
	unsigned views = 0;			// views that gathered from the GPU visibility
	unsigned clusters = 0;		// clusters taken (visible or adjacent, inside the frustum)
	unsigned adjacent = 0;		// of those, taken only as a neighbour of a visible cluster
	unsigned subsectors = 0;	// subsectors gathered
	unsigned sectors = 0;		// sectors whose things were gathered
};
extern LevelMeshGatherStats lmGatherStats;
extern glcycle_t LevelMeshGatherTime;

void ResetProfilingData();
void CheckBench();
void  checkBenchActive();
void PerfLogUpdate();

extern int doBench;


#endif
