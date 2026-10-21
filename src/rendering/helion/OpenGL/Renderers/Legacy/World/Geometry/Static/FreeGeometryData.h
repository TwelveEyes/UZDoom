/*
** FreeGeometryData.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Static.FreeGeometryData and FreeGeometryList.
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

#include <vector>
#include "World/Static/StaticGeometryData.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static
{

struct FreeGeometryData
{
	int TextureHandle = 0;
	::Helion::World::Static::StaticGeometryData Geometry;
	bool Released = false;

	FreeGeometryData() = default;
	FreeGeometryData(int textureHandle, const ::Helion::World::Static::StaticGeometryData& geometryData)
		: TextureHandle(textureHandle), Geometry(geometryData)
	{
	}
};

class FreeGeometryList
{
public:
	std::vector<FreeGeometryData> Geometry;
	int LastReleasedIndex = -1;

	FreeGeometryList() { Geometry.reserve(32); }
};

}
