/*
** RenderBlockMapData.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.RenderBlockMapData.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Island (the connected sector group of a sector with its bounding box) has no UZDoom counterpart: the
** blockmap walk checks the FSection bounds of each dynamic sector instead (LegacyWorldRenderer::RenderSectors),
** so ViewIsland is dropped.
*/

#pragma once

#include <optional>
#include "Util/Geometry.h"

class AActor;

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

struct RenderBlockMapData
{
	AActor* ViewerEntity = nullptr;
	std::optional<::Helion::Geometry::Vec2D> OccludePos;
	::Helion::Geometry::Vec2D ViewPosInterpolated = ::Helion::Geometry::Vec2D(0, 0);
	::Helion::Geometry::Vec2D ViewDirection = ::Helion::Geometry::Vec2D(0, 0);
	::Helion::Geometry::Vec3D ViewPosInterpolated3D = ::Helion::Geometry::Vec3D(0, 0, 0);
	::Helion::Geometry::Vec3D ViewPos3D = ::Helion::Geometry::Vec3D(0, 0, 0);
	int CheckCount = 0;
	int MaxDistance = 0;
	// Helion: int. UZDoom's whole-map default (65536, Renderer.cpp) squared overflows int, so this is a double.
	double MaxDistanceSquared = 0;
};

}
