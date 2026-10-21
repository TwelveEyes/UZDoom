/*
** DecalVertex.h
**
** UZDoom-only (plan M4.5). Helion has no decals. A decal is an arbitrary quad on a wall with its own texture;
** everything that is not per vertex (alpha, texture layer, light, object colour) is a uniform of DecalProgram.
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

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry
{

// C# StructLayout(LayoutKind.Sequential, Pack = 1)
#pragma pack(push, 1)
struct DecalVertex
{
	// [VertexAttribute("pos", size: 3)] world position (x, y, z up)
	float X;
	float Y;
	float Z;

	// [VertexAttribute("uv", size: 2)] 0..1 over the decal texture (the program scales it into the array layer)
	float U;
	float V;

	DecalVertex() = default;
	DecalVertex(float x, float y, float z, float u, float v) : X(x), Y(y), Z(z), U(u), V(v) { }

	// Helion reflects over the [VertexAttribute] fields; C++ lists them here, in field order.
	static std::span<const Vertex::VertexAttributeAttribute> VertexAttributes()
	{
		static constexpr Vertex::VertexAttributeAttribute attributes[] =
		{
			{ "pos", Vertex::VertexAttributeAttribute::InferFromField, 3 },
			{ "uv", Vertex::VertexAttributeAttribute::InferFromField, 2 },
		};
		return attributes;
	}
};
#pragma pack(pop)

static_assert(sizeof(DecalVertex) == 5 * sizeof(float), "DecalVertex has to stay tightly packed");

}
