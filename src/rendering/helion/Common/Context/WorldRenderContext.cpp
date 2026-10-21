/*
** WorldRenderContext.cpp
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

#include "Common/Context/WorldRenderContext.h"

namespace Helion::Render::Common::Context
{

WorldRenderContext::WorldRenderContext(WorldCamera* camera, float interpolationFrac) :
	Camera(camera),
	InterpolationFrac(interpolationFrac)
{
}

void WorldRenderContext::Set(float interpolationFrac, bool drawAutomap, const Geometry::Vec2I& automapOffset, double automapScale)
{
	InterpolationFrac = interpolationFrac;
	DrawAutomap = drawAutomap;
	AutomapOffset = automapOffset;
	AutomapScale = automapScale;
}

}
