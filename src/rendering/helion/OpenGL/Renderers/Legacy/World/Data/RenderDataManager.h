/*
** RenderDataManager.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Data.RenderDataManager.
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

#include <array>
#include <cassert>
#include <functional>
#include <memory>
#include <vector>
#include "OpenGL/Renderers/Legacy/World/Data/RenderDataCollection.h"
#include "OpenGL/Renderers/Legacy/World/Data/RenderDataStyle.h"
#include "Resources/Definitions/Decorate/Properties/Enums/RenderStyle.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Data
{

template<typename TVertex>
class RenderDataManager
{
public:
	RenderDataManager(::Helion::Render::OpenGL::Shader::RenderProgram& program, GLLegacyTexture* healthBarTexture, RenderDataPool<TVertex>& renderDataPoolArray,
		RenderDataPool<TVertex>& renderDataPoolOverflow, std::function<void()> onDraw = {})
	{
		static_assert(RenderStyleLookup.size() == (size_t)Helion::Resources::Definitions::Decorate::Properties::Enums::RenderStyle::Count,
			"Render style lookup size mismatch");

		// The overflow pool will use dynamic lookups that will be released on clear.
		// The pool used for texture arrays will be locked to their texture id since they are the small number of render buckets.
		m_renderData[0] = CreateRenderDataStyleArray(RenderDataCollectionMode::Recycle, renderDataPoolOverflow, onDraw);
		m_renderData[1] = CreateRenderDataStyleArray(RenderDataCollectionMode::Pinned, renderDataPoolArray, onDraw);

		m_healthBarData = std::make_unique<RenderData<TVertex>>(program, 8192, healthBarTexture);
	}

	RenderDataManager(const RenderDataManager&) = delete;
	RenderDataManager& operator=(const RenderDataManager&) = delete;

	bool HasDataToRenderByStyle(RenderDataStyle style) const
	{
		return m_renderData[0][(int)style]->HasDataToRender() || m_renderData[1][(int)style]->HasDataToRender();
	}

	void Clear()
	{
		for (auto& array : m_renderData)
		{
			for (auto& collection : array)
				collection->Clear();
		}
		m_healthBarData->Clear();
	}

	RenderData<TVertex>& GetHealthBarData() { return *m_healthBarData; }

	void RenderHealthBars()
	{
		m_healthBarData->Draw();
	}

	RenderData<TVertex>& GetByRenderStyle(Helion::Resources::Definitions::Decorate::Properties::Enums::RenderStyle style,
		GLLegacyTexture* texture, GLLegacyTexture* brightmapTexture = nullptr)
	{
		int index = texture->IsParentArray ? 1 : 0;
		return m_renderData[index][(int)RenderStyleLookup[(int)style]]->Get(texture, brightmapTexture);
	}

	void RenderByRenderStyle(RenderDataStyle style)
	{
		m_renderData[0][(int)style]->Render();
		m_renderData[1][(int)style]->Render();
	}

	void Dispose()
	{
		for (auto& array : m_renderData)
		{
			for (auto& collection : array)
				collection->Dispose();
		}
		m_healthBarData.reset();
	}

private:
	static constexpr std::array<RenderDataStyle, 8> RenderStyleLookup =
	{
		RenderDataStyle::Normal,
		RenderDataStyle::Normal,
		RenderDataStyle::Fuzzy,
		RenderDataStyle::Translucent,
		RenderDataStyle::Add,
		RenderDataStyle::ColorAdd,
		RenderDataStyle::ColorAdd,
		RenderDataStyle::ColorAdd
	};

	using CollectionArray = std::vector<std::unique_ptr<RenderDataCollection<TVertex>>>;

	static CollectionArray CreateRenderDataStyleArray(RenderDataCollectionMode mode, RenderDataPool<TVertex>& renderDataPool, const std::function<void()>& onDraw)
	{
		CollectionArray renderDataStylesArray;
		renderDataStylesArray.reserve((size_t)RenderDataStyle::Count);
		for (int i = 0; i < (int)RenderDataStyle::Count; i++)
			renderDataStylesArray.push_back(std::make_unique<RenderDataCollection<TVertex>>(renderDataPool, mode, onDraw));
		return renderDataStylesArray;
	}

	CollectionArray m_renderData[2];
	std::unique_ptr<RenderData<TVertex>> m_healthBarData;
};

}
