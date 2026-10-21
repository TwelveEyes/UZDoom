/*
** StaticDataApplier.cpp
**
** Port of Helion.World.Static.StaticDataApplier (world-side half).
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

#include "StaticDataApplier.h"

#include "g_levellocals.h"
#include "p_3dfloors.h"
#include "rendering/helion/World/Blockmap/RenderBlockMap.h"

namespace Helion::World::Static
{

static Blockmap::RenderBlockMap* GetRenderBlockmap(const sector_t* sector)
{
	return sector->Level != nullptr ? sector->Level->renderBlockmap : nullptr;
}

bool StaticDataApplier::IsMoving(const sector_t* sector)
{
	return ((sector->renderDynamic[sector_t::floor] | sector->renderDynamic[sector_t::ceiling]) & SD_Movement) != 0;
}

// A sector is also affected while the control sector of one of its 3D floors moves.
bool StaticDataApplier::IsAffectedByMovement(const sector_t* sector)
{
	if (IsMoving(sector))
		return true;

	for (const F3DFloor* ffloor : sector->e->XFloor.ffloors)
	{
		if (ffloor->model != nullptr && IsMoving(ffloor->model))
			return true;
	}
	return false;
}

void StaticDataApplier::SetSectorSidesMovement(sector_t* sector)
{
	for (unsigned i = 0; i < sector->Lines.Size(); i++)
	{
		line_t* line = sector->Lines[i];
		if (line->sidedef[1] != nullptr)
			line->sidedef[1]->renderDynamic |= SD_Movement;
		if (line->sidedef[0] != nullptr)
			line->sidedef[0]->renderDynamic |= SD_Movement;
	}
}

void StaticDataApplier::SetSectorDynamic(sector_t* sector, int planes, uint8_t sectorDynamic)
{
	if (planes & SectorPlanes_Floor)
		sector->renderDynamic[sector_t::floor] |= sectorDynamic;
	if (planes & SectorPlanes_Ceiling)
		sector->renderDynamic[sector_t::ceiling] |= sectorDynamic;

	Blockmap::RenderBlockMap* blockmap = GetRenderBlockmap(sector);
	if (blockmap != nullptr && (sectorDynamic & (SD_Movement | SD_Scroll | SD_Portal)) != 0)
		blockmap->LinkDynamic(sector);

	if (sectorDynamic & SD_Movement)
	{
		SetSectorSidesMovement(sector);

		// A moving control sector changes every sector one of its 3D floors is attached to.
		for (sector_t* target : sector->e->XFloor.attached)
		{
			if (blockmap != nullptr)
				blockmap->LinkDynamic(target);
			SetSectorSidesMovement(target);
		}
	}

	if (sectorDynamic & SD_TransferHeights)
	{
		for (unsigned i = 0; i < sector->Lines.Size(); i++)
		{
			line_t* line = sector->Lines[i];
			for (side_t* side : line->sidedef)
			{
				if (side != nullptr && side->sector == sector)
					side->renderDynamic |= SD_TransferHeights;
			}
		}
	}
}

void StaticDataApplier::RefreshMovementLinks(sector_t* sector)
{
	if (IsAffectedByMovement(sector))
		return;

	// Floor and ceiling can scroll independently of moving; scrolling and portal sectors stay linked.
	const bool scrolling = ((sector->renderDynamic[sector_t::floor] | sector->renderDynamic[sector_t::ceiling]) & (SD_Scroll | SD_Portal)) != 0;
	Blockmap::RenderBlockMap* blockmap = GetRenderBlockmap(sector);
	if (!scrolling && blockmap != nullptr)
		blockmap->UnlinkDynamic(sector);

	for (unsigned i = 0; i < sector->Lines.Size(); i++)
	{
		line_t* line = sector->Lines[i];
		if (line->frontsector != nullptr && IsAffectedByMovement(line->frontsector))
			continue;
		if (line->backsector != nullptr && IsAffectedByMovement(line->backsector))
			continue;

		for (side_t* side : line->sidedef)
		{
			if (side != nullptr)
				side->renderDynamic &= ~SD_Movement;
		}
	}
}

void StaticDataApplier::ClearSectorDynamicMovement(sector_t* sector, int pos)
{
	sector->renderDynamic[pos] &= ~SD_Movement;

	// Floor and ceiling can move independently so don't clear it yet.
	if (IsMoving(sector) || (sector->renderDynamic[pos] & SD_TransferHeights) != 0)
		return;

	RefreshMovementLinks(sector);
	for (sector_t* target : sector->e->XFloor.attached)
		RefreshMovementLinks(target);
}

void StaticDataApplier::SetSideScroll(side_t* side, bool scrollY)
{
	side->renderDynamic |= SD_Scroll;
	if (scrollY)
		side->renderDynamic |= SD_ScrollY;

	Blockmap::RenderBlockMap* blockmap = side->GetLevel()->renderBlockmap;
	if (blockmap != nullptr)
		blockmap->LinkDynamicSide(side);
}

void StaticDataApplier::LinkPolyobjectSides(const TArray<side_t*>& sides)
{
	for (side_t* side : sides)
	{
		side->renderDynamic |= SD_Polyobject;
		Blockmap::RenderBlockMap* blockmap = side->GetLevel()->renderBlockmap;
		if (blockmap != nullptr)
			blockmap->LinkDynamicSide(side);
	}
}

void StaticDataApplier::UnlinkPolyobjectSides(const TArray<side_t*>& sides)
{
	for (side_t* side : sides)
	{
		Blockmap::RenderBlockMap* blockmap = side->GetLevel()->renderBlockmap;
		if (blockmap != nullptr)
			blockmap->UnlinkDynamicSide(side);
	}
}

}
