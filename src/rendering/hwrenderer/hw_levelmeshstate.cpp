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
** 3D lights (LevelMeshLight3D) are a fourth stream. Their candidates are
** the lights reading a changed sector (LevelMeshBake::LightDepend) and a
** sweep. A sector whose 3D-floor layout moved away from the bake (the
** planes the bake split and lit its surfaces by: LevelMeshBake::LayoutPlanes)
** sets LML3_DEFERRED on its lights: the vertex stage hides the surfaces lit
** by them and the classic path draws the sector's side tiers and planes,
** until the layout is back at the bake.
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
// Debug: print every side tier / plane that switches between the levelmesh
// and the classic path (LMTS_DEFERRED).
CVAR(Bool, lm_statelog, false, CVAR_NOSAVE)

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
TArray<uint32_t> DirtyLights;
TArray<LevelMeshLight3D> DirtyLightRecords, PreviousLightRecords;
TArray<uint32_t> MovedSectors;		// dirty sectors whose planes moved
TArray<uint32_t> LayoutChanged;		// sectors whose LayoutMoved flips with this update

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

// Whether the baked surfaces of a side tier can show its current state:
// the vertex stage applies offsets, but the u / v (and a clipped mid
// texture's height) were computed with the bake's FTexCoordInfo, so a
// texture of another size or a changed scale needs the classic emitter.
static bool SideStateExpressible(const LevelMeshTierBase &tb, side_t &side, int tier, FTextureID texid)
{
	if (!texid.isValid()) return false;		// no texture: the classic decides what (if anything) to draw
	auto tex = TexMan.GetGameTexture(texid, true);
	// Another material program would need another bucket.
	if (tex == nullptr || !tex->isValid() || LevelMesh_MaterialProgram(tex) != tb.program) return false;
	FTexCoordInfo tci;
	tci.GetFromTexture(tex, (float)side.GetTextureXScale(tier), (float)side.GetTextureYScale(tier),
		!!(side.GetLevel()->flags3 & LEVEL3_FORCEWORLDPANNING));
	if (tci.mRenderWidth != tb.renderWidth || tci.mRenderHeight != tb.renderHeight || tci.mScale.X != tb.scaleX ||
		tci.mScale.Y != tb.scaleY || tci.mWorldPanning != tb.worldPanning)
		return false;
	// HWWall::DoMidTexture clips an unwrapped two-sided mid texture to its
	// height above / below the y offset.
	if (tb.midClip && (float)side.GetTextureYOffset(tier) != tb.yOffset) return false;
	if (LevelMesh_HasWallColorEffects(&side, tier, side.sector)) return false;
	return true;
}

void LevelMesh_FillSideState(const LevelMeshBake *bake, side_t &side, int tier, LevelMeshSideState &out)
{
	out = {};
	FTextureID texid = side.GetTexture(tier);
	out.textureId = texid.GetIndex();
	out.xOffset = (float)side.GetTextureXOffset(tier);
	out.yOffset = (float)side.GetTextureYOffset(tier);
	out.xScale = (float)side.GetTextureXScale(tier);
	out.yScale = (float)side.GetTextureYScale(tier);
	unsigned ref = side.Index() * 3 + tier;
	if (ref < bake->TierBase.Size() && bake->TierBase[ref].valid && !SideStateExpressible(bake->TierBase[ref], side, tier, texid))
		out.flags |= LMTS_DEFERRED;
}

