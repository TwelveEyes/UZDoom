/*
** StaticGeometryData.h
**
** Port of Helion.World.Static.StaticGeometryData.
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

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static
{
class GeometryData;
}

namespace Helion::World::Static
{

// Where the vertices of a wall / plane live in the static geometry VBO of its texture.
struct StaticGeometryData
{
	::Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static::GeometryData* GeometryData = nullptr;
	int Index = 0;
	int Length = 0;

	StaticGeometryData() = default;
	StaticGeometryData(::Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static::GeometryData* geometryData, int index, int length)
		: GeometryData(geometryData), Index(index), Length(length)
	{
	}
};

}
