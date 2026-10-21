/*
** RenderDataCollection.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Data.RenderDataCollection.
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
#include "OpenGL/Renderers/Legacy/World/Data/RenderDataPool.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Data
{

enum class RenderDataCollectionMode
{
	// The RenderData is pinned to the texture. Get will always return the same RenderData for requested texture.
	Pinned,
	// The RenderData is recycled when cleared. Any Get will return the next available RenderData. This keeps the list smaller for varying textures.
	Recycle
};

/// A collection of render data for specific textures. This exists because we want
/// to track vertices for alpha and non-alpha, but keep them in separate lists.
/// Instead of copy pasting the logic, they're now in their own class.
template<typename TVertex>
class RenderDataCollection
{
public:
	RenderDataCollection(RenderDataPool<TVertex>& renderDataPool, RenderDataCollectionMode mode, std::function<void()> onDraw = {})
		: m_renderDataPool(&renderDataPool), m_mode(mode), m_onDraw(std::move(onDraw))
	{
		m_allRenderData.resize(2048, nullptr);
		m_dataToRender.Grow(2048);
	}

	bool HasDataToRender() const { return m_dataToRender.Size() > 0; }

	void Clear()
	{
		for (unsigned i = 0; i < m_dataToRender.Size(); i++)
		{
			RenderData<TVertex>* data = m_dataToRender[i];
			data->Clear();
			if (m_mode == RenderDataCollectionMode::Recycle)
			{
				SetLookup(data->Texture->TextureId, nullptr);
				m_renderDataPool->Return(data);
			}
		}

		m_dataToRender.Clear();
		m_renderCount++;
	}

	TArray<RenderData<TVertex>*>& GetDataToRender() { return m_dataToRender; }

	RenderData<TVertex>& Get(GLLegacyTexture* texture, GLLegacyTexture* brightmapTexture = nullptr)
	{
		if (m_mode == RenderDataCollectionMode::Pinned)
			return GetStatic(texture, brightmapTexture);

		return GetDynamic(texture, brightmapTexture);
	}

	void Render()
	{
		if (m_dataToRender.Size() == 0)
			return;

		for (unsigned i = 0; i < m_dataToRender.Size(); i++)
		{
			if (m_dataToRender[i]->Draw() && m_onDraw)
				m_onDraw();
		}
	}

	// Drops the lookup. The RenderData objects belong to the pool.
	void Dispose()
	{
		std::fill(m_allRenderData.begin(), m_allRenderData.end(), nullptr);
		m_dataToRender.Clear();
	}

private:
	RenderData<TVertex>*& LookupSlot(int id)
	{
		if ((size_t)id >= m_allRenderData.size())
			m_allRenderData.resize((size_t)id + 1024, nullptr);
		return m_allRenderData[id];
	}

	void SetLookup(int id, RenderData<TVertex>* data) { LookupSlot(id) = data; }

	RenderData<TVertex>& GetDynamic(GLLegacyTexture* texture, GLLegacyTexture* brightmapTexture)
	{
		RenderData<TVertex>*& slot = LookupSlot(texture->TextureId);
		if (slot == nullptr)
			slot = &m_renderDataPool->Get(texture, brightmapTexture);

		RenderData<TVertex>* data = slot;
		if (data->RenderCount != m_renderCount)
		{
			m_dataToRender.Push(data);
			data->RenderCount = m_renderCount;
		}

		return *data;
	}

	RenderData<TVertex>& GetStatic(GLLegacyTexture* texture, GLLegacyTexture* brightmapTexture)
	{
		RenderData<TVertex>*& slot = LookupSlot(texture->TextureId);
		if (slot == nullptr)
		{
			RenderData<TVertex>* created = &m_renderDataPool->Get(texture, brightmapTexture);
			created->RenderCount = m_renderCount - 1;
			slot = created;
		}

		RenderData<TVertex>* data = slot;
		if (data->RenderCount != m_renderCount)
		{
			m_dataToRender.Push(data);
			data->RenderCount = m_renderCount;
		}

		return *data;
	}

	std::vector<RenderData<TVertex>*> m_allRenderData;
	TArray<RenderData<TVertex>*> m_dataToRender;
	RenderDataPool<TVertex>* m_renderDataPool;
	RenderDataCollectionMode m_mode;
	std::function<void()> m_onDraw;
	int m_renderCount = 0;
};

}
