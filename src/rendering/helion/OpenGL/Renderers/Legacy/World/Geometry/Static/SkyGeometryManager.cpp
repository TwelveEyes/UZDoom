/*
** SkyGeometryManager.cpp
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
*/

#include "SkyGeometryManager.h"

#include <cstring>
#include "r_defs.h"
#include "World/Static/RenderStaticState.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Geometry::Static
{

using ::Helion::World::Geometry::Walls::WallLocation;
using ::Helion::World::Static::RenderStaticState;
using Sky::Sphere::SkyGeometryVertex;

void SkyGeometryManager::Clear()
{
}

void SkyGeometryManager::AddSide(Sky::ISkyComponent* sky, side_t* side, WallLocation wallLocation, SkyVertices vertices)
{
	if (vertices.empty())
		return;

	int index = sky->Vbo().Count();
	sky->Add(vertices.data(), (int)vertices.size());

	auto& state = RenderStaticState::Side(side);
	if (state.SkyGeometry == nullptr)
		state.SkyGeometry = std::make_unique<StaticSideSkyData>();

	auto geometryData = std::make_unique<StaticSkyGeometryData>(&sky->Vbo(), index, (int)vertices.size());
	switch (wallLocation)
	{
	case WallLocation::Upper:
		state.SkyGeometry->Upper = std::move(geometryData);
		break;
	case WallLocation::Lower:
		state.SkyGeometry->Lower = std::move(geometryData);
		break;
	case WallLocation::Middle:
		state.SkyGeometry->Middle = std::move(geometryData);
		break;
	default:
		break;
	}
}

void SkyGeometryManager::AddPlane(Sky::ISkyComponent* sky, int planeId, SkyVertices vertices)
{
	int index = sky->Vbo().Count();
	sky->Add(vertices.data(), (int)vertices.size());
	RenderStaticState::Plane(planeId).SkyGeometry = std::make_unique<StaticSkyGeometryData>(&sky->Vbo(), index, (int)vertices.size());
}

void SkyGeometryManager::UpdateSide(side_t* side, WallLocation wallLocation, SkyVertices vertices)
{
	if (vertices.empty())
		return;

	auto& state = RenderStaticState::Side(side);
	if (state.SkyGeometry == nullptr)
		return;

	StaticSkyGeometryData* data = state.SkyGeometry->Middle.get();
	switch (wallLocation)
	{
	case WallLocation::Upper:
		data = state.SkyGeometry->Upper.get();
		break;
	case WallLocation::Lower:
		data = state.SkyGeometry->Lower.get();
		break;
	default:
		break;
	}

	if (data != nullptr)
		UpdateSkyGeometry(*data, vertices);
}

bool SkyGeometryManager::HasPlane(int planeId)
{
	return RenderStaticState::Plane(planeId).SkyGeometry != nullptr;
}

bool SkyGeometryManager::HasSide(const side_t* side)
{
	return RenderStaticState::Side(side).SkyGeometry != nullptr;
}

void SkyGeometryManager::UpdatePlane(int planeId, SkyVertices vertices)
{
	auto& data = RenderStaticState::Plane(planeId).SkyGeometry;
	if (data == nullptr)
		return;

	UpdateSkyGeometry(*data, vertices);
}

void SkyGeometryManager::ClearGeometryVertices(side_t* side, WallLocation wallLocation)
{
	auto& state = RenderStaticState::Side(side);
	if (state.SkyGeometry == nullptr)
		return;

	switch (wallLocation)
	{
	case WallLocation::Upper:
		ClearSkyGeometry(state.SkyGeometry->Upper.get());
		break;
	case WallLocation::Lower:
		ClearSkyGeometry(state.SkyGeometry->Lower.get());
		break;
	case WallLocation::Middle:
		ClearSkyGeometry(state.SkyGeometry->Middle.get());
		break;
	default:
		break;
	}
}

void SkyGeometryManager::ClearGeometryVertices(int planeId)
{
	ClearSkyGeometry(RenderStaticState::Plane(planeId).SkyGeometry.get());
}

void SkyGeometryManager::ClearSkyGeometry(StaticSkyGeometryData* data)
{
	if (data == nullptr)
		return;

	memset(&data->Vbo->Data[(unsigned)data->Index], 0, sizeof(SkyGeometryVertex) * (size_t)data->Length);
	data->Vbo->UploadSubData(data->Index, data->Length);
}

void SkyGeometryManager::UpdateSkyGeometry(StaticSkyGeometryData& data, SkyVertices vertices)
{
	for (int i = 0; i < data.Length; i++)
		data.Vbo->Data[(unsigned)(data.Index + i)] = vertices[(size_t)i];

	data.Vbo->UploadSubData(data.Index, data.Length);
}

}
