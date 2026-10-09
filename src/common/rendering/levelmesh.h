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

struct FLevelLocals;

namespace levelmesh
{
//============================================================================
//
// Surface / record flag bits
//
//============================================================================

enum ESurfaceFlags : uint32_t
{
	LMF_ISFLAT = 1 << 0,		// a flat (floor/ceiling/3D-floor) rather than a wall
	LMF_SKY = 1 << 1,			// upper wall of a sky sector (u baked with sky resolution)
	LMF_3DFLOOR = 1 << 2,		// surface is a 3D floor (fake flat referencing a model)
	LMF_LOADDYN = 1 << 3,		// vertex slotA holds a vparam, not a baked z
	LMF_FAKECONTRAST = 1 << 4,	// fake-contrast rel pair baked below
	LMF_LIGHTMAP = 1 << 5,		// surface has lightmap UVs
	LMF_POLYOBJ = 1 << 6,		// mid wall of a polyobject line
};

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
	float slotB;		// 12
	float u;			// 16
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
	uint32_t textureIndex;		// 0   texture index (FTextureID::GetIndex), or -1
	uint32_t regionIndex;		// 4
	uint32_t planeRef[4];		// 8   model references (sector/plane or 3D-floor)
	uint32_t lightRef;			// 24  3D-light chain ref, or LM_LIGHTREF_OWN
	uint32_t flags;			// 28
	float vparam[4];			// 32  per-vertex vparam for load-dynamic surfaces
	int16_t light;				// 48  baked static light level
	int16_t tierLight;		// 50
	int16_t relSmooth;		// 52  fake-contrast rel (smooth)
	int16_t relNonSmooth;		// 54  fake-contrast rel (non-smooth)
	int16_t lightlistRef;		// 56  -1 for statics
	uint16_t bandOffset;		// 58  offset into the per-level band table
	uint16_t bandCount;		// 60
	uint8_t skyScrollKind;		// 62  0 sky1 / 1 sky2 / 2 mist (sky surfaces)
	uint8_t skyScrollKind2;		// 63  doublesky second layer
};
static_assert(sizeof(LevelMeshSurface) == 64, "LevelMeshSurface must be 64 bytes");

constexpr uint32_t LM_LIGHTREF_OWN = 0xFFFFFFFF;	// lightRef == this value: wall's own chain

//============================================================================
//
// Per-region sub-range table entry. Within a region the entries are sorted by
// texture index, bands grouped within a texture. The world AABB is precomputed
// so the frame build can frustum-cull whole sub-ranges.
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
	uint32_t texRangeOffset;	// offset into the sub-range table
	uint32_t texRangeCount;
	uint32_t actorOffset;	// offset into the per-region actor list
	uint32_t actorCount;
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
	// per-region sub-range tables (one contiguous table, offset per region)
	TArray<LevelMeshSubRange> subRanges;
	// per-level band table
	TArray<LevelMeshBand> bands;
	// region records
	TArray<LevelMeshRegion> regions;
	// per-region actor list (sprite source indices), flat
	TArray<uint32_t> actors;

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
	// budget sanity check.
	size_t GPUMemoryBytes() const
	{
		size_t bytes = vertices.Size() * sizeof(LevelMeshVertex);
		for (const auto &ibo : regionIBOs) bytes += ibo.Size() * sizeof(uint32_t);
		return bytes;
	}
};

}
