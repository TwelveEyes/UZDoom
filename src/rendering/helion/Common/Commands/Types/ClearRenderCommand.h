/*
** ClearRenderCommand.h
**
** Port of Helion.Render.OpenGL.Commands.Types.ClearRenderCommand.
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

struct ClearRenderCommand
{
	static inline const Geometry::Color DefaultClearColor = Geometry::Color(0, 0, 0, 0);

	bool Color;
	bool Depth;
	bool Stencil;
	Geometry::Color ClearColor;

	ClearRenderCommand(bool color, bool depth, bool stencil, Geometry::Color clearColor) :
		Color(color),
		Depth(depth),
		Stencil(stencil),
		ClearColor(clearColor)
	{
	}

	static ClearRenderCommand All() { return All(Geometry::Color(16, 16, 16)); }

	static ClearRenderCommand All(Geometry::Color clearColor)
	{
		return ClearRenderCommand(true, true, true, clearColor);
	}

	static ClearRenderCommand DepthOnly()
	{
		return ClearRenderCommand(false, true, false, DefaultClearColor);
	}
};

}
