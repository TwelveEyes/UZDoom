/*
** ScissorCommand.h
**
** Port of Helion.Render.OpenGL.Commands.Types.ScissorCommand.
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

enum class ScissorEnable
{
	KeepState,
	Disable,
	Enable,
};

struct ScissorCommand
{
	Geometry::Box2I Box;
	ScissorEnable Enable;
};

}
