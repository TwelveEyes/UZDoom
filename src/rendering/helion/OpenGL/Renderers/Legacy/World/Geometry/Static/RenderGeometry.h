/*
** RenderGeometry.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Static.RenderGeometry.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** The lists own their GeometryData; every other reference to a GeometryData (texture lookup,
** StaticGeometryData of walls and planes) is a plain pointer, like the object references of the C# version.
**
*/

#pragma once

#include <memory>
#include <vector>
#include "GeometryData.h"
#include "OpenGL/Renderers/Legacy/World/GeometryTypeLookup.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryType.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static
{

class RenderGeometry
{
public:
	using GeometryList = std::vector<std::unique_ptr<GeometryData>>;

	RenderGeometry() : m_lookup([] { return GeometryList(); }) { }

	void AddGeometry(GeometryType type, std::unique_ptr<GeometryData> data)
	{
		m_lookup.Get(type).push_back(std::move(data));
	}

	GeometryList& GetGeometry(GeometryType type)
	{
		return m_lookup.Get(type);
	}

	auto& GetAllGeometry()
	{
		return m_lookup.GetItems();
	}

	void ClearVbo();
	void DisposeAndClear();

private:
	GeometryTypeLookup<GeometryList> m_lookup;
};

}
