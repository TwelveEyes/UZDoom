/*
** levelmesh_state.cpp
**
** Packing of the per-sector state ring and the 3D light state buffer for
** the backend-neutral levelmesh data layer. Every PackSnapshot re-derives
** the full record set from the live FLevelLocals data, so the ring slot is
** byte-identical across runs for identical game state (ADR 0003).
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

#include "levelmesh_state.h"
#include "r_defs.h"
#include "g_levellocals.h"
#include "p_3dfloors.h"
#include <cstring>

using levelmesh::FLevelMeshState;
using levelmesh::LevelMeshSectorState;
using levelmesh::LevelMeshLightState;
using levelmesh::LevelMeshPackedState;

//============================================================================
//
// Local helpers
//
//============================================================================

// The 9-byte FColormap pack (spec section 4): LightColor(3) BlendFactor(1)
// Desaturation(1) FadeColor(3) FogDensity(1). FogDensity is a uint16 in
// FColormap; the spec layout carries one byte (the low byte; the UDMF
// loader clamps it to 0..256).
static void LM_PackColormap(uint8_t *dst, const FColormap &cm)
{
	dst[0] = cm.LightColor.r;
	dst[1] = cm.LightColor.g;
	dst[2] = cm.LightColor.b;
	dst[3] = cm.BlendFactor;
	dst[4] = cm.Desaturation;
	dst[5] = cm.FadeColor.r;
	dst[6] = cm.FadeColor.g;
	dst[7] = cm.FadeColor.b;
	dst[8] = (uint8_t)cm.FogDensity;
}

//============================================================================
//
// FLevelMeshState
//
//============================================================================

void FLevelMeshState::Init(FLevelLocals &level)
{
	sectorCount = level.sectors.Size();
	size_t slotSize = (size_t)sectorCount * sizeof(LevelMeshSectorState);
	sectorRing.Resize((size_t)HW_MAX_PIPELINE_BUFFERS * slotSize);
	memset(sectorRing.Data(), 0, sectorRing.Size());
	lightData.Clear();
	lightSources.Clear();
	// Initial fill at level load (spec section 3 build order step 5).
	PackSnapshot(level, 0);
}

LevelMeshPackedState FLevelMeshState::PackSnapshot(FLevelLocals &level, int slot)
{
	LevelMeshPackedState out = {};
	if (slot < 0 || slot >= HW_MAX_PIPELINE_BUFFERS)
	{
		return out;
	}
	if (level.sectors.Size() != (size_t)sectorCount)
	{
		// The sector set is static per level; re-init defensively.
		Init(level);
	}
	int nsectors = level.sectors.Size();
	size_t slotSize = (size_t)sectorCount * sizeof(LevelMeshSectorState);
	uint8_t *base = sectorRing.Data() + (size_t)slot * slotSize;

	//--- sector state records (full copy, sector-index order) --------------
	for (int i = 0; i < nsectors; i++)
	{
		const sector_t &sec = level.sectors[i];
		LevelMeshSectorState &rec = *(LevelMeshSectorState *)(base + (size_t)i * sizeof(LevelMeshSectorState));
		const DVector3 &fn = sec.floorplane.Normal();
		const DVector3 &cn = sec.ceilingplane.Normal();
		rec.floorPlane[0] = (float)fn.X;
		rec.floorPlane[1] = (float)fn.Y;
		rec.floorPlane[2] = (float)fn.Z;
		rec.floorPlane[3] = (float)sec.floorplane.fD();
		rec.ceilPlane[0] = (float)cn.X;
		rec.ceilPlane[1] = (float)cn.Y;
		rec.ceilPlane[2] = (float)cn.Z;
		rec.ceilPlane[3] = (float)sec.ceilingplane.fD();
		// Inside the interpolation window these are the tick-blended values
		// DoInterpolations() wrote into sector_t (scroll via
		// DSectorScrollInterpolation); on non-interpolated frames the raw
		// tick values - exactly what the classic path renders. The scroll is
		// the sector plane's xform offset (HWSectorPlane::GetFromSector reads
		// the same GetXOffset/GetYOffset values).
		rec.floorScroll[0] = (float)sec.GetXOffset(sector_t::floor);
		rec.floorScroll[1] = (float)sec.GetYOffset(sector_t::floor);
		rec.ceilScroll[0] = (float)sec.GetXOffset(sector_t::ceiling);
		rec.ceilScroll[1] = (float)sec.GetYOffset(sector_t::ceiling);
		rec.lightlevel = (uint16_t)sec.lightlevel;
		rec.planeLight[0] = (int16_t)sec.GetPlaneLight(sector_t::floor);
		rec.planeLight[1] = (int16_t)sec.GetPlaneLight(sector_t::ceiling);
		// bit0 transdoor is locked to 0 (ticket 05 territory); bits 1/2 carry
		// the per-plane ABSLIGHTING state for the VS light chain.
		uint16_t lmflags = 0;
		if (sec.GetFlags(sector_t::floor) & PLANEF_ABSLIGHTING)
		{
			lmflags |= 2;
		}
		if (sec.GetFlags(sector_t::ceiling) & PLANEF_ABSLIGHTING)
		{
			lmflags |= 4;
		}
		rec.flags = lmflags;
		// Raw PalEntry (ABGR): the 0 = texture-glow fallback / ~0u = glow
		// disabled sentinels are preserved by the raw store. Direct field
		// access: the GetGlowColor/GetGlowHeight accessors are non-const.
		rec.glowFloorColor = sec.planes[sector_t::floor].GlowColor;
		rec.glowFloorHeight = (float)sec.planes[sector_t::floor].GlowHeight;
		rec.glowCeilColor = sec.planes[sector_t::ceiling].GlowColor;
		rec.glowCeilHeight = (float)sec.planes[sector_t::ceiling].GlowHeight;
		LM_PackColormap(rec.colormap, sec.Colormap);
		memset(rec.reserved, 0, sizeof(rec.reserved));
		memset(rec.reservedF, 0, sizeof(rec.reservedF));
		rec.reservedU32 = 0;
	}

	//--- 3D light state buffer (rebuilt every pack) -------------------------
	// One record per 3D light: the lightlist_t entries of every sector's
	// XFloor.lightlist that have a lightsource. Entry 0 of a lightlist is
	// the sector's own ceiling light (lightsource == NULL) and is the
	// surface record's lightlistRef -1. The same F3DFloor appears in the
	// lightlist of every sector it is attached to, so deduplicate by it.
	lightData.Clear();
	lightSources.Clear();
	for (int i = 0; i < nsectors; i++)
	{
		const sector_t &sec = level.sectors[i];
		const TArray<lightlist_t> &lightlist = sec.e->XFloor.lightlist;
		int nlights = lightlist.Size();
		for (int l = 0; l < nlights; l++)
		{
			const lightlist_t &ll = lightlist[l];
			if (ll.lightsource == nullptr) continue;
			if (lightSources.Contains(ll.lightsource)) continue;
			lightSources.Push(ll.lightsource);
			size_t off = lightData.Size();
			lightData.Resize(off + sizeof(LevelMeshLightState));
			memset(&lightData[off], 0, sizeof(LevelMeshLightState));
			LevelMeshLightState &rec = *(LevelMeshLightState *)&lightData[off];
			rec.lightlevel = (int16_t)*ll.p_lightlevel;
			LM_PackColormap(rec.colormap, ll.extra_colormap);
			rec.reserved = 0;
		}
	}

	out.sectorState = base;
	out.sectorSize = slotSize;
	out.lightState = lightData.Data();
	out.lightSize = lightData.Size();
	return out;
}

const uint8_t *FLevelMeshState::GetSectorSlot(int slot) const
{
	if (slot < 0 || slot >= HW_MAX_PIPELINE_BUFFERS) return nullptr;
	return sectorRing.Data() + (size_t)slot * SectorSlotSize();
}

size_t FLevelMeshState::SectorSlotSize() const
{
	return (size_t)sectorCount * sizeof(LevelMeshSectorState);
}

const uint8_t *FLevelMeshState::GetLightData() const
{
	return lightData.Data();
}

size_t FLevelMeshState::LightDataSize() const
{
	return lightData.Size();
}

int FLevelMeshState::LightCount() const
{
	return lightSources.Size();
}
