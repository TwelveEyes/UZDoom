/*
** RenderDataStyle.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Data.RenderDataStyle.
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

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Data
{

enum class RenderDataStyle
{
	Normal = 0,
	Fuzzy,
	Translucent,
	Add,
	ColorAdd,
	FogBarrier,
	Count
};

}
