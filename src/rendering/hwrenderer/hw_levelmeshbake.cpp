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
** Walls: every non-polyobject side runs once through HWWall::Process in
** mesh-helper mode (HWWallDispatcher without a draw info). The captured
** HWWalls produce their vertices through HWWall::CreateVertices, including
** the gl_seamless splits, and are fan-triangulated like the classic
** DT_TriangleFan draw.
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
#include <cmath>

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

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

EXTERN_CVAR(Bool, gl_uselevelmesh)
EXTERN_CVAR(Bool, gl_seamless)

// PUBLIC DATA DEFINITIONS -------------------------------------------------

// Debug: when set, every bake writes <dir>/<map>.obj (tools/abtest/lm_bake_survey.py).
CVAR(String, lm_bake_objdir, "", 0)
// Debug: print the first N round-trip failures of each bake.
CVAR(Int, lm_bake_debug, 0, 0)
// Cluster triangle budget (ticket 05: 256). Takes effect at the next level setup.
CVAR(Int, lm_cluster_budget, 256, 0)
// Debug: material programs the levelmesh draws (bit per ELevelMeshProgram,
// read at level setup); the others stay on the classic path.
CVAR(Int, lm_programs, 7, CVAR_NOSAVE)

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

	uint32_t BeginSurface(LevelMeshSurface &surf)
	{
		for (auto &r : surf.planeRefs) r = LM_NOPLANE;
		surf.vRef = LM_NOPLANE;
		surf.edgeTop = LM_NOPLANE;
		surf.firstVertex = Out->Vertices.Size();
		surf.firstIndex = Out->Indices.Size();
		return Out->Surfaces.Size();
	}

	void EndSurface(LevelMeshSurface &surf, subsector_t *leaf)
	{
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

	void ProcessSides();
	void BakeWall(HWWall &w);
	uint32_t WallVRef(HWWall &w, sector_t *rf, sector_t *rb);
	uint32_t WallEdgeTop(HWWall &w, sector_t *rf, sector_t *rb, uint32_t firstv, int n);
	void ProcessFlats();
	void BakeFlat(subsector_t *sub, sector_t *sec, int h, sector_t *model, int mplane, int lmindex, bool ffloor, bool portal);
	void ComputeOwnership();
};

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

	for (auto &side : Level->sides)
	{
		if (side.Flags & WALLF_POLYOBJ)
		{
			Out->Stats.polySidesSkipped++;
			continue;
		}
		if (side.numsegs == 0 || side.segs == nullptr) continue;
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
		for (auto &w : mh.portals) Out->PortalWalls.Push(w);
		for (auto &m : mh.upper) Out->UpperMissing.Push(m);
		for (auto &m : mh.lower) Out->LowerMissing.Push(m);
		mh.list.Clear();
		mh.translucent.Clear();
		mh.portals.Clear();
		mh.upper.Clear();
		mh.lower.Clear();
	}
}

} // namespace

// The levelmesh program of a texture's material (FMaterial's shader index:
// default, warp 1, warp 2), or -1 for the material shaders the levelmesh
// does not build (specular / PBR, paletted, user shaders, canvases).
// Animation frames are assumed to share their base's shader.
int LevelMesh_MaterialProgram(FGameTexture *tex)
{
	if (tex == nullptr) return LMP_DEFAULT;
	if (tex->isHardwareCanvas() || tex->GetUseType() == ETextureType::SWCanvas) return -1;
	if (tex->GetShaderIndex() != SHADER_Default) return -1;
	int program = LMP_DEFAULT;
	if (tex->isWarped()) program = tex->isWarped() == 1 ? LMP_WARP1 : LMP_WARP2;
	else if (tex->GetNormalmap() != nullptr) return -1;	// specular / PBR
	return (lm_programs & (1 << program)) ? program : -1;
}

bool LevelMesh_NeedsClassicMaterial(FGameTexture *tex)
{
	return LevelMesh_MaterialProgram(tex) < 0;
}

