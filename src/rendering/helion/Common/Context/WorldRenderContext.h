/*
** WorldRenderContext.h
**
** Port of Helion.Render.Common.Context.WorldRenderContext.
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

#include "Common/World/Camera.h"
#include "Util/Geometry.h"

namespace Helion::Render::Common::Context
{

class WorldRenderContext
{
public:
	using WorldCamera = Common::World::Camera;

	WorldCamera* Camera;
	// UZDoom: the projection of the viewpoint (RenderInfo::ProjectionMatrix); null = Helion's fixed projection.
	const Geometry::mat4* Projection = nullptr;
	// Level->info->pixelstretch (height scale of the view matrix).
	float PixelStretch = 1.0f;
	float InterpolationFrac;
	Geometry::Dimension Viewport = Geometry::Dimension(640, 480);
	bool DrawAutomap = false;
	Geometry::Vec2I AutomapOffset = Geometry::Vec2I(0, 0);
	double AutomapScale = 1.0;

	WorldRenderContext(WorldCamera* camera, float interpolationFrac);

	void Set(float interpolationFrac, bool drawAutomap, const Geometry::Vec2I& automapOffset, double automapScale);
};

}
