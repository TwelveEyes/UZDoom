/*
** ViewportCommand.h
**
** Port of Helion.Render.OpenGL.Commands.Types.ViewportCommand.
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

namespace Helion::Render::OpenGL::Commands::Types
{

struct ViewportCommand
{
	Geometry::Dimension Dimension;
	Geometry::Vec2I Offset;

	ViewportCommand(Geometry::Dimension dimension, Geometry::Vec2I offset) : Dimension(dimension), Offset(offset)
	{
	}
};

}
