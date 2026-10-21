/*
** SkySphereVertex.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Sky.Sphere.SkySphereVertex.
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

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Sky::Sphere
{

// C# StructLayout(LayoutKind.Sequential, Pack = 1)
#pragma pack(push, 1)
struct SkySphereVertex
{
	// [VertexAttribute("pos", size: 3)]
	float X;
	float Y;
	float Z;

	// [VertexAttribute("uv", size: 2)]
	float U;
	float V;

	SkySphereVertex() = default;
	SkySphereVertex(float x, float y, float z, float u, float v) : X(x), Y(y), Z(z), U(u), V(v) { }

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

static_assert(sizeof(SkySphereVertex) == 5 * sizeof(float), "SkySphereVertex has to stay tightly packed");

}
