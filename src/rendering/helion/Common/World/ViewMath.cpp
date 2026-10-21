/*
** ViewMath.cpp
**
** Port of Helion.Render.Common.World.ViewMath.
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

#include "Common/World/ViewMath.h"

namespace Helion::Render::Common::World
{

using namespace Helion::Geometry;

mat4 ViewMath::Mvp(const Context::WorldRenderContext& renderInfo)
{
	float w = (float)renderInfo.Viewport.Width;
	float h = (float)renderInfo.Viewport.Height * 0.825f;
	// VSMatrix::perspective takes the vertical field of view in degrees and builds the same
	// matrix as GlmSharp's mat4.PerspectiveFov(fovY, w, h, zNear, zFar).
	float fovY = 63.2f;

	// TODO: Propagate an Entity in here.
	float zNear = 4.0f;
	// float zNear = (float)((renderInfo.ViewerEntity.LowestCeilingZ - renderInfo.ViewerEntity.HighestFloorZ - renderInfo.ViewerEntity.ViewZ) * 0.68);
	// zNear = MathHelper.Clamp(zNear, 0.5f, 7.9f);

	mat4 view = renderInfo.Camera->ViewMatrix(renderInfo.InterpolationFrac, renderInfo.PixelStretch);

	mat4 projection;
	if (renderInfo.Projection != nullptr)
		projection = *renderInfo.Projection;
	else
		projection.perspective(fovY, w / h, zNear, 65536.0f);
	projection.multMatrix(view);
	return projection;
}

}
