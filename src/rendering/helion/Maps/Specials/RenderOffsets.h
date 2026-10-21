/*
** RenderOffsets.h
**
** Port of Helion.Maps.Specials.RenderOffsets.
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

namespace Helion::Maps::Specials
{

struct RenderOffsets
{
	int Gametick = 0;
	::Helion::Geometry::Vec2D Offset = ::Helion::Geometry::Vec2D(0, 0);
	::Helion::Geometry::Vec2D LastOffset = ::Helion::Geometry::Vec2D(0, 0);
	::Helion::Geometry::Vec2D Scale = ::Helion::Geometry::Vec2D(0, 0);
	double Rotate = 0;
};

}
