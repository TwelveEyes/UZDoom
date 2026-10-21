/*
** DynamicVertex.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.DynamicVertex.
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
struct DynamicVertex
{
	// [VertexAttribute("pos", size: 3)]
	float X;
	float Y;
	float Z;

	// [VertexAttribute("uv", size: 2)]
	float U;
	float V;

	// [VertexAttribute]
	float SurfaceOptions;

	// [VertexAttribute]
	float LightLevelAdd;

	// [VertexAttribute("prevPos", size: 3)]
	float PrevX;
	float PrevY;
	float PrevZ;

	// [VertexAttribute("prevUV", size: 2)]
	float PrevU;
	float PrevV;

	// [VertexAttribute]
	float RenderOptions;

	// UZDoom-only (plan M3.2, lightmaps): atlas texture coordinates and LightMap array layer (< 0 = no lightmap),
	// filled from side_t::lightmap / subsector_t::lightmap like hw_walls.cpp.
	// [VertexAttribute("lightmapUV", size: 2)]
	float LightmapU = 0;
	float LightmapV = 0;

	// [VertexAttribute]
	float LightmapIndex = -1;

	// UZDoom-only (r_dithertransparency, DitherTransparency.h): 1 = the surface is drawn with the dither pattern. Set on the
	// copy in the frame's vertex buffer, never in the cached side / flat vertices.
	// [VertexAttribute]
	float Dither = 0;

	// Helion reflects over the [VertexAttribute] fields; C++ lists them here, in field order.
	static std::span<const Vertex::VertexAttributeAttribute> VertexAttributes()
	{
		static constexpr Vertex::VertexAttributeAttribute attributes[] =
		{
			{ "pos", Vertex::VertexAttributeAttribute::InferFromField, 3 },
			{ "uv", Vertex::VertexAttributeAttribute::InferFromField, 2 },
			{ "surfaceOptions" },
			{ "lightLevelAdd" },
			{ "prevPos", Vertex::VertexAttributeAttribute::InferFromField, 3 },
			{ "prevUV", Vertex::VertexAttributeAttribute::InferFromField, 2 },
			{ "renderOptions" },
			{ "lightmapUV", Vertex::VertexAttributeAttribute::InferFromField, 2 },
			{ "lightmapIndex" },
			{ "dither" },
		};
		return attributes;
	}
};
#pragma pack(pop)

static_assert(sizeof(DynamicVertex) == 17 * sizeof(float), "DynamicVertex has to stay tightly packed");

}
