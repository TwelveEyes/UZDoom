/*
** WallTextureMapper.h
**
** Port of Helion.Render.Common.World.Triangulation.WallTextureMapper.
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

#include "Common/Textures/IRenderableTextureHandle.h"
#include "Util/Geometry.h"

struct line_t;
struct side_t;

namespace Helion::Render::Common::World::Triangulation
{

/// <summary>
/// A helper to take texture geometry and turn it into UV coordinates.
/// </summary>
class WallTextureMapper
{
public:
	static Geometry::Box2F OneSidedWallUV(line_t* line, side_t* side, float length, float spanZ,
		const Textures::IRenderableTextureHandle& textureHandle);
};

}
