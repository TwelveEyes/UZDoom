/*
** RenderWorldDataList.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Data.RenderWorldDataList.
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
#include <vector>
#include "tarray.h"
#include "OpenGL/Renderers/Legacy/World/Data/RenderWorldDataPool.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Data
{

class RenderWorldDataList
{
public:
	std::vector<RenderWorldData*> RenderData;

	RenderWorldDataList(RenderWorldDataPool& pool, std::function<void()> onDraw);

	void Reset();
	RenderWorldData& Add(GLLegacyTexture* texture, GLLegacyTexture* brightmapTexture = nullptr);
	bool HasDataToRender() const { return m_dataToRender.Size() > 0; }
	void Draw();
	void Clear();
	void ReleaseUnmanagedResources();

private:
	std::vector<RenderWorldData*> m_allRenderData;
	TArray<RenderWorldData*> m_dataToRender;
	RenderWorldDataPool* m_pool;
	std::function<void()> m_onDraw;
	int m_renderCount = 0;
};

}
