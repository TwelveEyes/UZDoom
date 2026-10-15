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
	LMSF_LIGHTLIST = 1 << 11,	// lit by lightSector's 3D lightlist: banded (classic split planes) or one 3D light (Put3DWall, P_GetPlaneLight)
	LMSF_SKY = 1 << 12,			// unused for mesh surfaces (sky goes to side lists); reserved
	LMSF_DEFERRED = 1 << 13,	// not drawn by the levelmesh yet: the classic path owns it (LMB_DEFERRED)
	LMSF_TEXCLAMPY = 1 << 14,	// TEXF_ClampY texture mode (classic SetTextureClamp: clamped two-sided mid texture)
	LMSF_TEXFULLBRIGHT = 1 << 15,	// flats: FGameTexture::isFullbright (HWFlat::Process: white colormap, light 255)
	LMSF_NOSECTORFOG = 1 << 16,	// walls: fog of light 255 without the sector colormap (RENDERWALL_M2SNF)
	LMSF_WALLTYPESHIFT = 24,	// RENDERWALL_* in bits 24..27
};

// LevelMeshSurface::sideFlags: the side_t::GetLightLevel inputs of the
// surface's tier (WALLF_* translated by the bake).
enum ELevelMeshSideLightFlags : uint32_t
{
	LMSD_ABSLIGHTING = 1,		// WALLF_ABSLIGHTING
	LMSD_ABSLIGHTING_TIER = 1 << 1,	// WALLF_ABSLIGHTING_TIER << tier
	LMSD_NOFAKECONTRAST = 1 << 2,	// WALLF_NOFAKECONTRAST
	LMSD_SMOOTHLIGHTING = 1 << 3,	// WALLF_SMOOTHLIGHTING
	LMSD_LIGHT_FOG = 1 << 4,	// WALLF_LIGHT_FOG
};

enum ELevelMeshSurfaceKind : uint32_t
{
	LMSK_WALL,
	LMSK_FLOOR,
	LMSK_CEILING,
};

// std430-compatible: 28 x 32 bit.
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
	int32_t lightLevel;		// classic lightlevel at bake (RescaleLightLevel applied; reference only, the resolve recomputes it)
	int32_t relLight;		// classic rellight at bake (reference only)
	int32_t sideLight;		// side Light (low 16) | TierLights[tier] (high 16)
	int32_t fakeContrast;	// smooth rel (low 16) | classic rel (high 16): side_t::GetFakeContrastRel
	uint32_t sideFlags;		// LMSD_*
	int32_t lightmapPage;	// -1 = none
	float normal[3];		// walls: horizontal, pointing into the side's sector; flats: plane normal (floor up, ceiling down)
	uint32_t firstVertex, numVertices;
	uint32_t firstIndex, numIndices;	// into the cluster-ordered index buffer
	// Walls: the plane bounding the tier from above in the classic (upper:
	// front ceiling, lower: back floor, one-sided: ceiling); LM_NOPLANE =
	// no bound. The vertex stage keeps every non-top vertex at or below it,
	// so a tier whose planes crossed (a door opened past the front ceiling,
	// a lift lowered past the front floor) collapses instead of turning
	// inside out, like the classic, which stops emitting it.
	uint32_t edgeTop;
	uint32_t reserved[3];
};
static_assert(sizeof(LevelMeshSurface) == 28 * 4, "LevelMeshSurface must stay std430-compatible");

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
	float bmin[3];			// xy AABB; z from the load heights, refreshed when an anchor plane moves (LevelMesh_UpdateState)
	uint32_t firstIndex;
	float bmax[3];
	uint32_t numIndices;
	uint32_t bucketIndices[LMB_COUNT];	// per bucket, consecutive from firstIndex in ELevelMeshBucket order
	uint32_t bucketMask;	// 1 << bucket for every non-empty bucket
	uint32_t firstSector, numSectors;	// into the cluster -> sector list (vertex anchor sectors)
	uint32_t firstSurface, numSurfaces;	// into the cluster -> surface list
};
static_assert(sizeof(LevelMeshCluster) == 16 * 4, "LevelMeshCluster must stay std430-compatible");

// Per-sector state. std430-compatible: 24 x 32 bit. Every field is
// streamed dirty-only (LevelMesh_UpdateState compares the whole packed
// record, LevelMesh_FillSectorState packs it), so a new light ingredient
// only needs a field here and a line in the packer.
struct LevelMeshSectorState
{
	LevelMeshPlane planes[2];	// floor, ceiling (LM_PackPlane)
	float texZ[2];				// GetPlaneTexZ: wall texture anchoring
	int32_t lightLevel;			// sector_t::lightlevel
	int32_t planeLight[2];		// GetFloorLight / GetCeilingLight
	uint32_t lightColor;		// Colormap.LightColor (PalEntry)
	uint32_t fadeColor;			// Colormap.FadeColor (PalEntry)
	uint32_t colormap;			// Colormap.Desaturation | BlendFactor << 8 | FogDensity << 16
	// sector_t::GetWallGlow: rgb + height (alpha) of the ceiling (top) and
	// floor (bottom) glow, sector glow or the current frame's texture glow;
	// both heights 0 when it returns false (no HWF_GLOW).
	float glowTop[4];
	float glowBottom[4];
};
static_assert(sizeof(LevelMeshSectorState) == 24 * 4, "LevelMeshSectorState must stay std430-compatible");

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

