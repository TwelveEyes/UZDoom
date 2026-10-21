/*
** FreeGeometryManager.cpp
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

#include "FreeGeometryManager.h"

#include <cassert>
#include <climits>
#include "GeometryData.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static
{

using ::Helion::World::Static::StaticGeometryData;

void FreeGeometryManager::Add(const StaticGeometryData& geometryData, GeometryType type, bool repeatY)
{
	if (geometryData.GeometryData == nullptr)
		return;

	int textureHandle = m_arrayTextureLookup.GetWorldArrayTextureHandle(geometryData.GeometryData->TextureHandle, repeatY);
	GeometryKey key(textureHandle, type, repeatY);

	std::unique_ptr<FreeGeometryList>& list = m_data[key];
	if (list == nullptr)
		list = std::make_unique<FreeGeometryList>();

	AssertDuplicate(*list, geometryData);

	if (list->LastReleasedIndex != -1)
	{
		list->Geometry[(size_t)list->LastReleasedIndex] = FreeGeometryData(textureHandle, geometryData);
		list->LastReleasedIndex = -1;
		return;
	}

	for (size_t i = 0; i < list->Geometry.size(); i++)
	{
		if (list->Geometry[i].Released)
		{
			list->Geometry[i] = FreeGeometryData(textureHandle, geometryData);
			return;
		}
	}

	list->Geometry.push_back(FreeGeometryData(textureHandle, geometryData));
}

void FreeGeometryManager::AssertDuplicate([[maybe_unused]] const FreeGeometryList& list, [[maybe_unused]] const StaticGeometryData& geometryData)
{
#ifndef NDEBUG
	for (const FreeGeometryData& item : list.Geometry)
	{
		if (item.Released)
			continue;

		assert(item.Geometry.Index != geometryData.Index && "GeometryData already added.");
	}
#endif
}

bool FreeGeometryManager::GetAndRemove(int textureHandle, GeometryType type, bool repeatY, int vertexLength, StaticGeometryData& data)
{
	int minLength = INT_MAX;
	int minIndex = -1;
	textureHandle = m_arrayTextureLookup.GetWorldArrayTextureHandle(textureHandle, repeatY);
	GeometryKey key(textureHandle, type, repeatY);

	auto it = m_data.find(key);
	if (it == m_data.end())
		return false;

	FreeGeometryList& list = *it->second;
	for (size_t i = 0; i < list.Geometry.size(); i++)
	{
		FreeGeometryData& itemData = list.Geometry[i];
		if (itemData.Released)
		{
			if (list.LastReleasedIndex == -1)
				list.LastReleasedIndex = (int)i;
			continue;
		}

		if (itemData.Geometry.Length >= vertexLength && itemData.Geometry.Length < minLength)
		{
			minLength = itemData.Geometry.Length;
			minIndex = (int)i;

			if (itemData.Geometry.Length == vertexLength)
				break;
		}
	}

	if (minIndex == -1)
		return false;

	if (list.LastReleasedIndex == -1)
		list.LastReleasedIndex = minIndex;

	FreeGeometryData& itemData = list.Geometry[(size_t)minIndex];
	data = StaticGeometryData(itemData.Geometry.GeometryData, itemData.Geometry.Index, vertexLength);
	itemData.Released = true;
	itemData.Geometry.Length = 0;
	return true;
}

void FreeGeometryManager::Clear()
{
	m_data.clear();
}

}
