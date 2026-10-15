/*
** hw_levelmeshstate.cpp
**
** Levelmesh dirty state stream: sector records and cluster z-ranges
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** For every rendered viewpoint, inside the D_Render interpolation window,
** the candidate sectors' packed GPU records are compared with the copy last
** handed to the backend; only the records that differ are uploaded. A full
** compare of every sector costs O(sectors) per frame (1.2 ms for the 36k
** sectors of myhouse MAP01), so the candidates are:
**
**  - the sectors changed by the previous frame's updates (catches the last
**    frame of a move, and RestoreInterpolations moving the planes back);
**  - the sectors of every sector plane interpolation (moving planes, linked
**    sectors, 3D midtex attachments);
**  - the sectors of every sector effect and light thinker (movers that do
**    not interpolate, such as animated doors, and light changes);
**  - the sectors reported by the change hooks (LevelMesh_SectorChanged:
**    light level and colormap setters, used by ACS and the light specials);
**  - a round-robin sweep of lm_statesweep sectors, the backstop for state
**    that scripts write directly (bounded latency, not per frame).
**
** When a sector's planes change, the z-ranges of the clusters anchored to
** them are recomputed from the uploaded planes: every vertex of a cluster
** is LM_PlaneZ(anchor plane) + offset, possibly clamped down to a wall's
** edgeTop plane, so the range is the anchor planes over the cluster's xy
** box plus the cluster's offset range per plane. For a level plane this
** reproduces the vertex z bounds exactly; sloped planes are evaluated at the
** box corners and padded against rounding.
**
** Diagnostics: lm_statefull compares every sector every frame, lm_clusterz 0
** stops the cluster z refresh, lm_stateupdate 0 freezes the GPU state at the
** load values.
*/

// HEADER FILES ------------------------------------------------------------

#include <algorithm>
#include <cfloat>
#include <cstring>

#include "hw_levelmeshbake.h"
#include "g_levellocals.h"
#include "dsectoreffect.h"
#include "r_interpolate.h"
#include "hw_clock.h"
#include "c_cvars.h"
#include "v_video.h"

// PUBLIC DATA DEFINITIONS -------------------------------------------------

// Sectors compared per update by the round-robin sweep (0 = no sweep).
CVAR(Int, lm_statesweep, 512, CVAR_NOSAVE)
// Debug: stream state changes at all (0 freezes the GPU state at the load values).
CVAR(Bool, lm_stateupdate, true, CVAR_NOSAVE)
// Debug: compare every sector on every update.
CVAR(Bool, lm_statefull, false, CVAR_NOSAVE)
// Debug: refresh the z-ranges of clusters anchored to moved planes (0 keeps
// the load heights: the negative control for the culling-hole test).
CVAR(Bool, lm_clusterz, true, CVAR_NOSAVE)

// PRIVATE DATA DEFINITIONS ------------------------------------------------

namespace
{

// Rounding margin of a sloped plane evaluated at the box corners instead
// of at the vertices.
constexpr float SLOPE_PAD = 1.f;

uint64_t StreamFrame = 1;

// Scratch arrays, reused across frames (game thread only).
TArray<uint32_t> DirtySectors;
TArray<LevelMeshSectorState> DirtyRecords;
TArray<LevelMeshSectorState> PreviousRecords;	// parallel to DirtySectors
TArray<uint32_t> DirtyClusters;
TArray<LevelMeshClusterZ> ClusterUpdates;
TArray<LevelMeshClusterZ> PreviousClusterZ;	// parallel to ClusterUpdates
TArray<uint32_t> Candidates;

} // namespace

// CODE --------------------------------------------------------------------

//==========================================================================
//
// LevelMesh_FillSectorState
//
//==========================================================================

void LevelMesh_FillSectorState(sector_t &sec, LevelMeshSectorState &out)
{
	out = {};
	for (int p = 0; p < 2; p++)
	{
		out.planes[p] = LM_PackPlane(sec.GetSecPlane(p));
		out.texZ[p] = (float)sec.GetPlaneTexZ(p);
		out.glowColor[p] = sec.GetGlowColor(p).d;
		out.glowHeight[p] = (float)sec.GetGlowHeight(p);
	}
	out.lightLevel = sec.lightlevel;
	out.planeLight[0] = sec.GetFloorLight();
	out.planeLight[1] = sec.GetCeilingLight();
	out.lightColor = sec.Colormap.LightColor.d;
	out.fadeColor = sec.Colormap.FadeColor.d;
	out.colormap = sec.Colormap.Desaturation | (uint32_t(sec.Colormap.BlendFactor) << 8) | (uint32_t(sec.Colormap.FogDensity) << 16);
}