// Per-view shading inputs of the resolve (resolve.comp). The C++ light
// chain (hw_lighting.cpp, hw_setcolor.cpp, hw_walls.cpp, hw_flats.cpp)
// stays authoritative: the game side fills this block from it
// (LevelMesh_FillShading) and the shader ports the per-surface part line
// by line. Every cvar, level field and CPU float rounding the port must not
// redo on the GPU is a precomputed value or a table here. A storage buffer
// (LMBIND_SHADING), not a uniform block: the tables are 5 KB. std430.
enum ELevelMeshShadingFlags : uint32_t
{
	LMSH_FULLBRIGHT = 1,			// HWDrawInfo::isFullbrightScene
	LMSH_FADETABLE = 1 << 1,		// LEVEL_HASFADETABLE
	LMSH_FORCEFAKECONTRAST = 1 << 2,	// LEVEL3_FORCEFAKECONTRAST
	LMSH_SMOOTHLIGHTING = 1 << 3,	// LEVEL2_SMOOTHLIGHTING
	LMSH_NOLIGHTFADE = 1 << 4,		// LEVEL3_NOLIGHTFADE
	LMSH_BRIGHTFOG = 1 << 5,		// FLevelLocals::brightfog
	LMSH_INSKYBOX = 1 << 6,			// portalState.inskybox
	LMSH_OUTSIDEFOG = 1 << 7,		// outsidefogdensity != 0 && APART(info->outsidefog) != 0xff
};

struct LevelMeshShading
{
	int32_t lightMode;			// ELightMode of the view (HWDrawInfo::lightmode)
	uint32_t flags;				// LMSH_*
	int32_t fakeContrast;		// r_fakecontrast
	int32_t fogMode;			// gl_fogmode
	int32_t distanceCullType;	// r_distance_cull_type
	int32_t extraLight;			// getExtraLight()
	int32_t cullFogDensity;		// SetFog: clamp(19200 / culldist, 1, 255)
	uint32_t cullColor;			// FLevelLocals::cullcolor
	int32_t levelFogDensity;	// FLevelLocals::fogdensity
	int32_t outsideFogDensity;	// FLevelLocals::outsidefogdensity
	uint32_t outsideFog;		// level_info_t::outsidefog
	float maxLightDist;			// SetShaderLight: MAXDIST
	float fogDensityScale;		// FRenderState::SetFog: -LOG2E / 64000
	uint32_t reserved[3];
	int32_t rescale[256];		// RescaleLightLevel(0..255)
	int32_t darkLight[256];		// CalcLightLevel: RoundHalfEven(192.f - (192 - level) * 1.87f)
	float distFog[2][256];		// hw_lighting.cpp distfogtable
	float softLight[256];		// FRenderState::SetSoftLightLevel: level / 255.f (a GPU division may round differently)
};
static_assert(sizeof(LevelMeshShading) == (16 + 5 * 256) * 4, "LevelMeshShading must stay std430-compatible");

// The resolved shading of one surface for the current view (written by
// resolve.comp, read by the vertex stage). std430-compatible: 12 x 32 bit.
struct LevelMeshResolved
{
	uint32_t color;			// SetColor: light color r | g << 8 | b << 16 | desaturation << 24
	uint32_t fogColor;		// SetFog color (rgb)
	int32_t fogMode;		// uFogEnabled (VkRenderState::FillPushConstants)
	float softLight;		// uLightLevel (mLightParms[3])
	float fogDensity;		// uFogDensity (mLightParms[2])
	float lightFactor;		// uLightFactor (mLightParms[1])
	float lightDist;		// uLightDist (mLightParms[0])
	uint32_t glow;			// bit 0: HWF_GLOW kept (the glow colors apply)
	uint32_t reserved[4];
};
static_assert(sizeof(LevelMeshResolved) == 12 * 4, "LevelMeshResolved must stay std430-compatible");

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
	LMVF_FULLBRIGHT = 1,	// debug: draw every surface with the render state's (fullbright) light instead of the resolve
};

struct LevelMeshView
{
	float viewProj[16];		// ProjectionMatrix * ViewMatrix (GL world space: x, z, y)
	int cullMode = LMCULL_HIZ;
	int drawMode = LMDRAW_COMPACT;
	uint32_t flags = 0;		// LMVF_*
	float maskThreshold = 0.5f;	// alpha threshold of the masked bucket (gl_mask_threshold)
	const LevelMeshShading *shading = nullptr;	// resolve inputs (valid during the call)
};

template<class T> struct LevelMeshSpan
{
	const T *data = nullptr;
	unsigned size = 0;
};

// A refreshed cluster z-range (the xy box never changes).
struct LevelMeshClusterZ
{
	uint32_t cluster;
	float zmin, zmax;
	uint32_t reserved;
};
static_assert(sizeof(LevelMeshClusterZ) == 4 * 4, "LevelMeshClusterZ must stay std430-compatible");

// One frame's dirty state, as LevelMesh_UpdateState hands it to
// DFrameBuffer::UpdateLevelMeshState. Every index appears at most once.
// The arrays only need to stay valid during the call.
struct LevelMeshStateUpdate
{
	uint64_t id = 0;		// the bake the records belong to
	LevelMeshSpan<uint32_t> sectorIndices;
	LevelMeshSpan<LevelMeshSectorState> sectors;	// parallel to sectorIndices
	LevelMeshSpan<LevelMeshClusterZ> clusters;
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
