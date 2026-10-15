/*
** hw_levelmeshstate.cpp
**
** Levelmesh dirty state stream: sector, side and plane-xform records and
** cluster z-ranges
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
**    light level, colormap and glow setters, used by ACS and the light
**    specials);
**  - the sectors whose wall glow comes from an animated texture;
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
** Side tiers (texture, offsets, scales) and plane texture transforms are
** separate record streams with the same scheme. Their candidates are the
** previous frame's changes, the records reported by the change hooks in the
** side_t / sector_t setters (scrollers, their interpolations, switches,
** Line_SetTextureOffset, ...: LevelMesh_SideChanged, LevelMesh_PlaneChanged)
** and a round-robin sweep. The hooks find the record by pointer arithmetic
** on the level's arrays, never through the object: the map loader calls the
** setters before a side's sector pointer is valid.
**
** Diagnostics: lm_statefull compares every record every frame, lm_clusterz 0
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
#include "texturemanager.h"

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
TArray<uint32_t> DirtySides;
TArray<LevelMeshSideState> DirtySideRecords, PreviousSideRecords;
TArray<uint32_t> DirtyXforms;
TArray<LevelMeshPlaneXform> DirtyXformRecords, PreviousXformRecords;

// The level the change hooks report into (set once its records are resident).
LevelMeshBake *HookBake;
side_t *HookSides;
unsigned HookSideCount;
sector_t *HookSectors;
unsigned HookSectorCount;

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
	}
	// Without the result (HWF_GLOW) the classic draws no glow at all, even
	// where a color has a height: the floor's sector glow overwrites the
	// ceiling's result.
	if (!sec.GetWallGlow(out.glowTop, out.glowBottom)) out.glowTop[3] = out.glowBottom[3] = 0.f;
	out.lightLevel = sec.lightlevel;
	out.planeLight[0] = sec.GetFloorLight();
	out.planeLight[1] = sec.GetCeilingLight();
	out.lightColor = sec.Colormap.LightColor.d;
	out.fadeColor = sec.Colormap.FadeColor.d;
	out.colormap = sec.Colormap.Desaturation | (uint32_t(sec.Colormap.BlendFactor) << 8) | (uint32_t(sec.Colormap.FogDensity) << 16);
}

//==========================================================================
//
// LevelMesh_FillSideState / LevelMesh_FillPlaneXform
//
//==========================================================================

void LevelMesh_FillSideState(side_t &side, int tier, LevelMeshSideState &out)
{
	out = {};
	out.textureId = side.GetTexture(tier).GetIndex();
	out.xOffset = (float)side.GetTextureXOffset(tier);
	out.yOffset = (float)side.GetTextureYOffset(tier);
	out.xScale = (float)side.GetTextureXScale(tier);
	out.yScale = (float)side.GetTextureYScale(tier);
}

void LevelMesh_FillPlaneXform(sector_t &sec, int pos, LevelMeshPlaneXform &out)
{
	out = {};
	auto &xf = sec.planes[pos].xform;
	out.xOffset = (float)xf.xOffs;
	out.yOffset = (float)xf.yOffs;
	out.baseyOffset = (float)xf.baseyOffs;
	out.xScale = (float)xf.xScale;
	out.yScale = (float)xf.yScale;
	out.angle = (float)xf.Angle.Degrees();
	out.baseAngle = (float)xf.baseAngle.Degrees();
}

//==========================================================================
//
// LevelMesh_ResetState
//
//==========================================================================

LevelMeshBake::~LevelMeshBake()
{
	if (HookBake == this) HookBake = nullptr;
}

void LevelMesh_ResetState(FLevelLocals *Level, LevelMeshBake *bake, const TArray<LevelMeshSectorState> &sectors,
	const TArray<LevelMeshSideState> &sides, const TArray<LevelMeshPlaneXform> &xforms)
{
	auto &st = bake->Stream;
	st.Sides.Reset(sides);
	st.Xforms.Reset(xforms);
	HookBake = bake;
	HookSides = Level->sides.Size() ? &Level->sides[0] : nullptr;
	HookSideCount = Level->sides.Size();
	HookSectors = Level->sectors.Size() ? &Level->sectors[0] : nullptr;
	HookSectorCount = Level->sectors.Size();
	st.Sectors = sectors;
	st.SectorStamp.Resize(sectors.Size());
	for (auto &s : st.SectorStamp) s = 0;
	st.ClusterStamp.Resize(bake->Clusters.Size());
	for (auto &s : st.ClusterStamp) s = 0;
	st.TouchedFlag.Resize(sectors.Size());
	for (auto &s : st.TouchedFlag) s = 0;
	st.Touched.Clear();
	// sector_t::GetWallGlow rechecks the animation frame of a glowing,
	// not auto-glowing plane texture: that glow changes without a setter.
	// (The plane textures themselves are fixed until ticket 11.)
	st.AnimatedGlow.Clear();
	for (unsigned s = 0; s < sectors.Size() && s < Level->sectors.Size(); s++)
	{
		for (int p = 0; p < 2; p++)
		{
			auto tex = TexMan.GetGameTexture(Level->sectors[s].GetTexture(p));
			if (tex && tex->isGlowing() && !tex->isAutoGlowing() && Level->sectors[s].GetGlowColor(p) == 0)
			{
				st.AnimatedGlow.Push(s);
				break;
			}
		}
	}
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

void LevelMesh_SideChanged(side_t *side, int tier)
{
	if (HookBake == nullptr || side < HookSides || side >= HookSides + HookSideCount) return;
	unsigned s = unsigned(side - HookSides);
	auto &rs = HookBake->Stream.Sides;
	if (tier >= 0 && tier < 3) rs.Touch(s * 3 + tier);
	else for (int t = 0; t < 3; t++) rs.Touch(s * 3 + t);
}

void LevelMesh_PlaneChanged(sector_t *sec, int pos)
{
	if (HookBake == nullptr || sec < HookSectors || sec >= HookSectors + HookSectorCount || pos < 0 || pos > 1) return;
	HookBake->Stream.Xforms.Touch(unsigned(sec - HookSectors) * 2 + pos);
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
// One record stream's candidates, compared with the last uploaded records
//
//==========================================================================

template<class T, class Fill>
static unsigned StreamRecords(LevelMeshRecordStream<T> &rs, uint32_t stamp, bool full, unsigned sweep, Fill fill,
	TArray<uint32_t> &indices, TArray<T> &records, TArray<T> &previous)
{
	unsigned n = rs.Records.Size(), candidates = 0;
	auto check = [&](unsigned i)
	{
		if (i >= n || rs.Stamp[i] == stamp) return;
		rs.Stamp[i] = stamp;
		candidates++;
		T rec;
		fill(i, rec);
		if (memcmp(&rec, &rs.Records[i], sizeof(rec)) == 0) return;
		previous.Push(rs.Records[i]);
		rs.Records[i] = rec;
		indices.Push(i);
		records.Push(rec);
	};
	if (full)
	{
		for (unsigned i = 0; i < n; i++) check(i);
		return candidates;
	}
	for (uint32_t i : rs.ChangedBefore) check(i);
	for (uint32_t i : rs.Changed) check(i);
	Candidates = std::move(rs.Touched);
	rs.Touched.Clear();
	for (uint32_t i : Candidates)
	{
		rs.TouchedFlag[i] = 0;
		check(i);
	}
	sweep = std::min(sweep, n);
	for (unsigned k = 0; k < sweep; k++)
	{
		check(rs.SweepCursor);
		if (++rs.SweepCursor >= n) rs.SweepCursor = 0;
	}
	return candidates;
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
		bake->ClusterPlaneStart.Size() != (ncl ? ncl + 1 : 0) || bake->SectorClusterStart.Size() != nsec + 1 ||
		st.Sides.Records.Size() != Level->sides.Size() * 3 || st.Xforms.Records.Size() != nsec * 2) return;

	LevelMeshStateTime.Clock();
	if (st.Frame != StreamFrame)
	{
		// A viewpoint after the first in a frame (camera textures) sees the
		// same state; only the first one of a frame rotates the change lists.
		st.Frame = StreamFrame;
		std::swap(st.ChangedBefore, st.Changed);
		st.Changed.Clear();
		std::swap(st.Sides.ChangedBefore, st.Sides.Changed);
		st.Sides.Changed.Clear();
		std::swap(st.Xforms.ChangedBefore, st.Xforms.Changed);
		st.Xforms.Changed.Clear();
	}
	if (++st.Stamp == 0)
	{
		for (auto &s : st.SectorStamp) s = 0;
		for (auto &s : st.ClusterStamp) s = 0;
		for (auto &s : st.Sides.Stamp) s = 0;
		for (auto &s : st.Xforms.Stamp) s = 0;
		st.Stamp = 1;
	}
	const uint32_t stamp = st.Stamp;
	DirtySectors.Clear();
	DirtyRecords.Clear();
	PreviousRecords.Clear();
	DirtyClusters.Clear();
	ClusterUpdates.Clear();
	PreviousClusterZ.Clear();
	DirtySides.Clear();
	DirtySideRecords.Clear();
	PreviousSideRecords.Clear();
	DirtyXforms.Clear();
	DirtyXformRecords.Clear();
	PreviousXformRecords.Clear();
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
		for (uint32_t s : st.AnimatedGlow) check(s);
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

	unsigned sweep = (unsigned)std::max<int>(lm_statesweep, 0);
	candidates += StreamRecords(st.Sides, stamp, lm_statefull, sweep,
		[&](unsigned i, LevelMeshSideState &out) { LevelMesh_FillSideState(Level->sides[i / 3], i % 3, out); },
		DirtySides, DirtySideRecords, PreviousSideRecords);
	candidates += StreamRecords(st.Xforms, stamp, lm_statefull, sweep,
		[&](unsigned i, LevelMeshPlaneXform &out) { LevelMesh_FillPlaneXform(Level->sectors[i >> 1], i & 1, out); },
		DirtyXforms, DirtyXformRecords, PreviousXformRecords);

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
	if (DirtySectors.Size() || ClusterUpdates.Size() || DirtySides.Size() || DirtyXforms.Size())
	{
		LevelMeshStateUpdate update;
		update.id = bake->Id;
		update.sectorIndices = LM_Span(DirtySectors);
		update.sectors = LM_Span(DirtyRecords);
		update.clusters = LM_Span(ClusterUpdates);
		update.sideIndices = LM_Span(DirtySides);
		update.sides = LM_Span(DirtySideRecords);
		update.xformIndices = LM_Span(DirtyXforms);
		update.xforms = LM_Span(DirtyXformRecords);
		bytes = screen->UpdateLevelMeshState(update);
		if (bytes == 0)
		{
			// Not uploaded: keep the mirror equal to the device, so the
			// changes are found again by the next update.
			for (unsigned i = 0; i < DirtySectors.Size(); i++) st.Sectors[DirtySectors[i]] = PreviousRecords[i];
			for (unsigned i = 0; i < DirtySides.Size(); i++) st.Sides.Records[DirtySides[i]] = PreviousSideRecords[i];
			for (unsigned i = 0; i < DirtyXforms.Size(); i++) st.Xforms.Records[DirtyXforms[i]] = PreviousXformRecords[i];
			DirtySides.Clear();
			DirtyXforms.Clear();
			for (auto &z : PreviousClusterZ)
			{
				bake->Clusters[z.cluster].bmin[2] = z.zmin;
				bake->Clusters[z.cluster].bmax[2] = z.zmax;
			}
			ClusterUpdates.Clear();
		}
	}
	st.Changed.Append(DirtySectors);
	st.Sides.Changed.Append(DirtySides);
	st.Xforms.Changed.Append(DirtyXforms);
	LevelMeshStateTime.Unclock();

	lmStateStats.candidates += candidates;
	lmStateStats.sectors += bytes ? DirtySectors.Size() : 0;
	lmStateStats.sides += DirtySides.Size();
	lmStateStats.xforms += DirtyXforms.Size();
	lmStateStats.clusters += ClusterUpdates.Size();
	lmStateStats.bytes += bytes;
}