//==========================================================================
//
// LevelMesh_ResetState
//
//==========================================================================

void LevelMesh_ResetState(LevelMeshBake *bake, const TArray<LevelMeshSectorState> &sectors)
{
	auto &st = bake->Stream;
	st.Sectors = sectors;
	st.SectorStamp.Resize(sectors.Size());
	for (auto &s : st.SectorStamp) s = 0;
	st.ClusterStamp.Resize(bake->Clusters.Size());
	for (auto &s : st.ClusterStamp) s = 0;
	st.TouchedFlag.Resize(sectors.Size());
	for (auto &s : st.TouchedFlag) s = 0;
	st.Touched.Clear();
	st.Changed.Clear();
	st.ChangedBefore.Clear();
	st.Frame = 0;
	st.Stamp = 0;
	st.SweepCursor = 0;
}

//==========================================================================
//
// Change hooks (sector_t setters) and the frame boundary
//
//==========================================================================

void LevelMesh_SectorChanged(sector_t *sec)
{
	LevelMeshBake *bake = sec->Level ? sec->Level->levelMeshBake : nullptr;
	if (bake == nullptr) return;
	auto &st = bake->Stream;
	unsigned s = sec->Index();
	if (s >= st.TouchedFlag.Size() || st.TouchedFlag[s]) return;
	st.TouchedFlag[s] = 1;
	st.Touched.Push(s);
}

void LevelMesh_BeginFrame()
{
	StreamFrame++;
}

//==========================================================================
//
// Cluster z-range from the uploaded anchor planes
//
//==========================================================================

static LevelMeshClusterZ ClusterZ(const LevelMeshBake *bake, uint32_t c)
{
	const LevelMeshCluster &cl = bake->Clusters[c];
	float zmin = FLT_MAX, zmax = -FLT_MAX;
	for (uint32_t k = bake->ClusterPlaneStart[c]; k < bake->ClusterPlaneStart[c + 1]; k++)
	{
		const LevelMeshClusterPlane &cp = bake->ClusterPlanes[k];
		const LevelMeshPlane &plane = bake->Stream.Sectors[cp.ref >> 1].planes[cp.ref & 1];
		float lo, hi;
		if (plane.nx == 0.f && plane.ny == 0.f)
		{
			// The same value LM_PlaneZ produces at every vertex.
			lo = hi = LM_PlaneZ(plane, cl.bmin[0], cl.bmin[1]);
		}
		else
		{
			float z[4] = {
				LM_PlaneZ(plane, cl.bmin[0], cl.bmin[1]), LM_PlaneZ(plane, cl.bmax[0], cl.bmin[1]),
				LM_PlaneZ(plane, cl.bmin[0], cl.bmax[1]), LM_PlaneZ(plane, cl.bmax[0], cl.bmax[1]),
			};
			lo = std::min({ z[0], z[1], z[2], z[3] }) - SLOPE_PAD;
			hi = std::max({ z[0], z[1], z[2], z[3] }) + SLOPE_PAD;
		}
		zmin = std::min(zmin, lo + cp.minOffset);
		zmax = std::max(zmax, hi + cp.maxOffset);
	}
	return { c, zmin, zmax, 0 };
}

//==========================================================================
//
// LevelMesh_UpdateState
//
//==========================================================================

