/*
** PortalStencilVertex.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Portals.PortalStencilVertex.
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

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals
{

/// <summary>
/// Intended to be a trivial vertex that is used only for being turned into
/// fragments for stencil writing.
/// </summary>
// C# StructLayout(LayoutKind.Sequential, Pack = 1)
#pragma pack(push, 1)
struct PortalStencilVertex
{
	// [VertexAttribute]
	float PosX;
	float PosY;
	float PosZ;

	PortalStencilVertex() = default;
	PortalStencilVertex(float x, float y, float z) : PosX(x), PosY(y), PosZ(z)
	{
	}

	// Helion reflects over the [VertexAttribute] fields; C++ lists them here, in field order.
	static std::span<const Vertex::VertexAttributeAttribute> VertexAttributes()
	{
		static constexpr Vertex::VertexAttributeAttribute attributes[] =
		{
			{ "pos", Vertex::VertexAttributeAttribute::InferFromField, 3 },
		};
		return attributes;
	}
};
#pragma pack(pop)

static_assert(sizeof(PortalStencilVertex) == 3 * sizeof(float), "PortalStencilVertex has to stay tightly packed");

}
