/*
** SkyGeometryVertex.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Sky.Sphere.SkyGeometryVertex.
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
struct SkyGeometryVertex
{
	// [VertexAttribute("pos", size: 3)]
	float X;
	float Y;
	float Z;

	// [VertexAttribute("prevZ", required: false)]
	float PrevZ;

	static std::span<const Vertex::VertexAttributeAttribute> VertexAttributes()
	{
		static constexpr Vertex::VertexAttributeAttribute attributes[] =
		{
			{ "pos", Vertex::VertexAttributeAttribute::InferFromField, 3 },
			{ "prevZ", Vertex::VertexAttributeAttribute::InferFromField, 1, Vertex::VertexAttribPointerType::Float, false, false, false },
		};
		return attributes;
	}
};
#pragma pack(pop)

static_assert(sizeof(SkyGeometryVertex) == 4 * sizeof(float), "SkyGeometryVertex has to stay tightly packed");

}