void LevelMesh_FillPlaneXform(const LevelMeshBake *bake, sector_t &sec, int pos, LevelMeshPlaneXform &out)
{
	out = {};
	FTextureID texid = sec.GetTexture(pos);
	out.textureId = texid.GetIndex();
	// HWFlat::ProcessSector draws no sky plane; untextured planes and a
	// material program other than the baked surfaces' (another bucket) stay
	// classic.
	auto tex = TexMan.GetGameTexture(texid, true);
	unsigned ref = sec.Index() * 2 + pos;
	int baked = ref < bake->PlaneProgram.Size() ? bake->PlaneProgram[ref] : -2;
	if (texid == skyflatnum || !texid.isValid() || tex == nullptr || !tex->isValid() || (baked >= -1 && LevelMesh_MaterialProgram(tex) != baked))
		out.flags |= LMTS_DEFERRED;
	// HWFlat::ProcessSector draws a reflective plane (Sector_SetPlaneReflection)
	// translucent over its plane mirror, and a portal plane opaque only at
	// alpha 1; the bake made a surface only for the opaque cases. Plane
	// colors set at runtime are not expressible yet.
	bool portal = sec.ValidatePortal(pos) != nullptr;
	bool bakedPortal = ref < bake->PlanePortal.Size() && bake->PlanePortal[ref];
	if (sec.GetReflect(pos) > 0 || portal != bakedPortal || (portal && sec.GetAlpha(pos) != 1.) || LevelMesh_HasPlaneColorEffects(&sec, pos))
		out.flags |= LMTS_DEFERRED;
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
// LevelMesh_FillLight3D
//
// The light's current lightlist entry, found by the values that identify it
// (LevelMeshLight3DSource): the classic reads the entry's extra_colormap,
// which P_RecalculateLights refreshes from the setters.
//
//==========================================================================

void LevelMesh_FillLight3D(FLevelLocals *Level, const LevelMeshLight3DSource &src, bool layoutMoved, LevelMeshLight3D &out)
{
	out = {};
	out.lightLevel = *src.lightLevel;
	const lightlist_t *found = nullptr;
	if (src.sector < Level->sectors.Size())
	{
		for (auto &l : Level->sectors[src.sector].e->XFloor.lightlist)
		{
			sector_t *model = l.lightsource ? l.lightsource->model : nullptr;
			int flags = l.lightsource ? (l.lightsource->flags & (FF_FADEWALLS | FF_FOG)) : 0;
			bool fadeWalls = l.caster && (l.caster->flags & FF_FADEWALLS);
			if (model == src.lightsourceModel && flags == src.lightsourceFlags && l.p_lightlevel == src.lightLevel && fadeWalls == src.casterFadeWalls)
			{
				found = &l;
				break;
			}
		}
	}
	if (found == nullptr || layoutMoved)
	{
		out.flags |= LML3_DEFERRED;
		if (found == nullptr) return;
	}
	const FColormap &cm = found->extra_colormap;
	out.lightColor = cm.LightColor.d;
	out.fadeColor = cm.FadeColor.d;
	out.colormap = cm.Desaturation | (uint32_t(cm.BlendFactor) << 8) | (uint32_t(cm.FogDensity) << 16);
	if (src.casterFadeWalls && cm.FadeColor != 0) out.flags |= LML3_COPYFOG;
}

// Whether a sector's 3D-floor layout (the planes the bake split and lit its
// surfaces by) differs from the bake, in the records last streamed.
static bool LayoutMoved(const LevelMeshBake *bake, unsigned s)
{
	for (uint32_t k = bake->LayoutStart[s]; k < bake->LayoutStart[s + 1]; k++)
	{
		const LevelMeshLayoutPlane &lp = bake->LayoutPlanes[k];
		if (memcmp(&bake->Stream.Sectors[lp.ref >> 1].planes[lp.ref & 1], &lp.plane, sizeof(lp.plane)) != 0) return true;
	}
	return false;
}

// Classic draw-list ownership of a side tier / sector plane: owned while
// baked, the record is expressible, and its sector's 3D-floor layout is at
// the bake.
static uint8_t TierOwned(FLevelLocals *Level, const LevelMeshBake *bake, uint32_t ref)
{
	if (!bake->SideTierBaked[ref] || (bake->Stream.Sides.Records[ref].flags & LMTS_DEFERRED)) return 0;
	sector_t *sec = Level->sides[ref / 3].sector;
	return !(sec && bake->Stream.LayoutMoved[sec->Index()]);
}

static uint8_t PlaneOwned(const LevelMeshBake *bake, uint32_t ref)
{
	if (!bake->SectorPlaneBaked[ref] || (bake->Stream.Xforms.Records[ref].flags & LMTS_DEFERRED)) return 0;
	return !bake->Stream.LayoutMoved[ref >> 1];
}

static void SetTierOwned(FLevelLocals *Level, LevelMeshBake *bake, uint32_t ref)
{
	if (ref >= bake->SideTierOwned.Size()) return;
	uint8_t owned = TierOwned(Level, bake, ref);
	if (lm_statelog && owned != bake->SideTierOwned[ref])
		Printf("Levelmesh state: side %u tier %u -> %s\n", ref / 3, ref % 3, owned ? "levelmesh" : "classic");
	bake->SideTierOwned[ref] = owned;
}

static void SetPlaneOwned(LevelMeshBake *bake, uint32_t ref)
{
	if (ref >= bake->SectorPlaneOwned.Size()) return;
	uint8_t owned = PlaneOwned(bake, ref);
	if (lm_statelog && owned != bake->SectorPlaneOwned[ref])
		Printf("Levelmesh state: sector %u plane %u -> %s\n", ref >> 1, ref & 1, owned ? "levelmesh" : "classic");
	bake->SectorPlaneOwned[ref] = owned;
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
	const TArray<LevelMeshSideState> &sides, const TArray<LevelMeshPlaneXform> &xforms, const TArray<LevelMeshLight3D> &lights3D)
{
	auto &st = bake->Stream;
	st.Sides.Reset(sides);
	st.Xforms.Reset(xforms);
	st.Lights3D.Reset(lights3D);
	st.LayoutMoved.Resize(sectors.Size());
	for (auto &m : st.LayoutMoved) m = 0;
	st.LayoutRecheck.Clear();
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

void LevelMesh_SectorColorsChanged(sector_t *sec)
{
	if (HookBake == nullptr || sec < HookSectors || sec >= HookSectors + HookSectorCount) return;
	unsigned s = unsigned(sec - HookSectors);
	HookBake->Stream.Xforms.Touch(s * 2);
	HookBake->Stream.Xforms.Touch(s * 2 + 1);
	// Walls without their own colors take the sector's (side_t::GetSpecialColor).
	for (line_t *line : sec->Lines)
		for (side_t *side : line->sidedef)
			if (side && side->sector == sec) LevelMesh_SideChanged(side, -1);
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
// Side tier / plane records as streamed
//
// A texture the backend has not resolved yet (a swap to an id the level did
// not reference at load) would draw as the null texture: the record keeps
// the previous texture for the frame the backend needs to resolve the new
// one, and the record is checked again next update.
//
//==========================================================================

static void FillStreamedSide(FLevelLocals *Level, LevelMeshBake *bake, unsigned i, LevelMeshSideState &out)
{
	LevelMesh_FillSideState(bake, Level->sides[i / 3], i % 3, out);
	const LevelMeshSideState &prev = bake->Stream.Sides.Records[i];
	if (!(out.flags & LMTS_DEFERRED) && out.textureId != prev.textureId && !screen->LevelMeshTextureReady(out.textureId))
	{
		if (lm_statelog) Printf("Levelmesh state: side %u tier %u waits a frame for texture %d\n", i / 3, i % 3, out.textureId);
		out.textureId = prev.textureId;
		bake->Stream.Sides.Touch(i);
	}
}

static void FillStreamedPlane(FLevelLocals *Level, LevelMeshBake *bake, unsigned i, LevelMeshPlaneXform &out)
{
	sector_t &sec = Level->sectors[i >> 1];
	LevelMesh_FillPlaneXform(bake, sec, i & 1, out);
	const LevelMeshPlaneXform &prev = bake->Stream.Xforms.Records[i];
	if (out.textureId == prev.textureId) return;
	if (!(out.flags & LMTS_DEFERRED) && !screen->LevelMeshTextureReady(out.textureId))
	{
		if (lm_statelog) Printf("Levelmesh state: sector %u plane %u waits a frame for texture %d\n", i >> 1, i & 1, out.textureId);
		out.textureId = prev.textureId;
		bake->Stream.Xforms.Touch(i);
		return;
	}
	// The plane texture changed: the sector's wall glow may follow it now.
	LevelMesh_SectorChanged(&sec);
	auto &glow = bake->Stream.AnimatedGlow;
	unsigned s = i >> 1;
	if (glow.Find(s) == glow.Size())
	{
		auto tex = TexMan.GetGameTexture(sec.GetTexture(i & 1));
		if (tex && tex->isGlowing() && !tex->isAutoGlowing() && sec.GetGlowColor(i & 1) == 0) glow.Push(s);
	}
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
	if (st.Sectors.Size() != nsec || st.SectorStamp.Size() != nsec || st.ClusterStamp.Size() != ncl || st.LayoutMoved.Size() != nsec ||
		bake->LayoutStart.Size() != nsec + 1 || bake->LightDependStart.Size() != nsec + 1 || st.Lights3D.Records.Size() != bake->Lights3D.Size() ||
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
		std::swap(st.Lights3D.ChangedBefore, st.Lights3D.Changed);
		st.Lights3D.Changed.Clear();
	}
	if (++st.Stamp == 0)
	{
		for (auto &s : st.SectorStamp) s = 0;
		for (auto &s : st.ClusterStamp) s = 0;
		for (auto &s : st.Sides.Stamp) s = 0;
		for (auto &s : st.Xforms.Stamp) s = 0;
		for (auto &s : st.Lights3D.Stamp) s = 0;
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
	DirtyLights.Clear();
	DirtyLightRecords.Clear();
	PreviousLightRecords.Clear();
	MovedSectors.Clear();
	LayoutChanged.Clear();
	unsigned candidates = 0;

	// Side tiers and plane textures first: a plane texture change can change
	// its sector's wall glow (FillStreamedPlane reports the sector).
	unsigned recordSweep = (unsigned)std::max<int>(lm_statesweep, 0);
	candidates += StreamRecords(st.Sides, stamp, lm_statefull, recordSweep,
		[&](unsigned i, LevelMeshSideState &out) { FillStreamedSide(Level, bake, i, out); },
		DirtySides, DirtySideRecords, PreviousSideRecords);
	candidates += StreamRecords(st.Xforms, stamp, lm_statefull, recordSweep,
		[&](unsigned i, LevelMeshPlaneXform &out) { FillStreamedPlane(Level, bake, i, out); },
		DirtyXforms, DirtyXformRecords, PreviousXformRecords);

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
		if (moved) MovedSectors.Push(s);
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


	// 3D-floor layouts depending on a moved plane; the lights of a sector
	// whose layout flips, and the lights reading a changed sector.
	auto layout = [&](uint32_t t)
	{
		bool moved = LayoutMoved(bake, t);
		if (moved == !!st.LayoutMoved[t] || LayoutChanged.Find(t) < LayoutChanged.Size()) return;
		st.LayoutMoved[t] = moved;
		LayoutChanged.Push(t);
		for (uint32_t k = bake->SectorLightStart[t]; k < bake->SectorLightStart[t + 1]; k++) st.Lights3D.Touch(bake->SectorLights[k]);
	};
	Candidates = std::move(st.LayoutRecheck);
	st.LayoutRecheck.Clear();
	for (uint32_t t : Candidates) layout(t);
	for (uint32_t s : MovedSectors)
		for (uint32_t k = bake->LayoutDependStart[s]; k < bake->LayoutDependStart[s + 1]; k++) layout(bake->LayoutDepend[k]);
	for (uint32_t s : DirtySectors)
		for (uint32_t k = bake->LightDependStart[s]; k < bake->LightDependStart[s + 1]; k++) st.Lights3D.Touch(bake->LightDepend[k]);
	candidates += StreamRecords(st.Lights3D, stamp, lm_statefull, recordSweep,
		[&](unsigned i, LevelMeshLight3D &out)
		{
			auto &src = bake->Lights3D[i];
			LevelMesh_FillLight3D(Level, src, st.LayoutMoved[src.sector], out);
		},
		DirtyLights, DirtyLightRecords, PreviousLightRecords);

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
	if (DirtySectors.Size() || ClusterUpdates.Size() || DirtySides.Size() || DirtyXforms.Size() || DirtyLights.Size())
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
		update.light3DIndices = LM_Span(DirtyLights);
		update.lights3D = LM_Span(DirtyLightRecords);
		bytes = screen->UpdateLevelMeshState(update);
		if (bytes == 0)
		{
			// Not uploaded: keep the mirror equal to the device, so the
			// changes are found again by the next update.
			for (unsigned i = 0; i < DirtySectors.Size(); i++) st.Sectors[DirtySectors[i]] = PreviousRecords[i];
			for (unsigned i = 0; i < DirtySides.Size(); i++) st.Sides.Records[DirtySides[i]] = PreviousSideRecords[i];
			for (unsigned i = 0; i < DirtyXforms.Size(); i++) st.Xforms.Records[DirtyXforms[i]] = PreviousXformRecords[i];
			for (unsigned i = 0; i < DirtyLights.Size(); i++) st.Lights3D.Records[DirtyLights[i]] = PreviousLightRecords[i];
			for (uint32_t t : LayoutChanged)
			{
				st.LayoutMoved[t] = !st.LayoutMoved[t];
				st.LayoutRecheck.Push(t);
			}
			DirtySides.Clear();
			DirtyXforms.Clear();
			DirtyLights.Clear();
			LayoutChanged.Clear();
			for (auto &z : PreviousClusterZ)
			{
				bake->Clusters[z.cluster].bmin[2] = z.zmin;
				bake->Clusters[z.cluster].bmax[2] = z.zmax;
			}
			ClusterUpdates.Clear();
		}
	}
	// Classic draw-list ownership follows LMTS_DEFERRED and the 3D-floor
	// layouts (uploaded records only).
	for (uint32_t ref : DirtySides) SetTierOwned(Level, bake, ref);
	for (uint32_t ref : DirtyXforms) SetPlaneOwned(bake, ref);
	for (uint32_t t : LayoutChanged)
	{
		if (lm_statelog) Printf("Levelmesh state: sector %u 3D-floor layout %s\n", t, st.LayoutMoved[t] ? "moved: classic" : "at the bake: levelmesh");
		sector_t &sec = Level->sectors[t];
		for (line_t *line : sec.Lines)
			for (side_t *side : line->sidedef)
				if (side && side->sector == &sec)
					for (int tier = 0; tier < 3; tier++) SetTierOwned(Level, bake, side->Index() * 3 + tier);
		SetPlaneOwned(bake, t * 2);
		SetPlaneOwned(bake, t * 2 + 1);
	}
	st.Changed.Append(DirtySectors);
	st.Sides.Changed.Append(DirtySides);
	st.Xforms.Changed.Append(DirtyXforms);
	st.Lights3D.Changed.Append(DirtyLights);
	LevelMeshStateTime.Unclock();

	lmStateStats.candidates += candidates;
	lmStateStats.sectors += bytes ? DirtySectors.Size() : 0;
	lmStateStats.sides += DirtySides.Size();
	lmStateStats.xforms += DirtyXforms.Size();
	lmStateStats.clusters += ClusterUpdates.Size();
	lmStateStats.bytes += bytes;
}
