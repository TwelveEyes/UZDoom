/*
** hw_levelmeshbake.cpp
**
** Levelmesh bake: static level geometry produced by the classic emitters
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Polyobject origin subsectors (SSECF_POLYORG) are skipped like
** HWDrawInfo::DoSubsector skips them: their polyobject's vertices moved to
** the start spot, so their geometry no longer bounds them.
**
** Walls: every non-polyobject side runs once through HWWall::Process in
** mesh-helper mode (HWWallDispatcher without a draw info). The captured
** HWWalls produce their vertices through HWWall::CreateVertices, including
** the gl_seamless splits, and are fan-triangulated like the classic
** DT_TriangleFan draw.
**
** Polyobjects: every side of a polyobject that lies in one sector at bake
** runs through HWWall::Process like a seg of HWDrawInfo::AddPolyobjs, on a
** seg spanning the whole side. The walls are emitted at the spawn position,
** then made polyobject-local (LocalizePolyobjects, after clustering) and
** placed by a streamed affine transform (LevelMeshPolyobj).
**
** Flats: every subsector of every sector section is triangulated by the
** classic CreateVerticesForSubsector, once per plane; 3D-floor planes reuse
** the target sector's triangulation.
**
** Plane anchoring: the emitters produce absolute z values. For each vertex
** the bake picks the plane that defines it (by the wall type's edge
** semantics, then by matching the value) and stores the offset that makes
** LM_PlaneZ(plane) + offset reproduce the baked z bit-exactly. Wall v is
** stored relative to the texture-top plane (vRef) with the surface's dv/dz.
** Where the classic takes a min/max over planes (mid textures, 3D-floor
** clipping), the anchor is the plane that wins at load.
*/

// HEADER FILES ------------------------------------------------------------

#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <functional>
#include <map>
#include <unordered_map>

#include "hw_levelmeshbake.h"
#include "g_levellocals.h"
#include "p_lnspec.h"
#include "r_sky.h"
#include "texturemanager.h"
#include "c_dispatch.h"
#include "c_cvars.h"
#include "printf.h"
#include "i_time.h"
#include "v_video.h"
#include "m_argv.h"
#include "hwrenderer/scene/hw_lighting.h"
#include "hw_vertexbuilder.h"
#include "flatvertices.h"
#include "hwrenderer/scene/hw_drawinfo.h"
#include "hwrenderer/scene/hw_fakeflat.h"
#include "po_man.h"

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

EXTERN_CVAR(Bool, gl_uselevelmesh)
EXTERN_CVAR(Bool, gl_seamless)

// PUBLIC DATA DEFINITIONS -------------------------------------------------

// Debug: when set, every bake writes <dir>/<map>.obj (tools/abtest/lm_bake_survey.py).
CVAR(String, lm_bake_objdir, "", 0)
// Debug: lm_planeusers for these sectors' 3D-floor models after each bake.
CVAR(String, lm_bake_planeusers, "", 0)
// Debug: print the first N round-trip failures of each bake.
CVAR(Int, lm_bake_debug, 0, 0)
// Cluster triangle budget (ticket 05: 256). Takes effect at the next level setup.
CVAR(Int, lm_cluster_budget, 256, 0)
// Debug: material programs the levelmesh draws (bit per ELevelMeshProgram,
// read at level setup); the others stay on the classic path.
CVAR(Int, lm_programs, 31, CVAR_NOSAVE)
// Debug: draw surfaces lit by a 3D light through the levelmesh (read at
// level setup); 0 leaves them on the classic path.
CVAR(Bool, lm_light3d, true, CVAR_NOSAVE)
// Debug: 3D-floor surfaces the levelmesh draws (read at level setup): 1
// sides, 2 opaque planes, 4 light bands; the others stay on the classic path.
CVAR(Int, lm_3dfloors, 7, CVAR_NOSAVE)
// Debug: bake polyobjects (ticket 13; 0 leaves every polyobject on the
// classic path). Read at level setup.
CVAR(Bool, lm_polyobjs, true, CVAR_NOSAVE)

// PRIVATE DATA DEFINITIONS ------------------------------------------------

namespace
{

constexpr int MAX_CANDIDATES = 64;

struct Candidate
{
	uint32_t ref;
	LevelMeshPlane plane;
};

struct CandidateList
{
	Candidate items[MAX_CANDIDATES];
	int count = 0;

	void Add(sector_t *sec, int ceiling)
	{
		if (sec == nullptr) return;
		uint32_t ref = LM_PlaneRef(sec->Index(), ceiling);
		for (int i = 0; i < count; i++)
			if (items[i].ref == ref) return;
		if (count < MAX_CANDIDATES)
			items[count++] = { ref, LM_PackPlane(sec->GetSecPlane(ceiling)) };
	}

	void Add3DFloors(sector_t *sec)
	{
		if (sec == nullptr) return;
		for (auto rover : sec->e->XFloor.ffloors)
		{
			if (!(rover->flags & FF_EXISTS)) continue;
			Add(rover->top.model, rover->top.isceiling);
			Add(rover->bottom.model, rover->bottom.isceiling);
		}
	}

	void Append(const CandidateList &other)
	{
		for (int i = 0; i < other.count; i++)
		{
			bool found = false;
			for (int j = 0; j < count; j++)
				if (items[j].ref == other.items[i].ref) { found = true; break; }
			if (!found && count < MAX_CANDIDATES) items[count++] = other.items[i];
		}
	}
};

// Finds 'off' with base + off == target in float arithmetic. When the sum's
// granularity (an ulp of the larger operand) cannot hit the target exactly,
// the closest representable sum within one ulp is accepted as inexact.
enum ESolve { SOLVE_EXACT, SOLVE_ULP, SOLVE_FAIL };

// LM_PlaneZ of a plane ref, from the level's current planes.
float PlaneRefZ(FLevelLocals *Level, uint32_t ref, float x, float y)
{
	return LM_PlaneZ(LM_PackPlane(Level->sectors[ref >> 1].GetSecPlane(ref & 1)), x, y);
}

ESolve SolveOffset(float base, float target, float &off)
{
	off = target - base;
	float best = off, bestdiff = INFINITY;
	for (int i = 0; i < 8; i++)
	{
		float r = base + off;
		if (r == target) return SOLVE_EXACT;
		float diff = fabsf(r - target);
		if (diff < bestdiff) { bestdiff = diff; best = off; }
		off = std::nextafter(off, r < target ? INFINITY : -INFINITY);
	}
	off = best;
	float mag = std::max({ fabsf(base), fabsf(target), fabsf(off) });
	return bestdiff <= std::nextafter(mag, INFINITY) - mag ? SOLVE_ULP : SOLVE_FAIL;
}

float MatchTolerance(float z)
{
	return std::max(1.f / 256.f, fabsf(z) * (1.f / (1 << 20)));
}

sector_t *RealSector(FLevelLocals *Level, sector_t *sec)
{
	return sec ? &Level->sectors[sec->sectornum] : nullptr;
}

struct Light3DHash
{
	size_t operator()(const LevelMeshLight3DSource &k) const
	{
		size_t h = std::hash<uint32_t>()(k.sector);
		h = h * 31 + std::hash<const void *>()(k.lightsourceModel);
		h = h * 31 + std::hash<int>()(k.lightsourceFlags);
		h = h * 31 + std::hash<const void *>()(k.lightLevel);
		return h * 2 + k.casterFadeWalls;
	}
};

//==========================================================================
//
// Bake state
//
//==========================================================================

struct Baker
{
	FLevelLocals *Level;
	LevelMeshBake *Out;
	TArray<FFlatVertex> vbuf;
	TArray<uint8_t> roles;
	VertexContainer gen;
	int curType = 0;
	unsigned offsetByType[16] = {};
	std::unordered_map<LevelMeshLight3DSource, uint32_t, Light3DHash> lightIndex;

	// The stable index of a lightlist entry of 'sec' (LevelMeshLight3DSource).
	uint32_t Light3DIndex(sector_t *sec, const lightlist_t &light)
	{
		LevelMeshLight3DSource k;
		k.sector = sec->Index();
		k.lightsourceModel = light.lightsource ? light.lightsource->model : nullptr;
		k.lightsourceFlags = light.lightsource ? (light.lightsource->flags & (FF_FADEWALLS | FF_FOG)) : 0;
		k.lightLevel = light.p_lightlevel;
		k.casterFadeWalls = light.caster && (light.caster->flags & FF_FADEWALLS);
		auto it = lightIndex.find(k);
		if (it != lightIndex.end()) return it->second;
		uint32_t index = Out->Lights3D.Size();
		Out->Lights3D.Push(k);
		lightIndex.emplace(k, index);
		return index;
	}

	uint32_t BeginSurface(LevelMeshSurface &surf)
	{
		for (auto &r : surf.planeRefs) r = LM_NOPLANE;
		surf.vRef = LM_NOPLANE;
		surf.edgeTop = LM_NOPLANE;
		surf.edgeClamp = LM_NOPLANE;
		surf.leaf = 0xffffffffu;
		surf.line = 0xffffffffu;
		surf.firstVertex = Out->Vertices.Size();
		surf.firstIndex = Out->Indices.Size();
		return Out->Surfaces.Size();
	}

	// 'seg': a wall's seg (its linedef is what the surface exposes).
	void EndSurface(LevelMeshSurface &surf, subsector_t *leaf, seg_t *seg = nullptr)
	{
		surf.leaf = leaf ? leaf->Index() : 0xffffffffu;
		if (seg && seg->linedef) surf.line = seg->linedef->Index();
		surf.numVertices = Out->Vertices.Size() - surf.firstVertex;
		surf.numIndices = Out->Indices.Size() - surf.firstIndex;
		Out->Stats.triangles += surf.numIndices / 3;
		Out->Surfaces.Push(surf);
		Out->SurfaceLeaf.Push(leaf ? leaf->Index() : -1);
	}

	void AddPlaneRef(LevelMeshSurface &surf, uint32_t ref, bool &overflow)
	{
		for (auto r : surf.planeRefs)
			if (r == ref) return;
		for (auto &r : surf.planeRefs)
			if (r == LM_NOPLANE) { r = ref; return; }
		overflow = true;
	}

	float TexZ(uint32_t ref)
	{
		return (float)Level->sectors[ref >> 1].GetPlaneTexZ(ref & 1);
	}

	LevelMeshPlane Plane(uint32_t ref)
	{
		return LM_PackPlane(Level->sectors[ref >> 1].GetSecPlane(ref & 1));
	}

	// Anchors z to the first candidate that matches, else to 'fallback' with an offset.
	void AnchorZ(LevelMeshVertex &v, float z, const CandidateList &list, uint32_t fallback, uint32_t role)
	{
		const Candidate *pick = nullptr;
		float tol = MatchTolerance(z);
		for (int i = 0; i < list.count; i++)
		{
			if (fabsf(LM_PlaneZ(list.items[i].plane, v.x, v.y) - z) <= tol)
			{
				pick = &list.items[i];
				break;
			}
		}
		uint32_t ref;
		LevelMeshPlane plane;
		if (pick)
		{
			ref = pick->ref;
			plane = pick->plane;
		}
		else
		{
			ref = fallback != LM_NOPLANE ? fallback : (list.count ? list.items[0].ref : 0);
			plane = Plane(ref);
			if (Out->Stats.offsetAnchored[role]++ < (unsigned)lm_bake_debug)
				Printf("LMBAKE offset: role %u type %d xy %g %g z %.9g fallback sector %u plane %u (z %.9g)\n", role, curType, v.x, v.y, z, ref >> 1, ref & 1, LM_PlaneZ(plane, v.x, v.y));
			offsetByType[curType & 15]++;
		}
		float base = LM_PlaneZ(plane, v.x, v.y);
		ESolve res = SolveOffset(base, z, v.zOffset);
		if (res == SOLVE_ULP) Out->Stats.zInexact++;
		else if (res == SOLVE_FAIL)
		{
			if (Out->Stats.zMismatch++ < (unsigned)lm_bake_debug)
				Printf("LMBAKE zmis: role %u ref %u (sector %u plane %u) xy %g %g z %.9g base %.9g picked %d plane %g %g %g %g\n", role, ref, ref >> 1, ref & 1, v.x, v.y, z, base, pick != nullptr, plane.nx, plane.ny, plane.d, plane.negiC);
		}
		v.anchor = ref | (role << LMV_ROLESHIFT);
		assert(res != SOLVE_FAIL);
	}

	// 3D-floor groups (LevelMeshBake::SideFFGroup / SectorFFGroup) and what
	// their baked surfaces assume (LevelMeshFFWatch).
	TArray<TArray<LevelMeshFFWatch>> groupWatches;
	TArray<uint8_t> groupForceDeferred;

	uint32_t NewFFGroup()
	{
		groupWatches.Reserve(1);
		groupForceDeferred.Push(0);
		return groupWatches.Size() - 1;
	}

	static void WatchPlane(TArray<LevelMeshFFWatch> &list, sector_t *sec, int pos, uint32_t kind = LMFW_PLANE)
	{
		if (sec == nullptr) return;
		uint32_t ref = LM_PlaneRef(sec->Index(), pos);
		for (auto &w : list)
			if (w.kind == kind && w.ref == ref) return;
		LevelMeshFFWatch w = {};
		w.kind = kind;
		w.ref = ref;
		w.plane = LM_PackPlane(sec->GetSecPlane(pos));
		w.textureId = sec->GetTexture(pos).GetIndex();
		list.Push(w);
	}

	// A sector's planes and its 3D floors' model planes (P_Recalculate3DFloors
	// orders and clips the 3D floors by them; the classic picks and clips the
	// 3D-floor sides and planes by them).
	static void WatchLayout(TArray<LevelMeshFFWatch> &list, sector_t *sec)
	{
		if (sec == nullptr) return;
		WatchPlane(list, sec, sector_t::floor);
		WatchPlane(list, sec, sector_t::ceiling);
		for (auto rover : sec->e->XFloor.ffloors)
		{
			WatchPlane(list, rover->top.model, rover->top.isceiling);
			WatchPlane(list, rover->bottom.model, rover->bottom.isceiling);
		}
	}

	static void WatchTier(TArray<LevelMeshFFWatch> &list, side_t *side, int tier, bool colors)
	{
		if (side == nullptr) return;
		uint32_t ref = side->Index() * 3 + tier;
		for (auto &w : list)
		{
			if (w.kind == LMFW_TIER && w.ref == ref)
			{
				w.colors |= colors;
				return;
			}
		}
		LevelMeshFFWatch w = {};
		w.kind = LMFW_TIER;
		w.ref = ref;
		w.textureId = side->GetTexture(tier).GetIndex();
		w.xOffset = (float)side->GetTextureXOffset(tier);
		w.yOffset = (float)side->GetTextureYOffset(tier);
		w.xScale = (float)side->GetTextureXScale(tier);
		w.yScale = (float)side->GetTextureYScale(tier);
		w.colors = colors;
		list.Push(w);
	}

