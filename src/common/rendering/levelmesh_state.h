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
//	0		float	floorPlane[4]	sec->floorplane (nx, ny, nz, d) - tilted floor plane
//	16		float	ceilPlane[4]	sec->ceilingplane (nx, ny, nz, d) - tilted ceiling plane
//	32		float	floorScroll[2]	floor UV offset (applied in VS)
//	40		float	ceilScroll[2]	ceiling UV offset
//	48		uint16	lightlevel		raw sector->lightlevel (0..255)
//	50		int16	planeLight[2]	[0] floor, [1] ceiling - secplane Light offset
//	54		uint16	flags			bit0 transdoor (VS applies the classic z-1 floor offset)
//							| bit1 floor ABSLIGHTING | bit2 ceil ABSLIGHTING
//	56		uint32	glowFloorColor	packed PalEntry (ABGR);
//							0 = texture-glow fallback, ~0u = glow disabled
//	60		float	glowFloorHeight
//	64		uint32	glowCeilColor	packed PalEntry (ABGR);
//							0 = texture-glow fallback, ~0u = glow disabled
//	68		float	glowCeilHeight
//	72		uint8	colormap[9]	FColormap: LightColor(3) BlendFactor(1)
//							Desaturation(1) FadeColor(3) FogDensity(1)
//	81		uint8	reserved[3]	zero
//	84		float	reserved[2]	0 (05's skyOffset slot - reserved)
//	92		uint32	reserved		0
//
// The natural field order below already lands every member at its spec
// offset (the first 4-byte member after byte 47 is at 56), so no
// #pragma pack is needed; the offsetof asserts pin the layout.
//
//============================================================================

struct LevelMeshSectorState
{
	float floorPlane[4];		// 0   (nx, ny, nz, d) tilted floor plane
	float ceilPlane[4];		// 16  (nx, ny, nz, d) tilted ceiling plane
	float floorScroll[2];		// 32  floor UV offset (applied in VS)
	float ceilScroll[2];		// 40  ceiling UV offset
	uint16_t lightlevel;		// 48  raw sec->lightlevel (0..255)
	int16_t planeLight[2];		// 50  [0] floor, [1] ceiling secplane Light offset
	uint16_t flags;			// 54  bit0 transdoor, bit1 floor ABSLIGHTING,
							//     bit2 ceil ABSLIGHTING
	uint32_t glowFloorColor;	// 56  packed PalEntry (ABGR); 0 = texture-glow
							//     fallback, ~0u = glow disabled
	float glowFloorHeight;		// 60
	uint32_t glowCeilColor;		// 64  packed PalEntry (ABGR); 0 = texture-glow
							//     fallback, ~0u = glow disabled
	float glowCeilHeight;		// 68
	uint8_t colormap[9];		// 72  packed FColormap
	uint8_t reserved[3];		// 81
	float reservedF[2];		// 84  0
	uint32_t reservedU32;		// 92  0
};
static_assert(sizeof(LevelMeshSectorState) == 96, "LevelMeshSectorState must be 96 bytes");
static_assert(offsetof(LevelMeshSectorState, ceilPlane) == 16, "LevelMeshSectorState layout");
static_assert(offsetof(LevelMeshSectorState, floorScroll) == 32, "LevelMeshSectorState layout");
static_assert(offsetof(LevelMeshSectorState, ceilScroll) == 40, "LevelMeshSectorState layout");
static_assert(offsetof(LevelMeshSectorState, lightlevel) == 48, "LevelMeshSectorState layout");
static_assert(offsetof(LevelMeshSectorState, planeLight) == 50, "LevelMeshSectorState layout");
static_assert(offsetof(LevelMeshSectorState, flags) == 54, "LevelMeshSectorState layout");
static_assert(offsetof(LevelMeshSectorState, glowFloorColor) == 56, "LevelMeshSectorState layout");
static_assert(offsetof(LevelMeshSectorState, glowFloorHeight) == 60, "LevelMeshSectorState layout");
static_assert(offsetof(LevelMeshSectorState, glowCeilColor) == 64, "LevelMeshSectorState layout");
static_assert(offsetof(LevelMeshSectorState, glowCeilHeight) == 68, "LevelMeshSectorState layout");
static_assert(offsetof(LevelMeshSectorState, colormap) == 72, "LevelMeshSectorState layout");
static_assert(offsetof(LevelMeshSectorState, reserved) == 81, "LevelMeshSectorState layout");
static_assert(offsetof(LevelMeshSectorState, reservedF) == 84, "LevelMeshSectorState layout");
static_assert(offsetof(LevelMeshSectorState, reservedU32) == 92, "LevelMeshSectorState layout");

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
