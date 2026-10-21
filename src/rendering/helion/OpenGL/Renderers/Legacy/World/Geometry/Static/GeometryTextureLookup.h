/*
** GeometryTextureLookup.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Static.GeometryTextureLookup.
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

#include "TextureGeometryLookup.h"
#include "OpenGL/Renderers/Legacy/World/GeometryTypeLookup.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryType.h"
#include "OpenGL/Texture/IArrayTextureLookup.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static
{

class GeometryTextureLookup
{
public:
	explicit GeometryTextureLookup(Texture::IArrayTextureLookup& arrayTextureLookup)
		: m_lookup([] { return TextureGeometryLookup(); }), m_arrayTextureLookup(arrayTextureLookup)
	{
	}

	void Clear()
	{
		for (TextureGeometryLookup& item : m_lookup.GetItems())
			item.Clear();
	}

	bool TryGetValue(GeometryType type, int textureHandle, bool repeatY, GeometryData*& value)
	{
		textureHandle = m_arrayTextureLookup.GetWorldArrayTextureHandle(textureHandle, repeatY);
		return m_lookup.Get(type).TryGetValue(textureHandle, repeatY, value);
	}

	void Add(GeometryType type, int textureHandle, bool repeatY, GeometryData* data)
	{
		textureHandle = m_arrayTextureLookup.GetWorldArrayTextureHandle(textureHandle, repeatY);
		m_lookup.Get(type).Add(textureHandle, repeatY, data);
	}

private:
	GeometryTypeLookup<TextureGeometryLookup> m_lookup;
	Texture::IArrayTextureLookup& m_arrayTextureLookup;
};

}