namespace
{

static bool SameColormap(const FColormap &a, const FColormap &b)
{
	return a.LightColor == b.LightColor && a.FadeColor == b.FadeColor && a.Desaturation == b.Desaturation &&
		a.BlendFactor == b.BlendFactor && a.FogDensity == b.FogDensity;
}

// True if a wall the classic emitted carries its sector's own light (the
// shading resolve recomputes that), false if HWWall::Put3DWall gave it the
// light level or colormap of a 3D light.
static bool SectorLit(HWWall &w, sector_t *rf, side_t *side, int tier)
{
	if (!SameColormap(w.Colormap, w.frontsector->Colormap)) return false;
	bool foggy = !w.Colormap.FadeColor.isBlack() || (rf->Level->flags & LEVEL_HASFADETABLE);
	int orglightlevel = hw_ClampLight(w.frontsector->lightlevel);
	int rel;
	return w.lightlevel == RescaleLightLevel(side->GetLightLevel(foggy, orglightlevel, tier, false, &rel));
}

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
	// The untranslated id, so the translation table follows the animation:
	// w.texture is the frame current at bake.
	FTextureID sideTex = side->GetTexture(tier);
	if (!is3d && TexMan.GetGameTexture(sideTex, true) == w.texture) surf.textureId = sideTex.GetIndex();
	else
	{
		surf.textureId = w.texture->GetID().GetIndex();
		if (!is3d) st.wallTextureNotSide++;
	}
	surf.sideRef = uint32_t(side->Index()) * 3 + tier;
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
		}
	}
	surf.lightSector = rf->Index();
	surf.lightList = -1;
	if (w.lightlist)
	{
		// Banded wall: the classic draws it once per light layer of the sector with split planes (ticket 12).
		surf.flags |= LMSF_LIGHTLIST;
		surf.lightSector = (w.sub ? w.sub->sector : w.seg->frontsector)->Index();
	}
	else if (w.frontsector && w.frontsector->e->XFloor.lightlist.Size() > 0 && !SectorLit(w, rf, side, tier))
	{
		// Within one light layer: HWWall::Put3DWall took the light level or
		// colormap of a 3D light, not the sector's (ticket 12).
		surf.flags |= LMSF_LIGHTLIST;
	}
	surf.lightLevel = w.lightlevel;
	surf.relLight = w.rellight;
	surf.sideLight = (uint16_t)side->Light | (int32_t(side->TierLights[tier]) << 16);
	surf.fakeContrast = (uint16_t)side->GetFakeContrastRel(true) | (int32_t(side->GetFakeContrastRel(false)) << 16);
	surf.sideFlags = SideLightFlags(side, tier);
	if (w.type == RENDERWALL_M2SNF) surf.flags |= LMSF_NOSECTORFOG;
	surf.lightmapPage = w.lindex >= 0 ? int(w.lindex) : -1;
	// Not drawn by the levelmesh yet: 3D floors and light bands (ticket 12),
	// heightsecs (view-dependent fake, ticket 16), sky-hack mid textures (the
	// classic draws them with a depth bias, GLDL_MASKEDWALLSOFS).
	if (surf.flags & (LMSF_3DFLOOR | LMSF_HEIGHTSEC | LMSF_LIGHTLIST)) surf.flags |= LMSF_DEFERRED;
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
	EndSurface(surf, w.seg->Subsector);
	st.wallSurfaces++;
}

//==========================================================================
//
// Flats
//
//==========================================================================

