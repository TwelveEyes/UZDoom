/*
** StaticDataApplier.h
**
** Port of Helion.World.Static.StaticDataApplier (the world-side half: dynamic
** flag bookkeeping and render blockmap linking). Flood-fill side flags belong to
** the renderer and are not handled here.
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

#include <cstdint>
#include "tarray.h"

struct sector_t;
struct side_t;

namespace Helion::World::Static
{

// Plane mask for SetSectorDynamic (Helion SectorPlanes).
enum SectorPlanes : int
{
	SectorPlanes_Floor = 1,
	SectorPlanes_Ceiling = 2,
	SectorPlanes_Both = 3,
};

class StaticDataApplier
{
public:
	// Helion Sector.IsMoving: either plane carries the Movement flag.
	static bool IsMoving(const sector_t* sector);

	// Sets the ESectorDynamic bits on the given planes and applies the side flags / blockmap links
	// that go with them (SD_Movement, SD_TransferHeights, SD_Scroll).
	static void SetSectorDynamic(sector_t* sector, int planes, uint8_t sectorDynamic);

	// Clears SD_Movement on one plane (sector_t::floor / sector_t::ceiling) once the mover is done.
	static void ClearSectorDynamicMovement(sector_t* sector, int pos);

	// Scrolling wall: SD_Scroll (+ SD_ScrollY) on the side and link into the render blockmap.
	static void SetSideScroll(side_t* side, bool scrollY);

	// Polyobject sides are always dynamic; relinked whenever the polyobject moves or rotates.
	static void LinkPolyobjectSides(const TArray<side_t*>& sides);
	static void UnlinkPolyobjectSides(const TArray<side_t*>& sides);

private:
	static bool IsAffectedByMovement(const sector_t* sector);
	static void SetSectorSidesMovement(sector_t* sector);
	static void RefreshMovementLinks(sector_t* sector);
};

}