	// The group of a side's 3D-floor sides (HWWall::DoFFloorBlocks: the
	// front and back sectors' planes and 3D floors; BuildFFBlock: the side's
	// tiers and the 3D floors' master sides).
	uint32_t SideGroup(side_t *side, sector_t *rf, sector_t *rb)
	{
		uint32_t &g = Out->SideFFGroup[side->Index()];
		if (g != ~0u) return g;
		g = NewFFGroup();
		Out->FFGroupSource.Push(side->Index());
		if (!(lm_3dfloors & 1)) groupForceDeferred[g] = 1;
		auto &list = groupWatches[g];
		WatchLayout(list, rf);
		WatchLayout(list, rb);
		WatchTier(list, side, side_t::top, false);
		WatchTier(list, side, side_t::mid, true);	// HWWall::RenderTexturedWall colors (renderwalltotier: mid)
		WatchTier(list, side, side_t::bottom, false);
		for (sector_t *s : { rf, rb })
		{
			if (s == nullptr) continue;
			for (auto rover : s->e->XFloor.ffloors)
				if (rover->master && rover->master->sidedef[0]) WatchTier(list, rover->master->sidedef[0], side_t::mid, false);
		}
		return g;
	}

	uint32_t SectorGroup(sector_t *sec)
	{
		uint32_t &g = Out->SectorFFGroup[sec->Index()];
		if (g != ~0u) return g;
		g = NewFFGroup();
		Out->FFGroupSource.Push(-1 - sec->Index());
		if (!(lm_3dfloors & 2)) groupForceDeferred[g] = 1;
		WatchLayout(groupWatches[g], sec);
		return g;
	}

	uint32_t GroupSideRef(uint32_t g) const
	{
		return Level->sides.Size() * 3 + g;
	}

	// Sky (ticket 14)
	std::map<LevelMeshSkySource, uint32_t> skySourceIndex;
	uint32_t SkySource(sector_t *sec, int plane, sector_t *colorSec)
	{
		LevelMeshSkySource k = { uint32_t(sec->Index()), uint32_t(plane), uint32_t(colorSec->Index()) };
		auto it = skySourceIndex.find(k);
		if (it != skySourceIndex.end()) return it->second;
		uint32_t index = Out->SkySources.Size();
		Out->SkySources.Push(k);
		skySourceIndex.emplace(k, index);
		return index;
	}
	bool SideSkyClassic(seg_t *seg, sector_t *rf, sector_t *rb);
	uint32_t BakeQuad(seg_t *seg, sector_t *rf, sector_t *rb, const float xy[4], const float zb[2], const float zt[2], LevelMeshSurface &surf);
	void BakeSkyQuad(seg_t *seg, sector_t *rf, sector_t *rb, const float xy[4], const float zb[2], const float zt[2], uint32_t source, sector_t *closedBack);
	void BakeExposureQuad(seg_t *seg, sector_t *front);
	void BakeExposureQuads();
	TArray<seg_t *> exposureSegs;	// the first seg of every side ProcessSides baked
	void BakeSkyWall(const HWWall &w, sector_t *rf, sector_t *rb);
	void BakeSkyClosedWalls(seg_t *seg, sector_t *rf, sector_t *rb);
	void BakeSkyCap(subsector_t *sub, sector_t *sec, int h);