void LevelMesh_UpdateState(FLevelLocals *Level)
{
	LevelMeshBake *bake = Level->levelMeshBake;
	if (bake == nullptr || screen == nullptr || !lm_stateupdate || !screen->LevelMeshResident(bake->Id)) return;
	auto &st = bake->Stream;
	unsigned nsec = Level->sectors.Size();
	unsigned ncl = bake->Clusters.Size();
	if (st.Sectors.Size() != nsec || st.SectorStamp.Size() != nsec || st.ClusterStamp.Size() != ncl ||
		bake->ClusterPlaneStart.Size() != (ncl ? ncl + 1 : 0) || bake->SectorClusterStart.Size() != nsec + 1) return;

	LevelMeshStateTime.Clock();
	if (st.Frame != StreamFrame)
	{
		// A viewpoint after the first in a frame (camera textures) sees the
		// same state; only the first one of a frame rotates the change lists.
		st.Frame = StreamFrame;
		std::swap(st.ChangedBefore, st.Changed);
		st.Changed.Clear();
	}
	if (++st.Stamp == 0)
	{
		for (auto &s : st.SectorStamp) s = 0;
		for (auto &s : st.ClusterStamp) s = 0;
		st.Stamp = 1;
	}
	const uint32_t stamp = st.Stamp;
	DirtySectors.Clear();
	DirtyRecords.Clear();
	PreviousRecords.Clear();
	DirtyClusters.Clear();
	ClusterUpdates.Clear();
	PreviousClusterZ.Clear();
	unsigned candidates = 0;

	auto check = [&](unsigned s)
	{
		if (s >= nsec || st.SectorStamp[s] == stamp) return;
		st.SectorStamp[s] = stamp;
		candidates++;
		LevelMeshSectorState rec;
		LevelMesh_FillSectorState(Level->sectors[s], rec);
		LevelMeshSectorState &old = st.Sectors[s];
		if (memcmp(&rec, &old, sizeof(rec)) == 0) return;
		bool moved = memcmp(rec.planes, old.planes, sizeof(rec.planes)) != 0;
		PreviousRecords.Push(old);
		old = rec;
		DirtySectors.Push(s);
		DirtyRecords.Push(rec);
		if (!moved || !lm_clusterz) return;
		for (uint32_t k = bake->SectorClusterStart[s]; k < bake->SectorClusterStart[s + 1]; k++)
		{
			uint32_t c = bake->SectorClusters[k];
			if (st.ClusterStamp[c] == stamp) continue;
			st.ClusterStamp[c] = stamp;
			DirtyClusters.Push(c);
		}
	};

	if (lm_statefull)
	{
		for (unsigned s = 0; s < nsec; s++) check(s);
	}
	else
	{
		for (uint32_t s : st.ChangedBefore) check(s);
		for (uint32_t s : st.Changed) check(s);
		Candidates = std::move(st.Touched);
		st.Touched.Clear();
		for (uint32_t s : Candidates)
		{
			st.TouchedFlag[s] = 0;
			check(s);
		}
		Level->interpolator.ForEachMovedSector([&](sector_t *sec) { check(sec->Index()); });
		for (int stat : { STAT_SECTOREFFECT, STAT_LIGHT })
		{
			TThinkerIterator<DSectorEffect> it(Level, stat);
			while (DSectorEffect *effect = it.Next())
			{
				if (sector_t *sec = effect->GetSector()) check(sec->Index());
			}
		}
		unsigned sweep = std::min((unsigned)std::max<int>(lm_statesweep, 0), nsec);
		for (unsigned i = 0; i < sweep; i++)
		{
			check(st.SweepCursor);
			if (++st.SweepCursor >= nsec) st.SweepCursor = 0;
		}
	}

	for (uint32_t c : DirtyClusters)
	{
		auto &cl = bake->Clusters[c];
		PreviousClusterZ.Push({ c, cl.bmin[2], cl.bmax[2], 0 });
		LevelMeshClusterZ z = ClusterZ(bake, c);
		cl.bmin[2] = z.zmin;
		cl.bmax[2] = z.zmax;
		ClusterUpdates.Push(z);
	}

	size_t bytes = 0;
	if (DirtySectors.Size() || ClusterUpdates.Size())
	{
		LevelMeshStateUpdate update;
		update.id = bake->Id;
		update.sectorIndices = LM_Span(DirtySectors);
		update.sectors = LM_Span(DirtyRecords);
		update.clusters = LM_Span(ClusterUpdates);
		bytes = screen->UpdateLevelMeshState(update);
		if (bytes == 0)
		{
			// Not uploaded: keep the mirror equal to the device, so the
			// changes are found again by the next update.
			for (unsigned i = 0; i < DirtySectors.Size(); i++) st.Sectors[DirtySectors[i]] = PreviousRecords[i];
			for (auto &z : PreviousClusterZ)
			{
				bake->Clusters[z.cluster].bmin[2] = z.zmin;
				bake->Clusters[z.cluster].bmax[2] = z.zmax;
			}
			ClusterUpdates.Clear();
		}
	}
	st.Changed.Append(DirtySectors);
	LevelMeshStateTime.Unclock();

	lmStateStats.candidates += candidates;
	lmStateStats.sectors += bytes ? DirtySectors.Size() : 0;
	lmStateStats.clusters += ClusterUpdates.Size();
	lmStateStats.bytes += bytes;
}
