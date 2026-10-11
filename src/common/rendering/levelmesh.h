/*
** levelmesh.h
**
** Backend-neutral static level-mesh model. The whole level's geometry is
** baked once at load into a flat vertex pool, per-region index buffers, and
** fixed-size surface/region records. The GL33 draw path (later slices) and
** the raytrace adapter both consume this model; the classic scene path is
** untouched.
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
#include "vectors.h"
#include "levelmesh_state.h"

struct FLevelLocals;

namespace levelmesh
{
//============================================================================
//
// Surface / record flag bits. The lower bits follow the spec section 3 flag
// table; LMF_ISFLAT / LMF_3DFLOOR / LMF_POLYOBJ do not appear in that table,
// so they use free bits 3, 10 and 12 instead of colliding with the
// openingMask / unpeggedLower / tier bits. LMF_LOADDYN is the spec's bit-0 dynamic
// flag; LMF_FAKECONTRAST is an internal bookkeeping bit outside the table (the rel
// values are baked in the record either way and the draw path selects them by tier).
//
//============================================================================

enum ESurfaceFlags : uint32_t
{
	LMF_DYNAMIC = 1 << 0,		// spec bit 0: dynamic (vertex slotA holds vparam, not baked z)
	LMF_SKY = 1 << 1,			// spec bit 1: sky surface (upper wall of a sky sector)
	LMF_LIGHTMAP = 1 << 2,		// spec bit 2: hasLightmap (surface has lightmap UVs)
	LMF_ISFLAT = 1 << 3,		// free bit: a flat (floor/ceiling/3D-floor) rather than a wall
	LMF_OPENING_UPPER = 1 << 4,	// spec bits 4-6 openingMask: upper window present
	LMF_OPENING_MIDDLE = 1 << 5, // spec openingMask: middle window present
	LMF_OPENING_LOWER = 1 << 6,  // spec openingMask: lower window present
	LMF_UNPEGGEDLOWER = 1 << 7,  // spec bit 7: lower wall does not track the front floor
	LMF_TIER = 3 << 8,		// spec bits 8-9: tier 0=upper / 1=middle / 2=lower (side_t index)
	LMF_POLYOBJ = 1 << 10,		// free bit: mid wall of a polyobject line
	LMF_3DFLOOR = 1 << 12,		// free bit: surface is a 3D floor (fake flat referencing a model)
};

// Legacy aliases kept for the builder's existing flag composition.
constexpr uint32_t LMF_LOADDYN = LMF_DYNAMIC;	// vertex slotA holds a vparam, not a baked z
constexpr uint32_t LMF_FAKECONTRAST = 1u << 13;	// internal free bit: fake-contrast rel pair baked below

// Wall light ingredients for the VS port of side_t::GetLightLevel (spec
// section 6 port list item 2): the side's static light flags, copied at build.
// The live sector light comes from the sector state ring (lightRef sector).
constexpr uint32_t LMF_SIDE_ABSLIGHT = 1u << 14;	// WALLF_ABSLIGHTING
constexpr uint32_t LMF_SIDE_ABSTIER = 1u << 15;	// WALLF_ABSLIGHTING_TIER << tier
constexpr uint32_t LMF_SIDE_NOFAKECONTRAST = 1u << 16;	// WALLF_NOFAKECONTRAST
constexpr uint32_t LMF_SIDE_SMOOTHLIGHT = 1u << 17;	// WALLF_SMOOTHLIGHTING
constexpr uint32_t LMF_SIDE_LIGHTFOG = 1u << 18;	// WALLF_LIGHT_FOG

// Per-group draw state (sub-range key next to the texture): the classic sets
// these per wall draw, so the levelmesh groups by them and binds them once per
// group. Bits 0-1 are the sampler clamp passed to SetMaterial exactly like the
// classic `flags & 3` (HWF_CLAMPX = 1 / HWF_CLAMPY = 2 == CLAMP_X / CLAMP_Y /
// CLAMP_XY); bit 2 is the TM_CLAMPY texture clamp of two-sided mid textures
// (HWWall::RenderTexturedWall: SetTextureClamp for M2S, unwarped textures).
constexpr uint32_t LMDS_CLAMPX = 1u;
constexpr uint32_t LMDS_CLAMPY = 2u;
constexpr uint32_t LMDS_TEXCLAMPY = 4u;
// Masked pass: alpha test at gl_mask_threshold. The classic only routes
// passflag-2 walls (two-sided mid, 3D blocks) and 3D-floor flats with a masked
// texture to its masked lists; every other surface draws in the plain pass
// (threshold 0) even with a masked texture (HWDrawInfo::AddWall / AddFlat).
constexpr uint32_t LMDS_MASKED = 8u;
// Double-sided group (no back-face culling). Everything else is culled with
// clockwise front faces: the classic draws a side / plane only from its
// front (HWDrawInfo::AddLine back-face reject, floors from above, ceilings
// from below), and the mesh must not show the back of a one-sided wall.
// 3D-floor flats are viewable from both sides.
constexpr uint32_t LMDS_NOCULL = 16u;

//============================================================================
//
// planeRef[4] / lightRef encodings (spec section 3):
//   planeRef: sectorIndex<<1 | planeBit (0=floor, 1=ceiling); flats store the
//             LM_PLANEREF_NONE sentinel in slots [2]/[3].
//   lightRef: sectorIndex<<2 | slot (0=floor, 1=ceiling, 2=wall), resolved
//             through heightsec transfer-light relationships at build time.
//
//============================================================================

constexpr uint32_t LM_PLANEREF_NONE = static_cast<uint32_t>(-1);	// flat slots [2]/[3]
constexpr uint32_t LM_LIGHT_SLOT_FLOOR = 0;
constexpr uint32_t LM_LIGHT_SLOT_CEILING = 1;
constexpr uint32_t LM_LIGHT_SLOT_WALL = 2;

//============================================================================
//
// 32-byte vertex pool element.
//
// Static surface:  slotA = baked z, slotB = baked v.
// Load-dynamic:    slotA = vparam (index into the per-frame sector state
//                   ring), slotB = 0.
//
//============================================================================

struct LevelMeshVertex
{
	float x;			// 0
	float y;			// 4
	float slotA;		// 8
	float u;			// 12  (u before slotB so aTexCoord can bind the adjacent
	float slotB;		// 16   pair (u, slotB); the GL VAO binds vec2 attributes)
	float lmu;		// 20
	float lmv;		// 24
	uint32_t surfaceIndex;	// 28
};
static_assert(sizeof(LevelMeshVertex) == 32, "LevelMeshVertex must be 32 bytes");

//============================================================================
//
// 64-byte surface record. All statics baked at build time; the dynamic
// per-frame light chain is resolved from the sector state ring by the
// shader using planeRef / lightRef.
//
//============================================================================

struct LevelMeshSurface
{
	uint32_t textureIndex;		// 0   texture index (FTextureID::GetIndex), or 0xFFFFFFFF
	uint32_t regionIndex;		// 4
	uint32_t planeRef[4];		// 8   sector/plane refs; flats: own floor/ceil, [2]=[3]=sentinel
	uint32_t lightRef;			// 24  sectorIndex<<2|slot (0=floor,1=ceiling,2=wall)
	uint32_t flags;			// 28
	float vparam[4];			// 32  [0] lower unpegged offset rel. front floor, [1] middle window,
							//     [2] upper window offset, [3] reserved (spec: V = z - TexZ + vparam)
	int16_t light;				// 48  walls: side_t::Light (raw); flats: unused
	int16_t tierLight;		// 50  walls: side_t::TierLights[tier]; flats: unused
	int16_t relSmooth;		// 52  raw fake-contrast rel, smooth formula (non-smooth when delta.X == 0)
	int16_t relNonSmooth;		// 54  raw fake-contrast rel, non-smooth formula
	int16_t lightlistRef;		// 56  -1 for statics
	uint16_t bandOffset;		// 58  offset into the per-level band table
	uint16_t bandCount;		// 60
	uint8_t skyScrollKind;		// 62  0 sky1 / 1 sky2 / 2 mist (sky surfaces)
	uint8_t skyScrollKind2;		// 63  doublesky second layer
};
static_assert(sizeof(LevelMeshSurface) == 64, "LevelMeshSurface must be 64 bytes");

//============================================================================
//
// Per-surface sub-range record. One per surface in build order (parallel to
// FLevelMesh::surfaces): the surface's index range in its region's IBO plus
// the precomputed world AABB. The per-(region, texture) table the region
// record points at (LevelMeshTexRange) is grouped from these records.
//
//============================================================================

struct LevelMeshSubRange
{
	uint32_t textureIndex;
	uint32_t iboOffset;		// index into the region's IBO
	uint32_t iboCount;
	float aabbMin[2];
	float aabbMax[2];
	uint32_t bandIndex;		// 0 = the surface's own band
	uint32_t drawState;		// LMDS_* bits (group key next to the texture)
};

//============================================================================
//
// Per-(region, texture) sub-range table entry (spec section 3 region record).
// Within a region the entries are sorted by texture index; each entry is the
// IBO span covering all of the region's surfaces of that texture, with the
// union of their world AABBs so the frame build can frustum-cull whole
// texture groups. firstSurface / surfaceCount span the parallel per-surface
// sub-range records.
//
//============================================================================

struct LevelMeshTexRange
{
	uint32_t textureIndex;
	uint32_t iboOffset;		// start of the group's span in the region's IBO
	uint32_t iboCount;		// span length
	float aabbMin[2];
	float aabbMax[2];
	uint32_t firstSurface;	// first per-surface sub-range of the group
	uint32_t surfaceCount;	// per-surface sub-ranges in the group
	uint32_t drawState;		// LMDS_* bits shared by every surface of the group
};

//============================================================================
//
// Per-level band table entry. A wall inside a 3D volume is drawn as per-band
// sub-draws; each band clips against the model sector's plane. The band table
// is static; the split planes are read from the sector state ring at draw time.
//
//============================================================================

struct LevelMeshBand
{
	uint32_t modelSector;
	uint32_t plane;			// 0 = floor, 1 = ceiling
	int32_t lightRef;			// 3D-light state buffer entry, or -1 (wall's own)
};

//============================================================================
//
// Region record. The container is fixed here; the transform / frontFace /
// scissor / stencil semantics are owned by the portal slice (06) and are
// identity / CCW / none on the main (whole-level) region.
//
//============================================================================

struct LevelMeshRegion
{
	uint32_t vertexOffset;	// offset into the vertex pool
	uint32_t vertexCount;
	uint32_t iboOffset;		// offset into the region IBO (per-region buffer)
	uint32_t iboCount;
	uint32_t texRangeOffset;	// offset into the per-(region, texture) sub-range table
	uint32_t texRangeCount;
	uint32_t actorOffset;	// offset into the per-region subsector index list
	uint32_t actorCount;		// number of subsectors; the draw pass walks each
				// subsector's live sprite list at draw time
	// --- transform slot (section 5) ---
	float transform[16];	// 4x4 remapped viewpoint; identity on main region
	uint32_t frontFace;		// 0 = CCW, 1 = CW
	uint32_t stencilLevel;	// scissor/stencil for portal clipping
};

//============================================================================
//
// FLevelMesh
//
//============================================================================

struct FLevelMesh
{
	// whole-level vertex pool (32-byte elements)
	TArray<LevelMeshVertex> vertices;
	// baked world positions (for the raytrace adapter; always present)
	TArray<FVector3> positions;
	// per-region index buffers
	TArray<TArray<uint32_t>> regionIBOs;
	// surface records
	TArray<LevelMeshSurface> surfaces;
	// per-surface sub-range records (build order, parallel to surfaces)
	TArray<LevelMeshSubRange> subRanges;
	// per-(region, texture) sub-range table (one contiguous table, offset per region)
	TArray<LevelMeshTexRange> texRanges;
	// per-level band table
	TArray<LevelMeshBand> bands;
	// region records
	TArray<LevelMeshRegion> regions;
	// per-region subsector index list, flat (into FLevelLocals::subsectors);
	// the draw pass walks each subsector's live sprite list at draw time
	TArray<uint32_t> actors;
	// per-sector dynamic state: the 96-byte record ring (spec section 4) and
	// the 3D light state buffer. Initial fill is one PackSnapshot at build.
	FLevelMeshState state;

	// The build replaces the classic DoomLevelMesh build site. It walks
	// FLevelLocals and produces the whole model.
	static FLevelMesh Build(FLevelLocals &level);

	// Number of vertices in the whole pool.
	size_t VertexCount() const { return vertices.Size(); }
	// Number of surfaces.
	size_t SurfaceCount() const { return surfaces.Size(); }
	// Number of regions.
	size_t RegionCount() const { return regions.Size(); }

	// Memory footprint of the GPU-facing buffers (pool + IBOs), for the
	// budget sanity check. (The sector state ring is accounted by the
	// backend; it is not part of the static geometry budget.)
	size_t GPUMemoryBytes() const
	{
		size_t bytes = vertices.Size() * sizeof(LevelMeshVertex);
		for (const auto &ibo : regionIBOs) bytes += ibo.Size() * sizeof(uint32_t);
		return bytes;
	}
};

}
