/*
** levelmesh_frame.h
**
** Per-frame frame build for the backend-neutral level-mesh data layer:
** the compact per-frame culled draw list (one entry per visible
** per-(region, texture) sub-range) plus the view-frustum plane derivation
** used by the cull walk. The GL33 draw path (and later the raytrace
** adapter) consumes the draw list; the classic scene path is untouched.
**
** Ticket 03 chunk E1: the frame build runs inside D_Render's
** interpolation window, before the classic RenderView, for each level
** with gl_uselevelmesh enabled. Portals are a later ticket (06); this
** module walks every region of the model with the same code path, so
** 06 extends it without changing the API here.
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
#include <cstddef>
#include "tarray.h"
#include "hwrenderer/data/buffers.h"	// HW_MAX_PIPELINE_BUFFERS (ring slot math)

class player_t;

namespace levelmesh
{
// Forward-declared here (fully defined in levelmesh.h) to keep this header
// light; the classic scene layer only ever sees the compact draw list.
struct FLevelMesh;
//============================================================================
//
// One draw list entry: a visible per-(region, texture) sub-range. The
// draw pass resolves the pair against FLevelMesh::regions[r].texRange*
// and FLevelMesh::texRanges[t] to get the IBO span to draw.
//
//============================================================================

struct LevelMeshDrawEntry
{
	uint32_t regionIndex;	// index into FLevelMesh::regions
	uint32_t texRangeIndex;	// index into FLevelMesh::texRanges (global)
};

//============================================================================
//
// FLevelMeshFrame
//
// Per-level per-frame state for the level-mesh frame build. A member of
// FLevelLocals (levelMeshFrame); the level object is refilled in place on
// map load, so Prepare() is idempotent and re-sizes the draw list when a
// new level's static bound differs from the current one.
//
// The draw list has a STATIC BOUND: the total number of texRange entries
// across all regions (every entry that could possibly survive). It is
// allocated once per Prepare; Build() then writes survivors directly into
// it with no allocation.
//
//============================================================================

struct FLevelMeshFrame
{
	// The culled draw list, first EntryCount() entries valid. After Prepare
	// it holds exactly `bound` default-constructed entries; after Build it
	// is clamped to the surviving count, so Size() == EntryCount().
	TArray<LevelMeshDrawEntry> drawList;
	// The static bound of the prepared mesh: total texRange entries across
	// all regions (0 until Prepare). Build never appends past this index.
	uint32_t bound = 0;

	// [levelmesh] E2: the sector state ring slot this frame's data was
	// uploaded to (the one D_Render passed to PackSnapshot and
	// UploadLevelMeshSlot); -1 until the first Build. The draw pass feeds it
	// to the shader as uSectorStateSlot so the VS fetches the ring slot the
	// CPU just wrote.
	int currentSlot = -1;

	// Size the draw list to the static bound for this mesh. Idempotent: a
	// second call with the same bound is a no-op, so it is safe to run
	// every frame. Build() self-Prepares lazily (see there).
	void Prepare(const FLevelMesh &mesh);

	// The per-frame cull walk: test every region's texRange entries'
	// precomputed world AABBs against the six view-frustum planes and
	// append the survivors to the draw list. Returns the entry count.
	// No allocation after the first Build (Prepare is lazy, see above).
	uint32_t Build(const FLevelMesh &mesh, const float frustumPlanes[24]);

	// Number of entries in this frame's draw list.
	uint32_t EntryCount() const { return (uint32_t)drawList.Size(); }
	// The entries (first EntryCount() are valid).
	const LevelMeshDrawEntry *Entries() const { return drawList.Data(); }

	// Frame slot helper: the sector state ring has HW_MAX_PIPELINE_BUFFERS
	// slots; one frame writes exactly one. The counter lives with the call
	// site (D_Render) and only drives the ring rotation.
	static int NextSlot(uint64_t &counter)
	{
		return (int)(counter++ % HW_MAX_PIPELINE_BUFFERS);
	}

};

//============================================================================
//
// LevelMesh_CalcFrustumPlanes
//
// Derives the six view-frustum planes (near, far, left, right, top,
// bottom) for a player viewpoint into out[24] as six float4s
// (a, b, c, d) with the "inside = dot(p, plane) >= 0" convention.
//
// Replicates the classic view math: the view matrix is built exactly like
// HWDrawInfo::SetViewMatrix (roll/pitch/yaw + translate + x-flip scale with
// the level pixelstretch) and the projection exactly like the mono path of
// VREyeInfo::GetProjection (perspective from r_viewwindow's widescreen
// ratio, zNear 5 / zFar 65536). The planes are then extracted from the
// VP = P*V matrix rows. The cull walk runs BEFORE RenderView computes the
// real VPUniforms, which is why this helper exists.
//
// Note: this uses the player's current (uninterpolated) origin and angles;
// the classic view interpolates sub-tic position. The difference is sub-tic
// movement at most, and the cull walk only ever removes entries that are
// fully outside - see the handoff for the parity discussion.
//
//============================================================================

void LevelMesh_CalcFrustumPlanes(player_t *pl, float out[24]);

}
