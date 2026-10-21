/*
** RenderSector.h
**
** UZDoom-only: by-value view of a sector as the Helion geometry renderer sees it.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion's Sector owns Floor/Ceiling SectorPlane objects (Z, PrevZ, TextureHandle, light, NoRender ...)
** and the geometry renderer builds temporary "fake" sectors with only some heights set (3D floor slices,
** transfer heights views). UZDoom keeps that data in sector_t, so the geometry renderer works on this
** by-value snapshot. RenderPlane derives from the live SectorPlane so it converts to the const SectorPlane&
** the WorldTriangulator takes. SectorPlane::Sector points at the sector_t whose floorplane/ceilingplane
** carries the slope: the real sector, or for transfer height views the cached fake-flat copy.
*/

#pragma once

#include "World/Geometry/Sectors/SectorPlane.h"
#include "World/Geometry/Sectors/TransferHeightsView.h"

struct sector_t;

namespace Helion::World::Geometry::Sectors
{

struct RenderPlane : public SectorPlane
{
	// FTextureID::GetIndex() of the plane flat.
	int TextureHandle = 0;
	// sector_t::GetPlaneLight(pos) and PLANEF_ABSLIGHTING.
	int LightLevelAdd = 0;
	bool LightLevelAbsolute = false;
	// PLANEF_NORENDER.
	bool NoRender = false;
	bool MidTextureHack = false;
	// Unique per level: sector index * 2 + face for real planes, assigned by Sector3D for 3D planes.
	int Id = 0;

	RenderPlane() = default;
	static RenderPlane From(sector_t* sector, SectorPlaneFace face);
};

struct RenderSector
{
	// The sector_t carrying the plane equations (real sector or fake-flat copy).
	sector_t* Sector = nullptr;
	// Index of the real sector (light buffer index, render state tables).
	int Id = 0;
	RenderPlane Floor;
	RenderPlane Ceiling;
	// FTextureID::GetIndex() of sector_t::planes[].skytexture[0].
	int FloorSkyTextureHandle = 0;
	int CeilingSkyTextureHandle = 0;
	short LightLevel = 0;
	// sector_t::GetHeightSec() != nullptr on the real sector (Helion Sector.TransferHeights != null).
	bool TransferHeights = false;
	// Light sector indices of 3D floor lighting (Helion TransferFloorLightSector/TransferCeilingLightSector).
	int TransferFloorLightSectorId = 0;
	int TransferCeilingLightSectorId = 0;

	RenderSector() = default;
	// Implicit so that real sectors can be passed wherever the geometry renderer takes a RenderSector.
	RenderSector(sector_t* sector) { *this = From(sector); }
	static RenderSector From(sector_t* sector);

	RenderPlane& GetSectorPlane(SectorPlaneFace face) { return face == SectorPlaneFace::Floor ? Floor : Ceiling; }
	const RenderPlane& GetSectorPlane(SectorPlaneFace face) const { return face == SectorPlaneFace::Floor ? Floor : Ceiling; }
};

// Helion Sector.GetRenderSector(TransferHeightView): the sector as seen from the top/middle/bottom view of
// a transfer heights (Boom fake floor) sector. Implemented in GeometryRenderer.FakeFlat.cpp with the rules
// of hw_fakeflat.cpp (hw_FakeFlat). Sectors without a height sector return RenderSector::From(sector).
RenderSector GetRenderSector(sector_t* sector, TransferHeightView view);
// GetRenderSector for the back (other) sector of a two-sided line: hw_FakeFlat(back = true), which additionally moves the
// floor of a SECMF_OVERLAPPING sector (ceiling below floor) up to its ceiling.
RenderSector GetBackRenderSector(sector_t* sector, TransferHeightView view);

// Drops the cached fake-flat sector copies (call on level change).
void ClearFakeFlatSectors();

// Previous-tick Z (sector_t::GetPlaneTexZ) of a plane for GPU interpolation (plan D12): the oldtexz of the plane's
// DSectorPlaneInterpolation, or currentZ when it has none. pos is sector_t::floor / sector_t::ceiling.
double PrevPlaneTexZ(sector_t* sector, int pos, double currentZ);

}