	void ProcessSides();
	void ProcessPolyobjects();
	void BakeWall(HWWall &w);
	bool BakeBands(HWWall &w, const LevelMeshSurface &surf, uint32_t firstv, int n, sector_t *lsec, float texz);
	uint32_t WallVRef(HWWall &w, sector_t *rf, sector_t *rb);
	uint32_t WallEdgeTop(HWWall &w, sector_t *rf, sector_t *rb, uint32_t firstv, int n);
	void ProcessFlats();
	void BakeFlat(subsector_t *sub, sector_t *sec, int h, sector_t *model, int mplane, int lmindex, int lmplane, F3DFloor *rover, bool portal);
	void ComputeOwnership();
	void BuildFFTables();
	void BuildLight3DTables();
};


//==========================================================================
//
// Sky walls and caps (ticket 14)
//
// The classic sky is the background: the sky walls (HWWall::SkyNormal /
// SkyTop / SkyBottom: from a sky plane to z +-32768) of every visible sky
// configuration register an HWSkyPortal, which writes them (and caps at
// z +-32767 over their bounding box) into depth and stencil and draws the
// dome there. The bake keeps those walls as LMSF_SKY surfaces (LMP_SKY
// bucket), and caps every sky plane at +-32767 with its triangulation; the
// HWSkyPortal of the surface's sky draws them in place of its own lines.
//
// The sky walls of a sky-sky line depend on the back sector being closed
// (a door): the bake makes them for every such line, flagged
// LMSF_SKYCLOSED (the vertex stage shows them while the back sector is
// closed), and drops the copies the classic emitted for the doors closed
// at bake. Sides whose sky depends on the view (heightsecs, the Vrack2b
// lower-sky rule, horizon lines) or on texture-offset hacks (the Icarus
// MAP14 rule) keep the classic's sky walls (SideSkyOwned 0).
//
//==========================================================================

static bool NoSkyWalls(seg_t *seg, sector_t *bs)
{
	return bs->special == GLSector_NoSkyDraw || (bs->MoreFlags & SECMF_NOSKYWALLS) != 0 || (seg->linedef->flags & ML_NOSKYWALLS) != 0;
}

static bool ValidTexture(side_t *side, int tier)
{
	auto tex = TexMan.GetGameTexture(side->GetTexture(tier), true);
	return tex && tex->isValid();
}

// The upper / lower sky of a sky-sky line (HWWall::SkyTop / SkyBottom: the
// closed-back-sector case).
static bool SkySkyUpper(seg_t *seg, sector_t *rf, sector_t *rb)
{
	return rb && rf->GetTexture(sector_t::ceiling) == skyflatnum && rb->GetTexture(sector_t::ceiling) == skyflatnum && !NoSkyWalls(seg, rb) && !rb->transdoor;
}

static bool SkySkyLower(seg_t *seg, sector_t *rf, sector_t *rb)
{
	return rb && rf->GetTexture(sector_t::floor) == skyflatnum && rb->GetTexture(sector_t::floor) == skyflatnum && !NoSkyWalls(seg, rb) &&
		!ValidTexture(seg->sidedef, side_t::bottom);
}

bool Baker::SideSkyClassic(seg_t *seg, sector_t *rf, sector_t *rb)
{
	if (rf->GetHeightSec() || (rb && rb->GetHeightSec())) return true;
	// HWWall::DoHorizon: the sky of a horizon line starts at the viewer's z.
	if (seg->linedef->special == Line_Horizon) return true;
	if (rb == nullptr) return false;
	// SkyTop: the transparent door hack of a sky-sky line (Icarus MAP14).
	if (SkySkyUpper(seg, rf, rb) && !rb->floorplane.isSlope() && !rf->floorplane.isSlope() &&
		rb->GetPlaneTexZ(sector_t::floor) == rf->GetPlaneTexZ(sector_t::floor) + 1.) return true;
	// SkyBottom: a view-dependent lower sky (Vrack2b).
	if (rf->GetTexture(sector_t::floor) == skyflatnum && !NoSkyWalls(seg, rb) && !ValidTexture(seg->sidedef, side_t::bottom) &&
		rb->GetTexture(sector_t::floor) != skyflatnum) return true;
	return false;
}

// One wall quad's vertices, indices and plane refs (sky walls, exposure
// quads), as HWWall::CreateVertices orders a wall's corners: (x1, y1)
// bottom, top, then (x2, y2) top, bottom. A finite edge is anchored to the
// side's plane it lies on; an edge at +-32768 to its end's finite vertex's
// plane with an offset.
uint32_t Baker::BakeQuad(seg_t *seg, sector_t *rf, sector_t *rb, const float xy[4], const float zb[2], const float zt[2], LevelMeshSurface &surf)
{
	CandidateList list;
	list.Add(rf, sector_t::ceiling); list.Add(rf, sector_t::floor);
	list.Add(rb, sector_t::ceiling); list.Add(rb, sector_t::floor);
	uint32_t sindex = BeginSurface(surf);
	uint32_t firstv = Out->Vertices.Size();
	const float corner[4][3] = {
		{ xy[0], xy[1], zb[0] }, { xy[0], xy[1], zt[0] }, { xy[2], xy[3], zt[1] }, { xy[2], xy[3], zb[1] },
	};
	for (int end = 0; end < 2; end++)
	{
		const float *lo = corner[end == 0 ? 0 : 3], *hi = corner[end == 0 ? 1 : 2];
		bool hiInf = hi[2] >= 32768.f, loInf = lo[2] <= -32768.f;
		LevelMeshVertex v[2] = {};
		for (int k = 0; k < 2; k++)
		{
			v[k].x = (k ? hi : lo)[0];
			v[k].y = (k ? hi : lo)[1];
			v[k].surface = sindex;
		}
		// The finite vertex first, then the infinite one on its plane.
		int fin = hiInf ? 0 : 1;
		const float *fc = fin ? hi : lo;
		AnchorZ(v[fin], fc[2], list, LM_NOPLANE, fin ? LMV_TOP : LMV_BOTTOM);
		int inf = 1 - fin;
		if ((inf == 1 && hiInf) || (inf == 0 && loInf))
		{
			uint32_t ref = v[fin].anchor & LMV_REFMASK;
			float base = LM_PlaneZ(Plane(ref), v[inf].x, v[inf].y);
			SolveOffset(base, (inf ? hi : lo)[2], v[inf].zOffset);
			v[inf].anchor = ref | (uint32_t(inf ? LMV_TOP : LMV_BOTTOM) << LMV_ROLESHIFT);
		}
		else AnchorZ(v[inf], (inf ? hi : lo)[2], list, LM_NOPLANE, inf ? LMV_TOP : LMV_BOTTOM);
		if (end == 0) { Out->Vertices.Push(v[0]); Out->Vertices.Push(v[1]); }
		else { Out->Vertices.Push(v[1]); Out->Vertices.Push(v[0]); }
	}
	for (int i = 1; i + 1 < 4; i++)
	{
		Out->Indices.Push(firstv);
		Out->Indices.Push(firstv + i);
		Out->Indices.Push(firstv + i + 1);
	}
	bool overflow = false;
	for (int i = 0; i < 4; i++) AddPlaneRef(surf, Out->Vertices[firstv + i].anchor & LMV_REFMASK, overflow);
	surf.lightSector = rf->Index();
	surf.lightList = -1;
	surf.lightmapPage = -1;
	float dx = xy[2] - xy[0], dy = xy[3] - xy[1];
	float len = sqrtf(dx * dx + dy * dy);
	if (len > 0)
	{
		surf.normal[0] = dy / len;
		surf.normal[1] = -dx / len;
	}
	return sindex;
}

void Baker::BakeSkyQuad(seg_t *seg, sector_t *rf, sector_t *rb, const float xy[4], const float zb[2], const float zt[2], uint32_t source, sector_t *closedBack)
{
	LevelMeshSurface surf = {};
	uint32_t sindex = BakeQuad(seg, rf, rb, xy, zb, zt, surf);
	surf.flags = LMSK_WALL | LMSF_SKY | (uint32_t(LMP_SKY) << LMSF_PROGRAMSHIFT);
	if (closedBack)
	{
		surf.flags |= LMSF_SKYCLOSED;
		surf.vRef = LM_PlaneRef(closedBack->Index(), sector_t::floor);
		Out->Stats.skyClosedWalls++;
	}
	surf.sideRef = source;
	Out->SkySurfaceSide.Push(seg->sidedef ? seg->sidedef->Index() : -1);
	Out->SkySurfaceIndex.Push(sindex);
	EndSurface(surf, seg->Subsector, seg);
	Out->Stats.skyWalls++;
}

// Ticket 18: a side's exposure quad, from its sector's floor to its
// ceiling along the whole line (as BakeSkyClosedWalls orders the side's
// ends). Not drawn: the mark pass rasterizes it, so a line seen through an
// opening (or a one-sided line whose wall the levelmesh does not draw) is
// exposed like the classic's clipper-passing segs (ML_MAPPED).
void Baker::BakeExposureQuad(seg_t *seg, sector_t *front)
{
	line_t *line = seg->linedef;
	vertex_t *v1 = seg->sidedef == line->sidedef[0] ? line->v1 : line->v2;
	vertex_t *v2 = seg->sidedef == line->sidedef[0] ? line->v2 : line->v1;
	float xy[4] = { (float)v1->fX(), (float)v1->fY(), (float)v2->fX(), (float)v2->fY() };
	float zb[2] = { (float)front->floorplane.ZatPoint(v1), (float)front->floorplane.ZatPoint(v2) };
	float zt[2] = { (float)front->ceilingplane.ZatPoint(v1), (float)front->ceilingplane.ZatPoint(v2) };
	LevelMeshSurface surf = {};
	BakeQuad(seg, front, nullptr, xy, zb, zt, surf);
	surf.flags = LMSK_WALL | (uint32_t(LMP_EXPOSURE) << LMSF_PROGRAMSHIFT);
	surf.sideRef = 0xffffffffu;
	surf.textureId = 0;
	EndSurface(surf, seg->Subsector, seg);
	Out->Stats.exposureQuads++;
}

// After ComputeOwnership (exposure quads are no tier's): every two-sided
// side, and every one-sided side whose mid tier the levelmesh does not draw
// (no wall, or a wall left to the classic path), gets an exposure quad.
// Polyobject sides get none (their walls expose their lines).
void Baker::BakeExposureQuads()
{
	for (seg_t *seg : exposureSegs)
	{
		if (seg->backsector == nullptr)
		{
			unsigned ref = seg->sidedef->Index() * 3 + side_t::mid;
			if (ref < Out->SideTierOwned.Size() && Out->SideTierOwned[ref]) continue;
		}
		BakeExposureQuad(seg, seg->frontsector);
	}
	exposureSegs.Reset();
}

// A sky wall the classic emitter produced (HWWall::PutPortal, mesh mode).
void Baker::BakeSkyWall(const HWWall &w, sector_t *rf, sector_t *rb)
{
	int plane = w.portaltype == PORTALTYPE_SKY ? sector_t::ceiling : w.portalplane;
	float xy[4] = { w.glseg.x1, w.glseg.y1, w.glseg.x2, w.glseg.y2 };
	BakeSkyQuad(w.seg, rf, rb, xy, w.zbottom, w.ztop, SkySource(rf, plane, RealSector(Level, w.frontsector)), nullptr);
}

// The sky walls of a sky-sky line that exist while the back sector is closed.
void Baker::BakeSkyClosedWalls(seg_t *seg, sector_t *rf, sector_t *rb)
{
	line_t *line = seg->linedef;
	vertex_t *v1 = seg->sidedef == line->sidedef[0] ? line->v1 : line->v2;
	vertex_t *v2 = seg->sidedef == line->sidedef[0] ? line->v2 : line->v1;
	float xy[4] = { (float)v1->fX(), (float)v1->fY(), (float)v2->fX(), (float)v2->fY() };
	if (SkySkyUpper(seg, rf, rb))
	{
		float zb[2] = { (float)rb->ceilingplane.ZatPoint(v1), (float)rb->ceilingplane.ZatPoint(v2) };
		float zt[2] = { 32768.f, 32768.f };
		BakeSkyQuad(seg, rf, rb, xy, zb, zt, SkySource(rf, sector_t::ceiling, rf), rb);
	}
	if (SkySkyLower(seg, rf, rb))
	{
		float zb[2] = { -32768.f, -32768.f };
		float zt[2] = { (float)rb->floorplane.ZatPoint(v1), (float)rb->floorplane.ZatPoint(v2) };
		BakeSkyQuad(seg, rf, rb, xy, zb, zt, SkySource(rf, sector_t::floor, rf), rb);
	}
}

// A sky plane's cap: its triangulation at z +-32767 (the HWPortal caps).
void Baker::BakeSkyCap(subsector_t *sub, sector_t *sec, int h)
{
	uint32_t ref = LM_PlaneRef(sec->Index(), h);
	LevelMeshPlane plane = Plane(ref);
	float capz = h == sector_t::ceiling ? 32767.f : -32767.f;
	LevelMeshSurface surf = {};
	uint32_t sindex = BeginSurface(surf);
	uint32_t firstv = Out->Vertices.Size();
	for (auto &qv : gen.vertices)
	{
		LevelMeshVertex v = {};
		v.x = (float)qv.vertex->fX();
		v.y = (float)qv.vertex->fY();
		v.surface = sindex;
		SolveOffset(LM_PlaneZ(plane, v.x, v.y), capz, v.zOffset);
		v.anchor = ref | (LMV_FLAT << LMV_ROLESHIFT);
		Out->Vertices.Push(v);
	}
	bool flip = h == sector_t::ceiling;
	for (unsigned i = 0; i + 2 < gen.indices.Size(); i += 3)
	{
		Out->Indices.Push(firstv + gen.indices[i]);
		Out->Indices.Push(firstv + gen.indices[flip ? i + 2 : i + 1]);
		Out->Indices.Push(firstv + gen.indices[flip ? i + 1 : i + 2]);
	}
	surf.planeRefs[0] = ref;
	surf.flags = (h == sector_t::ceiling ? LMSK_CEILING : LMSK_FLOOR) | LMSF_SKY | (uint32_t(LMP_SKY) << LMSF_PROGRAMSHIFT);
	surf.sideRef = SkySource(sec, h, sec);
	surf.lightSector = sec->Index();
	surf.lightList = -1;
	surf.lightmapPage = -1;
	surf.normal[2] = h == sector_t::ceiling ? -1.f : 1.f;
	EndSurface(surf, sub);
	Out->Stats.skyCaps++;
}

//==========================================================================
//
// Walls
//
//==========================================================================

void Baker::ProcessSides()
{
	HWMeshHelper mh;
	HWWallDispatcher disp(Level, &mh, getRealLightmode(Level, true));
	sector_t backcopy;
	Out->TierBase.Resize(Level->sides.Size() * 3);
	for (auto &tb : Out->TierBase) tb = {};
	Out->PlaneProgram.Resize(Level->sectors.Size() * 2);
	for (auto &p : Out->PlaneProgram) p = -2;
	Out->PlanePortal.Resize(Level->sectors.Size() * 2);
	for (auto &p : Out->PlanePortal) p = 0;
	Out->SideFFGroup.Resize(Level->sides.Size());
	for (auto &g : Out->SideFFGroup) g = ~0u;
	Out->SectorFFGroup.Resize(Level->sectors.Size());
	for (auto &g : Out->SectorFFGroup) g = ~0u;
	Out->SideSkyOwned.Resize(Level->sides.Size());
	for (auto &o : Out->SideSkyOwned) o = 0;

	for (auto &side : Level->sides)
	{
		if (side.Flags & WALLF_POLYOBJ)
		{
			Out->Stats.polySidesSkipped++;
			continue;
		}
		if (side.numsegs == 0 || side.segs == nullptr) continue;
		// HWDrawInfo::DoSubsector never renders polyobject origin
		// subsectors (their polyobject's vertices were moved to the start
		// spot), so none of their segs is drawn. A side split between such
		// a subsector and a rendered one is baked whole (counted).
		int polyOrgSegs = 0;
		for (int i = 0; i < side.numsegs; i++)
			if (side.segs[i]->Subsector && (side.segs[i]->Subsector->flags & SSECF_POLYORG)) polyOrgSegs++;
		if (polyOrgSegs == side.numsegs)
		{
			Out->Stats.polyOriginSides++;
			continue;
		}
		if (polyOrgSegs > 0) Out->Stats.polyOriginPartialSides++;
		seg_t *seg = side.segs[0];
		if (seg->sidedef != &side || seg->linedef == nullptr) continue;

		// As HWDrawInfo::AddLine, without the view: the fake-flat of a
		// heightsec sector depends on the view area, so those are baked
		// unfaked and flagged.
		sector_t *front = seg->frontsector;
		sector_t *back = nullptr;
		if (seg->backsector)
		{
			if (front->sectornum == seg->backsector->sectornum)
			{
				if (!seg->linedef->isVisualPortal())
				{
					auto tex = TexMan.GetGameTexture(side.GetTexture(side_t::mid), true);
					if (!tex || !tex->isValid()) continue;
				}
				back = front;
			}
			else if (!seg->backsector->GetHeightSec() || seg->backsector->heightsec == seg->backsector)
			{
				back = hw_FakeFlat(seg->backsector, area_default, true, &backcopy);
			}
			else back = seg->backsector;
		}

		HWWall wall;
		wall.sub = seg->Subsector;
		wall.Process(&disp, seg, front, back, false);
		Out->Stats.sidesProcessed++;

		for (auto &w : mh.list)
		{
			if (w.type == RENDERWALL_FOGBOUNDARY || w.type == RENDERWALL_COLOR || w.texture == nullptr)
				Out->FogWalls.Push(w);
			else
				BakeWall(w);
		}
		for (auto &w : mh.translucent) Out->TranslucentWalls.Push(w);
		exposureSegs.Push(seg);
		// Sky walls (ticket 14): the levelmesh's, unless the side's sky
		// depends on the view. Mesh mode leaves a sky plane's portal type
		// unset (-1, HWWall::SkyPlane); SkyLine sets PORTALTYPE_SKY.
		sector_t *rf = RealSector(Level, front), *rb = RealSector(Level, back);
		bool skyClassic = SideSkyClassic(seg, rf, rb);
		bool hasSky = false;
		for (auto &w : mh.portals)
		{
			bool isSky = w.portaltype == PORTALTYPE_SKY ||
				(w.portaltype == -1 && rf->GetTexture(w.portalplane) == skyflatnum && rf->ValidatePortal(w.portalplane) == nullptr);
			if (!isSky || skyClassic)
			{
				Out->PortalWalls.Push(w);
				hasSky |= isSky;
				continue;
			}
			// The sky-sky door walls closed at bake: baked below, for any state.
			int plane = w.portaltype == PORTALTYPE_SKY ? sector_t::ceiling : w.portalplane;
			if (plane == sector_t::ceiling ? SkySkyUpper(seg, rf, rb) : SkySkyLower(seg, rf, rb)) continue;
			BakeSkyWall(w, rf, rb);
		}
		if (skyClassic)
		{
			if (hasSky) Out->Stats.skyClassicSides++;
		}
		else
		{
			BakeSkyClosedWalls(seg, rf, rb);
			Out->SideSkyOwned[side.Index()] = 1;
		}
		for (auto &m : mh.upper) Out->UpperMissing.Push(m);
		for (auto &m : mh.lower) Out->LowerMissing.Push(m);
		mh.list.Clear();
		mh.translucent.Clear();
		mh.portals.Clear();
		mh.upper.Clear();
		mh.lower.Clear();
	}
}

//==========================================================================
//
// Polyobjects
//
// HWDrawInfo::AddPolyobjs draws a polyobject per seg of a subsector's
// mini-BSP: HWWall::Process(seg, currentsector, seg->backsector ?
// currentsector : nullptr), the seg's front / back sector being the side's
// (FNodeBuilder::AddPolySegs: the origin container, from which two-sided
// polyobject walls take their heights and texture alignment). The bake makes
// the same call once per side, on a seg spanning the whole side at the
// polyobject's position at bake; the classic's per-subsector cut only adds
// vertices along the edges. Only polyobjects inside one sector are baked:
// their walls' planes and light come from that sector. One that lies across
// sectors, in a heightsec or 3D-floor sector, with portals, or with a wall
// the mesh cannot draw stays classic.
//
//==========================================================================

void Baker::ProcessPolyobjects()
{
	auto &polys = Level->Polyobjects;
	unsigned npoly = polys.Size();
	auto &st = Out->Stats;
	st.polyobjs = npoly;
	Out->SidePoly.Resize(Level->sides.Size());
	for (auto &p : Out->SidePoly) p = -1;
	Out->PolyBaked.Resize(npoly);
	Out->PolyDeferred.Resize(npoly);
	Out->PolySector.Resize(npoly);
	Out->PolyOrigin.Resize(npoly);
	for (unsigned i = 0; i < npoly; i++)
	{
		Out->PolyBaked[i] = 0;
		Out->PolyDeferred[i] = 0;
		Out->PolySector[i] = 0;
		// A whole-unit origin keeps local = world - origin exact for the
		// usual integral vertices, so the transform at rest reproduces them.
		Out->PolyOrigin[i] = DVector2(floor(polys[i].StartSpot.pos.X), floor(polys[i].StartSpot.pos.Y));
	}

	HWMeshHelper mh;
	HWWallDispatcher disp(Level, &mh, getRealLightmode(Level, true));
	for (unsigned i = 0; i < npoly; i++)
	{
		FPolyObj &poly = polys[i];
		for (side_t *side : poly.Sidedefs) Out->SidePoly[side->Index()] = int(i);

		// The sector (HWDrawInfo::DoSubsector's) the classic draws it in.
		if (poly.subsectorlinks == nullptr) poly.CreateSubsectorLinks();
		subsector_t *sub = nullptr;
		sector_t *sec = nullptr;
		bool single = true;
		for (FPolyNode *pn = poly.subsectorlinks; pn != nullptr; pn = pn->snext)
		{
			if (pn->subsector == nullptr || pn->subsector->sector == nullptr) continue;
			sector_t *s = &Level->sectors[pn->subsector->sector->sectornum];
			if (sec == nullptr)
			{
				sec = s;
				sub = pn->subsector;
			}
			else if (s != sec) single = false;
		}
		if (!lm_polyobjs || sec == nullptr || !single || sec->GetHeightSec() || sec->e->XFloor.ffloors.Size() > 0 || sec->e->XFloor.lightlist.Size() > 0 ||
			poly.bHasPortals || (sub->flags & SSECF_POLYORG))
		{
			st.polyClassicSectors++;
			continue;
		}

		unsigned firstSurface = Out->Surfaces.Size();
		bool ok = true;
		for (side_t *side : poly.Sidedefs)
		{
			line_t *line = side->linedef;
			if (line == nullptr) continue;
			bool front = line->sidedef[0] == side;
			vertex_t verts[2];
			memset((void *)verts, 0, sizeof(verts));
			verts[0].set((front ? line->v1 : line->v2)->fPos());
			verts[1].set((front ? line->v2 : line->v1)->fPos());
			seg_t seg;
			memset((void *)&seg, 0, sizeof(seg));
			seg.v1 = &verts[0];
			seg.v2 = &verts[1];
			seg.sidedef = side;
			seg.linedef = line;
			seg.frontsector = side->sector;
			seg.backsector = line->frontsector == side->sector ? line->backsector : line->frontsector;
			seg.Subsector = sub;
			seg.sidefrac = 1.f;

			HWWall wall;
			wall.sub = sub;
			wall.Process(&disp, &seg, sec, seg.backsector ? sec : nullptr, false);
			for (auto &w : mh.list)
			{
				if (w.type == RENDERWALL_FOGBOUNDARY || w.type == RENDERWALL_COLOR || w.texture == nullptr)
				{
					ok = false;
					continue;
				}
				// One seg per side: no splits at the side's other segs (those
				// are the origin container's, which do not move).
				w.flags |= HWWall::HWF_NOSPLIT;
				BakeWall(w);
			}
			if (mh.translucent.Size() || mh.portals.Size()) ok = false;
			mh.list.Clear();
			mh.translucent.Clear();
			mh.portals.Clear();
			mh.upper.Clear();
			mh.lower.Clear();
		}
		for (unsigned k = firstSurface; k < Out->Surfaces.Size(); k++)
			if (Out->Surfaces[k].flags & LMSF_DEFERRED) ok = false;
		if (!ok || firstSurface == Out->Surfaces.Size())
		{
			// The classic draws every wall of it (the surfaces stay deferred).
			for (unsigned k = firstSurface; k < Out->Surfaces.Size(); k++) Out->Surfaces[k].flags |= LMSF_DEFERRED;
			if (!ok) st.polyClassicWalls++;
			continue;
		}
		for (unsigned k = firstSurface; k < Out->Surfaces.Size(); k++)
		{
			Out->Surfaces[k].flags |= LMSF_POLYOBJ;
			Out->Surfaces[k].leaf = 0xffffffffu;	// moves: no BSP leaf of its own (the line still exposes)
			Out->SurfaceLeaf[k] = -2 - int(i);
		}
		st.polySurfaces += Out->Surfaces.Size() - firstSurface;
		st.polyobjsBaked++;
		Out->PolyBaked[i] = 1;
		Out->PolySector[i] = sec->Index();
	}
}

} // namespace

// The levelmesh program of a texture's material (FMaterial's shader index,
// chosen as its constructor does: default, warp 1, warp 2, specular, PBR),
// or -1 for the material shaders the levelmesh does not build (paletted,
// user shaders, canvases). Animation frames are assumed to share their
// base's shader.
int LevelMesh_MaterialProgram(FGameTexture *tex)
{
	if (tex == nullptr) return LMP_DEFAULT;
	if (tex->isHardwareCanvas() || tex->GetUseType() == ETextureType::SWCanvas) return -1;
	if (tex->GetShaderIndex() != SHADER_Default) return -1;
	int program = LMP_DEFAULT;
	if (tex->isWarped()) program = tex->isWarped() == 1 ? LMP_WARP1 : LMP_WARP2;
	else if (tex->GetNormalmap() && tex->GetSpecularmap()) program = LMP_SPECULAR;
	else if (tex->GetNormalmap() && tex->GetMetallic() && tex->GetRoughness() && tex->GetAmbientOcclusion()) program = LMP_PBR;
	return (lm_programs & (1 << program)) ? program : -1;
}

bool LevelMesh_NeedsClassicMaterial(FGameTexture *tex)
{
	return LevelMesh_MaterialProgram(tex) < 0;
}

namespace
{

// The side_t::GetLightLevel inputs of a tier, as LMSD_* bits.
static uint32_t SideLightFlags(side_t *side, int tier)
{
	uint32_t f = 0;
	if (side->Flags & WALLF_ABSLIGHTING) f |= LMSD_ABSLIGHTING;
	if (side->Flags & (WALLF_ABSLIGHTING_TIER << tier)) f |= LMSD_ABSLIGHTING_TIER;
	if (side->Flags & WALLF_NOFAKECONTRAST) f |= LMSD_NOFAKECONTRAST;
	if (side->Flags & WALLF_SMOOTHLIGHTING) f |= LMSD_SMOOTHLIGHTING;
	if (side->Flags & WALLF_LIGHT_FOG) f |= LMSD_LIGHT_FOG;
	return f;
}

// Side colors, additive color and texture manipulation the classic wall draw
// applies (HWWall::RenderTexturedWall); white / none draws unchanged.
} // namespace

