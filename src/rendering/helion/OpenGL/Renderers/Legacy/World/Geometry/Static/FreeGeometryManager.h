/*
** FreeGeometryManager.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Static.FreeGeometryManager.
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
#include <memory>
#include <optional>
#include <unordered_map>
#include "FreeGeometryData.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryType.h"
#include "OpenGL/Texture/IArrayTextureLookup.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static
{

struct GeometryKey
{
	int TextureHandle;
	GeometryType Type;
	bool RepeatY;

	GeometryKey(int textureHandle, GeometryType type, bool repeatY) : TextureHandle(textureHandle), Type(type), RepeatY(repeatY) { }

	bool operator==(const GeometryKey& other) const
	{
		return TextureHandle == other.TextureHandle && Type == other.Type && RepeatY == other.RepeatY;
	}
};

struct GeometryKeyHash
{
	size_t operator()(const GeometryKey& key) const
	{
		size_t hash = std::hash<int>()(key.TextureHandle);
		hash = hash * 31 + std::hash<int>()((int)key.Type);
		hash = hash * 31 + std::hash<bool>()(key.RepeatY);
		return hash;
	}
};

class FreeGeometryManager
{
public:
	explicit FreeGeometryManager(Texture::IArrayTextureLookup& arrayTextureLookup) : m_arrayTextureLookup(arrayTextureLookup) { }

	void Add(const ::Helion::World::Static::StaticGeometryData& geometryData, GeometryType type, bool repeatY);

	// Takes the smallest released block that holds at least vertexLength vertices.
	bool GetAndRemove(int textureHandle, GeometryType type, bool repeatY, int vertexLength, ::Helion::World::Static::StaticGeometryData& data);

	void Clear();

private:
	Texture::IArrayTextureLookup& m_arrayTextureLookup;
	std::unordered_map<GeometryKey, std::unique_ptr<FreeGeometryList>, GeometryKeyHash> m_data;

	static void AssertDuplicate(const FreeGeometryList& list, const ::Helion::World::Static::StaticGeometryData& geometryData);
};

}
