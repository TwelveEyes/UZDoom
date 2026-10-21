/*
** SkyProjection.h
**
** UZDoom-only: the projection of the sky sphere / box (Helion Renderer.GetFieldOfViewInfo).
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Helion builds the sky projection from the same field of view as the world projection: RenderInfo supplies it.
**
*/

#pragma once

#include "Common/Shared/RenderInfo.h"
#include "Util/Geometry.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky
{

// The projection of the world view (stereo / ortho included) with the clip planes of the sky geometry.
inline ::Helion::Geometry::mat4 SkyProjection(const Shared::RenderInfo& renderInfo, float zNear, float zFar)
{
	return renderInfo.ProjectionMatrix(zNear, zFar);
}

}
