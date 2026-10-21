/*
** RenderWallSliceArgs.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.RenderWallSliceArgs / RenderWallSliceResult.
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
#include <vector>
#include "OpenGL/Renderers/Legacy/World/DynamicVertex.h"
#include "OpenGL/Renderers/Legacy/World/Data/RenderDataStyle.h"
#include "OpenGL/Renderers/Legacy/World/Sky/Sphere/SkyGeometryVertex.h"
#include "OpenGL/Texture/Legacy/GLLegacyTexture.h"
#include "World/Geometry/Sectors/RenderSector.h"
#include "World/Geometry/Walls/WallLocation.h"

struct side_t;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry
{

using DynamicVertexArray = std::vector<DynamicVertex>;
using SkyVertexArray = std::vector<Sky::Sphere::SkyGeometryVertex>;

struct RenderWallSliceArgs
{
	side_t* Side = nullptr;
	side_t* OtherSide = nullptr;
	bool IsFrontSide = false;
	bool RenderSkySide = false;
	bool AllowAlpha = false;
	Data::RenderDataStyle Style = Data::RenderDataStyle::Normal;
	::Helion::World::Geometry::Walls::WallLocation WallLocation = ::Helion::World::Geometry::Walls::WallLocation::None;
	// By value in C++: the slice code rewrites the heights of these (Helion mutates shared fake sectors).
	::Helion::World::Geometry::Sectors::RenderSector WallSector;
	::Helion::World::Geometry::Sectors::RenderSector LightSector;
	::Helion::World::Geometry::Sectors::RenderSector FacingSector;
	::Helion::World::Geometry::Sectors::RenderSector OtherSector;
	side_t* OffsetSide = nullptr;
	double StartOffsetY = 0;
	int Count = 0;
};

// Helion's ref struct. Vertices views geometry owned by the renderer (valid until the next slice call).
struct RenderWallSliceResult
{
	std::span<DynamicVertex> Vertices;
	SkyVertexArray* SkyVertices = nullptr;
	SkyVertexArray* SkyVertices2 = nullptr;
	Texture::Legacy::GLLegacyTexture* Texture = nullptr;
	bool AddOffset = true;

	RenderWallSliceResult() = default;
	RenderWallSliceResult(DynamicVertexArray* vertices, SkyVertexArray* skyVertices, Texture::Legacy::GLLegacyTexture* texture,
		SkyVertexArray* skyVertices2 = nullptr, bool addOffset = true) :
		Vertices(vertices == nullptr ? std::span<DynamicVertex>() : std::span<DynamicVertex>(*vertices)),
		SkyVertices(skyVertices), SkyVertices2(skyVertices2), Texture(texture), AddOffset(addOffset)
	{
	}

	static RenderWallSliceResult EmptyNoAddOffset() { return RenderWallSliceResult(nullptr, nullptr, nullptr, nullptr, false); }
	static RenderWallSliceResult Empty3D() { return RenderWallSliceResult(nullptr, nullptr, nullptr); }
};

}
