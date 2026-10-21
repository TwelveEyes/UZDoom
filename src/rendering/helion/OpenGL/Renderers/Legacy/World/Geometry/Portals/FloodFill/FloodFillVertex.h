/*
** FloodFillVertex.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Portals.FloodFill.FloodFillVertex.
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
#include "Util/Geometry.h"
#include "OpenGL/Vertex/VertexAttributes.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Portals::FloodFill
{

// C# StructLayout(LayoutKind.Sequential, Pack = 1)
#pragma pack(push, 1)
struct FloodFillVertex
{
	// [VertexAttribute("pos", size: 3)]
	float PosX;
	float PosY;
	float PosZ;

	// [VertexAttribute("planeZ", size: 1)]
	float PlaneZ;

	// [VertexAttribute("minViewZ", size: 1)]
	float MinViewZ;

	// [VertexAttribute("maxViewZ", size: 1)]
	float MaxViewZ;

	// [VertexAttribute("prevZ", size: 1)]
	float PrevZ;

	// [VertexAttribute("prevPlaneZ", size: 1)]
	float PrevPlaneZ;

	// [VertexAttribute("surfaceOptions", size: 1)]
	float SurfaceOptions;

	// [VertexAttribute("renderOptions", size: 1)]
	float RenderOptions;

	// [VertexAttribute("mapId", size: 1, required: false)]
	float MapId;

	FloodFillVertex() = default;
	FloodFillVertex(const ::Helion::Geometry::Vec3F& pos, float prevZ, float planeZ, float prevPlaneZ, float minPlaneZ, float maxPlaneZ,
		float surfaceOptions, float renderOptions, int mapId)
		: PosX(pos.X), PosY(pos.Y), PosZ(pos.Z), PlaneZ(planeZ), MinViewZ(minPlaneZ), MaxViewZ(maxPlaneZ), PrevZ(prevZ),
		PrevPlaneZ(prevPlaneZ), SurfaceOptions(surfaceOptions), RenderOptions(renderOptions), MapId((float)mapId)
	{
	}

	// Helion reflects over the [VertexAttribute] fields; C++ lists them here, in field order.
	static std::span<const Vertex::VertexAttributeAttribute> VertexAttributes()
	{
		static constexpr Vertex::VertexAttributeAttribute attributes[] =
		{
			{ "pos", Vertex::VertexAttributeAttribute::InferFromField, 3 },
			{ "planeZ" },
			{ "minViewZ" },
			{ "maxViewZ" },
			{ "prevZ" },
			{ "prevPlaneZ" },
			{ "surfaceOptions" },
			{ "renderOptions" },
			{ "mapId", Vertex::VertexAttributeAttribute::InferFromField, 1, Vertex::VertexAttribPointerType::Float, false, false, false },
		};
		return attributes;
	}
};
#pragma pack(pop)

static_assert(sizeof(FloodFillVertex) == 11 * sizeof(float), "FloodFillVertex has to stay tightly packed");

}
