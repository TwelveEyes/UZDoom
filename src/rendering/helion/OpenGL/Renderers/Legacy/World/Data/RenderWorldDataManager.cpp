/*
** RenderWorldDataManager.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Data.RenderWorldDataManager.
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

#include "OpenGL/Renderers/Legacy/World/Data/RenderWorldDataManager.h"
#include <cassert>
#include "OpenGL/Renderers/Legacy/World/Geometry/CoverWallUtil.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Data
{

using Geometry::GeometryType;

RenderWorldDataManager::RenderWorldDataManager(::Helion::Render::OpenGL::Shader::RenderProgram* program, std::function<void()> onDraw)
	: m_pool(*program, 1024),
	  m_onDraw(std::move(onDraw)),
	  m_lookup([this]() { return AllocateDataList(); })
{
}

std::unique_ptr<RenderWorldDataList> RenderWorldDataManager::AllocateDataList()
{
	return std::make_unique<RenderWorldDataList>(m_pool, m_onDraw);
}

void RenderWorldDataManager::Reset()
{
	for (auto& item : m_lookup.GetItems())
		item->Reset();
}

void RenderWorldDataManager::InitCoverWallRenderData(GLLegacyTexture* texture, ::Helion::Render::OpenGL::Shader::RenderProgram* program, GLLegacyTexture* brightmapTexture)
{
	if (!m_coverWalls)
		m_coverWalls = std::make_unique<RenderWorldData>(texture, *program, brightmapTexture);
}

RenderWorldData& RenderWorldDataManager::GetRenderData(GLLegacyTexture* texture, GeometryType type, GLLegacyTexture* brightmapTexture)
{
	return m_lookup.Get(type)->Add(texture, brightmapTexture);
}

void RenderWorldDataManager::AddCoverWallVertices(side_t* side, std::span<DynamicVertex> vertices, Helion::World::Geometry::Walls::WallLocation location, bool oneSided)
{
	if (!m_coverWalls || !BufferCoverWalls)
		return;

	assert(vertices.size() == 6 && "Wall vertices should be 6");

	auto& vbo = m_coverWalls->Pipeline.Vbo;
	int index = vbo.Count();
	vbo.AddMemoryCopy(std::span<const DynamicVertex>(vertices.data(), vertices.size()));
	Geometry::CoverWallUtil::SetCoverWallVertices(side, vbo.Data.Data(), index, location);
}

void RenderWorldDataManager::AddCoverFlatVertices(std::span<const DynamicVertex> vertices)
{
	if (!m_coverWalls || !BufferCoverWalls)
		return;

	m_coverWalls->Pipeline.Vbo.AddMemoryCopy(vertices);
}

void RenderWorldDataManager::Clear()
{
	for (auto& item : m_lookup.GetItems())
		item->Clear();

	if (m_coverWalls)
		m_coverWalls->Clear();
}

void RenderWorldDataManager::RenderWalls()
{
	m_lookup.Get(GeometryType::Wall)->Draw();
}

void RenderWorldDataManager::RenderTwoSidedMiddleWalls()
{
	m_lookup.Get(GeometryType::TwoSidedMiddleWall)->Draw();
}

void RenderWorldDataManager::RenderMiddle3D()
{
	m_lookup.Get(GeometryType::Middle3D)->Draw();
}

void RenderWorldDataManager::RenderFlats()
{
	m_lookup.Get(GeometryType::Flat)->Draw();
}

void RenderWorldDataManager::RenderCoverWalls()
{
	if (m_coverWalls)
		m_coverWalls->Draw();
}

void RenderWorldDataManager::Render(GeometryType type)
{
	m_lookup.Get(type)->Draw();
}

void RenderWorldDataManager::Render(RenderDataStyle style)
{
	m_lookup.Get(Geometry::ToGeometryType(style))->Draw();
}

bool RenderWorldDataManager::HasStyleToRender(RenderDataStyle style)
{
	return m_lookup.Get(Geometry::ToGeometryType(style))->HasDataToRender();
}

void RenderWorldDataManager::Dispose()
{
	for (auto& item : m_lookup.GetItems())
		item->ReleaseUnmanagedResources();
	m_coverWalls.reset();
}

}
