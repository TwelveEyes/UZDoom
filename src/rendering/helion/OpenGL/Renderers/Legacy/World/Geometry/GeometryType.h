/*
** GeometryType.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.GeometryType.
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

#include "OpenGL/Renderers/Legacy/World/Data/RenderDataStyle.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry
{

enum class GeometryType
{
	Wall,
	TwoSidedMiddleWall,
	Middle3D,
	Flat,

	Fuzzy,
	Translucent,
	TranslucentAdd,
	TranslucentColorAdd,
	FogBarrier,
	Count
};

// GeometryTypeExtensions.NeedsCoverWall
inline bool NeedsCoverWall(GeometryType style)
{
	return (int)style < (int)GeometryType::Fuzzy && style != GeometryType::TwoSidedMiddleWall;
}

// RenderDataStyleExtensions.ToGeometryType
inline GeometryType ToGeometryType(Data::RenderDataStyle style)
{
	return (GeometryType)((int)style + ((int)GeometryType::Fuzzy - 1));
}

}
