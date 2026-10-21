/*
** RenderWorldDataList.cpp
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

#include "OpenGL/Renderers/Legacy/World/Data/RenderWorldDataList.h"
#include <algorithm>

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Data
{

RenderWorldDataList::RenderWorldDataList(RenderWorldDataPool& pool, std::function<void()> onDraw)
	: m_allRenderData(1024, nullptr), m_pool(&pool), m_onDraw(std::move(onDraw))
{
	m_dataToRender.Grow(1024);
}

void RenderWorldDataList::Reset()
{
	for (RenderWorldData*& renderData : m_allRenderData)
	{
		if (renderData != nullptr)
			m_pool->Add(renderData);

		renderData = nullptr;
	}

	m_dataToRender.Clear();
	RenderData.clear();
}

RenderWorldData& RenderWorldDataList::Add(GLLegacyTexture* texture, GLLegacyTexture* brightmapTexture)
{
	if ((int)m_allRenderData.size() <= texture->TextureId)
		m_allRenderData.resize((size_t)texture->TextureId + 1024, nullptr);

	RenderWorldData* data = m_allRenderData[texture->TextureId];
	if (data == nullptr)
	{
		data = &m_pool->Get(texture, brightmapTexture);
		m_allRenderData[texture->TextureId] = data;
		RenderData.push_back(data);
	}

	if (data->RenderCount != m_renderCount)
	{
		m_dataToRender.Push(data);
		data->RenderCount = m_renderCount;
	}

	return *data;
}

void RenderWorldDataList::Draw()
{
	for (unsigned i = 0; i < m_dataToRender.Size(); i++)
	{
		if (m_dataToRender[i]->Draw() && m_onDraw)
			m_onDraw();
	}
}

void RenderWorldDataList::Clear()
{
	for (unsigned i = 0; i < m_dataToRender.Size(); i++)
		m_dataToRender[i]->Clear();
	m_dataToRender.Clear();

	m_renderCount++;
}

// The buffers belong to the pool and are destroyed with it; only the references are dropped here.
void RenderWorldDataList::ReleaseUnmanagedResources()
{
	std::fill(m_allRenderData.begin(), m_allRenderData.end(), nullptr);
	RenderData.clear();
	m_dataToRender.Clear();
}

}
