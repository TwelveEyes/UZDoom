/*
** PrimitiveRenderType.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Primitives.PrimitiveRenderType.
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

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Primitives
{

enum class PrimitiveRenderType
{
	Line,
	Rail,
	Count	// Enum.GetValues<PrimitiveRenderType>().Length
};

}