bool LevelMesh_HasWallColorEffects(side_t *side, int tier, sector_t *frontsector)
{
	if (frontsector == nullptr) return false;
	if (uint32_t(side->GetSpecialColor(tier, side_t::walltop, frontsector)) != 0xffffffffu) return true;
	if (uint32_t(side->GetSpecialColor(tier, side_t::wallbottom, frontsector)) != 0xffffffffu) return true;
	if ((uint32_t(side->GetAdditiveColor(tier, frontsector)) & 0xffffff) != 0) return true;
	return side->textures[tier].TextureFx.AddColor.a != 0;
}

bool LevelMesh_HasPlaneColorEffects(sector_t *sec, int pos)
{
	return sec->SpecialColors[pos] != 0xffffffff || (sec->AdditiveColors[pos] & 0xffffff) != 0 || sec->planes[pos].TextureFx.AddColor.a != 0;
}

namespace
{

//==========================================================================
//
// The plane that bounds a wall tier from above (LevelMeshSurface::edgeTop).
// HWWall::Process emits an upper tier only below the front ceiling, a lower
// tier only below the back floor, a one-sided wall only below the ceiling.
// The bound is dropped when a non-top vertex already lies above it at load
// (a top clipped by another plane, an offset anchor): clamping would move it.
//
//==========================================================================

uint32_t Baker::WallEdgeTop(HWWall &w, sector_t *rf, sector_t *rb, uint32_t firstv, int n)
{
	sector_t *sec = nullptr;
	int plane = sector_t::ceiling;
	switch (w.type)
	{
	case RENDERWALL_TOP:
	case RENDERWALL_M1S:
		sec = rf;
		break;
	case RENDERWALL_BOTTOM:
		sec = rb;
		plane = sector_t::floor;
		break;
	default:
		return LM_NOPLANE;
	}
	if (sec == nullptr) return LM_NOPLANE;
	LevelMeshPlane edge = LM_PackPlane(sec->GetSecPlane(plane));
	for (int i = 0; i < n; i++)
	{
		const LevelMeshVertex &v = Out->Vertices[firstv + i];
		if ((v.anchor >> LMV_ROLESHIFT) == LMV_TOP) continue;
		float z = PlaneRefZ(Level, v.anchor & LMV_REFMASK, v.x, v.y) + v.zOffset;
		if (z > LM_PlaneZ(edge, v.x, v.y))
		{
			Out->Stats.edgeUnbounded++;
			return LM_NOPLANE;
		}
	}
	return LM_PlaneRef(sec->Index(), plane);
}

//==========================================================================
//
// The plane the texture top follows (DoTexture / DoMidTexture callers in
// HWWall::Process). 3D-floor sides are resolved by the caller.
//
//==========================================================================

uint32_t Baker::WallVRef(HWWall &w, sector_t *rf, sector_t *rb)
{
	auto ref = [](sector_t *s, int p) { return s ? LM_PlaneRef(s->Index(), p) : LM_NOPLANE; };
	int lflags = w.seg->linedef->flags;
	switch (w.type)
	{
	case RENDERWALL_M1S:
		return (lflags & ML_DONTPEGBOTTOM) ? ref(rf, sector_t::floor) : ref(rf, sector_t::ceiling);

	case RENDERWALL_TOP:
		return (lflags & ML_DONTPEGTOP) ? ref(rf, sector_t::ceiling) : ref(rb, sector_t::ceiling);

	case RENDERWALL_BOTTOM:
		if (!(lflags & ML_DONTPEGBOTTOM)) return ref(rb, sector_t::floor);
		if (w.frontsector->GetTexture(sector_t::ceiling) == skyflatnum && w.backsector &&
			w.backsector->GetTexture(sector_t::ceiling) == skyflatnum)
			return ref(rb, sector_t::ceiling);
		return ref(rf, sector_t::ceiling);

	case RENDERWALL_M2S:
	case RENDERWALL_M2SNF:
		if (!rb) rb = rf;
		if (lflags & ML_DONTPEGBOTTOM)
			return rf->GetPlaneTexZ(sector_t::floor) >= rb->GetPlaneTexZ(sector_t::floor) ? ref(rf, sector_t::floor) : ref(rb, sector_t::floor);
		return rf->GetPlaneTexZ(sector_t::ceiling) <= rb->GetPlaneTexZ(sector_t::ceiling) ? ref(rf, sector_t::ceiling) : ref(rb, sector_t::ceiling);

	default:
		return LM_NOPLANE;
	}
}

void Baker::BakeWall(HWWall &w)
{
	auto &st = Out->Stats;
	sector_t *rf = RealSector(Level, w.frontsector);
	sector_t *rb = RealSector(Level, w.backsector);
	side_t *side = w.seg->sidedef;
	bool is3d = w.type == RENDERWALL_FFBLOCK;
	// Light comes from the sector the wall is drawn in; a two-sided
	// polyobject wall takes its heights and texture alignment from its seg's
	// sectors, the origin container (HWWall::Process).
	sector_t *lightFront = rf;
	if ((side->Flags & WALLF_POLYOBJ) && w.seg->backsector)
	{
		rf = RealSector(Level, w.seg->frontsector);
		rb = RealSector(Level, w.seg->backsector);
	}

	// Vertices, exactly as HWWall::MakeVertices(false) would produce them.
	bool split = gl_seamless && !(w.flags & HWWall::HWF_NOSPLIT);
	unsigned maxv = split ? w.CountVertices() : 4;
	maxv += 2 * side->numsegs + 8;	// CountVertices is not a strict bound for the upper-edge split
	if (vbuf.Size() < maxv)
	{
		vbuf.Resize(maxv);
		roles.Resize(maxv);
	}
	FFlatVertex *ptr = vbuf.Data();
	int n = w.CreateVertices(ptr, split, roles.Data());
	assert((unsigned)n <= maxv);
	if (n < 3) return;

	// Edge candidates in semantic priority order.
	CandidateList top, bottom, extra;
	switch (w.type)
	{
	case RENDERWALL_TOP:
		top.Add(rf, sector_t::ceiling); top.Add(rb, sector_t::ceiling);
		bottom.Add(rb, sector_t::ceiling); bottom.Add(rf, sector_t::ceiling);
		break;
	case RENDERWALL_BOTTOM:
		top.Add(rb, sector_t::floor); top.Add(rf, sector_t::floor);
		bottom.Add(rf, sector_t::floor); bottom.Add(rb, sector_t::floor);
		break;
	case RENDERWALL_M1S:
		top.Add(rf, sector_t::ceiling);
		bottom.Add(rf, sector_t::floor);
		break;
	case RENDERWALL_FFBLOCK:
		top.Add3DFloors(rf); top.Add3DFloors(rb);
		bottom.Add3DFloors(rf); bottom.Add3DFloors(rb);
		break;
	default:	// two-sided mid textures: the lower ceiling / higher floor bound them
		top.Add(rb, sector_t::ceiling); top.Add(rf, sector_t::ceiling);
		bottom.Add(rb, sector_t::floor); bottom.Add(rf, sector_t::floor);
		break;
	}
	extra.Add(rf, sector_t::ceiling); extra.Add(rf, sector_t::floor);
	extra.Add(rb, sector_t::ceiling); extra.Add(rb, sector_t::floor);
	extra.Add3DFloors(rf); extra.Add3DFloors(rb);
	top.Append(extra);
	bottom.Append(extra);

	uint32_t vref = WallVRef(w, rf, rb);
	curType = w.type;

	// HWWall::Process clamps an upper tier's bottom up to the front floor
	// where that lies above the back ceiling at both ends, and a lower
	// tier's top down to the front ceiling where that lies below the back
	// floor (not with ML_DRAWFULLHEIGHT). Those vertices are anchored to the
	// back sector's plane with the clamp kept as LevelMeshSurface::edgeClamp,
	// so a lift moving past its neighbour's plane is followed. Only taken
	// when max / min of the two planes reproduces every edge vertex exactly.
	uint32_t clampRef = LM_NOPLANE, clampSem = LM_NOPLANE;
	bool clampMax = false;
	uint8_t clampRole = 0;
	if (rb && !is3d && !(w.seg->linedef->flags & ML_DRAWFULLHEIGHT))
	{
		if (w.type == RENDERWALL_TOP && (w.frontsector->GetTexture(sector_t::floor) != skyflatnum || w.backsector->GetTexture(sector_t::floor) != skyflatnum))
		{
			clampRef = LM_PlaneRef(rf->Index(), sector_t::floor);
			clampSem = LM_PlaneRef(rb->Index(), sector_t::ceiling);
			clampMax = true;
			clampRole = HWWall::VR_BOTTOM;
		}
		else if (w.type == RENDERWALL_BOTTOM)
		{
			clampRef = LM_PlaneRef(rf->Index(), sector_t::ceiling);
			clampSem = LM_PlaneRef(rb->Index(), sector_t::floor);
			clampRole = HWWall::VR_TOP;
		}
		if (clampRef != LM_NOPLANE)
		{
			LevelMeshPlane cp = Plane(clampRef), sp = Plane(clampSem);
			for (int i = 0; i < n; i++)
			{
				if (roles[i] != clampRole) continue;
				float zc = LM_PlaneZ(cp, vbuf[i].x, vbuf[i].y), zs = LM_PlaneZ(sp, vbuf[i].x, vbuf[i].y);
				if ((clampMax ? std::max(zs, zc) : std::min(zs, zc)) != vbuf[i].z)
				{
					clampRef = LM_NOPLANE;
					break;
				}
			}
		}
	}

	LevelMeshSurface surf = {};
	uint32_t sindex = BeginSurface(surf);
	uint32_t firstv = Out->Vertices.Size();

	// dv/dz from the corner pairs (texture v is linear in z on a wall).
	float vs[2] = { 0, 0 };
	bool hasvs[2] = { false, false };
	float dzl = w.ztop[0] - w.zbottom[0], dzr = w.ztop[1] - w.zbottom[1];
	if (fabsf(dzl) > 1e-3f) { vs[0] = (w.tcs[HWWall::UPLFT].v - w.tcs[HWWall::LOLFT].v) / dzl; hasvs[0] = true; }
	if (fabsf(dzr) > 1e-3f) { vs[1] = (w.tcs[HWWall::UPRGT].v - w.tcs[HWWall::LORGT].v) / dzr; hasvs[1] = true; }
	float vscale = hasvs[0] ? vs[0] : vs[1];
	if (hasvs[0] && hasvs[1] && fabsf(vs[0] - vs[1]) > 1e-4f * std::max(fabsf(vs[0]), 1e-3f)) st.vScaleInconsistent++;

	for (int i = 0; i < n; i++)
	{
		const FFlatVertex &fv = vbuf[i];
		LevelMeshVertex v;
		v.x = fv.x;
		v.y = fv.y;
		v.u = fv.u;
		v.lu = fv.lu;
		v.lv = fv.lv;
		v.surface = sindex;
		if (clampRef != LM_NOPLANE && roles[i] == clampRole)
		{
			v.zOffset = 0.f;
			v.anchor = clampSem | (uint32_t(clampMax ? LMV_BOTTOM : LMV_TOP) << LMV_ROLESHIFT);
			Out->Vertices.Push(v);
			continue;
		}
		switch (roles[i])
		{
		case HWWall::VR_TOP:
			AnchorZ(v, fv.z, top, vref, LMV_TOP);
			break;
		case HWWall::VR_BOTTOM:
			AnchorZ(v, fv.z, bottom, vref, LMV_BOTTOM);
			break;
		default:
		{
			// gl_seamless side split: a height from the planes around that vertex
			vertex_t *vt = (fv.x == w.glseg.x1 && fv.y == w.glseg.y1) ? w.vertexes[0] : w.vertexes[1];
			CandidateList sidec;
			if (vt && vt->sectors)
			{
				for (int k = 0; k < vt->numsectors; k++)
				{
					sidec.Add(vt->sectors[k], sector_t::floor);
					sidec.Add(vt->sectors[k], sector_t::ceiling);
				}
			}
			sidec.Append(extra);
			AnchorZ(v, fv.z, sidec, LM_NOPLANE, LMV_SIDE);
			break;
		}
		}
		Out->Vertices.Push(v);
	}

	if (is3d)
	{
		// The rover's top plane is not kept in the HWWall; the upper-left anchor is it unless clipped.
		vref = Out->Vertices[firstv + 1].anchor & LMV_REFMASK;
		st.ffloorVRefHeuristic++;
	}

	// v relative to the texture-top plane.
	float texz = vref != LM_NOPLANE ? TexZ(vref) : 0.f;
	for (int i = 0; i < n; i++)
	{
		auto &v = Out->Vertices[firstv + i];
		float z = vbuf[i].z;
		float t = vscale * (z - texz);
		ESolve res = SolveOffset(t, vbuf[i].v, v.vOffset);
		if (res == SOLVE_ULP) st.vInexact++;
		else if (res == SOLVE_FAIL)
		{
			if (st.vMismatch++ < (unsigned)lm_bake_debug)
				Printf("LMBAKE vmis: side %d type %d z %.9g texz %.9g vscale %.9g t %.9g v %.9g\n", side->Index(), w.type, z, texz, vscale, t, vbuf[i].v);
		}
		assert(res != SOLVE_FAIL);
	}

	// Classic DT_TriangleFan.
	for (int i = 1; i + 1 < n; i++)
	{
		Out->Indices.Push(firstv);
		Out->Indices.Push(firstv + i);
		Out->Indices.Push(firstv + i + 1);
	}

	bool overflow = false;
	for (int i = 0; i < n; i++) AddPlaneRef(surf, Out->Vertices[firstv + i].anchor & LMV_REFMASK, overflow);
	if (overflow) st.planeRefOverflow++;

	int tier = w.type == RENDERWALL_TOP ? side_t::top : w.type == RENDERWALL_BOTTOM ? side_t::bottom : side_t::mid;
	// HWWall::Process lights 3D-floor sides with the upper tier's side light.
	int lightTier = is3d ? side_t::top : tier;
	surf.flags = LMSK_WALL | (uint32_t(w.type) << LMSF_WALLTYPESHIFT);
	if (is3d) surf.flags |= LMSF_3DFLOOR;
	// The classic list rule (HWDrawInfo::AddWall): only two-sided mid textures
	// and 3D-floor sides can be masked. FTexture::Masked is not final before the
	// pixels load, so every two-sided mid texture is alpha tested; an opaque
	// texture passes the test everywhere.
	if (w.type == RENDERWALL_M2S || w.type == RENDERWALL_M2SNF || (is3d && w.texture->isMasked())) surf.flags |= LMSF_MASKED;
	if (w.flags & HWWall::HWF_CLAMPX) surf.flags |= LMSF_CLAMPX;
	if (w.flags & HWWall::HWF_CLAMPY) surf.flags |= LMSF_CLAMPY;
	if (w.flags & HWWall::HWF_GLOW) surf.flags |= LMSF_GLOW;
	if ((rf && rf->GetHeightSec()) || (rb && rb->GetHeightSec()))
	{
		surf.flags |= LMSF_HEIGHTSEC;
		st.heightsecSurfaces++;
	}
	if (w.glseg.x1 != w.glseg.x2 && (w.ztop[0] - w.zbottom[0]) != (w.ztop[1] - w.zbottom[1])) surf.flags |= LMSF_SKEW;
	surf.vRef = vref;
	surf.vScale = vscale;
	surf.edgeTop = WallEdgeTop(w, rf, rb, firstv, n);
	if (clampRef != LM_NOPLANE)
	{
		surf.edgeClamp = clampRef;
		if (clampMax) surf.flags |= LMSF_EDGECLAMPMAX;
		st.edgeClamped++;
	}
	// The untranslated id, so the translation table follows the animation:
	// w.texture is the frame current at bake.
	FTextureID sideTex = side->GetTexture(tier);
	if (!is3d && TexMan.GetGameTexture(sideTex, true) == w.texture) surf.textureId = sideTex.GetIndex();
	else if (is3d)
	{
		// HWWall::BuildFFBlock takes the side's upper / lower texture or the
		// 3D floor's master side's mid texture: the untranslated one of those.
		surf.textureId = w.texture->GetID().GetIndex();
		TArray<FTextureID> ids;
		ids.Push(side->GetTexture(side_t::top));
		ids.Push(side->GetTexture(side_t::bottom));
		for (sector_t *s : { rf, rb })
		{
			if (s == nullptr) continue;
			for (auto rover : s->e->XFloor.ffloors)
				if (rover->master && rover->master->sidedef[0]) ids.Push(rover->master->sidedef[0]->GetTexture(side_t::mid));
		}
		for (auto id : ids)
		{
			if (id.isValid() && TexMan.GetGameTexture(id, true) == w.texture)
			{
				surf.textureId = id.GetIndex();
				break;
			}
		}
	}
	else
	{
		surf.textureId = w.texture->GetID().GetIndex();
		st.wallTextureNotSide++;
	}
	surf.sideRef = is3d ? GroupSideRef(SideGroup(side, rf, rb)) : uint32_t(side->Index()) * 3 + tier;
	if (!is3d)
	{
		// The texture offsets the baked u / v contain, and the FTexCoordInfo
		// that maps them (GetTexCoordInfo in hw_walls.cpp).
		FTexCoordInfo tci;
		tci.GetFromTexture(w.texture, (float)side->GetTextureXScale(tier), (float)side->GetTextureYScale(tier), !!(Level->flags3 & LEVEL3_FORCEWORLDPANNING));
		float sx = fabsf(tci.mScale.X), sy = fabsf(tci.mScale.Y);
		surf.xOffsetBase = (float)side->GetTextureXOffset(tier);
		surf.yOffsetBase = (float)side->GetTextureYOffset(tier);
		surf.texOffsetScale = (sx == 1.f || tci.mWorldPanning) ? 1.f : sx;
		surf.rowOffsetScale = (sy == 1.f || tci.mWorldPanning) ? 1.f : sy;
		surf.renderWidth = (float)tci.mRenderWidth;
		bool mid2 = w.type == RENDERWALL_M2S || w.type == RENDERWALL_M2SNF;
		if (mid2) surf.wrapWidth = tci.TextureAdjustWidth();
		if (surf.textureId == sideTex.GetIndex())
		{
			surf.flags |= LMSF_STATETEXTURE;
			auto &tb = Out->TierBase[surf.sideRef];
			tb.renderWidth = tci.mRenderWidth;
			tb.renderHeight = tci.mRenderHeight;
			tb.scaleX = tci.mScale.X;
			tb.scaleY = tci.mScale.Y;
			tb.worldPanning = tci.mWorldPanning;
			tb.valid = true;
			bool wrap = (w.seg->linedef->flags & ML_WRAP_MIDTEX) || (side->Flags & WALLF_WRAP_MIDTEX);
			tb.midClip = mid2 && !wrap;
			tb.yOffset = surf.yOffsetBase;
			tb.lineAlpha = (float)w.seg->linedef->alpha;
			tb.additive = (w.seg->linedef->flags & ML_ADDTRANS) != 0;
		}
	}
	surf.lightSector = lightFront->Index();
	surf.lightList = -1;
	// HWWall::Process: a wall of a sector with 3D floors goes through
	// SplitWall, which lights every piece with the light layer it lies in
	// (Put3DWall) or, where a layer's plane crosses it, draws it once per
	// layer with split planes (the HWWall keeps the lightlist).
	sector_t *lsec = RealSector(Level, w.sub ? w.sub->sector : w.seg->frontsector);
	auto &lightlist = lsec->e->XFloor.lightlist;
	if (w.lightlist)
	{
		// Banded wall: cut into its light layers (BakeBands).
		surf.flags |= LMSF_LIGHTLIST;
		surf.lightSector = lsec->Index();
		st.bandedWalls++;
	}
	else if (lightlist.Size() > 0)
	{
		// SplitWall: the first layer whose lower plane lies at or below the piece.
		unsigned li = lightlist.Size() - 1;
		for (unsigned i = 0; i + 1 < lightlist.Size(); i++)
		{
			secplane_t &p = lightlist[i + 1].plane;
			float low1 = (float)p.ZatPoint(w.glseg.x1, w.glseg.y1);
			float low2 = (float)p.ZatPoint(w.glseg.x2, w.glseg.y2);
			if (low1 <= w.zbottom[0] && low2 <= w.zbottom[1])
			{
				li = i;
				break;
			}
		}
		surf.lightList = int32_t(Light3DIndex(lsec, lightlist[li]));
		// Put3DWall keeps the wall's light level only for the side's own sector light.
		if (lightlist[li].p_lightlevel != &side->sector->lightlevel) surf.flags |= LMSF_LIGHT3DLEVEL;
		st.light3DSurfaces++;
	}
	surf.lightLevel = w.lightlevel;
	surf.relLight = w.rellight;
	surf.sideLight = (uint16_t)side->Light | (int32_t(side->TierLights[lightTier]) << 16);
	surf.fakeContrast = (uint16_t)side->GetFakeContrastRel(true) | (int32_t(side->GetFakeContrastRel(false)) << 16);
	surf.sideFlags = SideLightFlags(side, lightTier);
	if (w.type == RENDERWALL_M2SNF) surf.flags |= LMSF_NOSECTORFOG;
	surf.lightmapPage = w.lindex >= 0 ? int(w.lindex) : -1;
	// Not drawn by the levelmesh yet: heightsecs (view-dependent fake,
	// ticket 16), sky-hack mid textures (the classic draws them with a depth
	// bias, GLDL_MASKEDWALLSOFS).
	if (surf.flags & LMSF_HEIGHTSEC) surf.flags |= LMSF_DEFERRED;
	if (surf.lightList >= 0 && !lm_light3d) surf.flags |= LMSF_DEFERRED;
	if ((w.flags & HWWall::HWF_SKYHACK) && w.type == RENDERWALL_M2S) surf.flags |= LMSF_DEFERRED;
	// HWWall::RenderTexturedWall: clamped two-sided mids also clamp in the shader.
	if ((w.flags & HWWall::HWF_CLAMPY) && (w.type == RENDERWALL_M2S || w.type == RENDERWALL_M2SNF) && !w.texture->isWarped()) surf.flags |= LMSF_TEXCLAMPY;
	// Not yet expressible by the levelmesh shading: non-default material
	// shaders, and side colors / texture manipulation (ticket 11; the
	// resolve covers light, fog, desaturation and glow). The values
	// HWWall::RenderTexturedWall applies are tested, not WALLF_EXTCOLOR: the
	// map loader sets that flag on every side (sector_t::CheckExColorFlag).
	int program = LevelMesh_MaterialProgram(w.texture);
	if (program < 0 || LevelMesh_HasWallColorEffects(side, tier, w.frontsector)) surf.flags |= LMSF_DEFERRED;
	else surf.flags |= uint32_t(program) << LMSF_PROGRAMSHIFT;
	if (surf.flags & LMSF_STATETEXTURE) Out->TierBase[surf.sideRef].program = program;
	float dx = w.glseg.x2 - w.glseg.x1, dy = w.glseg.y2 - w.glseg.y1;
	float len = sqrtf(dx * dx + dy * dy);
	if (len > 0)
	{
		surf.normal[0] = dy / len;
		surf.normal[1] = -dx / len;
	}
	surf.normal[2] = 0;
	if (surf.flags & LMSF_LIGHTLIST)
	{
		if ((lm_3dfloors & 4) && BakeBands(w, surf, firstv, n, lsec, texz)) return;
		surf.flags |= LMSF_DEFERRED;
		st.bandsDeferred++;
	}
	EndSurface(surf, w.seg->Subsector, w.seg);
	st.wallSurfaces++;
}

//==========================================================================
//
// Light bands
//
// A wall a 3D light's plane crosses is drawn by the classic once per light
// layer, clipped to the layer by split planes (HWWall::RenderTexturedWall
// with a lightlist). The bake cuts it into one surface per layer instead:
// the 3D-floor layout is fixed while the levelmesh draws the sector (a moved
// layout defers its lights, LML3_DEFERRED), so the cuts stay where the
// split planes are. A cut vertex lies on the layer plane, anchored to the
// sector plane that is that plane (offset 0, LMV_SIDE). One cluster draw
// covers all bands of a wall.
//
//==========================================================================

namespace
{

struct BandVertex
{
	double x, y, z, u, v, lu, lv;
	LevelMeshVertex mv;
};

// Keeps the part of 'in' with sign * (plane z - z) >= 0 (sign 1: below the
// plane, -1: above it). Cut vertices get the plane's anchor.
void ClipBand(const TArray<BandVertex> &in, TArray<BandVertex> &out, const secplane_t &plane, double sign,
	uint32_t ref, const LevelMeshPlane &packed, float vScale, float texz)
{
	constexpr double EPS = 1. / 4096;
	out.Clear();
	unsigned n = in.Size();
	if (n < 3) return;
	auto dist = [&](const BandVertex &p) { return sign * (plane.ZatPoint(p.x, p.y) - p.z); };
	auto cut = [&](const BandVertex &a, const BandVertex &b, double da, double db)
	{
		double t = da / (da - db);
		BandVertex c;
		c.x = a.x + t * (b.x - a.x);
		c.y = a.y + t * (b.y - a.y);
		c.u = a.u + t * (b.u - a.u);
		c.v = a.v + t * (b.v - a.v);
		c.lu = a.lu + t * (b.lu - a.lu);
		c.lv = a.lv + t * (b.lv - a.lv);
		c.mv = a.mv;
		c.mv.x = (float)c.x;
		c.mv.y = (float)c.y;
		c.mv.u = (float)c.u;
		c.mv.lu = (float)c.lu;
		c.mv.lv = (float)c.lv;
		c.mv.zOffset = 0.f;
		c.mv.anchor = ref | (uint32_t(LMV_SIDE) << LMV_ROLESHIFT);
		float z = LM_PlaneZ(packed, c.mv.x, c.mv.y);
		c.z = z;
		c.mv.vOffset = (float)c.v - vScale * (z - texz);
		out.Push(c);
	};
	for (unsigned i = 0; i < n; i++)
	{
		const BandVertex &a = in[i], &b = in[(i + 1) % n];
		double da = dist(a), db = dist(b);
		bool ina = da >= -EPS, inb = db >= -EPS;
		if (ina && inb) out.Push(b);
		else if (ina && !inb)
		{
			if (da > EPS) cut(a, b, da, db);
		}
		else if (!ina && inb)
		{
			if (db > EPS) cut(a, b, da, db);
			out.Push(b);
		}
	}
}

// Area of a wall polygon in (along the wall, z).
double BandArea(const TArray<BandVertex> &poly, double dx, double dy)
{
	double a = 0;
	for (unsigned i = 0; i < poly.Size(); i++)
	{
		auto &p = poly[i], &q = poly[(i + 1) % poly.Size()];
		double sp = p.x * dx + p.y * dy, sq = q.x * dx + q.y * dy;
		a += sp * q.z - sq * p.z;
	}
	return fabs(a) * 0.5;
}

} // namespace

bool Baker::BakeBands(HWWall &w, const LevelMeshSurface &surf, uint32_t firstv, int n, sector_t *lsec, float texz)
{
	auto &st = Out->Stats;
	auto &lightlist = lsec->e->XFloor.lightlist;
	if (lightlist.Size() < 2) return false;

	// The sector planes the layer planes are (a lightlist plane is a copy of
	// one: the sector's ceiling, a 3D floor's plane, the front floor).
	CandidateList cands;
	cands.Add(lsec, sector_t::ceiling);
	cands.Add(lsec, sector_t::floor);
	cands.Add3DFloors(lsec);
	sector_t *front = RealSector(Level, w.frontsector);
	cands.Add(front, sector_t::floor);
	auto findRef = [&](const secplane_t &plane) -> int
	{
		LevelMeshPlane q = LM_PackPlane(plane);
		for (int i = 0; i < cands.count; i++)
		{
			const LevelMeshPlane &c = cands.items[i].plane;
			auto same = [](float a, float b) { return fabsf(a - b) <= 1e-6f * std::max(1.f, fabsf(a)); };
			if (same(q.nx * q.negiC, c.nx * c.negiC) && same(q.ny * q.negiC, c.ny * c.negiC) && same(q.d * q.negiC, c.d * c.negiC)) return i;
		}
		return -1;
	};

	TArray<BandVertex> poly;
	for (int i = 0; i < n; i++)
	{
		const FFlatVertex &fv = vbuf[i];
		poly.Push({ fv.x, fv.y, fv.z, fv.u, fv.v, fv.lu, fv.lv, Out->Vertices[firstv + i] });
	}
	double dx = w.glseg.x2 - w.glseg.x1, dy = w.glseg.y2 - w.glseg.y1;
	double len = sqrt(dx * dx + dy * dy);
	if (len <= 0) return false;
	dx /= len;
	dy /= len;

	// Every layer the classic draws (RenderTexturedWall's loop): the part
	// between its plane and the next layer's (the front floor for the last).
	struct Band { unsigned layer; TArray<BandVertex> poly; };
	TArray<Band> bands;
	TArray<BandVertex> tmp;
	for (unsigned i = 0; i < lightlist.Size(); i++)
	{
		const secplane_t &upper = lightlist[i].plane;
		const secplane_t &lower = i + 1 == lightlist.Size() ? w.frontsector->floorplane : lightlist[i + 1].plane;
		int ui = findRef(upper), li = findRef(lower);
		if (ui < 0 || li < 0)
		{
			st.bandPlaneUnmatched++;
			return false;
		}
		ClipBand(poly, tmp, upper, 1., cands.items[ui].ref, cands.items[ui].plane, surf.vScale, texz);
		Band band;
		band.layer = i;
		ClipBand(tmp, band.poly, lower, -1., cands.items[li].ref, cands.items[li].plane, surf.vScale, texz);
		if (band.poly.Size() >= 3 && BandArea(band.poly, dx, dy) > 1e-3) bands.Push(std::move(band));
	}
	if (bands.Size() == 0) return false;

	// Replace the whole wall by its bands.
	Out->Vertices.Resize(firstv);
	Out->Indices.Resize(surf.firstIndex);
	for (auto &band : bands)
	{
		LevelMeshSurface b = surf;
		uint32_t sindex = Out->Surfaces.Size();
		b.firstVertex = Out->Vertices.Size();
		b.firstIndex = Out->Indices.Size();
		for (auto &r : b.planeRefs) r = LM_NOPLANE;
		bool overflow = false;
		for (auto &p : band.poly)
		{
			LevelMeshVertex v = p.mv;
			v.surface = sindex;
			Out->Vertices.Push(v);
			AddPlaneRef(b, v.anchor & LMV_REFMASK, overflow);
		}
		if (overflow) st.planeRefOverflow++;
		for (unsigned i = 1; i + 1 < band.poly.Size(); i++)
		{
			Out->Indices.Push(b.firstVertex);
			Out->Indices.Push(b.firstVertex + i);
			Out->Indices.Push(b.firstVertex + i + 1);
		}
		const lightlist_t &light = lightlist[band.layer];
		b.flags &= ~LMSF_LIGHTLIST;
		b.lightList = int32_t(Light3DIndex(lsec, light));
		// RenderTexturedWall: the layer's light level where it has a caster.
		if (light.caster != nullptr) b.flags |= LMSF_LIGHT3DLEVEL;
		if (!lm_light3d) b.flags |= LMSF_DEFERRED;
		EndSurface(b, w.seg->Subsector, w.seg);
		st.wallSurfaces++;
		st.bandSurfaces++;
		st.light3DSurfaces++;
	}
	return true;
}

//==========================================================================
//
// Flats
//
//==========================================================================

// h: the side the plane is drawn from (floor: from above, ceiling: from
// below); model / mplane: the plane; lmindex / lmplane: its lightmap
// (sub->lightmap[lmplane][lmindex]); rover: the 3D floor of a 3D-floor plane.
void Baker::BakeFlat(subsector_t *sub, sector_t *sec, int h, sector_t *model, int mplane, int lmindex, int lmplane, F3DFloor *rover, bool portal)
{
	auto &st = Out->Stats;
	bool ffloor = rover != nullptr;
	const secplane_t &splane = model->GetSecPlane(mplane);
	uint32_t ref = LM_PlaneRef(model->Index(), mplane);
	LevelMeshPlane plane = LM_PackPlane(splane);
	float diff = (!ffloor && sec->transdoor && h == sector_t::floor) ? -1.f : 0.f;

	LightmapSurface *lm = nullptr;
	if (sec->HasLightmaps && sub->lightmap[lmplane] && lmindex >= 0)
	{
		lm = &sub->lightmap[lmplane][lmindex];
		if (lm->Type == ST_NULL) lm = nullptr;
	}

	LevelMeshSurface surf = {};
	uint32_t sindex = BeginSurface(surf);
	uint32_t firstv = Out->Vertices.Size();
	for (auto &qv : gen.vertices)
	{
		vertex_t *vt = qv.vertex;
		LevelMeshVertex v;
		// as SetFlatVertex in hw_vertexbuilder.cpp
		v.x = (float)vt->fX();
		v.y = (float)vt->fY();
		float z = (float)splane.ZatPoint(vt) + diff;
		v.u = (float)vt->fX() / 64.f;
		v.vOffset = -(float)vt->fY() / 64.f;
		v.lu = v.lv = 0;
		if (lm)
		{
			for (unsigned j = 0; j < sub->numlines; j++)
			{
				if (sub->firstline[j].v1 == vt)
				{
					v.lu = lm->TexCoords[j * 2];
					v.lv = lm->TexCoords[j * 2 + 1];
					break;
				}
			}
		}
		v.surface = sindex;
		float base = LM_PlaneZ(plane, v.x, v.y);
		if (fabsf(base - z) > MatchTolerance(z) + fabsf(diff)) st.offsetAnchored[LMV_FLAT]++;
		ESolve res = SolveOffset(base, z, v.zOffset);
		if (res == SOLVE_ULP) st.zInexact++;
		else if (res == SOLVE_FAIL) st.zMismatch++;
		assert(res != SOLVE_FAIL);
		v.anchor = ref | (LMV_FLAT << LMV_ROLESHIFT);
		Out->Vertices.Push(v);
	}
	// The classic draws floors and ceilings from one triangulation without
	// culling. The levelmesh culls back faces, so a ceiling (seen from below)
	// gets the opposite winding of a floor. A 3D-floor plane is drawn from
	// the side(s) HWFlat::ProcessSector picks it for (its view test is the
	// back-face cull).
	bool flip = h == sector_t::ceiling;
	for (unsigned i = 0; i + 2 < gen.indices.Size(); i += 3)
	{
		Out->Indices.Push(firstv + gen.indices[i]);
		Out->Indices.Push(firstv + gen.indices[flip ? i + 2 : i + 1]);
		Out->Indices.Push(firstv + gen.indices[flip ? i + 1 : i + 2]);
	}

	surf.planeRefs[0] = ref;
	surf.flags = (h == sector_t::ceiling ? LMSK_CEILING : LMSK_FLOOR);
	if (ffloor) surf.flags |= LMSF_3DFLOOR;
	if (sec->GetHeightSec())
	{
		surf.flags |= LMSF_HEIGHTSEC;
		st.heightsecSurfaces++;
	}
	surf.lightList = -1;
	// HWFlat::ProcessSector: in a sector with 3D floors the plane takes the
	// colormap and (unless it is the sector's own) the light level of its
	// 3D light (P_GetPlaneLight).
	if (ffloor)
	{
		// HWFlat::SetFrom3DFloor: the light layer at the plane, from below
		// for a 3D floor's bottom; always its light level.
		bool top = lmplane == sector_t::floor;	// lightmaps: [floor] for the top, [ceiling] for the bottom
		lightlist_t *light = P_GetPlaneLight(sec, top ? rover->top.plane : rover->bottom.plane, !top);
		surf.lightList = int32_t(Light3DIndex(sec, *light));
		surf.flags |= LMSF_LIGHT3DLEVEL;
		st.light3DSurfaces++;
	}
	else if (sec->e->XFloor.ffloors.Size() > 0 && sec->e->XFloor.lightlist.Size() > 0)
	{
		lightlist_t *light = P_GetPlaneLight(sec, &sec->GetSecPlane(h), h == sector_t::ceiling);
		// The classic floor also takes the 3D light's level for an absolute
		// plane light when the light has no source; the ceiling never does.
		bool absolute = (sec->GetFlags(h) & PLANEF_ABSLIGHTING) && (h == sector_t::ceiling || light->lightsource != nullptr);
		surf.lightList = int32_t(Light3DIndex(sec, *light));
		if (!absolute && light->p_lightlevel != &sec->lightlevel) surf.flags |= LMSF_LIGHT3DLEVEL;
		st.light3DSurfaces++;
	}
	if (surf.flags & (LMSF_HEIGHTSEC | LMSF_LIGHTLIST)) surf.flags |= LMSF_DEFERRED;
	if (surf.lightList >= 0 && !lm_light3d) surf.flags |= LMSF_DEFERRED;
	FTextureID texid = model->GetTexture(mplane);
	auto tex = TexMan.GetGameTexture(texid, true);
	// As walls: material shaders and the plane colors / texture manipulation
	// HWFlat::DrawFlat applies (ticket 11).
	int program = LevelMesh_MaterialProgram(tex);
	if (program < 0 || LevelMesh_HasPlaneColorEffects(model, mplane)) surf.flags |= LMSF_DEFERRED;
	else surf.flags |= uint32_t(program) << LMSF_PROGRAMSHIFT;
	// HWDrawInfo::AddFlat: only 3D-floor and portal planes are alpha tested.
	if (tex && tex->isMasked() && (ffloor || portal)) surf.flags |= LMSF_MASKED;
	surf.vScale = 0;
	surf.textureId = texid.GetIndex();
	// Sector planes follow their plane's streamed texture (3D-floor planes stay baked, ticket 12).
	if (!ffloor)
	{
		surf.flags |= LMSF_STATETEXTURE;
		Out->PlaneProgram[ref] = int8_t(program);
		Out->PlanePortal[ref] = portal;
	}
	surf.sideRef = 0xffffffffu;
	if (ffloor)
	{
		uint32_t g = SectorGroup(sec);
		WatchPlane(groupWatches[g], model, mplane, LMFW_PLANETEX);
		surf.sideRef = GroupSideRef(g);
	}
	surf.lightSector = sec->Index();
	surf.lightLevel = RescaleLightLevel(h == sector_t::floor ? sec->GetFloorLight() : sec->GetCeilingLight());
	surf.lightmapPage = lm ? int(lm->LightmapNum) : -1;
	const DVector3 &nrm = splane.Normal();
	float sign = (mplane == h) ? 1.f : -1.f;	// a 3D floor's top is its model's ceiling, seen as a floor
	surf.normal[0] = sign * (float)nrm.X;
	surf.normal[1] = sign * (float)nrm.Y;
	surf.normal[2] = sign * (float)nrm.Z;
	EndSurface(surf, sub);
	if (ffloor) st.ffloorFlatSurfaces++;
	else st.flatSurfaces++;
}

// The 3D-floor planes HWFlat::ProcessSector draws, without its view test:
// planes seen from below top to bottom, then planes seen from above bottom
// to top, each within the range left by the previous one at the sector's
// center. 'skipFog': as in a fullbright scene (fog 3D floors skipped).
struct FFPick
{
	unsigned k;			// index in ffloors
	bool top;			// the 3D floor's top plane, else its bottom
	int drawnAs;		// sector_t::floor: seen from above, ceiling: from below
	bool operator==(const FFPick &o) const { return k == o.k && top == o.top && drawnAs == o.drawnAs; }
};

static bool FFTranslucent(F3DFloor *rover)
{
	return (rover->flags & (FF_TRANSLUCENT | FF_ADDITIVETRANS | FF_FOG)) || rover->alpha < 255;
}

static void PickFFPlanes(sector_t &sec, bool skipFog, TArray<FFPick> &out)
{
	out.Clear();
	auto &ffloors = sec.e->XFloor.ffloors;
	double lastceilingheight = sec.CenterCeiling();
	double lastfloorheight = sec.CenterFloor();
	auto usable = [&](F3DFloor *rover)
	{
		if ((rover->flags & (FF_EXISTS | FF_RENDERPLANES | FF_THISINSIDE)) != (FF_EXISTS | FF_RENDERPLANES)) return false;
		return !(skipFog && (rover->flags & FF_FOG));
	};
	for (unsigned k = 0; k < ffloors.Size(); k++)
	{
		F3DFloor *rover = ffloors[k];
		if (!usable(rover)) continue;
		if (!rover->top.copied && (rover->flags & (FF_INVERTPLANES | FF_BOTHPLANES)))
		{
			double ff_top = rover->top.plane->ZatPoint(sec.centerspot);
			if (ff_top < lastceilingheight)
			{
				out.Push({ k, true, sector_t::ceiling });
				lastceilingheight = ff_top;
			}
		}
		if (!rover->bottom.copied && !(rover->flags & FF_INVERTPLANES))
		{
			double ff_bottom = rover->bottom.plane->ZatPoint(sec.centerspot);
			if (ff_bottom < lastceilingheight)
			{
				out.Push({ k, false, sector_t::ceiling });
				lastceilingheight = ff_bottom;
				if (rover->alpha < 255) lastceilingheight += EQUAL_EPSILON;
			}
		}
	}
	for (int k = int(ffloors.Size()) - 1; k >= 0; k--)
	{
		F3DFloor *rover = ffloors[k];
		if (!usable(rover)) continue;
		if (!rover->bottom.copied && (rover->flags & (FF_INVERTPLANES | FF_BOTHPLANES)))
		{
			double ff_bottom = rover->bottom.plane->ZatPoint(sec.centerspot);
			if (ff_bottom > lastfloorheight || (rover->flags & FF_FIX))
			{
				out.Push({ unsigned(k), false, sector_t::floor });
				lastfloorheight = ff_bottom;
			}
		}
		if (!rover->top.copied && !(rover->flags & FF_INVERTPLANES))
		{
			double ff_top = rover->top.plane->ZatPoint(sec.centerspot);
			if (ff_top > lastfloorheight)
			{
				out.Push({ unsigned(k), true, sector_t::floor });
				lastfloorheight = ff_top;
				if (rover->alpha < 255) lastfloorheight -= EQUAL_EPSILON;
			}
		}
	}
}

void Baker::ProcessFlats()
{
	TArray<FFPick> picks, picksFullbright, opaque, opaqueFullbright;
	for (auto &sec : Level->sectors)
	{
		auto &ffloors = sec.e->XFloor.ffloors;
		PickFFPlanes(sec, false, picks);
		bool deferGroup = false;
		if (ffloors.Size() > 0)
		{
			// The fullbright scene skips fog 3D floors, which can change the
			// picks of the others; FF_FIX and FF_FADEWALLS carry colormap
			// state from one 3D plane to the next in ProcessSector; without a
			// lightlist there is no 3D light to resolve.
			PickFFPlanes(sec, true, picksFullbright);
			opaque.Clear();
			opaqueFullbright.Clear();
			for (auto &p : picks) if (!FFTranslucent(ffloors[p.k])) opaque.Push(p);
			for (auto &p : picksFullbright) if (!FFTranslucent(ffloors[p.k])) opaqueFullbright.Push(p);
			deferGroup = !(opaque == opaqueFullbright) || sec.e->XFloor.lightlist.Size() == 0;
			for (auto rover : ffloors)
				if ((rover->flags & FF_EXISTS) && (rover->flags & (FF_FIX | FF_FADEWALLS))) deferGroup = true;
			for (auto rover : ffloors)
			{
				if ((rover->flags & (FF_EXISTS | FF_RENDERPLANES | FF_THISINSIDE)) != (FF_EXISTS | FF_RENDERPLANES) || FFTranslucent(rover)) continue;
				for (int t = 0; t < 2; t++)
				{
					if ((t == 0 ? rover->top : rover->bottom).copied) continue;
					bool picked = false;
					for (auto &p : opaque) picked |= ffloors[p.k] == rover && p.top == (t == 0);
					if (!picked) Out->Stats.ffFlatsNotPicked++;
				}
			}
			if (deferGroup && opaque.Size() > 0) groupForceDeferred[SectorGroup(&sec)] = 1;
		}
		for (auto &section : Level->sections.SectionsForSector(&sec))
		{
			for (auto sub : section.subsectors)
			{
				// Never rendered by the classic (HWDrawInfo::DoSubsector):
				// the polyobject moved its vertices, so the fan spans the map.
				if (sub->flags & SSECF_POLYORG)
				{
					Out->Stats.polyOriginSubsectors++;
					continue;
				}
				gen.vertices.Clear();
				gen.vertexmap.Clear();
				gen.indices.Clear();
				CreateVerticesForSubsector(sub, gen, -1);
				if (gen.indices.Size() == 0) continue;

				// Sector planes (HWFlat::ProcessSector's opaque case).
				for (int h = sector_t::floor; h <= sector_t::ceiling; h++)
				{
					FTextureID texid = sec.GetTexture(h);
					bool sky = texid == skyflatnum;
					bool portal = sec.ValidatePortal(h) != nullptr;
					float alpha = portal ? (float)sec.GetAlpha(h) : 1.f - sec.GetReflect(h);
					if (sky || portal || sec.GetReflect(h) > 0)
						Out->PortalFlats.Push({ sub, &sec, nullptr, h });
					if (sky && !portal && !sec.GetHeightSec()) BakeSkyCap(sub, &sec, h);
					if (sky || alpha == 0.f) continue;
					auto tex = TexMan.GetGameTexture(texid, true);
					if (!tex || !tex->isValid())
					{
						Out->Stats.untexturedFlats++;
						continue;
					}
					if (alpha < 1.f)
					{
						Out->TranslucentFlats.Push({ sub, &sec, nullptr, h });
						continue;
					}
					BakeFlat(sub, &sec, h, &sec, h, 0, h, nullptr, portal);
				}

				// 3D-floor planes, on the target sector's triangulation.
				auto &ffloors = sec.e->XFloor.ffloors;
				for (unsigned k = 0; k < ffloors.Size(); k++)
				{
					F3DFloor *rover = ffloors[k];
					if ((rover->flags & (FF_EXISTS | FF_RENDERPLANES | FF_THISINSIDE)) != (FF_EXISTS | FF_RENDERPLANES)) continue;
					if (!FFTranslucent(rover)) continue;
					for (int t = 0; t < 2; t++)
					{
						auto &pr = t == 0 ? rover->top : rover->bottom;
						if (!pr.copied) Out->TranslucentFlats.Push({ sub, pr.model, rover, pr.isceiling });
					}
				}
				for (auto &pick : picks)
				{
					F3DFloor *rover = ffloors[pick.k];
					if (FFTranslucent(rover)) continue;
					auto &pr = pick.top ? rover->top : rover->bottom;
					auto tex = TexMan.GetGameTexture(*pr.texture, true);
					if (!tex || !tex->isValid())
					{
						Out->Stats.untexturedFlats++;
						continue;
					}
					BakeFlat(sub, &sec, pick.drawnAs, pr.model, pr.isceiling, int(pick.k) + 1, pick.top ? sector_t::floor : sector_t::ceiling, rover, false);
				}
			}
		}
	}
}

//==========================================================================
//
// Ownership: which classic draw-list entries the levelmesh replaces. A side
// tier is owned when all its mesh surfaces are drawn; one deferred piece
// (a light band, a 3D-floor clip) defers the whole tier, so the classic
// HWWalls of a tier are either all skipped or all drawn. A sector plane is
// owned when its (non-3D-floor) flats are in the mesh and drawn. A 3D-floor
// group (a side's 3D-floor sides, a sector's opaque 3D-floor planes) works
// like a tier.
//
//==========================================================================

void Baker::ComputeOwnership()
{
	enum { HAS_MESH = 1, HAS_DEFERRED = 2 };
	TArray<uint8_t> tiers(Level->sides.Size() * 3, true);
	TArray<uint8_t> planes(Level->sectors.Size() * 2, true);
	TArray<uint8_t> groups(groupWatches.Size(), true);
	memset(tiers.Data(), 0, tiers.Size());
	memset(planes.Data(), 0, planes.Size());
	for (unsigned g = 0; g < groups.Size(); g++) groups[g] = groupForceDeferred[g] ? HAS_DEFERRED : 0;
	uint32_t groupBase = GroupSideRef(0);

	for (auto &surf : Out->Surfaces)
	{
		if (surf.flags & LMSF_SKY) continue;	// owned per side (SideSkyOwned), drawn by the sky portals
		uint8_t bit = (surf.flags & LMSF_DEFERRED) ? HAS_DEFERRED : HAS_MESH;
		if (surf.flags & LMSF_3DFLOOR) groups[surf.sideRef - groupBase] |= bit;
		else if ((surf.flags & LMSF_KINDMASK) == LMSK_WALL) tiers[surf.sideRef] |= bit;
		else planes[surf.planeRefs[0]] |= bit;	// plane ref = sector * 2 + plane
	}
	for (auto &surf : Out->Surfaces)
	{
		if (surf.flags & LMSF_SKY) continue;
		if (surf.flags & LMSF_DEFERRED)
		{
			Out->Stats.deferredSurfaces++;
			continue;
		}
		bool defer = (surf.flags & LMSF_3DFLOOR) ? (groups[surf.sideRef - groupBase] & HAS_DEFERRED) != 0 :
			(surf.flags & LMSF_KINDMASK) == LMSK_WALL && (tiers[surf.sideRef] & HAS_DEFERRED);
		if (defer)
		{
			surf.flags |= LMSF_DEFERRED;
			Out->Stats.deferredByTier++;
			Out->Stats.deferredSurfaces++;
		}
	}
	Out->FFGroupBaked.Resize(groups.Size());
	for (unsigned g = 0; g < groups.Size(); g++)
	{
		Out->FFGroupBaked[g] = groups[g] == HAS_MESH;
		if (!Out->FFGroupBaked[g]) Out->Stats.ffGroupsDeferred++;
	}
	Out->FFGroupOwned = Out->FFGroupBaked;
	Out->Stats.ffGroups = groups.Size();
	Out->SideTierOwned.Resize(tiers.Size());
	for (unsigned i = 0; i < tiers.Size(); i++) Out->SideTierOwned[i] = tiers[i] == HAS_MESH;
	Out->SectorPlaneOwned.Resize(planes.Size());
	for (unsigned i = 0; i < planes.Size(); i++) Out->SectorPlaneOwned[i] = planes[i] == HAS_MESH;
	Out->SideTierBaked = Out->SideTierOwned;
	Out->SectorPlaneBaked = Out->SectorPlaneOwned;
}

//==========================================================================
//
// 3D-floor group tables: watches, and per sector / side the groups that
// watch it (LevelMeshBake::FFWatchStart, FFSectorDepend, FFSideDepend).
//
//==========================================================================

static void BuildCSR(unsigned n, const TArray<std::pair<uint32_t, uint32_t>> &pairs, TArray<uint32_t> &start, TArray<uint32_t> &list);

void Baker::BuildFFTables()
{
	TArray<std::pair<uint32_t, uint32_t>> bySector, bySide;
	Out->FFWatchStart.Resize(groupWatches.Size() + 1);
	Out->FFWatches.Clear();
	for (unsigned g = 0; g < groupWatches.Size(); g++)
	{
		Out->FFWatchStart[g] = Out->FFWatches.Size();
		unsigned firstSector = bySector.Size(), firstSide = bySide.Size();
		for (auto &w : groupWatches[g])
		{
			Out->FFWatches.Push(w);
			auto &list = w.kind == LMFW_TIER ? bySide : bySector;
			uint32_t key = w.kind == LMFW_TIER ? w.ref / 3 : w.ref >> 1;
			unsigned first = w.kind == LMFW_TIER ? firstSide : firstSector;
			bool dup = false;
			for (unsigned k = first; k < list.Size(); k++) dup |= list[k].first == key;
			if (!dup) list.Push({ key, g });
		}
	}
	Out->FFWatchStart[groupWatches.Size()] = Out->FFWatches.Size();
	BuildCSR(Level->sectors.Size(), bySector, Out->FFSectorDependStart, Out->FFSectorDepend);
	BuildCSR(Level->sides.Size(), bySide, Out->FFSideDependStart, Out->FFSideDepend);
	Out->Stats.ffWatches = Out->FFWatches.Size();
}

//==========================================================================
//
// 3D-light tables (LevelMeshBake::SectorLights, LayoutPlanes, ...)
//
//==========================================================================

// CSR of (key, value) pairs over 'n' keys, values in pair order.
static void BuildCSR(unsigned n, const TArray<std::pair<uint32_t, uint32_t>> &pairs, TArray<uint32_t> &start, TArray<uint32_t> &list)
{
	start.Resize(n + 1);
	for (auto &v : start) v = 0;
	for (auto &p : pairs) start[p.first + 1]++;
	for (unsigned i = 0; i < n; i++) start[i + 1] += start[i];
	list.Resize(pairs.Size());
	TArray<uint32_t> fill(n, true);
	for (unsigned i = 0; i < n; i++) fill[i] = start[i];
	for (auto &p : pairs) list[fill[p.first]++] = p.second;
}

void Baker::BuildLight3DTables()
{
	unsigned nsec = Level->sectors.Size();
	auto &lights = Out->Lights3D;
	TArray<std::pair<uint32_t, uint32_t>> pairs;

	for (unsigned i = 0; i < lights.Size(); i++) pairs.Push({ lights[i].sector, i });
	BuildCSR(nsec, pairs, Out->SectorLightStart, Out->SectorLights);

	// The planes a lit sector's split and 3D lights depend on: its own and
	// its 3D floors' (P_Recalculate3DFloors orders and clips by them).
	TArray<std::pair<uint32_t, uint32_t>> depend;
	Out->LayoutStart.Resize(nsec + 1);
	Out->LayoutPlanes.Clear();
	for (unsigned s = 0; s < nsec; s++)
	{
		Out->LayoutStart[s] = Out->LayoutPlanes.Size();
		if (Out->SectorLightStart[s] == Out->SectorLightStart[s + 1]) continue;
		sector_t &sec = Level->sectors[s];
		unsigned first = Out->LayoutPlanes.Size();
		auto add = [&](sector_t *model, int ceiling)
		{
			if (model == nullptr) return;
			uint32_t ref = LM_PlaneRef(model->Index(), ceiling);
			for (unsigned k = first; k < Out->LayoutPlanes.Size(); k++)
				if (Out->LayoutPlanes[k].ref == ref) return;
			Out->LayoutPlanes.Push({ ref, LM_PackPlane(model->GetSecPlane(ceiling)) });
			depend.Push({ uint32_t(model->Index()), s });
		};
		add(&sec, sector_t::floor);
		add(&sec, sector_t::ceiling);
		for (auto rover : sec.e->XFloor.ffloors)
		{
			add(rover->top.model, rover->top.isceiling);
			add(rover->bottom.model, rover->bottom.isceiling);
		}
	}
	Out->LayoutStart[nsec] = Out->LayoutPlanes.Size();
	// A model sector can be listed for the same dependent sector once per plane.
	TArray<std::pair<uint32_t, uint32_t>> unique;
	for (auto &d : depend)
	{
		bool dup = false;
		for (unsigned k = unique.Size(); k-- > 0 && unique[k].second == d.second;)
			if (unique[k].first == d.first) { dup = true; break; }
		if (!dup) unique.Push(d);
	}
	BuildCSR(nsec, unique, Out->LayoutDependStart, Out->LayoutDepend);

	// The sectors a light's values come from: its sector (colormap, fade of
	// F3DFloor::GetColormap), the light source's model, the light level's sector.
	pairs.Clear();
	const char *base = nsec ? (const char *)&Level->sectors[0].lightlevel : nullptr;
	for (unsigned i = 0; i < lights.Size(); i++)
	{
		auto &l = lights[i];
		uint32_t from[3] = { l.sector, l.lightsourceModel ? uint32_t(l.lightsourceModel->Index()) : l.sector, l.sector };
		ptrdiff_t off = (const char *)l.lightLevel - base;
		if (base && off >= 0 && off % ptrdiff_t(sizeof(sector_t)) == 0 && unsigned(off / ptrdiff_t(sizeof(sector_t))) < nsec)
			from[2] = uint32_t(off / ptrdiff_t(sizeof(sector_t)));
		for (int k = 0; k < 3; k++)
		{
			bool dup = false;
			for (int j = 0; j < k; j++) dup |= from[j] == from[k];
			if (!dup) pairs.Push({ from[k], i });
		}
	}
	BuildCSR(nsec, pairs, Out->LightDependStart, Out->LightDepend);
	Out->Stats.lights3D = lights.Size();
}

} // namespace

