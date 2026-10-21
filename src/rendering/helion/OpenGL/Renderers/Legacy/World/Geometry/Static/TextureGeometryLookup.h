/*
** TextureGeometryLookup.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Static.TextureGeometryLookup.
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

#include <unordered_map>

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static
{

class GeometryData;

class TextureGeometryLookup
{
public:
	void Clear()
	{
		m_textureToGeometryLookup.clear();
		m_textureToGeometryLookupClamp.clear();
	}

	void Add(int textureHandle, bool repeatY, GeometryData* data)
	{
		GetLookup(repeatY)[textureHandle] = data;
	}

	bool TryGetValue(int textureHandle, bool repeatY, GeometryData*& value)
	{
		auto& lookup = GetLookup(repeatY);
		auto it = lookup.find(textureHandle);
		if (it == lookup.end())
			return false;

		value = it->second;
		return true;
	}

private:
	std::unordered_map<int, GeometryData*> m_textureToGeometryLookup;
	std::unordered_map<int, GeometryData*> m_textureToGeometryLookupClamp;

	std::unordered_map<int, GeometryData*>& GetLookup(bool repeatY)
	{
		return repeatY ? m_textureToGeometryLookup : m_textureToGeometryLookupClamp;
	}
};

}
