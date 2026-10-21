/*
** WallTriangulation.h
**
** Port of Helion.Render.Common.World.Triangulation.WallTriangulation.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion's Wall parameter becomes (side_t*, WallLocation): see WallLocation.h.
*/

#pragma once

#include "Common/World/Triangulation/WallVertex.h"
#include "World/Geometry/Walls/WallLocation.h"

struct side_t;

namespace Helion::Render::Common::World::Triangulation
{

struct WallTriangulation
{
	WallVertex TopLeft;
	WallVertex TopRight;
	WallVertex BottomLeft;
	WallVertex BottomRight;

	WallTriangulation() = default;
	WallTriangulation(const WallVertex& topLeft, const WallVertex& topRight, const WallVertex& bottomLeft, const WallVertex& bottomRight) :
		TopLeft(topLeft), TopRight(topRight), BottomLeft(bottomLeft), BottomRight(bottomRight)
	{
	}

private:
	static WallTriangulation FromTwoSided(side_t* side, ::Helion::World::Geometry::Walls::WallLocation location);
	static WallTriangulation FromTwoSidedLower(side_t* side);
	static WallTriangulation FromTwoSidedUpper(side_t* side);
	static WallTriangulation FromTwoSidedMiddle(side_t* side);
};

}
