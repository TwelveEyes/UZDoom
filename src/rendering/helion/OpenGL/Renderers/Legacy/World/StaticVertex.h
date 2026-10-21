/*
** StaticVertex.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.StaticVertex.
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

#include <span>
#include "OpenGL/Vertex/VertexAttributes.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World
{

// C# StructLayout(LayoutKind.Sequential, Pack = 1)
#pragma pack(push, 1)
struct StaticVertex
{
	// [VertexAttribute("pos", size: 3)]
	float X;
	float Y;
	float Z;

	// [VertexAttribute("uv", size: 2)]
	float U;
	float V;

	// [VertexAttribute]
	float LightLevelAdd;

	// [VertexAttribute]
	float SurfaceOptions;

	// [VertexAttribute]
	float RenderOptions;

	// UZDoom-only (plan M3.2, lightmaps): atlas texture coordinates and LightMap array layer (< 0 = no lightmap),
	// filled from side_t::lightmap / subsector_t::lightmap like hw_walls.cpp.
	// [VertexAttribute("lightmapUV", size: 2)]
	float LightmapU = 0;
	float LightmapV = 0;

	// [VertexAttribute]
	float LightmapIndex = -1;

	StaticVertex() = default;
	StaticVertex(float x, float y, float z, float u, float v, float surfaceOptions, float lightLevelAdd, float renderOptions)
		: X(x), Y(y), Z(z), U(u), V(v), LightLevelAdd(lightLevelAdd), SurfaceOptions(surfaceOptions), RenderOptions(renderOptions)
	{
	}

	// Helion reflects over the [VertexAttribute] fields; C++ lists them here, in field order.
	static std::span<const Vertex::VertexAttributeAttribute> VertexAttributes()
	{
		static constexpr Vertex::VertexAttributeAttribute attributes[] =
		{
			{ "pos", Vertex::VertexAttributeAttribute::InferFromField, 3 },
			{ "uv", Vertex::VertexAttributeAttribute::InferFromField, 2 },
			{ "lightLevelAdd" },
			{ "surfaceOptions" },
			{ "renderOptions" },
			{ "lightmapUV", Vertex::VertexAttributeAttribute::InferFromField, 2 },
			{ "lightmapIndex" },
		};
		return attributes;
	}
};
#pragma pack(pop)

static_assert(sizeof(StaticVertex) == 11 * sizeof(float), "StaticVertex has to stay tightly packed");

}
