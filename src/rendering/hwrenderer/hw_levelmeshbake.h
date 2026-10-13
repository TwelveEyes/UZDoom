/*
** hw_levelmeshbake.h
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
** The bake runs once at level setup when the levelmesh path is selected
** (gl_uselevelmesh on a capable backend). Walls come from HWWall::Process in
** mesh-helper mode, flats from the classic subsector triangulation
** (ADR 0003). Every vertex is plane-anchored: its z is the GPU-identical
** plane evaluation LM_PlaneZ() of its anchor plane plus a baked offset, and
** its wall v coordinate follows the surface's texture-top plane. Nothing is
** re-baked when planes move.
**
** View-dependent emitter output (translucent walls, portals, fog boundaries,
** missing-texture fills, sky / portal / translucent flats) is kept in side
** lists, not in the mesh.
*/

#pragma once

// HEADER FILES ------------------------------------------------------------

#include "tarray.h"
#include "r_defs.h"
#include "hwrenderer/scene/hw_drawinfo.h"
#include "hwrenderer/scene/hw_drawstructs.h"
#include "hwrenderer/scene/hw_walldispatcher.h"

// TYPES -------------------------------------------------------------------

struct FLevelLocals;

// Plane reference: sector index << 1 | (ceiling ? 1 : 0).
constexpr uint32_t LM_NOPLANE = 0xffffffffu;

inline uint32_t LM_PlaneRef(int sectornum, int ceiling)
{
	return (uint32_t(sectornum) << 1) | (ceiling ? 1u : 0u);
}

// A plane as the GPU sees it. LM_PlaneZ is the canonical evaluation; the
// levelmesh vertex shader must use the same operation order.
struct LevelMeshPlane
{
	float nx, ny, d, negiC;
};

inline LevelMeshPlane LM_PackPlane(const secplane_t &plane)
{
	return { (float)plane.Normal().X, (float)plane.Normal().Y, (float)plane.fD(), (float)plane.negiC };
}

inline float LM_PlaneZ(const LevelMeshPlane &p, float x, float y)
{
	return (p.d + p.nx * x + p.ny * y) * p.negiC;
}

// Vertex anchor = plane ref | role << LMV_ROLESHIFT.
enum ELevelMeshVertexRole : uint32_t
{
	LMV_BOTTOM = 0,		// wall lower edge
	LMV_TOP = 1,		// wall upper edge
	LMV_SIDE = 2,		// wall gl_seamless side split; must stay within the edge's span
	LMV_FLAT = 3,		// flat vertex
	LMV_ROLESHIFT = 30,
	LMV_REFMASK = (1u << LMV_ROLESHIFT) - 1,
};

// z = LM_PlaneZ(anchor plane, x, y) + zOffset
// wall v = vOffset + surface.vScale * (z - TexZ(surface.vRef)); flats: v = vOffset
struct LevelMeshVertex
{
	float x, y;
	float zOffset;
	float u, vOffset;
	float lu, lv;		// lightmap uv (page: surface.lightmapPage)
	uint32_t anchor;
	uint32_t surface;
};

enum ELevelMeshSurfaceFlags : uint32_t
{
	LMSF_KINDMASK = 3,		// LMSK_*
	LMSF_3DFLOOR = 1 << 2,		// 3D-floor side or plane
	LMSF_MASKED = 1 << 3,		// masked material bucket (alpha test)
	LMSF_CLAMPX = 1 << 4,
	LMSF_CLAMPY = 1 << 5,
	LMSF_TWOSIDED = 1 << 6,		// no back-face culling (3D-floor flats); otherwise the front is the classic winding
	LMSF_GLOW = 1 << 7,		// HWF_GLOW at bake
	LMSF_HEIGHTSEC = 1 << 8,	// front or back sector has a Boom heightsec (view-dependent in classic, baked unfaked)
	// 1 << 9 reserved: dither eligibility (ticket 18; WALLF_DITHERTRANS_* is per-frame state)
	LMSF_SKEW = 1 << 10,		// sloped / skewed texture alignment
	LMSF_LIGHTLIST = 1 << 11,	// banded: drawn per light layer of lightSector's 3D lightlist (classic split planes)
	LMSF_SKY = 1 << 12,			// unused for mesh surfaces (sky goes to side lists); reserved
	LMSF_WALLTYPESHIFT = 24,	// RENDERWALL_* in bits 24..27
};