//==========================================================================
//
// LevelMeshBake
//
//==========================================================================

float LevelMeshBake::VertexZ(FLevelLocals *Level, const LevelMeshVertex &v) const
{
	float z = PlaneRefZ(Level, v.anchor & LMV_REFMASK, v.x, v.y) + v.zOffset;
	auto &surf = Surfaces[v.surface];
	uint32_t role = v.anchor >> LMV_ROLESHIFT;
	if (surf.edgeClamp != LM_NOPLANE)
	{
		float cz = PlaneRefZ(Level, surf.edgeClamp, v.x, v.y);
		if (surf.flags & LMSF_EDGECLAMPMAX) { if (role == LMV_BOTTOM) z = std::max(z, cz); }
		else if (role == LMV_TOP) z = std::min(z, cz);
	}
	uint32_t edge = surf.edgeTop;
	if (edge != LM_NOPLANE && role != LMV_TOP)
		z = std::min(z, PlaneRefZ(Level, edge, v.x, v.y));
	return z;
}

float LevelMeshBake::VertexV(FLevelLocals *Level, const LevelMeshVertex &v, float z) const
{
	auto &surf = Surfaces[v.surface];
	if (surf.vRef == LM_NOPLANE) return v.vOffset + surf.vScale * z;
	float texz = (float)Level->sectors[surf.vRef >> 1].GetPlaneTexZ(surf.vRef & 1);
	return v.vOffset + surf.vScale * (z - texz);
}

