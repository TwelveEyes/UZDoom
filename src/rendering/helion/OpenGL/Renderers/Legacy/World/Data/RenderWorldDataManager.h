/*
** RenderWorldDataManager.h
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

#pragma once

#include <functional>
#include <memory>
#include <span>
#include "OpenGL/Renderers/IStyleRenderer.h"
#include "OpenGL/Renderers/Legacy/World/GeometryTypeLookup.h"
#include "OpenGL/Renderers/Legacy/World/Data/RenderWorldDataList.h"
#include "OpenGL/Renderers/Legacy/World/Data/RenderWorldDataPool.h"
#include "OpenGL/Renderers/Legacy/World/Geometry/GeometryType.h"
#include "World/Geometry/Walls/WallLocation.h"

struct side_t;

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Data
{

class RenderWorldDataManager final : public StyleRendererBase
{
public:
	bool BufferCoverWalls = true;

	RenderWorldDataManager(::Helion::Render::OpenGL::Shader::RenderProgram* program, std::function<void()> onDraw);

	RenderWorldDataManager(const RenderWorldDataManager&) = delete;
	RenderWorldDataManager& operator=(const RenderWorldDataManager&) = delete;

	void Reset();
	void InitCoverWallRenderData(GLLegacyTexture* texture, ::Helion::Render::OpenGL::Shader::RenderProgram* program, GLLegacyTexture* brightmapTexture = nullptr);
	RenderWorldData& GetRenderData(GLLegacyTexture* texture, Geometry::GeometryType type, GLLegacyTexture* brightmapTexture = nullptr);
	void AddCoverWallVertices(side_t* side, std::span<DynamicVertex> vertices, Helion::World::Geometry::Walls::WallLocation location, bool oneSided);
	void AddCoverFlatVertices(std::span<const DynamicVertex> vertices);
	void Clear();
	void RenderWalls();
	void RenderTwoSidedMiddleWalls();
	void RenderMiddle3D();
	void RenderFlats();
	void RenderCoverWalls();
	void Render(Geometry::GeometryType type);
	void Render(RenderDataStyle style) override;
	bool HasStyleToRender(RenderDataStyle style) override;
	void Dispose();

private:
	std::unique_ptr<RenderWorldDataList> AllocateDataList();

	RenderWorldDataPool m_pool;
	std::function<void()> m_onDraw;
	GeometryTypeLookup<std::unique_ptr<RenderWorldDataList>> m_lookup;
	std::unique_ptr<RenderWorldData> m_coverWalls;
};

}