void Baker::BakeFlat(subsector_t *sub, sector_t *sec, int h, sector_t *model, int mplane, int lmindex, bool ffloor, bool portal)
{
	auto &st = Out->Stats;
	const secplane_t &splane = model->GetSecPlane(mplane);
	uint32_t ref = LM_PlaneRef(model->Index(), mplane);
	LevelMeshPlane plane = LM_PackPlane(splane);
	float diff = (!ffloor && sec->transdoor && h == sector_t::floor) ? -1.f : 0.f;

	LightmapSurface *lm = nullptr;
	if (sec->HasLightmaps && sub->lightmap[h] && lmindex >= 0)
	{
		lm = &sub->lightmap[h][lmindex];
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
	// gets the opposite winding of a floor.
	bool flip = !ffloor && h == sector_t::ceiling;
	for (unsigned i = 0; i + 2 < gen.indices.Size(); i += 3)
	{
		Out->Indices.Push(firstv + gen.indices[i]);
		Out->Indices.Push(firstv + gen.indices[flip ? i + 2 : i + 1]);
		Out->Indices.Push(firstv + gen.indices[flip ? i + 1 : i + 2]);
	}

	surf.planeRefs[0] = ref;
	surf.flags = (h == sector_t::ceiling ? LMSK_CEILING : LMSK_FLOOR);
	if (ffloor) surf.flags |= LMSF_3DFLOOR | LMSF_TWOSIDED;
	if (sec->GetHeightSec())
	{
		surf.flags |= LMSF_HEIGHTSEC;
		st.heightsecSurfaces++;
	}
	// HWFlat::ProcessSector: in a sector with 3D floors the plane takes the
	// light level and colormap of its 3D light (P_GetPlaneLight; ticket 12).
	if (!ffloor && sec->e->XFloor.ffloors.Size() > 0)
	{
		lightlist_t *light = P_GetPlaneLight(sec, &sec->GetSecPlane(h), h == sector_t::ceiling);
		FColormap cm = sec->Colormap;
		CopyFrom3DLight(cm, light);
		// The classic floor also takes the 3D light's level for an absolute
		// plane light when the light has no source; the ceiling never does.
		bool absolute = (sec->GetFlags(h) & PLANEF_ABSLIGHTING) && (h == sector_t::ceiling || light->lightsource != nullptr);
		bool ownLight = absolute || light->p_lightlevel == &sec->lightlevel;
		if (!ownLight || !SameColormap(cm, sec->Colormap)) surf.flags |= LMSF_LIGHTLIST;
	}
	if (surf.flags & (LMSF_3DFLOOR | LMSF_HEIGHTSEC | LMSF_LIGHTLIST)) surf.flags |= LMSF_DEFERRED;
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
	surf.lightSector = sec->Index();
	surf.lightList = -1;
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

void Baker::ProcessFlats()
{
	for (auto &sec : Level->sectors)
	{
		for (auto &section : Level->sections.SectionsForSector(&sec))
		{
			for (auto sub : section.subsectors)
			{
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
					BakeFlat(sub, &sec, h, &sec, h, 0, false, portal);
				}

				// 3D-floor planes, on the target sector's triangulation.
				auto &ffloors = sec.e->XFloor.ffloors;
				for (unsigned k = 0; k < ffloors.Size(); k++)
				{
					F3DFloor *rover = ffloors[k];
					if ((rover->flags & (FF_EXISTS | FF_RENDERPLANES | FF_THISINSIDE)) != (FF_EXISTS | FF_RENDERPLANES)) continue;
					bool translucent = (rover->flags & (FF_TRANSLUCENT | FF_ADDITIVETRANS | FF_FOG)) || rover->alpha < 255;
					for (int t = 0; t < 2; t++)
					{
						auto &pr = t == 0 ? rover->top : rover->bottom;
						if (pr.copied) continue;
						// the top is seen as a floor, the bottom as a ceiling
						int h = t == 0 ? sector_t::floor : sector_t::ceiling;
						if (translucent)
						{
							Out->TranslucentFlats.Push({ sub, pr.model, rover, pr.isceiling });
							continue;
						}
						auto tex = TexMan.GetGameTexture(*pr.texture, true);
						if (!tex || !tex->isValid())
						{
							Out->Stats.untexturedFlats++;
							continue;
						}
						BakeFlat(sub, &sec, h, pr.model, pr.isceiling, int(k) + 1, true, false);
					}
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
// owned when its (non-3D-floor) flats are in the mesh and drawn.
//
//==========================================================================

void Baker::ComputeOwnership()
{
	enum { HAS_MESH = 1, HAS_DEFERRED = 2 };
	TArray<uint8_t> tiers(Level->sides.Size() * 3, true);
	TArray<uint8_t> planes(Level->sectors.Size() * 2, true);
	memset(tiers.Data(), 0, tiers.Size());
	memset(planes.Data(), 0, planes.Size());

	for (auto &surf : Out->Surfaces)
	{
		uint8_t bit = (surf.flags & LMSF_DEFERRED) ? HAS_DEFERRED : HAS_MESH;
		if ((surf.flags & LMSF_KINDMASK) == LMSK_WALL) tiers[surf.sideRef] |= bit;
		else if (!(surf.flags & LMSF_3DFLOOR)) planes[surf.planeRefs[0]] |= bit;	// plane ref = sector * 2 + plane
	}
	for (auto &surf : Out->Surfaces)
	{
		if ((surf.flags & LMSF_KINDMASK) == LMSK_WALL && (tiers[surf.sideRef] & HAS_DEFERRED) && !(surf.flags & LMSF_DEFERRED))
		{
			surf.flags |= LMSF_DEFERRED;
			Out->Stats.deferredByTier++;
		}
		if (surf.flags & LMSF_DEFERRED) Out->Stats.deferredSurfaces++;
	}
	Out->SideTierOwned.Resize(tiers.Size());
	for (unsigned i = 0; i < tiers.Size(); i++) Out->SideTierOwned[i] = tiers[i] == HAS_MESH;
	Out->SectorPlaneOwned.Resize(planes.Size());
	for (unsigned i = 0; i < planes.Size(); i++) Out->SectorPlaneOwned[i] = planes[i] == HAS_MESH;
	Out->SideTierBaked = Out->SideTierOwned;
	Out->SectorPlaneBaked = Out->SectorPlaneOwned;
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
	uint32_t edge = Surfaces[v.surface].edgeTop;
	if (edge != LM_NOPLANE && (v.anchor >> LMV_ROLESHIFT) != LMV_TOP)
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
	Printf("LevelMeshBake %s: sides %u (poly skipped %u); side lists: translucent walls %u, portal walls %u, fog walls %u, upper missing %u, lower missing %u, translucent flats %u, portal/sky flats %u; untextured flats %u; heightsec surfaces %u\n",
		mapname, s.sidesProcessed, s.polySidesSkipped, s.translucentWalls, s.portalWalls, s.fogWalls, s.upperMissing, s.lowerMissing,
		s.translucentFlats, s.portalFlats, s.untexturedFlats, s.heightsecSurfaces);
	Printf("LevelMeshBake %s: deferred to the classic path %u surfaces (%u by tier); walls not drawn with their side texture %u\n", mapname,
		s.deferredSurfaces, s.deferredByTier, s.wallTextureNotSide);
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
	if (lm_bake_debug)
	{
		for (int i = 0; i < 16; i++)
			if (b.offsetByType[i]) Printf("LMBAKE offset-anchored wall vertices of type %d: %u\n", i, b.offsetByType[i]);
	}
	double t1 = I_msTimeF();
	b.ProcessFlats();
	b.ComputeOwnership();
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
