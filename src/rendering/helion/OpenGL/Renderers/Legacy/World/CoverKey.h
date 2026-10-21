/*
** CoverKey.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.CoverKey.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** WallLocation / SectorPlaneFace come from World/Geometry (the WallLocation.h and SectorPlaneFace.h ports),
** a hash functor is provided for unordered containers.
*/

#pragma once

#include <cstddef>
#include <functional>
#include "World/Geometry/Sectors/SectorPlaneFace.h"
#include "World/Geometry/Walls/WallLocation.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

struct CoverKey
{
	int Key1;
	int Key2;

	constexpr CoverKey(int key1, int key2) : Key1(key1), Key2(key2) { }

	static CoverKey MakeCoverWallKey(int sideId, ::Helion::World::Geometry::Walls::WallLocation location, bool oneSided)
	{
		return CoverKey(sideId, ((int)location << 1) | (oneSided ? 1 : 0));
	}

	static CoverKey MakeFlatKey(int sectorId, ::Helion::World::Geometry::Sectors::SectorPlaneFace plane)
	{
		return CoverKey(sectorId, (int)plane);
	}

	int GetHashCode() const
	{
		return Key1 + Key2 * 131072;
	}

	bool operator==(const CoverKey& other) const
	{
		return other.Key1 == Key1 && other.Key2 == Key2;
	}
};

struct CoverKeyHash
{
	size_t operator()(const CoverKey& key) const { return (size_t)key.GetHashCode(); }
};

}
