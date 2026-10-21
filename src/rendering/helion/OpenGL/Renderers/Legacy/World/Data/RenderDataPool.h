/*
** RenderDataPool.h
**
** Port of Helion.Render.OpenGL.Renderers.Legacy.World.Data.RenderDataPool.
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

#include <algorithm>
#include <memory>
#include <vector>
#include "OpenGL/Renderers/Legacy/World/Data/RenderData.h"

namespace Helion::Render::OpenGL::Renderers::Legacy::World::Data
{

// The pool owns every RenderData it ever created; Get/Return hand out raw pointers (references in Helion).
template<typename TVertex>
class RenderDataPool
{
public:
	int PoolSize;
	int VboCapacity;
	int UseCount = 0;

	RenderDataPool(::Helion::Render::OpenGL::Shader::RenderProgram& program, int poolSize, int vboCapacity)
		: PoolSize(poolSize), VboCapacity(vboCapacity), m_program(&program)
	{
		m_free.reserve(poolSize);
		RefillPool(PoolSize);
	}

	RenderDataPool(const RenderDataPool&) = delete;
	RenderDataPool& operator=(const RenderDataPool&) = delete;

	void RefillPool(int size)
	{
		size = std::min(size, PoolSize);
		for (int i = (int)m_free.size(); i < size; i++)
			m_free.push_back(Create());
	}

	void Return(RenderData<TVertex>* data)
	{
		data->Texture = nullptr;
		data->BrightMapTexture = nullptr;
		m_free.push_back(data);
		UseCount--;
	}

	RenderData<TVertex>& Get(GLLegacyTexture* texture, GLLegacyTexture* brightMapTexture = nullptr)
	{
		if (!m_free.empty())
		{
			RenderData<TVertex>* data = m_free.back();
			m_free.pop_back();
			data->Set(texture, brightMapTexture);
			UseCount++;
			return *data;
		}

		UseCount++;
		// RenderDataPool exhausted. Consider increasing the size.
		RenderData<TVertex>* data = Create();
		data->Set(texture, brightMapTexture);
		return *data;
	}

private:
	RenderData<TVertex>* Create()
	{
		m_owned.push_back(std::make_unique<RenderData<TVertex>>(*m_program, VboCapacity));
		return m_owned.back().get();
	}

	::Helion::Render::OpenGL::Shader::RenderProgram* m_program;
	std::vector<RenderData<TVertex>*> m_free;
	std::vector<std::unique_ptr<RenderData<TVertex>>> m_owned;
};

}
