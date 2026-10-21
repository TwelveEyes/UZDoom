/*
** RenderStaticState.cpp
**
** UZDoom-only. See RenderStaticState.h.
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

#include "RenderStaticState.h"

#include "g_levellocals.h"

namespace Helion::World::Static
{

void RenderStaticState::Reset(FLevelLocals* level)
{
	s_level = level;
	s_sides.clear();
	s_sectors.clear();
	s_sides.resize(level->sides.Size());
	s_sectors.resize(level->sectors.Size());
	s_planes.clear();
	s_planes.resize(level->sectors.Size() * 2);
	s_walls3D.clear();
	s_subsectorFlood.clear();
	s_subsectorFlood.resize(level->subsectors.Size());
}

void RenderStaticState::Clear()
{
	s_level = nullptr;
	s_sides.clear();
	s_sectors.clear();
	s_planes.clear();
	s_walls3D.clear();
	s_subsectorFlood.clear();
}

RenderPlaneState& RenderStaticState::Plane(int planeId)
{
	if ((size_t)planeId >= s_planes.size())
		s_planes.resize((size_t)planeId + 1 + s_planes.size() / 2);
	return s_planes[(size_t)planeId];
}

StaticGeometryData& RenderStaticState::Wall3D(int sector3DId, const side_t* side, int wallLocation)
{
	uint64_t key = ((uint64_t)(uint32_t)sector3DId << 34) ^ ((uint64_t)(uint32_t)side->Index() << 4) ^ (uint64_t)wallLocation;
	return s_walls3D[key];
}

RenderSideState& RenderStaticState::Side(const side_t* side)
{
	return s_sides[(size_t)side->Index()];
}

RenderSectorState& RenderStaticState::Sector(const sector_t* sector)
{
	return s_sectors[(size_t)sector->Index()];
}

RenderSectorState& RenderStaticState::SectorById(int id)
{
	if ((size_t)id >= s_sectors.size())
		s_sectors.resize((size_t)id + 1 + s_sectors.size() / 2);
	return s_sectors[(size_t)id];
}

int RenderStaticState::GameTick()
{
	return s_level != nullptr ? s_level->time : 0;
}

}
