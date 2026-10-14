/*
** hw_levelmeshdata.h
**
** Levelmesh GPU records shared by the bake (game side) and the backend
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Every record here is laid out as std430 32-bit scalars so it can be
** uploaded verbatim into a storage buffer. The levelmesh shaders must declare
** the same layouts. The backend never interprets level data: it receives the
** arrays through LevelMeshUploadData and resolves texture ids into bindless
** slots.
*/

#pragma once

// HEADER FILES ------------------------------------------------------------

#include <stdint.h>

// TYPES -------------------------------------------------------------------

// Plane reference: sector index << 1 | (ceiling ? 1 : 0).
constexpr uint32_t LM_NOPLANE = 0xffffffffu;

inline uint32_t LM_PlaneRef(int sectornum, int ceiling)
{
	return (uint32_t(sectornum) << 1) | (ceiling ? 1u : 0u);
}

// A plane as the GPU sees it. LM_PlaneZ is the canonical evaluation; the
// levelmesh vertex shader must use the same operation order (no FMA).
struct LevelMeshPlane
{
	float nx, ny, d, negiC;
};

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
static_assert(sizeof(LevelMeshVertex) == 9 * 4, "LevelMeshVertex must stay tightly packed");

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
	LMSF_DEFERRED = 1 << 13,	// not drawn by the levelmesh yet: the classic path owns it (LMB_DEFERRED)
	LMSF_TEXCLAMPY = 1 << 14,	// TEXF_ClampY texture mode (classic SetTextureClamp: clamped two-sided mid texture)
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
	uint32_t firstIndex, numIndices;	// into the cluster-ordered index buffer
};
static_assert(sizeof(LevelMeshSurface) == 24 * 4, "LevelMeshSurface must stay std430-compatible");

// Material buckets. Inside a cluster the index range is ordered by bucket.
// Opaque and masked follow the classic draw lists (HWDrawInfo::AddWall /
// AddFlat): only two-sided mid textures (and 3D-floor / portal planes) are
// alpha tested. LMB_DEFERRED holds the surfaces the levelmesh does not draw
// yet (3D floors, heightsecs, light bands, sky-hack mid textures); the classic
// path keeps drawing them (LevelMeshBake::SideTierOwned / SectorPlaneOwned).
// Ticket 11 makes bucket membership runtime state.
enum ELevelMeshBucket : uint32_t
{
	LMB_OPAQUE,
	LMB_MASKED,
	LMB_DEFERRED,
	LMB_COUNT,
	LMB_DRAWN = LMB_DEFERRED	// buckets the levelmesh draws
};

// A spatial group of triangles: a BSP subtree cut at the triangle budget.
// std430-compatible: 16 x 32 bit.
struct LevelMeshCluster
{
	float bmin[3];			// xy AABB; z from the load heights (ticket 09 refreshes it)
	uint32_t firstIndex;
	float bmax[3];
	uint32_t numIndices;
	uint32_t bucketIndices[LMB_COUNT];	// per bucket, consecutive from firstIndex in ELevelMeshBucket order
	uint32_t bucketMask;	// 1 << bucket for every non-empty bucket
	uint32_t firstSector, numSectors;	// into the cluster -> sector list (vertex anchor sectors)
	uint32_t firstSurface, numSurfaces;	// into the cluster -> surface list
};
static_assert(sizeof(LevelMeshCluster) == 16 * 4, "LevelMeshCluster must stay std430-compatible");

// Per-sector state. std430-compatible: 16 x 32 bit. Tickets 09 / 10 own the
// dirty stream and the light ingredients.
struct LevelMeshSectorState
{
	LevelMeshPlane planes[2];	// floor, ceiling (LM_PackPlane)
	float texZ[2];				// GetPlaneTexZ: wall texture anchoring
	int32_t lightLevel;			// sector_t::lightlevel
	int32_t planeLight[2];		// GetFloorLight / GetCeilingLight
	uint32_t flags;
	uint32_t reserved[2];
};
static_assert(sizeof(LevelMeshSectorState) == 16 * 4, "LevelMeshSectorState must stay std430-compatible");

// Per side x tier (index side * 3 + tier). std430-compatible: 8 x 32 bit.
struct LevelMeshSideState
{
	int32_t textureId;			// untranslated
	float xOffset, yOffset;
	float xScale, yScale;
	uint32_t reserved[3];
};
static_assert(sizeof(LevelMeshSideState) == 8 * 4, "LevelMeshSideState must stay std430-compatible");

// Per sector x plane (index = plane ref). std430-compatible: 8 x 32 bit.
struct LevelMeshPlaneXform
{
	float xOffset, yOffset, baseyOffset;
	float xScale, yScale;
	float angle, baseAngle;		// degrees
	uint32_t reserved;
};
static_assert(sizeof(LevelMeshPlaneXform) == 8 * 4, "LevelMeshPlaneXform must stay std430-compatible");

// Texture translation table entry, indexed by the untranslated texture id.
// Layer i of the material is bindless slot (slot + i). Slot 0 is the null
// texture; an unresolved or invalid texture maps to all zeros.
// std430-compatible: 4 x 32 bit.
struct LevelMeshTextureEntry
{
	uint32_t slot;
	uint32_t layers;		// number of layers (low 16 bits) | material layer flags (TEXF_*, high 16 bits)
	float displayWidth, displayHeight;	// FGameTexture display size (flat texture matrix)
};

// One levelmesh view, as the game side hands it to DFrameBuffer::DrawLevelMesh.
enum ELevelMeshCullMode : int
{
	LMCULL_NONE,		// every cluster, one pass (diagnostic)
	LMCULL_FRUSTUM,		// frustum only, one pass
	LMCULL_HIZ,			// two-phase: last frame's visible set, then Hi-Z
};

enum ELevelMeshDrawMode : int
{
	LMDRAW_COMPACT,		// visible clusters' indices compacted: one draw per bucket and phase
	LMDRAW_CLUSTERS,	// one indirect command per visible cluster and bucket (draw-indirect-count)
};

enum ELevelMeshViewFlags : uint32_t
{
	LMVF_FULLBRIGHT = 1,	// the render state carries the light; no per-surface interim light
};

struct LevelMeshView
{
	float viewProj[16];		// ProjectionMatrix * ViewMatrix (GL world space: x, z, y)
	int cullMode = LMCULL_HIZ;
	int drawMode = LMDRAW_COMPACT;
	uint32_t flags = 0;		// LMVF_*
	float maskThreshold = 0.5f;	// alpha threshold of the masked bucket (gl_mask_threshold)
};

template<class T> struct LevelMeshSpan
{
	const T *data = nullptr;
	unsigned size = 0;
};

// Everything the backend uploads at level setup. The arrays only need to
// stay valid during the SetLevelMeshData call.
struct LevelMeshUploadData
{
	uint64_t id = 0;		// unique per bake; LevelMeshResident() compares it
	LevelMeshSpan<LevelMeshVertex> vertices;
	LevelMeshSpan<uint32_t> indices;
	LevelMeshSpan<LevelMeshSurface> surfaces;
	LevelMeshSpan<LevelMeshCluster> clusters;
	LevelMeshSpan<uint32_t> clusterSectors;
	LevelMeshSpan<uint32_t> clusterSurfaces;
	LevelMeshSpan<LevelMeshSectorState> sectors;
	LevelMeshSpan<LevelMeshSideState> sides;
	LevelMeshSpan<LevelMeshPlaneXform> planeXforms;
	LevelMeshSpan<int> textureIds;		// ids resolved into the translation table
	unsigned translationTableSize = 0;	// number of texture ids (TexMan.NumTextures())
};
