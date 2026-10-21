/*
** SkyGeometryManager.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Geometry.Static.SkyGeometryManager.
**
**---------------------------------------------------------------------------
**
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Side.SkyGeometry / SectorPlane.SkyGeometry live in RenderStaticState (World/Static/RenderStaticState.h):
** side_t and sector_t have no room for them. Planes are (sector, sector_t::floor / sector_t::ceiling).
** Helion's null SkyGeometryVertex[] is an empty span.
**
*/

#pragma once

#include <span>
#include "OpenGL/Renderers/Legacy/World/Sky/ISkyComponent.h"
#include "OpenGL/Renderers/Legacy/World/Sky/Sphere/SkyGeometryVertex.h"
#include "World/Geometry/Walls/WallLocation.h"

struct sector_t;
struct side_t;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static
{

class StaticSkyGeometryData
{
public:
	Buffer::Array::Vertex::VertexBufferObject<Sky::Sphere::SkyGeometryVertex>* Vbo;
	int Index;
	int Length;

	StaticSkyGeometryData(Buffer::Array::Vertex::VertexBufferObject<Sky::Sphere::SkyGeometryVertex>* vbo, int index, int length)
		: Vbo(vbo), Index(index), Length(length)
	{
	}
};

class StaticSideSkyData
{
public:
	std::unique_ptr<StaticSkyGeometryData> Upper;
	std::unique_ptr<StaticSkyGeometryData> Middle;
	std::unique_ptr<StaticSkyGeometryData> Lower;
};

class SkyGeometryManager
{
public:
	using SkyVertices = std::span<const Sky::Sphere::SkyGeometryVertex>;

	static void Clear();

	static void AddSide(Sky::ISkyComponent* sky, side_t* side, ::Helion::World::Geometry::Walls::WallLocation wallLocation, SkyVertices vertices);
	// planeId = RenderPlane::Id
	static void AddPlane(Sky::ISkyComponent* sky, int planeId, SkyVertices vertices);

	static void UpdateSide(side_t* side, ::Helion::World::Geometry::Walls::WallLocation wallLocation, SkyVertices vertices);

	static bool HasPlane(int planeId);
	static bool HasSide(const side_t* side);

	static void UpdatePlane(int planeId, SkyVertices vertices);

	static void ClearGeometryVertices(side_t* side, ::Helion::World::Geometry::Walls::WallLocation wallLocation);
	static void ClearGeometryVertices(int planeId);

private:
	static void ClearSkyGeometry(StaticSkyGeometryData* data);
	static void UpdateSkyGeometry(StaticSkyGeometryData& data, SkyVertices vertices);
};

}
