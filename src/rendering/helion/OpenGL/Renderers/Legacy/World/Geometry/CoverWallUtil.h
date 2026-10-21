/*
** CoverWallUtil.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.CoverWallUtil.
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

#include <span>
#include "OpenGL/Renderers/Legacy/World/DynamicVertex.h"
#include "OpenGL/Renderers/Legacy/World/StaticVertex.h"
#include "World/Geometry/Walls/WallLocation.h"

struct side_t;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry
{

class CoverWallUtil
{
public:
	// vertices + index points at the 6 vertices of the wall.
	static void SetCoverWallVertices(side_t* side, DynamicVertex* vertices, int index, Helion::World::Geometry::Walls::WallLocation location);
	// staticVertices + index receives the 6 vertices.
	static void CopyCoverWallVertices(side_t* side, StaticVertex* staticVertices, std::span<const DynamicVertex> vertices, int index,
		Helion::World::Geometry::Walls::WallLocation location);
};

}
