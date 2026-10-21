/*
** WallTriangulation.cpp
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
*/

#include "Common/World/Triangulation/WallTriangulation.h"
#include "engineerrors.h"

namespace Helion::Render::Common::World::Triangulation
{

using ::Helion::World::Geometry::Walls::WallLocation;

WallTriangulation WallTriangulation::FromTwoSided(side_t* side, WallLocation location)
{
	switch (location)
	{
	case WallLocation::Upper:
		return FromTwoSidedUpper(side);
	case WallLocation::Middle:
		return FromTwoSidedMiddle(side);
	case WallLocation::Lower:
		return FromTwoSidedLower(side);
	default:
		I_Error("Unsupported wall location type when triangulating: %d", (int)location);
	}
}

// Helion leaves these three as stubs that return default(WallTriangulation).
WallTriangulation WallTriangulation::FromTwoSidedLower(side_t* side)
{
	return {};
}

WallTriangulation WallTriangulation::FromTwoSidedUpper(side_t* side)
{
	return {};
}

WallTriangulation WallTriangulation::FromTwoSidedMiddle(side_t* side)
{
	return {};
}

}
