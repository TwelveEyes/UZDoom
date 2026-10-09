/*
** levelmesh_state.h
**
** Per-sector dynamic state for the backend-neutral levelmesh data layer:
** one 96-byte sector state record per sector, full-copied every frame into
** a ring of HW_MAX_PIPELINE_BUFFERS slots (spec section 4, ADR 0003), plus
** a 12-byte record per 3D light built from the level's light thinker lists.
** The GL33 draw path fetches the current frame's slot for per-surface
** shading and for warping load-dynamic surfaces; the classic scene path is
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
#include "hwrenderer/data/buffers.h"

struct FLevelLocals;
struct FColormap;
struct F3DFloor;

namespace levelmesh
{
//============================================================================
//
// 96-byte sector state record (spec section 4).
//
// One per sector, indexed by sector index. A surface samples the record of
// the sector its planeRef / lightRef points at (its own sector, or the
// model sector for fake flats / 3D floors). The record carries the single
// current, CPU-interpolated values: inside the interpolation window
// DoInterpolations() has already written the tick-blended D / TexZ into
// sector_t, so reading the live plane objects is exactly what the classic
// path bakes per frame (ADR 0002).
//
// Offsets (96 bytes, 4-byte aligned, no padding gaps):
//
//	offset	type	field
//	0		float	floorNormal[3]	sec->floorplane normal (nx, ny, nz)
//	12		float	floorD			sec->floorplane D
//	16		float	ceilNormal[3]	sec->ceilingplane normal
//	28		float	ceilD			sec->ceilingplane D
//	32		float	floorTexZ		interpolated floor TexZ (GetPlaneTexZ)
//	36		float	ceilTexZ		interpolated ceiling TexZ
//	40		int32	lightlevel		raw sec->lightlevel
//	44		int32	planeLight[2]	[0] floor / [1] ceiling secplane Light offset
//	52		uint8	colormap[9]	FColormap: LightColor(3) BlendFactor(1)
//						Desaturation(1) FadeColor(3) FogDensity(1)
//	61		uint8	reserved[35]	zero; spec section 4's scroll / glow /
//						flags slots are reserved for later slices
//
//============================================================================

struct LevelMeshSectorState
{
	float floorNormal[3];	// 0
	float floorD;			// 12
	float ceilNormal[3];	// 16
	float ceilD;			// 28
	float floorTexZ;		// 32
	float ceilTexZ;		// 36
	int32_t lightlevel;		// 40
	int32_t planeLight[2];	// 44  [0] floor, [1] ceiling
	uint8_t colormap[9];	// 52  packed FColormap
	uint8_t reserved[35];	// 61
};
static_assert(sizeof(LevelMeshSectorState) == 96, "LevelMeshSectorState must be 96 bytes");

//============================================================================
//
// 12-byte 3D light record.
//
// One per 3D light (lightlist_t entry with a lightsource), built from the
// level's per-sector XFloor.lightlist lists. The lightlist's entry 0 (the
// sector's own ceiling light, lightsource == NULL) is not a 3D light and is
// not recorded; the surface record's lightlistRef of -1 means the sector's
// own light.
//
//	offset	type	field
//	0		int16	lightlevel	live 3D light level (*lightlist->p_lightlevel,
//				a short in the engine, so 16 bits are lossless)
//	2		uint8	colormap[9]	lightlist->extra_colormap, same 9-byte pack
//	11		uint8	reserved	zero
//
//============================================================================

// Packed (no natural-alignment padding): 2 + 9 + 1 = 12 bytes.
#pragma pack(push, 1)
struct LevelMeshLightState
{
	int16_t lightlevel;		// 0
	uint8_t colormap[9];	// 2  packed FColormap
	uint8_t reserved;		// 11
};
#pragma pack(pop)
static_assert(sizeof(LevelMeshLightState) == 12, "LevelMeshLightState must be 12 bytes");

//============================================================================
//
// Result of a PackSnapshot call: the byte ranges to stream into the frame
// record.
//
//============================================================================

struct LevelMeshPackedState
{
	const uint8_t *sectorState;	// base of the packed sector-state slot
	size_t sectorSize;			// sectorCount * 96
	const uint8_t *lightState;	// base of the 3D light buffer
	size_t lightSize;			// lightCount * 12
};

//============================================================================
//
// FLevelMeshState
//
// Owns the per-level sector state ring and the 3D light state buffer
// (spec section 4: each FLevelLocals gets its own buffer + snapshot).
// A member of FLevelMesh, which is built once per level at the map loader
// build site; the initial fill is one PackSnapshot at level load (spec
// section 3 build order step 5).
//
// The ring holds HW_MAX_PIPELINE_BUFFERS full copies of
// sectorCount * 96 bytes. PackSnapshot re-derives every record from the
// live sector data and writes ONLY the given slot; the backend selects the
// frame's slot by uniform. ADR 0003: for identical game state the packed
// bytes are identical across runs (no floats from uninitialized memory, no
// pointers, sector-index order).
//
// The 3D light buffer is a single flat byte array (12 B per light), rebuilt
// from the light thinker lists on every PackSnapshot. Light records are
// deduplicated by F3DFloor (the same 3D floor appears in the lightlist of
// every sector it is attached to) in sector-index order, then list order;
// the order is deterministic for identical game state, but records do not
// keep stable indices when the light set changes between frames - the frame
// build must resolve light references in the same frame it packs.
//
//============================================================================

struct FLevelMeshState
{
	// Ring: HW_MAX_PIPELINE_BUFFERS slots of sectorCount * 96 bytes.
	TArray<uint8_t> sectorRing;
	int sectorCount = 0;
	// 3D light state buffer: 12 bytes per light; lightSources parallels the
	// records and is the per-pack dedup set.
	TArray<uint8_t> lightData;
	TArray<const F3DFloor *> lightSources;

	// Size the ring for this level and do the initial fill (one
	// PackSnapshot into slot 0). Called from FLevelMesh::Build.
	void Init(FLevelLocals &level);

	// Re-derive every sector's record from the live sector data (the
	// single current CPU-interpolated values when called inside the
	// interpolation window) and write the full copy into the given ring
	// slot; also rebuilds the 3D light buffer. Returns the slot's base
	// pointer + size alongside the 3D light byte range.
	LevelMeshPackedState PackSnapshot(FLevelLocals &level, int slot);

	// Accessors for the frame build / backend.
	const uint8_t *GetSectorSlot(int slot) const;
	size_t SectorSlotSize() const;
	const uint8_t *GetLightData() const;
	size_t LightDataSize() const;
	int LightCount() const;
};

}
