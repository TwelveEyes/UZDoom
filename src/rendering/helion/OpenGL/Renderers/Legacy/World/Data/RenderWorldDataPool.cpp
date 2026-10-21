/*
** RenderWorldDataPool.cpp
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Data.RenderWorldDataPool.
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

#include "OpenGL/Renderers/Legacy/World/Data/RenderWorldDataPool.h"
#include <algorithm>

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Data
{

RenderWorldDataPool::RenderWorldDataPool(::Helion::Render::OpenGL::Shader::RenderProgram& program, int poolSize)
	: PoolSize(poolSize), m_program(&program)
{
	m_renderData.reserve(poolSize);
	RefillPool(PoolSize);
}

RenderWorldData* RenderWorldDataPool::Create()
{
	m_owned.push_back(std::make_unique<RenderWorldData>(*m_program));
	return m_owned.back().get();
}

void RenderWorldDataPool::RefillPool(int size)
{
	size = std::min(size, PoolSize);
	for (int i = (int)m_renderData.size(); i < size; i++)
		m_renderData.push_back(Create());
}

void RenderWorldDataPool::Add(RenderWorldData* renderData)
{
	renderData->Texture = nullptr;
	renderData->BrightmapTexture = nullptr;
	m_renderData.push_back(renderData);
	UseCount--;
}

RenderWorldData& RenderWorldDataPool::Get(GLLegacyTexture* texture, GLLegacyTexture* brightMapTexture)
{
	if (!m_renderData.empty())
	{
		RenderWorldData* data = m_renderData.back();
		m_renderData.pop_back();
		data->Set(texture, brightMapTexture);
		UseCount++;
		return *data;
	}

	UseCount++;
	// RenderWorldDataPool exhausted. Consider increasing the size.
	RenderWorldData* data = Create();
	data->Set(texture, brightMapTexture);
	return *data;
}

}
