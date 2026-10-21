/*
** RenderBlock.cpp
**
** Port of Helion.Render.RenderBlock.
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

#include "RenderBlock.h"
#include "g_levellocals.h"

namespace Helion::Render
{

namespace
{

struct PlaneInfo
{
	double Z;
	bool TransferHeights;
};

PlaneInfo GetPlane(sector_t* sector, int pos)
{
	return { sector->GetPlaneTexZ(pos), sector->GetHeightSec() != nullptr };
}

PlaneInfo GetPlane(const World::Geometry::Sectors::SectorPlane& plane)
{
	return { plane.Z, plane.TransferHeights };
}

bool IsBlockedPlanes(side_t* side, const PlaneInfo& frontFloorPlane, const PlaneInfo& backFloorPlane,
	const PlaneInfo& frontCeilingPlane, const PlaneInfo& backCeilingPlane)
{
	if (frontFloorPlane.TransferHeights || backFloorPlane.TransferHeights)
		return false;

	if (backCeilingPlane.Z <= frontFloorPlane.Z)
		return side->textures[side_t::top].texture.isValid();

	if (frontCeilingPlane.Z <= backFloorPlane.Z)
		return side->textures[side_t::bottom].texture.isValid();

	if (backCeilingPlane.Z <= backFloorPlane.Z)
	{
		if (backCeilingPlane.Z < frontCeilingPlane.Z)
			return side->textures[side_t::top].texture.isValid();
		if (backFloorPlane.Z < frontFloorPlane.Z)
			return side->textures[side_t::top].texture.isValid();
	}
	return false;
}

}

bool RenderBlock::IsSkyBlocked(line_t* line)
{
	assert(line->backsector != nullptr && "Cannot create LineOpening with one sided line");

	// TODO This can be smarter. This is just to allow rendering tricks for invisible platforms.
	if (line->frontsector->GetHeightSec() != nullptr || line->backsector->GetHeightSec() != nullptr)
		return false;

	// Closed door check. This check isn't really correct, but is required for some old rendering tricks to work.
	if (line->backsector->GetPlaneTexZ(sector_t::ceiling) <= line->frontsector->GetPlaneTexZ(sector_t::floor) ||
		line->backsector->GetPlaneTexZ(sector_t::floor) >= line->frontsector->GetPlaneTexZ(sector_t::ceiling))
		return true;

	return false;
}

bool RenderBlock::IsBlocked(line_t* line, bool onFrontSide)
{
	if (line->backsector == nullptr)
		return true;

	return onFrontSide ? IsBlocked(line->sidedef[0], line->frontsector, line->backsector) :
		IsBlocked(line->sidedef[1], line->backsector, line->frontsector);
}

bool RenderBlock::IsBlocked(side_t* side, sector_t* frontSector, sector_t* backSector)
{
	return IsBlockedPlanes(side, GetPlane(frontSector, sector_t::floor), GetPlane(backSector, sector_t::floor),
		GetPlane(frontSector, sector_t::ceiling), GetPlane(backSector, sector_t::ceiling));
}

bool RenderBlock::IsBlocked(side_t* side,
	const World::Geometry::Sectors::SectorPlane& frontFloorPlane, const World::Geometry::Sectors::SectorPlane& backFloorPlane,
	const World::Geometry::Sectors::SectorPlane& frontCeilingPlane, const World::Geometry::Sectors::SectorPlane& backCeilingPlane)
{
	return IsBlockedPlanes(side, GetPlane(frontFloorPlane), GetPlane(backFloorPlane),
		GetPlane(frontCeilingPlane), GetPlane(backCeilingPlane));
}

}
