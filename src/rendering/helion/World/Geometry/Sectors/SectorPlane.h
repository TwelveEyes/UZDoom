/*
** SectorPlane.h
**
** UZDoom-side view of Helion.World.Geometry.Sectors.SectorPlane.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion keeps one SectorPlane object per floor/ceiling with Z/PrevZ, flat
** offsets and a TransferHeights reference. UZDoom stores this data in
** sector_t::planes[]/floorplane/ceilingplane, so this is a cheap by-value
** snapshot built from a sector_t* and a plane index (sector_t::floor /
** sector_t::ceiling). PrevZ has no storage in sector_t: FromSector sets it
** equal to Z, callers that interpolate (plan D12) overwrite it from the
** interpolation data.
*/

#pragma once

#include "Maps/Specials/RenderOffsets.h"
#include "World/Geometry/Sectors/FlatTransformMethod.h"
#include "World/Geometry/Sectors/SectorPlaneFace.h"

struct sector_t;
struct secplane_t;

namespace Helion::World::Geometry::Sectors
{

struct SectorPlane
{
	sector_t* Sector = nullptr;
	SectorPlaneFace Facing = SectorPlaneFace::Floor;
	double Z = 0;
	double PrevZ = 0;
	Maps::Specials::RenderOffsets RenderOffsets;
	Sectors::FlatTransformMethod FlatTransformMethod = Sectors::FlatTransformMethod::RotateThenOffset;
	bool TransferHeights = false;

	static SectorPlane FromSector(sector_t* sector, SectorPlaneFace facing);

	// The UZDoom plane, for ZatPoint() on slopes.
	const secplane_t& Plane() const;
};

}
