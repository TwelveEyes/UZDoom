/*
** RenderGeometry.cpp
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
*/

#include "RenderGeometry.h"

#include <cstring>

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static
{

void RenderGeometry::ClearVbo()
{
	for (GeometryList& list : m_lookup.GetItems())
	{
		for (std::unique_ptr<GeometryData>& item : list)
		{
			auto& vbo = *item->Vbo;
			// ZeroArray: the whole allocation, so later sub uploads of cleared ranges are zero.
			memset(vbo.Data.Data(), 0, (size_t)vbo.Data.Max() * sizeof(StaticVertex));
			vbo.Clear();
		}
	}
}

void RenderGeometry::DisposeAndClear()
{
	for (GeometryList& list : m_lookup.GetItems())
	{
		for (std::unique_ptr<GeometryData>& data : list)
			data->Dispose();

		list.clear();
	}
}

}
