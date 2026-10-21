/*
** GeometryTypeLookup.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.GeometryTypeLookup.
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

#include <functional>
#include <vector>
#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryType.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

template<typename T>
class GeometryTypeLookup
{
public:
	explicit GeometryTypeLookup(const std::function<T()>& allocator)
	{
		m_items.reserve((size_t)Geometry::GeometryType::Count);
		for (int i = 0; i < (int)Geometry::GeometryType::Count; i++)
			m_items.push_back(allocator());
	}

	T& Get(Geometry::GeometryType type) { return m_items[(int)type]; }

	std::vector<T>& GetItems() { return m_items; }

private:
	std::vector<T> m_items;
};

}
