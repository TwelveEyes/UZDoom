/*
** WallVertex.h
**
** Port of Helion.Render.Common.World.Triangulation.WallVertex.
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

#include "Util/Geometry.h"

namespace Helion::Render::Common::World::Triangulation
{

struct WallVertex
{
	Geometry::Vec3F Pos;
	Geometry::Vec2F UV;

	WallVertex() = default;
	WallVertex(const Geometry::Vec3F& pos, const Geometry::Vec2F& uv) : Pos(pos), UV(uv) { }
};

}
