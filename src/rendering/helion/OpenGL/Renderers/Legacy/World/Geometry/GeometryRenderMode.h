/*
** GeometryRenderMode.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.GeometryRenderMode.
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

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry
{

enum class GeometryRenderMode
{
	// Only render dynamic geometry (moving sectors, scrolling floors etc). Default.
	Dynamic,
	// Render all geometry even if it's static.
	All
};

}
