/*
** WallLocation.h
**
** Port of Helion.World.Geometry.Walls.WallLocation.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** UZDoom has no Wall object; a wall is a side_t plus one of side_t::top,
** side_t::mid, side_t::bottom. Upper/Middle/Lower correspond to those.
*/

#pragma once

namespace Helion::World::Geometry::Walls
{

enum class WallLocation
{
	None,
	Upper,
	Middle,
	Lower,
	Middle3D
};

}
