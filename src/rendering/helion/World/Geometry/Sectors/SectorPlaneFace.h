/*
** SectorPlaneFace.h
**
** Port of Helion.World.Geometry.Sectors.SectorPlaneFace/SectorPlanes.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** SectorPlaneFace values equal sector_t::floor / sector_t::ceiling.
*/

#pragma once

namespace Helion::World::Geometry::Sectors
{

enum class SectorPlaneFace
{
	Floor,
	Ceiling,
};

enum class SectorPlanes
{
	None = 0,
	Floor = 1,
	Ceiling = 2
};

constexpr SectorPlanes operator|(SectorPlanes a, SectorPlanes b) { return (SectorPlanes)((int)a | (int)b); }
constexpr SectorPlanes operator&(SectorPlanes a, SectorPlanes b) { return (SectorPlanes)((int)a & (int)b); }

}
