/*
** FloodGeometry.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Portals.FloodFill.FloodGeometry.
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

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals::FloodFill
{

struct FloodGeometry
{
	int Key = 0;
	int TextureHandle = 0;
	int ArrayTextureHandle = 0;
	int OverrideLightIndex = 0;
	float RenderOptions = 0;
	int VboOffset = 0;
	int Vertices = 0;

	FloodGeometry() = default;
	FloodGeometry(int key, int textureHandle, int arrayTextureHandle, int overrideLightIndex, float renderOptions, int vboOffset, int vertices)
		: Key(key), TextureHandle(textureHandle), ArrayTextureHandle(arrayTextureHandle), OverrideLightIndex(overrideLightIndex),
		RenderOptions(renderOptions), VboOffset(vboOffset), Vertices(vertices)
	{
	}
};

}