void LevelMeshBake::PrintStats(const char *mapname) const
{
	auto &s = Stats;
	Printf("LevelMeshBake %s: %u surfaces (%u walls, %u flats, %u 3D flats), %u vertices, %u triangles, %.1f ms (walls %.1f, flats %.1f)\n",
		mapname, Surfaces.Size(), s.wallSurfaces, s.flatSurfaces, s.ffloorFlatSurfaces, s.vertices, s.triangles, s.totalMs, s.wallMs, s.flatMs);
	Printf("LevelMeshBake %s: sides %u (poly skipped %u; poly origin: sides %u, partial sides %u, subsectors %u); side lists: translucent walls %u, portal walls %u, fog walls %u, upper missing %u, lower missing %u, translucent flats %u, portal/sky flats %u; untextured flats %u; heightsec surfaces %u\n",
		mapname, s.sidesProcessed, s.polySidesSkipped, s.polyOriginSides, s.polyOriginPartialSides, s.polyOriginSubsectors, s.translucentWalls, s.portalWalls, s.fogWalls, s.upperMissing, s.lowerMissing,
		s.translucentFlats, s.portalFlats, s.untexturedFlats, s.heightsecSurfaces);
	Printf("LevelMeshBake %s: deferred to the classic path %u surfaces (%u by tier); walls not drawn with their side texture %u\n", mapname,
		s.deferredSurfaces, s.deferredByTier, s.wallTextureNotSide);
	Printf("LevelMeshBake %s: 3D lights %u, lit surfaces %u, banded walls %u (band surfaces %u, left classic %u, unmatched plane %u); edge-clamped tiers %u\n", mapname,
		s.lights3D, s.light3DSurfaces, s.bandedWalls, s.bandSurfaces, s.bandsDeferred, s.bandPlaneUnmatched, s.edgeClamped);
	Printf("LevelMeshBake %s: sky: %u walls (%u sky-sky door walls), %u caps, %u sources; %u sides left classic; exposure quads %u\n", mapname,
		s.skyWalls, s.skyClosedWalls, s.skyCaps, SkySources.Size(), s.skyClassicSides, s.exposureQuads);
	Printf("LevelMeshBake %s: polyobjects %u, baked %u (%u wall surfaces); classic: by sector %u, by wall %u\n", mapname,
		s.polyobjs, s.polyobjsBaked, s.polySurfaces, s.polyClassicSectors, s.polyClassicWalls);
	Printf("LevelMeshBake %s: 3D-floor groups %u (deferred at bake %u), watches %u; opaque 3D planes never drawn %u\n", mapname, s.ffGroups, s.ffGroupsDeferred, s.ffWatches, s.ffFlatsNotPicked);
	Printf("LevelMeshBake %s: round trip z mismatches %u, v mismatches %u (within 1 ulp: z %u, v %u); offset-anchored bottom %u top %u side %u flat %u; vScale inconsistent %u; planeRef overflow %u; 3D vRef heuristic %u; unbounded wall tops %u\n",
		mapname, s.zMismatch, s.vMismatch, s.zInexact, s.vInexact, s.offsetAnchored[LMV_BOTTOM], s.offsetAnchored[LMV_TOP], s.offsetAnchored[LMV_SIDE], s.offsetAnchored[LMV_FLAT],
		s.vScaleInconsistent, s.planeRefOverflow, s.ffloorVRefHeuristic, s.edgeUnbounded);
}

