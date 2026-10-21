/*
** PrimitiveVertex.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Primitives.PrimitiveVertex.
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
#include "Util/Geometry.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Primitives
{

#pragma pack(push, 1)
struct PrimitiveVertex
{
	// [VertexAttribute]
	::Helion::Geometry::Vec3F Pos;

	// [VertexAttribute]
	::Helion::Geometry::Vec3F Rgb;

	// [VertexAttribute]
	float Alpha;

	PrimitiveVertex() = default;
	PrimitiveVertex(const ::Helion::Geometry::Vec3F& pos, const ::Helion::Geometry::Vec3F& rgb, float alpha) : Pos(pos), Rgb(rgb), Alpha(alpha) { }

	static std::span<const Vertex::VertexAttributeAttribute> VertexAttributes()
	{
		static constexpr Vertex::VertexAttributeAttribute attributes[] =
		{
			{ "pos", Vertex::VertexAttributeAttribute::InferFromField, 3 },
			{ "rgb", Vertex::VertexAttributeAttribute::InferFromField, 3 },
			{ "alpha" },
		};
		return attributes;
	}
};
#pragma pack(pop)

static_assert(sizeof(PrimitiveVertex) == 7 * sizeof(float), "PrimitiveVertex has to stay tightly packed");

}