enum ELevelMeshSurfaceKind : uint32_t
{
	LMSK_WALL,
	LMSK_FLOOR,
	LMSK_CEILING,
};

// std430-compatible: 24 x 32 bit.
struct LevelMeshSurface
{
	uint32_t flags;			// LMSF_* | kind
	uint32_t planeRefs[4];	// distinct vertex anchor planes (LM_NOPLANE = unused); overflow counted in stats
	uint32_t vRef;			// texture-top plane ref (walls), LM_NOPLANE for flats
	float vScale;			// dv/dz (walls)
	int32_t textureId;		// untranslated FTextureID
	uint32_t sideRef;		// side * 3 + tier (side_t::top/mid/bottom), 0xffffffff for flats
	uint32_t lightSector;	// sector providing the base light
	int32_t lightList;		// 3D light layer index, -1 = none / not recorded (ticket 12)
	int32_t lightLevel;		// classic lightlevel at bake (RescaleLightLevel applied)
	int32_t relLight;		// classic rellight at bake
	int32_t sideLight;		// side Light (low 16) | TierLights[tier] (high 16)
	int32_t fakeContrast;	// smooth rel (low 16) | classic rel (high 16)
	uint32_t sideFlags;		// side_t::Flags (light / dither bits)
	int32_t lightmapPage;	// -1 = none
	float normal[3];		// walls: horizontal, pointing into the side's sector; flats: plane normal (floor up, ceiling down)
	uint32_t firstVertex, numVertices;
	uint32_t firstIndex, numIndices;
};
static_assert(sizeof(LevelMeshSurface) == 24 * 4, "LevelMeshSurface must stay std430-compatible");

// A flat that is not part of the opaque mesh (sky / portal / translucent).
struct LevelMeshFlatRef
{
	subsector_t *sub;
	sector_t *sector;		// owner of the plane (model sector for 3D floors)
	F3DFloor *ffloor;		// nullptr for sector planes
	int plane;				// sector_t::floor / ceiling
};

struct LevelMeshBakeStats
{
	unsigned wallSurfaces = 0, flatSurfaces = 0, ffloorFlatSurfaces = 0;
	unsigned vertices = 0, triangles = 0;
	unsigned sidesProcessed = 0, polySidesSkipped = 0, heightsecSurfaces = 0;
	unsigned translucentWalls = 0, portalWalls = 0, fogWalls = 0, upperMissing = 0, lowerMissing = 0;
	unsigned translucentFlats = 0, portalFlats = 0, untexturedFlats = 0;
	unsigned zMismatch = 0, vMismatch = 0;		// round-trip failures beyond 1 ulp (must be 0)
	unsigned zInexact = 0, vInexact = 0;		// reproduced within 1 ulp of the operands, not bit-exact
	unsigned offsetAnchored[4] = {};			// per role: no plane matched exactly, anchored with an offset
	unsigned vScaleInconsistent = 0;			// left/right dv/dz disagree
	unsigned planeRefOverflow = 0;				// surfaces with more than 4 distinct anchor planes
	unsigned ffloorVRefHeuristic = 0;			// 3D-floor sides whose vRef was taken from the top-left anchor
	double wallMs = 0, flatMs = 0, totalMs = 0;
};

class LevelMeshBake
{
public:
	TArray<LevelMeshVertex> Vertices;
	TArray<uint32_t> Indices;		// triangle list, surface-ordered, absolute vertex indices
	TArray<LevelMeshSurface> Surfaces;

	// Side lists (tickets 14-16). The HWWall copies keep the classic
	// emitter's view-independent output; pointers into level data stay valid
	// for the level's lifetime (lightlist pointers do not: they can be
	// reallocated by P_Recalculate3DFloors).
	TArray<HWWall> TranslucentWalls, PortalWalls, FogWalls;
	TArray<HWMissing> UpperMissing, LowerMissing;
	TArray<LevelMeshFlatRef> TranslucentFlats, PortalFlats;

	LevelMeshBakeStats Stats;

	// Evaluates a vertex's z / v from the current planes (CPU reference of the shader math).
	float VertexZ(FLevelLocals *Level, const LevelMeshVertex &v) const;
	float VertexV(FLevelLocals *Level, const LevelMeshVertex &v, float z) const;

	void PrintStats(const char *mapname) const;
	bool ExportObj(FLevelLocals *Level, const char *filename) const;
};

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// Returns nullptr when the levelmesh path is not selected for this level.
LevelMeshBake *LevelMeshBake_Create(FLevelLocals *Level);