bool LevelMeshBake::ExportObj(FLevelLocals *Level, const char *filename) const
{
	if (GeometryReleased) return false;
	FILE *f = fopen(filename, "w");
	if (!f) return false;
	fprintf(f, "# UZDoom levelmesh bake: %s\n# OBJ y-up: (x, z, -y) in map units\n", Level->MapName.GetChars());
	for (auto &v : Vertices)
	{
		float z = VertexZ(Level, v);
		fprintf(f, "v %g %g %g\n", v.x, z, -v.y);
	}
	uint32_t lastkind = ~0u;
	for (auto &s : Surfaces)
	{
		uint32_t kind = (s.flags & LMSF_KINDMASK) | (s.flags & LMSF_3DFLOOR);
		if (kind != lastkind)
		{
			static const char *names[] = { "walls", "floors", "ceilings" };
			fprintf(f, "g %s%s\n", names[s.flags & LMSF_KINDMASK], (s.flags & LMSF_3DFLOOR) ? "_3d" : "");
			lastkind = kind;
		}
		for (uint32_t i = 0; i < s.numIndices; i += 3)
		{
			const uint32_t *idx = &Indices[s.firstIndex + i];
			fprintf(f, "f %u %u %u\n", idx[0] + 1, idx[1] + 1, idx[2] + 1);
		}
	}
	fclose(f);
	return true;
}


