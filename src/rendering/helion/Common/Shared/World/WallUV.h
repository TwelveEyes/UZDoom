/*
** WallUV.h
**
** Port of Helion.Render.OpenGL.Shared.World.WallUV.
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

namespace Helion::Render::OpenGL::Shared::World
{

struct WallUV
{
	Geometry::Vec2F TopLeft = Geometry::Vec2F(0, 0);
	Geometry::Vec2F BottomRight = Geometry::Vec2F(0, 0);

	WallUV() = default;
	WallUV(const Geometry::Vec2F& topLeft, const Geometry::Vec2F& bottomRight) : TopLeft(topLeft), BottomRight(bottomRight) { }
};

}
