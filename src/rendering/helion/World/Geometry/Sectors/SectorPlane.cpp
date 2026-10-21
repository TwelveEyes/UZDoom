/*
** SectorPlane.cpp
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
*/

#include "World/Geometry/Sectors/SectorPlane.h"
#include "g_levellocals.h"

namespace Helion::World::Geometry::Sectors
{

SectorPlane SectorPlane::FromSector(sector_t* sector, SectorPlaneFace facing)
{
	int pos = facing == SectorPlaneFace::Floor ? sector_t::floor : sector_t::ceiling;

	SectorPlane plane;
	plane.Sector = sector;
	plane.Facing = facing;
	plane.Z = sector->GetPlaneTexZ(pos);
	plane.PrevZ = plane.Z;
	plane.TransferHeights = sector->GetHeightSec() != nullptr;

	// Same values hw_SetPlaneTextureRotation() consumes: the UZDoom matrix is
	// scale1 * translate(offs) * scale2 * rotate, i.e. rotate, offset, then scale.
	plane.RenderOffsets.Offset = ::Helion::Geometry::Vec2D(sector->GetXOffset(pos), sector->GetYOffset(pos));
	plane.RenderOffsets.LastOffset = plane.RenderOffsets.Offset;
	plane.RenderOffsets.Scale = ::Helion::Geometry::Vec2D(sector->GetXScale(pos), sector->GetYScale(pos));
	plane.RenderOffsets.Rotate = sector->GetAngle(pos).Radians();
	plane.FlatTransformMethod = Sectors::FlatTransformMethod::RotateThenOffset;
	return plane;
}

const secplane_t& SectorPlane::Plane() const
{
	return Facing == SectorPlaneFace::Floor ? Sector->floorplane : Sector->ceilingplane;
}

}