//==========================================================================
//
// LocalizePolyobjects
//
// After clustering (which needs the vertices where they are at load): the
// baked polyobjects' vertices become polyobject-local, their clusters get a
// local xy box, and PolyLocal keeps the polyobject's own vertices at bake
// for the transform fit (LevelMesh_FillPolyobj).
//
//==========================================================================

static void LocalizePolyobjects(FLevelLocals *Level, LevelMeshBake *bake)
{
	auto &polys = Level->Polyobjects;
	unsigned npoly = polys.Size();
	bake->PolyVertexStart.Resize(npoly + 1);
	bake->PolyLocal.Clear();
	for (unsigned i = 0; i < npoly; i++)
	{
		bake->PolyVertexStart[i] = bake->PolyLocal.Size();
		if (!bake->PolyBaked[i]) continue;
		for (vertex_t *v : polys[i].Vertices) bake->PolyLocal.Push(v->fPos() - bake->PolyOrigin[i]);
	}
	bake->PolyVertexStart[npoly] = bake->PolyLocal.Size();

	for (auto &surf : bake->Surfaces)
	{
		if (!(surf.flags & LMSF_POLYOBJ)) continue;
		int poly = bake->SidePoly[surf.sideRef / 3];
		const DVector2 &o = bake->PolyOrigin[poly];
		for (uint32_t k = 0; k < surf.numVertices; k++)
		{
			auto &v = bake->Vertices[surf.firstVertex + k];
			v.x = float(double(v.x) - o.X);
			v.y = float(double(v.y) - o.Y);
		}
	}

	unsigned first = bake->PolyClusterStart.Size() ? bake->PolyClusterStart[0] : bake->Clusters.Size();
	bake->PolyClusterBox.Resize((bake->Clusters.Size() - first) * 4);
	for (unsigned c = first; c < bake->Clusters.Size(); c++)
	{
		auto &cl = bake->Clusters[c];
		float *box = &bake->PolyClusterBox[(c - first) * 4];
		box[0] = box[1] = FLT_MAX;
		box[2] = box[3] = -FLT_MAX;
		for (uint32_t k = cl.firstIndex; k < cl.firstIndex + cl.numIndices; k++)
		{
			auto &v = bake->Vertices[bake->Indices[k]];
			box[0] = std::min(box[0], v.x);
			box[1] = std::min(box[1], v.y);
			box[2] = std::max(box[2], v.x);
			box[3] = std::max(box[3], v.y);
		}
	}
}

//==========================================================================
//
// Entry point
//
//==========================================================================

LevelMeshBake *LevelMeshBake_Create(FLevelLocals *Level, bool rebuild)
{
	// gl_uselevelmesh is latched at level setup; a rebuild (device reset) keeps the latched choice.
	if (!rebuild && !gl_uselevelmesh) return nullptr;
	if (screen == nullptr || !screen->SupportsLevelMesh(true)) return nullptr;

	double t0 = I_msTimeF();
	static uint64_t nextId = 0;
	auto bake = new LevelMeshBake;
	bake->Id = ++nextId;
	Baker b;
	b.Level = Level;
	b.Out = bake;

	b.ProcessSides();
	b.ProcessPolyobjects();
	if (lm_bake_debug)
	{
		for (int i = 0; i < 16; i++)
			if (b.offsetByType[i]) Printf("LMBAKE offset-anchored wall vertices of type %d: %u\n", i, b.offsetByType[i]);
	}
	double t1 = I_msTimeF();
	b.ProcessFlats();
	b.ComputeOwnership();
	b.BakeExposureQuads();
	b.BuildFFTables();
	b.BuildLight3DTables();
	// Ticket 18: the out-of-body view rules' inputs.
	for (auto &sec : Level->sectors)
		if (sec.isSecret() && sec.wasSecret()) bake->SecretSectors.Push(sec.Index());
	bake->RadarStamp.Resize(Level->subsectors.Size());
	for (auto &s : bake->RadarStamp) s = 0;
	bake->LineExposed.Resize(Level->lines.Size());
	for (auto &e : bake->LineExposed) e = 0;
	double t2 = I_msTimeF();

	auto &st = bake->Stats;
	st.vertices = bake->Vertices.Size();
	st.translucentWalls = bake->TranslucentWalls.Size();
	st.portalWalls = bake->PortalWalls.Size();
	st.fogWalls = bake->FogWalls.Size();
	st.upperMissing = bake->UpperMissing.Size();
	st.lowerMissing = bake->LowerMissing.Size();
	st.translucentFlats = bake->TranslucentFlats.Size();
	st.portalFlats = bake->PortalFlats.Size();
	st.wallMs = t1 - t0;
	st.flatMs = t2 - t1;
	st.totalMs = t2 - t0;
	bake->Vertices.ShrinkToFit();
	bake->Indices.ShrinkToFit();
	bake->Surfaces.ShrinkToFit();
	bake->PrintStats(Level->MapName.GetChars());

	LevelMeshClusters_Build(Level, bake, lm_cluster_budget);
	bake->PrintClusterStats(Level->MapName.GetChars());
	LocalizePolyobjects(Level, bake);

	if ((*lm_bake_planeusers)[0] != 0)
	{
		void PrintPlaneUsersList(FLevelLocals *Level, LevelMeshBake *bake, const char *list);
		PrintPlaneUsersList(Level, bake, lm_bake_planeusers);
	}
	if ((*lm_bake_objdir)[0] != 0)
	{
		FString name = FStringf("%s/%s.obj", *lm_bake_objdir, Level->MapName.GetChars());
		bool ok = bake->ExportObj(Level, name.GetChars());
		Printf("LevelMeshBake %s: %s %s\n", Level->MapName.GetChars(), ok ? "wrote" : "could not write", name.GetChars());
	}
	return bake;
}

//==========================================================================
//
// Console
//
//==========================================================================

CCMD(lm_bakestats)
{
	if (!primaryLevel || !primaryLevel->levelMeshBake)
	{
		Printf("No levelmesh bake for this level (gl_uselevelmesh off at level setup, or the device cannot run it).\n");
		return;
	}
	primaryLevel->levelMeshBake->PrintStats(primaryLevel->MapName.GetChars());
}

// Debug: the sky wall surfaces the bake made for a side, with their z now.
CCMD(lm_skyside)
{
	auto bake = primaryLevel ? primaryLevel->levelMeshBake : nullptr;
	if (bake == nullptr || argv.argc() < 2) return;
	int side = atoi(argv[1]);
	Printf("side %d: sky owned %d\n", side, side >= 0 && unsigned(side) < bake->SideSkyOwned.Size() ? bake->SideSkyOwned[side] : -1);
	if (side >= 0 && unsigned(side) < primaryLevel->sides.Size())
	{
		line_t *line = primaryLevel->sides[side].linedef;
		Printf(" tiers owned %d %d %d; line %d portaltransferred %u (portal %s)\n", bake->SideTierOwned[side * 3], bake->SideTierOwned[side * 3 + 1],
			bake->SideTierOwned[side * 3 + 2], line->Index(), line->portaltransferred, line->GetTransferredPortal() ? "yes" : "no");
	}
	for (unsigned i = 0; i < bake->SkySurfaceSide.Size(); i++)
	{
		if (bake->SkySurfaceSide[i] != side) continue;
		auto &surf = bake->Surfaces[bake->SkySurfaceIndex[i]];
		auto &src = bake->SkySources[surf.sideRef];
		Printf(" surface %u flags %x source %u (sector %u plane %u color %u) vRef %x", bake->SkySurfaceIndex[i], surf.flags, surf.sideRef, src.sector, src.plane, src.colorSector, surf.vRef);
		if (!bake->GeometryReleased)
			for (uint32_t k = 0; k < surf.numVertices; k++)
			{
				auto &v = bake->Vertices[surf.firstVertex + k];
				Printf(" (%g %g %g)", v.x, v.y, bake->VertexZ(primaryLevel, v));
			}
		Printf("\n");
	}
}

CCMD(lm_dumpobj)
{
	if (!primaryLevel || !primaryLevel->levelMeshBake)
	{
		Printf("No levelmesh bake for this level (gl_uselevelmesh off at level setup, or the device cannot run it).\n");
		return;
	}
	FString name = argv.argc() > 1 ? FString(argv[1]) : FStringf("levelmesh_%s.obj", primaryLevel->MapName.GetChars());
	if (primaryLevel->levelMeshBake->GeometryReleased)
		Printf("The CPU geometry was freed after the upload; set lm_keepgeometry 1 before loading the map.\n");
	else if (primaryLevel->levelMeshBake->ExportObj(primaryLevel, name.GetChars()))
		Printf("Wrote %s\n", name.GetChars());
	else
		Printf("Could not write %s\n", name.GetChars());
}

// Debug: the drawn (not deferred) surfaces that follow the planes of a
// sector, or of the 3D floors' model sectors of a sector with 3D floors
// (vertex anchors, vRef, edgeTop). lm_planeusers <sector> [3d] in the
// console; lm_bake_planeusers "<sector>,..." (3D models) after each bake.
static void PrintPlaneUsers(FLevelLocals *Level, LevelMeshBake *bake, unsigned s, bool models)
{
	if (s >= Level->sectors.Size()) return;
	TArray<unsigned> secs;
	if (models)
	{
		for (auto rover : Level->sectors[s].e->XFloor.ffloors)
			if (rover->model && secs.Find(rover->model->Index()) == secs.Size()) secs.Push(rover->model->Index());
	}
	else secs.Push(s);
	for (unsigned ms : secs)
	{
		Printf("LMPLANEUSERS sector %u%s %u:\n", s, models ? " model" : "", ms);
		for (unsigned i = 0; i < bake->Surfaces.Size(); i++)
		{
			auto &surf = bake->Surfaces[i];
			if (surf.flags & LMSF_DEFERRED) continue;
			bool hit = (surf.vRef != LM_NOPLANE && (surf.vRef >> 1) == ms) || (surf.edgeTop != LM_NOPLANE && (surf.edgeTop >> 1) == ms) ||
				(surf.edgeClamp != LM_NOPLANE && (surf.edgeClamp >> 1) == ms);
			for (auto r : surf.planeRefs) hit |= r != LM_NOPLANE && (r >> 1) == ms;
			if (!hit) continue;
			// 3D-floor surfaces: tier -1, side = their group (sideRef past the side tiers)
			bool tierRef = surf.sideRef != 0xffffffffu && !(surf.flags & LMSF_3DFLOOR);
			int sideOrGroup = tierRef ? int(surf.sideRef / 3) : (surf.flags & LMSF_3DFLOOR) ? int(surf.sideRef - Level->sides.Size() * 3) : -1;
			Printf("  surface %u kind %u type %u%s %d tier %d lightSector %u planes %d %d %d %d vRef %d edgeTop %d edgeClamp %d\n", i, surf.flags & LMSF_KINDMASK,
				(surf.flags >> LMSF_WALLTYPESHIFT) & 15, (surf.flags & LMSF_3DFLOOR) ? " 3D group" : " side", sideOrGroup, tierRef ? int(surf.sideRef % 3) : -1,
				surf.lightSector, int(surf.planeRefs[0]), int(surf.planeRefs[1]), int(surf.planeRefs[2]), int(surf.planeRefs[3]), int(surf.vRef), int(surf.edgeTop), int(surf.edgeClamp));
		}
	}
}

CCMD(lm_planeusers)
{
	auto bake = primaryLevel ? primaryLevel->levelMeshBake : nullptr;
	if (bake == nullptr || argv.argc() < 2) { Printf("Usage: lm_planeusers <sector> [3d] (needs a levelmesh)\n"); return; }
	PrintPlaneUsers(primaryLevel, bake, (unsigned)atoi(argv[1]), argv.argc() > 2);
}

void PrintPlaneUsersList(FLevelLocals *Level, LevelMeshBake *bake, const char *list)
{
	FString l = list;
	for (auto &tok : l.Split(",")) PrintPlaneUsers(Level, bake, (unsigned)atoi(tok.GetChars()), true);
}
